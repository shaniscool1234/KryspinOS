# KryspinOS — Cursor Rendering: Root Cause & Fix

## 1. Problem Statement (observed in `images/problemss/`)

Two interrelated defects in the window manager (`gui/wm.c`):

1. **Green/teal "clone" block left behind when the cursor moves.**
   The cursor sprite is drawn into the backbuffer but is not part of
   the damage list, so the next partial flip leaves stale backbuffer
   pixels on screen.
2. **Stutter when the cursor moves over a window or the taskbar.**
   The current `else if (m.moved)` branch still calls `draw_desktop()`,
   which can do the full chrome + window + clock + popup repaint — exactly
   the work the path was supposed to skip.

Reproduced from the screenshots:

| File | What it shows |
|------|---------------|
| `images/problemss/problem1.png` | Clean desktop — cursor over wallpaper, no artifact. This is the baseline. |
| `images/problemss/problem2.png` | Cursor in the centre; a large green/teal rectangle in the upper-right. The rectangle matches the bounds of the *previous* cursor position (12×19 expanded by 1 px on each side). |
| `images/problemss/problem3.png` | Smaller teal patch in the upper-right corner — the same "trail" artifact, partial because the damage coalesced with whatever was already in the damage list at that frame. |

## 2. Root Cause

### 2.1 The damage-tracking API

`gfx/graphics.c` exposes a damage-rectangle API that all drawing
primitives are *supposed* to call after touching the backbuffer:

| Function | Calls `gfx_damage_add` ? |
|----------|-------------------------|
| `gfx_fill` | yes (line 266) |
| `gfx_rect` | yes (line 305) |
| `gfx_char` | yes (line 350) |
| `gfx_text` | yes (line 373) |
| `gfx_text_blit` | yes (line 465) |
| `gfx_set_gradient_wallpaper` | yes (line 924) |
| `gfx_draw_bmp` | yes (line 871) |
| **`gfx_putpixel`** | **NO** |

`gfx_putpixel` writes a single pixel via `plot()` (graphics.c:212) and
returns. It never tells the damage system it touched the surface.

### 2.2 The cursor draws via `gfx_putpixel`

`cursor_draw_simple` (gui/wm.c:421) iterates the 12×19 cursor sprite and
calls `gfx_putpixel` for every opaque pixel. That is correct in
isolation — it is the fastest way to draw a sparse sprite — but it
also means the cursor is **invisible to the damage tracker**.

### 2.3 Why this leaves a green block

`gfx_flip_damaged()` (graphics.c:595) walks the damage list, merges
overlapping rects into a smaller set, and calls `flip_one()` for each
survivor. `flip_one()` is a `rep movsd` from the backbuffer into the
framebuffer MMIO. It copies **whatever happens to be in the
backbuffer at those coordinates** — there is no "what was on screen
last frame" anywhere in the pipeline.

Sequence that produces the artifact in `problem2.png`:

1. Cursor is at `(x0, y0)`. `cursor_old_x/y` are stale. The previous
   `restore_cursor_background` painted `desktop_cache[py*W+px]` back
   into the backbuffer at the old position, so the backbuffer is
   correct (gradient wallpaper pixels) at `(x0, y0)`.
2. User moves the mouse. `m.x`, `m.y` advance to `(x1, y1)`.
3. `wm_update` enters the `else if (m.moved)` branch (wm.c:1010).
4. `restore_cursor_background(cursor_old_x, cursor_old_y, ...)`
   paints gradient pixels at `(x0, y0)` using `gfx_putpixel`. Each
   putpixel is invisible to the damage tracker.
5. `gfx_damage_add(x0-1, y0-1, W+2, H+2)` — the *old* cursor rect is
   recorded. This is the only damage rect so far.
6. `draw_desktop()` runs. Depending on `frame_has_dragging`,
   `desktop_chrome_dirty`, `taskbar_chrome_dirty`, it calls
   `draw_taskbar_dynamic()` and `draw_popups()`, both of which call
   `gfx_rect`/`gfx_text` and add their own damage rects (typically
   along the taskbar at the bottom of the screen).
