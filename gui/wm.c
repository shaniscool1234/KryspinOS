#include <window.h>
#include <wm.h>
#include <apps.h>
#include <gfx.h>
#include <mouse.h>
#include <keyboard.h>
#include <string.h>
#include <heap.h>
#include <isr.h>
#include <ports.h>

#define COL_DESK   COLOR_RGB(24, 48, 80)
#define COL_TASK   COLOR_RGB(18, 22, 32)
#define COL_ACCENT COLOR_RGB(56, 132, 220)
#define COL_WIN    COLOR_RGB(236, 238, 242)
#define COL_TITLE  COLOR_RGB(40, 74, 120)
#define COL_TITLEF COLOR_RGB(48, 110, 186)
#define COL_TEXT   COLOR_RGB(20, 24, 32)
#define COL_WHITE  COLOR_RGB(255, 255, 255)
#define COL_SHADOW COLOR_RGB(8, 12, 20)
#define COL_BTN    COLOR_RGB(70, 78, 96)

#define CURSOR_W 12
#define CURSOR_H 19

static struct window windows[WM_MAX_WINDOWS];
static int z_top;
static struct window *focused;
static volatile u32 ticks;
static bool dirty = true;

static u32 cursor_save[CURSOR_W * CURSOR_H];
static i32 cursor_sx = -1, cursor_sy = -1;
static bool cursor_saved;

/* Classic arrow: 1 = black outline, 2 = white fill */
static const u8 cursor_sprite[CURSOR_H][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0}
};

static void pit_irq(struct regs *r) {
    (void)r;
    ticks++;
    if ((ticks % 10) == 0) {
        dirty = true;
    }
}

static void pit_init(void) {
    u32 div = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
    irq_register(0, pit_irq);
}

static int hit_window(i32 x, i32 y) {
    int best = -1, i, z = -1;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        struct window *w = &windows[i];
        if (!w->used) {
            continue;
        }
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) {
            if (w->z >= z) {
                z = w->z;
                best = i;
            }
        }
    }
    return best;
}

struct window *wm_create(const char *title, i32 x, i32 y, i32 w, i32 h) {
    int i;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i].used) {
            struct window *win = &windows[i];
            memset(win, 0, sizeof(*win));
            win->used = true;
            strncpy(win->title, title, sizeof(win->title) - 1);
            win->x = x;
            win->y = y;
            win->w = w;
            win->h = h;
            win->z = ++z_top;
            focused = win;
            dirty = true;
            return win;
        }
    }
    return NULL;
}

void wm_focus(struct window *w) {
    if (!w) {
        return;
    }
    w->z = ++z_top;
    focused = w;
    dirty = true;
}

struct window *wm_focused(void) {
    return focused;
}

void wm_open_notepad(const char *path) {
    struct window *w = wm_create("Notepad", 220, 80, 520, 360);
    if (w) {
        notepad_setup(w, path);
    }
}

static void cursor_restore(void) {
    i32 i, j;
    if (!cursor_saved) {
        return;
    }
    for (j = 0; j < CURSOR_H; j++) {
        for (i = 0; i < CURSOR_W; i++) {
            gfx_putpixel(cursor_sx + i, cursor_sy + j, cursor_save[j * CURSOR_W + i]);
        }
    }
    cursor_saved = false;
}

static void cursor_draw(i32 x, i32 y) {
    i32 i, j;
    cursor_restore();
    cursor_sx = x;
    cursor_sy = y;
    for (j = 0; j < CURSOR_H; j++) {
        for (i = 0; i < CURSOR_W; i++) {
            cursor_save[j * CURSOR_W + i] = gfx_getpixel(x + i, y + j);
        }
    }
    cursor_saved = true;
    for (j = 0; j < CURSOR_H; j++) {
        for (i = 0; i < CURSOR_W; i++) {
            u8 p = cursor_sprite[j][i];
            if (p == 1) {
                gfx_putpixel(x + i, y + j, COLOR_RGB(0, 0, 0));
            } else if (p == 2) {
                gfx_putpixel(x + i, y + j, COL_WHITE);
            }
        }
    }
}

static void draw_window(struct window *w) {
    u32 titlec = (w == focused) ? COL_TITLEF : COL_TITLE;
    gfx_rect(w->x + 3, w->y + 3, w->w, w->h, COL_SHADOW);
    gfx_rect(w->x, w->y, w->w, w->h, COL_WIN);
    gfx_rect(w->x, w->y, w->w, WM_TITLE_H, titlec);
    gfx_rect_border(w->x, w->y, w->w, w->h, COLOR_RGB(10, 14, 22));
    gfx_text_transparent(w->x + 8, w->y + 7, w->title, COL_WHITE);
    gfx_rect(w->x + w->w - 18, w->y + 4, 14, 14, COLOR_RGB(180, 64, 64));
    gfx_text_transparent(w->x + w->w - 15, w->y + 7, "x", COL_WHITE);
    if (w->paint) {
        w->paint(w);
    }
}

