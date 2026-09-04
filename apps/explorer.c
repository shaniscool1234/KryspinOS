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
    i32 y = w->y + WM_TITLE_H + 8;
    gfx_text(w->x + 10, y, "Root directory  (click to open)", COLOR_RGB(40, 50, 70), 0xFFFFFFFF);
    y += 16;
    gfx_rect(w->x + w->w - 70, w->y + WM_TITLE_H + 4, 56, 14, COLOR_RGB(56, 132, 220));
    gfx_text_transparent(w->x + w->w - 62, w->y + WM_TITLE_H + 7, "Refresh", COLOR_RGB(255, 255, 255));
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
        gfx_text(w->x + 16, y + 5, st->ents[i].type == VFS_DIR ? "[DIR]" : "[FILE]",
                 COLOR_RGB(40, 90, 160), 0xFFFFFFFF);
        gfx_text(w->x + 64, y + 5, line, COLOR_RGB(20, 24, 32), 0xFFFFFFFF);
        y += 22;
        if (y > w->y + w->h - 20) {
            break;
        }
    }
}

static void explorer_click(struct window *w, i32 lx, i32 ly) {
    struct explorer_state *st = (struct explorer_state *)w->data;
    int i;
    if (ly < 16 && lx > w->w - 70) {
        explorer_refresh(st);
        return;
    }
    if (ly < 24) {
        return;
    }
    i = (ly - 24) / 22;
    if (i >= 0 && i < st->count && st->ents[i].type == VFS_FILE) {
        wm_open_notepad(st->ents[i].name);
    }
}

void explorer_setup(struct window *w) {
    struct explorer_state *st = kcalloc(1, sizeof(*st));
    w->data = st;
    w->paint = explorer_paint;
    w->click = explorer_click;
    w->key = NULL;
    explorer_refresh(st);
}