7. `cursor_draw_simple(m.x, m.y)` paints the sprite at `(x1, y1)` via
   `gfx_putpixel`. **No damage added.** The backbuffer at `(x1, y1)`
   now contains cursor pixels, but the damage list does not know.
8. `gfx_flip_damaged()` runs. It coalesces the old-cursor rect with
   the taskbar dynamic rect. If the taskbar rect and the old-cursor
   rect do not overlap, they ship as two separate `flip_one` calls.
   The old-cursor rect is flipped (backbuffer was already gradient,
   so the screen ends up correct at the old position), but the *new*
   cursor rect is **not** flipped. The cursor pixels sit in the
   backbuffer until the next frame's damage happens to cover them —
   and until then, the previous frame's pixels are still on screen at
   the old position.
9. If the user moves the cursor *again* before the next 4-tick
   PIT frame fires, the second `m.moved` branch runs. It calls
   `restore_cursor_background` at the *new* old position — but the
   backbuffer there still holds the cursor pixels from step 7, so it
   blits cursor pixels over the screen at the previous position. This
   is the green/teal trail: the colour comes from whatever the
   gradient happened to be at that Y row, *tinted by the cursor
   pixels that never got cleared because the original clear happened
   before the new sprite was drawn, and both writes were to the same
   backbuffer region*.

The screenshot shows this exactly: the trail block in
`problem2.png` is offset from the current cursor position by roughly
the distance the mouse travelled in the most recent sample.

### 2.4 Why the m.moved path also causes stutter

`wm_update` enters the `else if (m.moved)` branch (wm.c:1010). It
calls `draw_desktop()` (wm.c:1021). `draw_desktop()` (wm.c:608) does:

* `if (desktop_chrome_dirty)` → `draw_desktop_chrome()` — full wallpaper +
  branding repaint.
* Loop over windows with `needs_paint` → `draw_window_chrome` +
  `draw_window_content`.
* `if (taskbar_chrome_dirty)` → `draw_taskbar_chrome`.
* `draw_taskbar_dynamic()` — always — repaints the clock and search text.
* `draw_popups()` — always.

If the cursor moves over a window with `needs_paint=true`, the window
content repaints on every mouse sample, even though the mouse sample
isn't actually changing anything visible. The whole point of the
`m.moved` fast path was to skip this work, and the current code
defeats that.

### 2.5 Latent initialization bug

`cursor_old_x` and `cursor_old_y` are both statically zero
(wm.c:61–62). The very first call to the cursor-moved branch will
therefore call `restore_cursor_background(0, 0, CURSOR_W, CURSOR_H)`,
which blits a 12×19 gradient patch from the desktop cache into the
top-left corner of the backbuffer. It does no visible harm at boot
because the desktop chrome is then redrawn over it, but it is a bug
that should be fixed while we are here: change the initial values to
the cursor's *real* start position (`cursor_x`, `cursor_y` at
`wm_init`), or use a sentinel (`-1, -1` checked against
`cursor_old_x == -1`) to skip the first call.

## 3. The Fix

The fix has three parts, in order. Each part closes one of the
failure modes above.

### Step 1 — Make the cursor visible to the damage system

Two equivalent options. Pick **one**, not both.

**Option A (recommended): add a single damage rect after the sprite
loop.** This is the minimal change and preserves the existing
per-pixel fast path.

In `gui/wm.c`, change the end of `cursor_draw_simple` (wm.c:421) so
that immediately after the inner loop finishes, before the function
returns, add:

```c
gfx_damage_add(x - 1, y - 1, CURSOR_W + 2, CURSOR_H + 2);
```

The +2 / -1 padding matches the rects the existing code already adds
for the old cursor position (wm.c:1017, 1019), so the coalescing
behaviour stays consistent.

After this change, *every* call to `cursor_draw_simple` will register
exactly one damage rect that covers the new cursor position. Any
subsequent `gfx_flip_damaged()` will therefore always include the new
cursor in the flipped region, and the green-block artifact disappears
the next time a frame flips.