static void draw_desktop(void) {
    int i, n;
    struct window *order[WM_MAX_WINDOWS];
    char bar[64];
    u32 sec;

    gfx_fill(COL_DESK);
    gfx_text_transparent(24, 24, "cursorOS", COL_WHITE);
    gfx_text_transparent(24, 36, "File Explorer  |  Notepad", COLOR_RGB(180, 200, 230));

    n = 0;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used) {
            order[n++] = &windows[i];
        }
    }
    /* simple z-sort */
    {
        int a, b;
        for (a = 0; a < n; a++) {
            for (b = a + 1; b < n; b++) {
                if (order[b]->z < order[a]->z) {
                    struct window *t = order[a];
                    order[a] = order[b];
                    order[b] = t;
                }
            }
        }
    }
    for (i = 0; i < n; i++) {
        draw_window(order[i]);
    }

    gfx_rect(0, (i32)gfx_height() - WM_TASKBAR_H, (i32)gfx_width(), WM_TASKBAR_H, COL_TASK);
    gfx_rect(0, (i32)gfx_height() - WM_TASKBAR_H, 4, WM_TASKBAR_H, COL_ACCENT);
    gfx_text_transparent(12, (i32)gfx_height() - 20, "cursorOS", COL_WHITE);

    gfx_rect(90, (i32)gfx_height() - 24, 88, 20, COL_BTN);
    gfx_text_transparent(98, (i32)gfx_height() - 18, "Explorer", COL_WHITE);
    gfx_rect(186, (i32)gfx_height() - 24, 80, 20, COL_BTN);
    gfx_text_transparent(198, (i32)gfx_height() - 18, "Notepad", COL_WHITE);

    sec = ticks / 100;
    bar[0] = '0' + (char)((sec / 60) / 10);
    bar[1] = '0' + (char)((sec / 60) % 10);
    bar[2] = ':';
    bar[3] = '0' + (char)((sec % 60) / 10);
    bar[4] = '0' + (char)(sec % 60 % 10);
    bar[5] = 0;
    gfx_text_transparent((i32)gfx_width() - 56, (i32)gfx_height() - 18, bar, COL_WHITE);
}

void wm_init(void) {
    struct window *ex;
    memset(windows, 0, sizeof(windows));
    z_top = 0;
    focused = NULL;
    pit_init();
    mouse_bounds((i32)gfx_width(), (i32)gfx_height());
    ex = wm_create("File Explorer", 40, 60, 360, 400);
    if (ex) {
        explorer_setup(ex);
    }
    wm_open_notepad("readme.txt");
}

void wm_update(void) {
    struct mouse_state m;
    char ch;
    int hi;
    static bool was_left;
    i32 tb;

    mouse_poll(&m);
    tb = (i32)gfx_height() - WM_TASKBAR_H;

    if (m.left && !was_left) {
        if (m.y >= tb) {
            if (m.x >= 90 && m.x < 178) {
                struct window *ex = wm_create("File Explorer", 48, 70, 360, 400);
                if (ex) {
                    explorer_setup(ex);
                }
            } else if (m.x >= 186 && m.x < 266) {
                wm_open_notepad("untitled.txt");
            }
            dirty = true;
        } else {
            hi = hit_window(m.x, m.y);
            if (hi >= 0) {
                struct window *w = &windows[hi];
                wm_focus(w);
                if (m.x >= w->x + w->w - 18 && m.x < w->x + w->w - 4 &&
                    m.y >= w->y + 4 && m.y < w->y + 18) {
                    w->used = false;
                    if (focused == w) {
                        focused = NULL;
                    }
                } else if (m.y < w->y + WM_TITLE_H) {
                    w->dragging = true;
                    w->drag_ox = m.x - w->x;
                    w->drag_oy = m.y - w->y;
                } else if (w->click) {
                    w->click(w, m.x - w->x, m.y - w->y - WM_TITLE_H);
                }
                dirty = true;
            }
        }
    }
    if (!m.left) {
        int i;
        for (i = 0; i < WM_MAX_WINDOWS; i++) {
            windows[i].dragging = false;
        }
    } else {
        int i;
        for (i = 0; i < WM_MAX_WINDOWS; i++) {
            if (windows[i].used && windows[i].dragging) {
                windows[i].x = m.x - windows[i].drag_ox;
                windows[i].y = m.y - windows[i].drag_oy;
                if (windows[i].x < 0) windows[i].x = 0;
                if (windows[i].y < 0) windows[i].y = 0;
                dirty = true;
            }
        }
    }
    was_left = m.left;

    while (keyboard_read(&ch)) {
        if (focused && focused->used && focused->key) {
            focused->key(focused, ch);
            dirty = true;
        }
    }

    if (dirty) {
        cursor_saved = false;
        draw_desktop();
        cursor_draw(m.x, m.y);
        dirty = false;
    } else if (m.moved) {
        cursor_draw(m.x, m.y);
    }
}
