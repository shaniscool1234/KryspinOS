#ifndef CURSOROS_WINDOW_H
#define CURSOROS_WINDOW_H

#include <types.h>

#define WM_MAX_WINDOWS 8
#define WM_TITLE_H     22
#define WM_TASKBAR_H   28

struct window {
    bool used;
    char title[40];
    i32 x, y, w, h;
    i32 z;
    bool dragging;
    i32 drag_ox, drag_oy;
    void (*paint)(struct window *self);
    void (*key)(struct window *self, char c);
    void (*click)(struct window *self, i32 lx, i32 ly);
    void *data;
};

struct window *wm_create(const char *title, i32 x, i32 y, i32 w, i32 h);
void wm_focus(struct window *w);
struct window *wm_focused(void);

#endif