**Option B (only if profiling shows Step 1's `flip_one` is the
bottleneck): add a dedicated `gfx_blit_cursor` primitive to `graphics.c`
that draws the sprite and registers damage in one pass, with the
damage recorded *after* the inner loop finishes so a single coalesce
step inside `gfx_flip_damaged()` still merges it with adjacent rects.**

For a 12×19 sprite at 60 Hz motion, Option A is fast enough. Only
reach for Option B if the damage-flipped region grows to dominate the
per-frame copy cost.

### Step 2 — Replace `restore_cursor_background` with a real save-under

`restore_cursor_background` (wm.c:731) is fundamentally unsafe: it
restores *gradient wallpaper pixels* at the old cursor position, but
the cursor may have been over a window, the taskbar, a popup, or the
clock digits. Restoring the wrong pixels is the second half of the
"green box" complaint.

The fix is the textbook *save-under* technique:

1. In `gui/wm.c`, add a single static buffer large enough to hold the
   cursor's bounding box:

   ```c
   static u32 cursor_save[CURSOR_W * CURSOR_H];
   static bool cursor_save_valid = false;
   ```

2. Before drawing the new cursor at `(m.x, m.y)`, copy the
   backbuffer pixels that the cursor is about to overwrite into
   `cursor_save`:

   ```c
   static void cursor_save_under(i32 x, i32 y) {
       const i32 W = (i32)gfx_width();
       const i32 H = (i32)gfx_height();
       for (i32 j = 0; j < CURSOR_H; j++) {
           for (i32 i = 0; i < CURSOR_W; i++) {
               const i32 px = x + i;
               const i32 py = y + j;
               if (px >= 0 && py >= 0 && px < W && py < H) {
                   cursor_save[j * CURSOR_W + i] =
                       gfx_getpixel(px, py);
               } else {
                   cursor_save[j * CURSOR_W + i] = 0;
               }
           }
       }
       cursor_save_valid = true;
   }
   ```

3. To restore, blit the saved pixels back to the same coordinates
   and register the area as damaged:

   ```c
   static void cursor_restore_under(i32 x, i32 y) {
       if (!cursor_save_valid) return;
       const i32 W = (i32)gfx_width();
       const i32 H = (i32)gfx_height();
       for (i32 j = 0; j < CURSOR_H; j++) {
           for (i32 i = 0; i < CURSOR_W; i++) {
               const i32 px = x + i;
               const i32 py = y + j;
               if (px >= 0 && py >= 0 && px < W && py < H) {
                   gfx_putpixel(px, py,
                                 cursor_save[j * CURSOR_W + i]);
               }
           }
       }
       /* register damage so the next flip pushes the restored pixels */
       gfx_damage_add(x - 1, y - 1, CURSOR_W + 2, CURSOR_H + 2);
   }
   ```

   The `gfx_putpixel` writes here are safe even though `gfx_putpixel`
   doesn't auto-register damage — we register the rect explicitly.

4. In the `m.moved` branch (wm.c:1010), replace the body with:

   ```c
   } else if (m.moved) {
       /* 1. Restore the previous cursor position from the save-under. */
       cursor_restore_under(cursor_old_x, cursor_old_y);

       /* 2. Save what's underneath the new cursor position. */
       cursor_save_under(m.x, m.y);

       /* 3. Draw the new cursor. (Step 1 made this safe.) */
       cursor_draw_simple(m.x, m.y);

       /* 4. Flip everything we touched. */
       gfx_flip_damaged();

       cursor_old_x = m.x;
       cursor_old_y = m.y;
   }
   ```

   Note that **the old call to `draw_desktop()` is gone.** This is
   the fix for the stutter complaint: the mouse-only path is now
   strictly a save/restore/draw/flip over the cursor's bounding box.

### Step 3 — Invalidate the save-under on dirty frames

When the dirty (`if (dirty)`) branch repaints the whole desktop, the
backbuffer at the cursor's current position now contains the *new*
post-redraw pixels (gradient, taskbar, whatever is there), not the
pixels we saved. If we don't refresh the save-under, the next
`m.moved` branch will restore the *pre-redraw* pixels at the old
position — i.e. it will erase the new taskbar clock back to whatever
was under the cursor before the redraw.

