#include <apps.h>
#include <window.h>
#include <vfs.h>
#include <gfx.h>
#include <heap.h>
#include <string.h>

#define NP_CAP 4096

struct notepad_state {
    char path[VFS_NAME_MAX];
    char *buf;
    u32 len;
    u32 cursor;
    char status[40];
};

static struct notepad_state *active_np;

static void np_save(struct notepad_state *st) {
    int fd = vfs_fopen(st->path, "w");
    if (fd < 0) {
        strncpy(st->status, "Save failed", sizeof(st->status) - 1);
        st->status[sizeof(st->status) - 1] = 0;
        return;
    }
    if (st->len) {
        vfs_fwrite(fd, st->buf, st->len);
    }
    vfs_fclose(fd);
    strncpy(st->status, "Saved", sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = 0;
}

static void notepad_paint(struct window *w) {
    struct notepad_state *st = (struct notepad_state *)w->data;
    i32 x, y, i;
    
    /* Clear the window content area first to prevent artifacts */
    gfx_rect(w->x, w->y + WM_TITLE_H, w->w, w->h - WM_TITLE_H, COLOR_RGB(255, 255, 255));
    
    gfx_rect(w->x + 8, w->y + WM_TITLE_H + 4, 48, 14, COLOR_RGB(56, 132, 220));
    gfx_text_transparent(w->x + 16, w->y + WM_TITLE_H + 7, "Save", COLOR_RGB(255, 255, 255));
    gfx_text_transparent(w->x + 64, w->y + WM_TITLE_H + 7, st->path, COLOR_RGB(40, 50, 70));
    gfx_text_transparent(w->x + 200, w->y + WM_TITLE_H + 7, st->status, COLOR_RGB(30, 120, 70));

    x = w->x + 10;
    y = w->y + WM_TITLE_H + 24;
    for (i = 0; (u32)i <= st->len; i++) {
        if ((u32)i == st->cursor) {
            gfx_rect(x, y, 1, 8, COLOR_RGB(20, 24, 32));
        }
        if ((u32)i == st->len) {
            break;
        }
        if (st->buf[i] == '\n') {
            x = w->x + 10;
            y += 10;
            if (y > w->y + w->h - 16) {
                break;
            }
            continue;
        }
        gfx_char(x, y, st->buf[i], COLOR_RGB(20, 24, 32), 0xFFFFFFFF);
        x += 8;
        if (x > w->x + w->w - 16) {
            x = w->x + 10;
            y += 10;
            if (y > w->y + w->h - 16) {
                break;
            }
        }
    }
}

static void notepad_key(struct window *w, char c) {
    struct notepad_state *st = (struct notepad_state *)w->data;
    st->status[0] = 0;
    if (c == '\b') {
        if (st->cursor > 0) {
            memmove(st->buf + st->cursor - 1, st->buf + st->cursor, st->len - st->cursor);
            st->cursor--;
            st->len--;
            st->buf[st->len] = 0;
        }
        return;
    }
    if (c == 27) {
        return;
    }
    if (st->len + 1 >= NP_CAP) {
        return;
    }
    memmove(st->buf + st->cursor + 1, st->buf + st->cursor, st->len - st->cursor);
    st->buf[st->cursor] = c;
    st->cursor++;
    st->len++;
    st->buf[st->len] = 0;
}

static void notepad_click(struct window *w, i32 lx, i32 ly) {
    struct notepad_state *st = (struct notepad_state *)w->data;
    if (ly < 16 && lx < 56) {
        np_save(st);
    }
}

void notepad_setup(struct window *w, const char *path) {
    struct notepad_state *st = kcalloc(1, sizeof(*st));
    int fd;
    if (!st) {
        return; /* Allocation failed */
    }
    st->buf = kcalloc(1, NP_CAP);
    if (!st->buf) {
        /* Allocation failed - clean up and return */
        kfree(st);
        return;
    }
    strncpy(st->path, path ? path : "untitled.txt", VFS_NAME_MAX - 1);
    st->path[VFS_NAME_MAX - 1] = 0;
    fd = vfs_fopen(st->path, "r");
    if (fd >= 0) {
        int n = vfs_fread(fd, st->buf, NP_CAP - 1);
        if (n > 0) {
            st->len = (u32)n;
            st->cursor = st->len;
        }
        vfs_fclose(fd);
    }
    w->data = st;
    w->paint = notepad_paint;
    w->key = notepad_key;
    w->click = notepad_click;
    strncpy(w->title, "Notepad", sizeof(w->title) - 1);
    w->title[sizeof(w->title) - 1] = 0;
    active_np = st;
}

void notepad_open_path(const char *path) {
    (void)path;
}
