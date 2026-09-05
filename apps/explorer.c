#include <apps.h>
#include <window.h>
#include <wm.h>
#include <vfs.h>
#include <gfx.h>
#include <heap.h>
#include <string.h>

struct explorer_state {
    struct vfs_dirent ents[32];
    int count;
};

static void explorer_refresh(struct explorer_state *st) {
    st->count = vfs_list(st->ents, 32);
}

static void explorer_paint(struct window *w) {
    struct explorer_state *st = (struct explorer_state *)w->data;
    int i;
    /*
     * The "Refresh" button is drawn 4..18 px below the title bar. The
     * file rows must start below the button (>= 22 px below the title
     * bar) or the first row's rectangle overdraws the button's bottom
     * edge. The previous value (+ 8) put the first row at + 8, which
     * overlapped the button from + 8 to + 18.
     */
    i32 y = w->y + WM_TITLE_H + 22;

    /* Clear the window content area first to prevent artifacts */
    gfx_rect(w->x, w->y + WM_TITLE_H, w->w, w->h - WM_TITLE_H, COLOR_RGB(255, 255, 255));

    gfx_text(w->x + 10, y, "Root directory  (click to open)", COLOR_RGB(40, 50, 70), 0xFFFFFFFF);
    y += 16;
    gfx_rect(w->x + w->w - 70, w->y + WM_TITLE_H + 4, 56, 14, COLOR_RGB(56, 132, 220));
    gfx_text_transparent(w->x + w->w - 62, w->y + WM_TITLE_H + 7, "Refresh", COLOR_RGB(255, 255, 255));
    /*
     * The "Root directory" header sits between the Refresh button and
     * the first row. Reset y to just below the button so rows don't
     * overlap the header band either.
     */
    y = w->y + WM_TITLE_H + 38;
    for (i = 0; i < st->count; i++) {
        char line[48];
        int n = 0;
        const char *nm = st->ents[i].name;
        while (nm[n] && n < 24) {
            line[n] = nm[n];
            n++;
        }
        line[n] = 0;
        gfx_rect(w->x + 10, y, w->w - 20, 18, COLOR_RGB(220, 226, 234));
        if (st->ents[i].type == VFS_DIR) {
            gfx_rect(w->x + 16, y + 5, 13, 9, COLOR_RGB(247, 188, 67));
            gfx_rect(w->x + 18, y + 3, 6, 3, COLOR_RGB(247, 188, 67));
        } else {
            gfx_rect(w->x + 18, y + 3, 10, 13, COLOR_RGB(112, 184, 248));
            gfx_rect(w->x + 20, y + 6, 6, 1, COLOR_RGB(255, 255, 255));
            gfx_rect(w->x + 20, y + 9, 6, 1, COLOR_RGB(255, 255, 255));
        }
        gfx_text(w->x + 38, y + 5, line, COLOR_RGB(20, 24, 32), 0xFFFFFFFF);
        y += 22;
        if (y > w->y + w->h - 20) {
            break;
        }
    }
}

static void explorer_click(struct window *w, i32 lx, i32 ly) {
    struct explorer_state *st = (struct explorer_state *)w->data;
    int i;
    /*
     * Click coordinate map (matches explorer_paint):
     *   Refresh button:     0..18  px below title bar
     *   "Root directory" hdr:22..30 px below title bar (16 px tall)
     *   First file row:      38..56 px below title bar (18 px tall)
     * Rows repeat every 22 px starting at +38.
     */
    if (ly < 16 && lx > w->w - 70) {
        explorer_refresh(st);
        return;
    }
    if (ly < 38) {
        return;
    }
    i = (ly - 38) / 22;
    if (i >= 0 && i < st->count && st->ents[i].type == VFS_FILE) {
        wm_open_notepad(st->ents[i].name);
    }
}

void explorer_setup(struct window *w) {
    struct explorer_state *st = kcalloc(1, sizeof(*st));
    if (!st) {
        return; /* Allocation failed */
    }
    w->data = st;
    w->paint = explorer_paint;
    w->click = explorer_click;
    w->key = NULL;
    explorer_refresh(st);
}