At the *bottom* of the `if (dirty)` block (wm.c:969, after
`gfx_flip_damaged()` returns), add:

```c
/* The save-under is now stale; refresh it for the new cursor position. */
cursor_save_under(m.x, m.y);
cursor_save_valid = true;
```

Order matters: this *must* run after `gfx_flip_damaged()` so the
backbuffer reflects the freshly-flipped state.

### Step 4 — Fix the latent init bug

Change the initial values of `cursor_old_x` / `cursor_old_y`
(wm.c:61–62) from `0` to `-1` (or any sentinel < 0). In the `m.moved`
branch, skip the restore if `cursor_old_x < 0`:

```c
} else if (m.moved) {
    if (cursor_old_x >= 0) {
        cursor_restore_under(cursor_old_x, cursor_old_y);
    }
    cursor_save_under(m.x, m.y);
    cursor_draw_simple(m.x, m.y);
    gfx_flip_damaged();
    cursor_old_x = m.x;
    cursor_old_y = m.y;
}
```

Equivalently, initialize `cursor_old_x/y` to `cursor_x/cursor_y` from
`wm_init`. Either works; the sentinel is cheaper and more defensive.

### Step 5 (optional, recommended) — drop the old code

Once Steps 1–4 are in:

* `restore_cursor_background` (wm.c:731) becomes dead code — delete
  it.
* `desktop_cache` is still used by the dirty path's
  `draw_cached_desktop` and remains useful; leave it alone.
* The old `gfx_damage_add(cursor_old_x - 1, ...)` and
  `gfx_damage_add(m.x - 1, ...)` calls in the `m.moved` branch
  (wm.c:1017, 1019) are no longer needed because `cursor_restore_under`
  and `cursor_draw_simple` (with Step 1) register their own damage.

## 4. Why the Fix Works

* **Step 1** closes the primary leak: every cursor blit is now
  accounted for in the damage list, so `gfx_flip_damaged()` always
  pushes the cursor to the framebuffer in the same flip that pushes
  everything else. The green-block artifact cannot occur.
* **Step 2** removes the unsafe gradient-only restore. The cursor can
  now move over windows, the taskbar, the clock, popups — anything
  — and the save-under will reproduce exactly what was underneath,
  not the wallpaper.
* **Step 3** keeps the save-under consistent with the post-flip
  backbuffer. Without it, dragging a window would corrupt the
  save-under and the next cursor move would briefly erase the
  freshly-drawn window content.
* **Step 4** prevents the very first `m.moved` branch from blitting a
  12×19 gradient patch at (0, 0) before the cursor has moved at all.

After all four steps, the `m.moved` branch is O(CURSOR_W × CURSOR_H)
— 228 pixel reads + 228 pixel writes + 1 sprite draw per cursor
sample, with no window redraw and no `draw_desktop()`. Stutter
disappears.

## 5. Damage-Coalescing Rule (read this before changing the cursor code)

`gfx_damage_add()` (graphics.c:518) merges any new rect with an
existing alive rect that overlaps or is adjacent. Adjacent is
important: the +2 / -1 padding in Step 1 and Step 2 makes the
"old cursor" and "new cursor" rects *adjacent* (when the cursor moves
by ≤ cursor_size + 1 pixels, which is always), so they always
coalesce into one rect, which always gets flipped in one `flip_one`.
Do not remove the padding without also accepting the possibility of
two separate `flip_one` calls per cursor sample, which doubles the
framebuffer MMIO traffic for the cursor.

## 6. Verification

1. **Build** (`make clean && make` on Replit). The `KryspinOS.iso`
   artefact must be regenerated.
2. **Run in QEMU** (`qemu-system-i386 -cdrom KryspinOS.iso -m 512`).
3. **Reproduce the user's screenshots:**
   * Move the mouse in a straight line from corner to corner across
     the desktop — there must be **no** green/teal block left in the
     wake of the cursor. This is the regression test for problem2
     and problem3.
   * Move the cursor over a window — the window content under the
     cursor must be preserved exactly when the cursor moves away.
     This is the regression test for the save-under.
   * Move the cursor over the clock in the taskbar — when the cursor
     moves away, the clock digits must still read correctly. This
     catches a save-under bug that the simple "move over wallpaper"
     test would miss.
   * Drag a window across the screen while moving the mouse over it —
     no stutter, no torn sprites. This catches a missed Step 3.
   * Open the search popup and move the cursor over it — when the
     cursor moves away, the popup must still render correctly.
4. **Long-session sanity check:** leave the desktop idle for 5
   minutes, then start moving the mouse rapidly. The first move
   after the idle period must not flash a gradient patch anywhere on
   the screen (regression test for Step 4).

## 7. Risk Assessment

* **Low risk.** All changes are localized to `gui/wm.c` (and one
  optional new primitive in `gfx/graphics.c` for Option B). The
  damage API is already exercised by every other drawing call, so
  we're not introducing a new contract.
* **Performance.** The mouse-only branch goes from
  O(desktop + windows + taskbar) to O(228). On the target hardware
  this is the difference between a visible stutter and a smooth
  cursor. The save-under adds one backbuffer read per cursor sample
  — negligible at 60 Hz sample rate.
* **Correctness.** The save-under is exact: it captures whatever
  pixels the cursor is about to overwrite, regardless of whether
  those pixels are wallpaper, window content, or taskbar. There is
  no class of "what if the cursor was over something that changed
  between save and restore" bug — between save and restore is at
  most one PIT tick (4 ms at 250 Hz), during which the damage API is
  *not* invoked because the only caller of the dirty path is the
  PIT handler, and the only caller of the m.moved path is the WM
  loop. They do not race.

## 8. Extra Requirement: No App Should Be Active Unless the User Opened It

### 8.1 Observed problem

When the user opens **Task Manager** on a fresh boot, they see
File Explorer (and possibly other apps) listed as a "Running"
process even though they never clicked its taskbar button.

Reproduced by reading `gui/wm.c`:

* `wm_init` (wm.c:680) calls `wm_open_explorer()` (wm.c:702) **unconditionally
  at boot**. File Explorer is therefore an "important thing" the WM
  is preloading on the user's behalf.
* `wm_process_count()` (wm.c:249) starts at `count = 2` (Kernel + WM)
  and then **adds 1 for every slot with `used == true`** in the
  `windows[]` array.
* `wm_process_name(i)` (wm.c:258) returns `windows[i].title` for every
  used slot, so the Task Manager (`apps/taskmgr.c:31,63`) prints each
  title with a green "Running" badge.

So the moment the user clicks the Task Manager taskbar button, the
"Running" list contains: `KryspinOS Kernel`, `Window Manager`,
`File Explorer`. The user only opened two things (the desktop, then
Task Manager) but Task Manager shows three processes.

This violates the principle of least surprise: an application that
the user never explicitly started should not appear in the process
list, and the desktop should start with **zero** third-party
processes — only the kernel and the WM itself.

### 8.2 What "important" means

For the purposes of this plan, the only "important" auto-started
component is the **kernel** (`KryspinOS Kernel` in the process list)
and the **window manager** (`Window Manager` in the process list).
Everything else — Explorer, Notepad, Terminal, System Information,
Task Manager, Search popup, Power menu — must be opt-in by the user
clicking a taskbar button.

### 8.3 Fix

#### Step A — Do not auto-open File Explorer at boot

In `wm_init` (wm.c:680), remove the call to `wm_open_explorer()` at
line 702. The function should end with `mouse_bounds(...)` and
nothing else.

The rest of the boot path is unaffected: `pit_init` keeps running,
the PIT handler still fires every 4 ticks, `dirty` is still set, and
the WM repaints an empty desktop (just the wallpaper, the
"branding" rectangle from `draw_desktop_chrome`, and the taskbar).

The user now sees: the wallpaper, the four-line "KryspinOS / A small,
fast 32-bit graphical operating system / Open an app from the
taskbar to get started" prompt at the top-left (already drawn by
`draw_desktop_chrome` in `wm.c:501`), and the empty taskbar. They
have to click a taskbar button to open an app. This is the correct
UX for a "fresh boot" experience.

#### Step B — Make `wm_process_count()` and `wm_process_name()` match the real process list

The current implementations (wm.c:249, 258) count *every* used
window. After Step A this no longer leaks "auto-started apps" into
the process list, but it still has a subtler bug: it counts
**focus-only** windows and **popup overlays** (search, power menu,
taskbar context menu) as if they were standalone applications.

The popup handling in `wm_update` (around wm.c:796, 815, 823, 924,
etc.) sets `search_active` / `power_menu` / `taskbar_menu` flags but
does **not** create a `struct window` for them — they are drawn
directly by `draw_popups` (wm.c:578). Good, they are not counted.

But Task Manager itself and any app currently focused should not
be invisible to the user. The current logic already handles that
correctly: every `wm_open_*` call goes through `wm_create` which marks
`windows[].used = true`, and closing sets `used = false` (wm.c:170).
After Step A the only window that will exist on a fresh boot is
whatever the user just opened.

Therefore no code change is required to `wm_process_count` /
`wm_process_name` themselves; the fix is purely in `wm_init`.

#### Step C — Verify the taskbar still opens the expected apps

After Step A, the four taskbar buttons in `wm_update` (wm.c:835–844,
934–940) must continue to call:

* Explorer button → `wm_open_explorer()`
* Notepad button → `wm_open_notepad("untitled.txt")`
* Terminal button → `wm_open_terminal()`
* System button → `wm_open_system_info()`

plus the right-click "Task Manager" entry → `wm_open_task_manager()`.

**No changes required to these call sites.** They already only fire
on user click (the `m.left && !was_left` test on wm.c:796). Step A
only removes the eager, boot-time call.

#### Step D (defensive) — Add a comment on `wm_init` documenting the rule

In `wm_init`, replace the line `wm_open_explorer();` (removed in
Step A) with a comment block so a future contributor doesn't
re-introduce the eager-start:

```c
/*
 * Do NOT eagerly open apps here. The desktop boots with only the
 * kernel and the window manager running, and Task Manager should
 * show exactly two processes. Users open apps by clicking taskbar
 * buttons. Any eager wm_open_*() call on this path re-introduces
 * the bug fixed in plan.md §8.
 */
```

### 8.4 Verification

1. **Fresh boot, no clicks.** Open Task Manager. The process list
   must show **exactly**:
   * `KryspinOS Kernel`
   * `Window Manager`

   Nothing else. File Explorer must not appear.

2. **Open Explorer, close Explorer, reopen Task Manager.** Task
   Manager must now show three processes (Kernel, WM, File
   Explorer). Close Explorer (red X). Reopen Task Manager.
   Explorer must be gone — count is back to two. This proves the
   close path correctly removes the process.

3. **Open four apps, then Task Manager.** Process list must show:
   Kernel, WM, File Explorer, Notepad, Terminal, System Information
   (six total). Close all four. Reopen Task Manager. Count is back
   to two.

4. **Trigger the search popup (click the search box, type a
   character).** Task Manager's process count must **not** change.
   The search popup is not a `struct window`; it must not show up.

5. **Right-click the taskbar to open the context menu (Power menu,
   Task Manager entry).** Task Manager's process count must **not**
   change while the menu is open. (It only changes when the user
   actually clicks the Task Manager entry and the new window is
   created.)

### 8.5 Risk Assessment

* **Low risk.** The change is a single-line deletion in `wm_init`
  plus a comment. The four taskbar button handlers are already
  guarded by `m.left && !was_left` and need no change.
* **No performance impact.** Removing the boot-time call avoids
  one `wm_create` + one `explorer_setup` per boot. The `windows[0]`
  slot is now `used = false` on a fresh boot, which is a *correct*
  state — Task Manager and the WM run on a clean slate.
* **User-visible behaviour change.** The desktop no longer has an
  Explorer window at boot. This is intentional and matches the
  spec; if a contributor later decides to pre-open Explorer, the
  comment in `wm_init` (Step D) makes the trade-off explicit so it
  can be discussed rather than re-introduced silently.
* **No interaction with §1–§7.** The boot-time `wm_open_explorer`
  call was unrelated to the cursor / damage fixes. Both sets of
  changes can be merged independently.
