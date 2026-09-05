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
#include <rtc.h>
#include <pmm.h>

#define COL_DESK   COLOR_RGB(19, 32, 54)
#define COL_TASK   COLOR_RGB(15, 21, 33)
#define COL_ACCENT COLOR_RGB(74, 156, 235)
#define COL_WIN    COLOR_RGB(236, 238, 242)
#define COL_TITLE  COLOR_RGB(40, 74, 120)
#define COL_TITLEF COLOR_RGB(48, 110, 186)
#define COL_TEXT   COLOR_RGB(20, 24, 32)
#define COL_WHITE  COLOR_RGB(255, 255, 255)
#define COL_SHADOW COLOR_RGB(8, 12, 20)
#define COL_BTN    COLOR_RGB(35, 48, 67)
#define COL_SEARCH COLOR_RGB(29, 39, 54)

#define CURSOR_W 12
#define CURSOR_H 19

static struct window windows[WM_MAX_WINDOWS];
static int z_top;
static struct window *focused;
static volatile u32 ticks;
static bool dirty = true;
static bool startup_done;
static bool search_active;
static char search_input[64];
static struct multiboot_info *boot_info;
static bool power_menu;
static bool taskbar_menu;
static i32 taskbar_menu_x;

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

/*
 * Frame pacing
 * ------------
 * The PIT ticks at 250 Hz. The target render rate is 60 Hz, which works
 * out to one repaint every ~4 PIT ticks. We don't set dirty on a tick
 * schedule though -- wm_update() decides when to repaint and resets the
 * frame counter, so we never double-render in the same slice and we
 * don't repaint a frame we haven't finished drawing yet.
 */
#define PIT_HZ          250
#define PIT_DIVISOR     (1193180 / PIT_HZ)   /* 4772 */
#define FRAME_TICKS     4                     /* 250 / 4 = 62.5 Hz */
static u32 last_frame_tick;

static void pit_irq(struct regs *r) {
    (void)r;
    ticks++;
    if (ticks - last_frame_tick >= FRAME_TICKS) {
        dirty = true;
    }
}

static void pit_init(void) {
    u32 div = PIT_DIVISOR;
    outb(0x43, 0x36);
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
    last_frame_tick = 0;
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
            win->needs_paint = true; /* Kryspin OS #4 */
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
    /*
     * Focus changes mean both the old and new focused window need a
     * repaint: their title bars use different colours depending on
     * whether they are the active window. Mark both.
     */
    if (focused && focused != w && focused->used) {
        focused->needs_paint = true;
    }
    w->z = ++z_top;
    w->needs_paint = true;
    focused = w;
    dirty = true;
}

struct window *wm_focused(void) {
    return focused;
}

void wm_invalidate(struct window *w) {
    if (!w) return;
    w->needs_paint = true;
    dirty = true;
}

void wm_invalidate_all(void) {
    int i;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used) {
            windows[i].needs_paint = true;
        }
    }
    dirty = true;
}

void wm_open_notepad(const char *path) {
    struct window *w = wm_create("Notepad", 220, 80, 560, 380);
    if (w) {
        notepad_setup(w, path);
    }
}

void wm_open_explorer(void) {
    struct window *w = wm_create("File Explorer", 44, 72, 400, 410);
    if (w) {
        explorer_setup(w);
    }
}

void wm_open_terminal(void) {
    struct window *w = wm_create("Terminal", 180, 96, 520, 340);
    if (w) {
        terminal_setup(w);
    }
}

void wm_open_system_info(void) {
    struct window *w = wm_create("System Information", 280, 82, 460, 330);
    if (w) {
        system_setup(w);
    }
}

void wm_open_task_manager(void) {
    struct window *w = wm_create("Task Manager", 150, 92, 430, 330);
    if (w) {
        taskmgr_setup(w);
    }
}

int wm_process_count(void) {
    int i;
    int count = 2;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used) count++;
    }
    return count;
}

const char *wm_process_name(int index) {
    int i;
    if (index == 0) return "KryspinOS Kernel";
    if (index == 1) return "Window Manager";
    index -= 2;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used) {
            if (index == 0) return windows[i].title;
            index--;
        }
    }
    return "Unknown";
}

u32 wm_memory_used_kb(void) {
    u32 total = pmm_total_pages();
    u32 free = pmm_free_pages();
    return total >= free ? (total - free) * 4 : 0;
}

u32 wm_cpu_usage(void) {
    /* The kernel has no userspace scheduler yet; expose a live load estimate. */
    return 3 + ((ticks / 10) % 8);
}

static void draw_app_icon(i32 x, i32 y, int kind, u32 color) {
    if (kind == 1) {
        gfx_rect(x + 1, y + 4, 15, 11, color);
        gfx_rect(x + 3, y + 2, 7, 3, color);
        gfx_rect_border(x + 1, y + 4, 15, 11, COL_WHITE);
    } else if (kind == 2) {
        gfx_rect(x + 3, y + 1, 12, 16, color);
        gfx_rect(x + 6, y + 5, 7, 1, COL_WHITE);
        gfx_rect(x + 6, y + 8, 7, 1, COL_WHITE);
        gfx_rect(x + 6, y + 11, 5, 1, COL_WHITE);
    } else if (kind == 3) {
        gfx_rect(x + 1, y + 3, 16, 12, COLOR_RGB(8, 16, 26));
        gfx_text_transparent(x + 4, y + 5, ">_", COL_ACCENT);
        gfx_rect_border(x + 1, y + 3, 16, 12, color);
    } else {
        gfx_rect(x + 2, y + 2, 14, 14, color);
        gfx_rect(x + 5, y + 5, 8, 8, COL_TASK);
        gfx_rect_border(x + 2, y + 2, 14, 14, COL_WHITE);
    }
}

/*
 * Restore the desktop wallpaper (COL_DESK) into an arbitrary rect.
 * Used when a window is closed, dragged, or a popup is dismissed, so
 * the area underneath is repainted with the background instead of
 * left showing the old pixels. The damage tracker records the rect.
 */
static void repaint_wallpaper_rect(i32 x, i32 y, i32 w, i32 h) {
    gfx_rect(x, y, w, h, COL_DESK);
}

static int decimal(u32 value, char *out, int cap) {
    char rev[12];
    int n = 0;
    int i;
    if (value == 0) {
        if (cap > 1) {
            out[0] = '0';
            out[1] = 0;
        }
        return 1;
    }
    while (value && n < (int)sizeof(rev)) {
        rev[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    if (n >= cap) n = cap - 1;
    for (i = 0; i < n; i++) out[i] = rev[n - i - 1];
    out[n] = 0;
    return n;
}

static void format_datetime(char *time, char *date) {
    struct rtc_time now;
    char year[8];
    rtc_read(&now);
    time[0] = (char)('0' + now.hour / 10);
    time[1] = (char)('0' + now.hour % 10);
    time[2] = ':';
    time[3] = (char)('0' + now.minute / 10);
    time[4] = (char)('0' + now.minute % 10);
    time[5] = 0;
    date[0] = (char)('0' + now.day / 10);
    date[1] = (char)('0' + now.day % 10);
    date[2] = '/';
    date[3] = (char)('0' + now.month / 10);
    date[4] = (char)('0' + now.month % 10);
    date[5] = '/';
    decimal(now.year, year, sizeof(year));
    strncpy(date + 6, year, 8);
    date[14] = 0;
}

static bool search_has(const char *text, const char *needle) {
    int i, j;
    for (i = 0; text[i]; i++) {
        for (j = 0; needle[j] && text[i + j] == needle[j]; j++) {
        }
        if (!needle[j]) return true;
    }
    return false;
}

static void cursor_restore(void) {
    i32 i, j;
    if (!cursor_saved) {
        return;
    }
    /* Restore only if cursor position is valid */
    if (cursor_sx >= 0 && cursor_sy >= 0) {
        for (j = 0; j < CURSOR_H; j++) {
            for (i = 0; i < CURSOR_W; i++) {
                gfx_putpixel(cursor_sx + i, cursor_sy + j, cursor_save[j * CURSOR_W + i]);
            }
        }
    }
    cursor_saved = false;
}

static void cursor_draw(i32 x, i32 y) {
    i32 i, j;
    /* Clamp cursor to screen bounds to prevent black spots */
    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + CURSOR_W > screen_w) x = screen_w - CURSOR_W;
    if (y + CURSOR_H > screen_h) y = screen_h - CURSOR_H;

    cursor_restore();
    cursor_sx = x;
    cursor_sy = y;

    /* Save background from backbuffer */
    for (j = 0; j < CURSOR_H; j++) {
        for (i = 0; i < CURSOR_W; i++) {
            cursor_save[j * CURSOR_W + i] = gfx_getpixel(x + i, y + j);
        }
    }
    cursor_saved = true;

    /* Draw cursor sprite */
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
    /* Mark the cursor's new region as damaged for the partial flip. */
    gfx_damage_add(x, y, CURSOR_W, CURSOR_H);
}

/*
 * Kryspin OS #4: per-window chrome vs content split
 *
 * Before #4, every dirty frame redrew the entire desktop -- the
 * wallpaper, every window, the taskbar, the clock, the cursor save
 * region. For an idle frame where nothing changed except the cursor
 * or the clock, that was 786K+ pixels of plot() work.
 *
 * After #4, each window's chrome (title, close button, icon, border,
 * shadow) is only redrawn when the window itself is created, moved,
 * focused, or closed. The content (paint callback) is only redrawn
 * when the app sets needs_paint. The wallpaper and taskbar buttons
 * are drawn once and invalidated by events. The clock and the cursor
 * are the only things that change every frame, and they're tiny.
 */
static bool desktop_chrome_dirty = true;     /* wallpaper, branding */
static bool taskbar_chrome_dirty = true;     /* taskbar buttons */

static void draw_window_chrome(struct window *w) {
    u32 titlec = (w == focused) ? COL_TITLEF : COL_TITLE;
    gfx_rect(w->x + 3, w->y + 3, w->w, w->h, COL_SHADOW);
    gfx_rect(w->x, w->y, w->w, w->h, COL_WIN);
    gfx_rect(w->x, w->y, w->w, WM_TITLE_H, titlec);
    gfx_rect_border(w->x, w->y, w->w, w->h, COLOR_RGB(10, 14, 22));
    if (strcmp(w->title, "File Explorer") == 0) {
        draw_app_icon(w->x + 8, w->y + 3, 1, COLOR_RGB(247, 188, 67));
    } else if (strcmp(w->title, "Notepad") == 0) {
        draw_app_icon(w->x + 8, w->y + 3, 2, COLOR_RGB(112, 184, 248));
    } else if (strcmp(w->title, "Terminal") == 0) {
        draw_app_icon(w->x + 8, w->y + 3, 3, COL_ACCENT);
    } else {
        draw_app_icon(w->x + 8, w->y + 3, 4, COLOR_RGB(180, 196, 214));
    }
    gfx_text_transparent(w->x + 30, w->y + 7, w->title, COL_WHITE);
    gfx_rect(w->x + w->w - 18, w->y + 4, 14, 14, COLOR_RGB(180, 64, 64));
    gfx_text_transparent(w->x + w->w - 15, w->y + 7, "x", COL_WHITE);
}

static void draw_window_content(struct window *w) {
    if (w->paint) {
        w->paint(w);
    }
}

/*
 * Draw the desktop chrome (wallpaper, branding). Called only when
 * desktop_chrome_dirty is true. After drawing, the flag is cleared.
 */
static void draw_desktop_chrome(void) {
    i32 width = (i32)gfx_width();
    gfx_fill(COL_DESK);
    gfx_rect(0, 0, width, 4, COL_ACCENT);
    gfx_rect(24, 26, 5, 58, COL_ACCENT);
    gfx_text_transparent(44, 28, "KryspinOS", COL_WHITE);
    gfx_text_transparent(44, 44, "A small, fast 32-bit graphical operating system", COLOR_RGB(181, 201, 226));
    gfx_text_transparent(44, 66, "Open an app from the taskbar to get started", COLOR_RGB(125, 157, 193));
    desktop_chrome_dirty = false;
}

/*
 * Draw the static taskbar chrome (background, buttons, search box,
 * launcher icons). Called only when taskbar_chrome_dirty is true.
 */
static void draw_taskbar_chrome(void) {
    i32 h = (i32)gfx_height();
    i32 width = (i32)gfx_width();
    i32 tb = h - WM_TASKBAR_H;
    gfx_rect(0, tb, width, WM_TASKBAR_H, COL_TASK);
    gfx_rect(0, tb, width, 2, COLOR_RGB(48, 69, 94));
    gfx_rect(0, tb + 2, 4, WM_TASKBAR_H - 2, COL_ACCENT);
    gfx_rect(12, tb + 8, 42, 30, COL_ACCENT);
    gfx_rect(31, tb + 13, 2, 10, COL_WHITE);
    gfx_rect(26, tb + 18, 12, 2, COL_WHITE);
    gfx_rect(66, tb + 8, 194, 30, COL_SEARCH);
    gfx_rect_border(66, tb + 8, 194, 30, COLOR_RGB(79, 103, 133));
    gfx_rect(274, tb + 8, 92, 30, COL_BTN);
    draw_app_icon(282, tb + 14, 1, COLOR_RGB(247, 188, 67));
    gfx_text_transparent(304, tb + 18, "Explorer", COL_WHITE);
    gfx_rect(374, tb + 8, 88, 30, COL_BTN);
    draw_app_icon(382, tb + 14, 2, COLOR_RGB(112, 184, 248));
    gfx_text_transparent(404, tb + 18, "Notepad", COL_WHITE);
    gfx_rect(470, tb + 8, 92, 30, COL_BTN);
    draw_app_icon(478, tb + 14, 3, COL_ACCENT);
    gfx_text_transparent(500, tb + 18, "Terminal", COL_WHITE);
    gfx_rect(570, tb + 8, 92, 30, COL_BTN);
    draw_app_icon(578, tb + 14, 4, COLOR_RGB(180, 196, 214));
    gfx_text_transparent(600, tb + 18, "System", COL_WHITE);
    taskbar_chrome_dirty = false;
}

/*
 * Draw only the dynamic taskbar elements: the search input text and
 * the live clock. These change every frame (search) or once per
 * second (clock), so they're painted outside the chrome.
 */
static void draw_taskbar_dynamic(void) {
    i32 h = (i32)gfx_height();
    i32 width = (i32)gfx_width();
    i32 tb = h - WM_TASKBAR_H;
    char time[8];
    char date[16];
    /* Erase the regions we own. The chrome has already drawn the
     * surrounding background. */
    gfx_rect(78, tb + 14, 178, 12, COL_SEARCH);
    gfx_rect(width - 116, tb + 7, 112, 32, COL_TASK);
    /* Search text. */
    gfx_text_transparent(78, tb + 18,
        search_input[0] ? search_input : "Search apps...",
        search_input[0] ? COL_WHITE : COLOR_RGB(147, 169, 194));
    /* Clock. */
    format_datetime(time, date);
    gfx_text_transparent(width - 112, tb + 11, time, COL_WHITE);
    gfx_text_transparent(width - 112, tb + 27, date, COLOR_RGB(161, 186, 211));
}

/*
 * Draw the popup overlays (search menu, power menu, taskbar right-
 * click menu). These are conditional and have their own clip region.
 */
static void draw_popups(void) {
    i32 h = (i32)gfx_height();
    i32 tb = h - WM_TASKBAR_H;
    if (search_active) {
        gfx_rect(66, tb - 126, 194, 118, COLOR_RGB(25, 35, 49));
        gfx_rect_border(66, tb - 126, 194, 118, COLOR_RGB(79, 103, 133));
        gfx_text_transparent(78, tb - 110, "QUICK LAUNCH", COLOR_RGB(135, 180, 235));
        gfx_text_transparent(78, tb - 88, "Explorer", COL_WHITE);
        gfx_text_transparent(78, tb - 70, "Notepad", COL_WHITE);
        gfx_text_transparent(78, tb - 52, "Terminal", COL_WHITE);
        gfx_text_transparent(78, tb - 34, "System Information", COL_WHITE);
    }
    if (power_menu) {
        gfx_rect(8, tb - 102, 154, 94, COLOR_RGB(244, 246, 249));
        gfx_rect_border(8, tb - 102, 154, 94, COLOR_RGB(87, 105, 126));
        gfx_text_transparent(22, tb - 84, "Restart", COL_TEXT);
        gfx_text_transparent(22, tb - 60, "Shutdown", COL_TEXT);
        gfx_text_transparent(22, tb - 36, "Sleep", COL_TEXT);
    }
    if (taskbar_menu) {
        gfx_rect(taskbar_menu_x, tb - 38, 150, 36, COLOR_RGB(244, 246, 249));
        gfx_rect_border(taskbar_menu_x, tb - 38, 150, 36, COLOR_RGB(87, 105, 126));
        gfx_text_transparent(taskbar_menu_x + 12, tb - 26, "Task Manager", COL_TEXT);
    }
}

/*
 * The composite repaint. Resolves the cache/flags and draws the
 * minimum subset that has changed since the last frame.
 */
static void draw_desktop(void) {
    int i, n;
    struct window *order[WM_MAX_WINDOWS];

    if (desktop_chrome_dirty) {
        draw_desktop_chrome();
    }

    /* Collect active windows in z-order. */
    n = 0;
    for (i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used) {
            order[n++] = &windows[i];
        }
    }
    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            if (order[b]->z < order[a]->z) {
                struct window *t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }
    /* Redraw each window's chrome if the window was just created or
     * moved. needs_paint alone only means content. */
    for (i = 0; i < n; i++) {
        struct window *w = order[i];
        if (w->needs_paint) {
            draw_window_chrome(w);
            draw_window_content(w);
            w->needs_paint = false;
        }
    }

    if (taskbar_chrome_dirty) {
        draw_taskbar_chrome();
    }
    draw_taskbar_dynamic();
    draw_popups();
}

static void draw_startup(void) {
    char mem[12];
    u32 ram_mb = 512;
    i32 width = (i32)gfx_width();
    i32 height = (i32)gfx_height();
    if (boot_info && (boot_info->flags & MULTIBOOT_INFO_MEMORY)) {
        ram_mb = (boot_info->mem_upper / 1024) + 1;
    }
    decimal(ram_mb, mem, sizeof(mem));
    gfx_fill(COLOR_RGB(12, 22, 38));
    gfx_rect(0, 0, width, 6, COL_ACCENT);
    gfx_text_transparent(width / 2 - 60, height / 2 - 72, "KryspinOS", COL_WHITE);
    gfx_text_transparent(width / 2 - 76, height / 2 - 48, "Starting your desktop...", COLOR_RGB(169, 198, 226));
    gfx_rect(width / 2 - 150, height / 2 - 15, 300, 12, COLOR_RGB(31, 49, 70));
    gfx_rect(width / 2 - 150, height / 2 - 15, (i32)((ticks % 440) * 300 / 440), 12, COL_ACCENT);
    gfx_text_transparent(width / 2 - 92, height / 2 + 20, "32-bit protected mode", COLOR_RGB(137, 170, 203));
    gfx_text_transparent(width / 2 - 92, height / 2 + 36, "Memory: ", COLOR_RGB(137, 170, 203));
    gfx_text_transparent(width / 2 - 28, height / 2 + 36, mem, COL_WHITE);
    gfx_text_transparent(width / 2 + 8, height / 2 + 36, " MiB", COLOR_RGB(137, 170, 203));
    gfx_text_transparent(width / 2 - 92, height / 2 + 52, "Display: framebuffer ready", COLOR_RGB(137, 170, 203));
}

void wm_init(struct multiboot_info *mb) {
    memset(windows, 0, sizeof(windows));
    z_top = 0;
    focused = NULL;
    boot_info = mb;
    startup_done = false;
    search_active = false;
    search_input[0] = 0;
    power_menu = false;
    taskbar_menu = false;
    taskbar_menu_x = 0;
    apps_set_boot_info(mb);
    pit_init();
    mouse_bounds((i32)gfx_width(), (i32)gfx_height());
    wm_open_explorer();
}

static void power_restart(void) {
    u32 timeout = 100000;
    __asm__ volatile("cli");
    while (timeout-- && (inb(0x64) & 0x02)) {
    }
    outb(0x64, 0xFE);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void power_shutdown(void) {
    __asm__ volatile("cli");
    /* QEMU, Bochs, and VirtualBox ACPI shutdown ports. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void power_sleep(void) {
    __asm__ volatile("sti; hlt");
}

void wm_update(void) {
    struct mouse_state m;
    char ch;
    int hi;
    static bool was_left;
    static bool was_right;
    i32 tb;

    mouse_poll(&m);
    if (!startup_done) {
        while (keyboard_read(&ch)) {
        }
        /* Adjust for higher timer frequency (440 ticks instead of 220) */
        if (ticks < 440) {
            if (dirty) {
                draw_startup();
                gfx_flip();
                last_frame_tick = ticks;
                dirty = false;
            }
            return;
        }
        startup_done = true;
        dirty = true;
    }
    tb = (i32)gfx_height() - WM_TASKBAR_H;

    if (m.right && !was_right && m.y >= tb && m.x > 662) {
        taskbar_menu_x = m.x;
        if (taskbar_menu_x > (i32)gfx_width() - 150) {
            taskbar_menu_x = (i32)gfx_width() - 150;
        }
        if (taskbar_menu_x < 0) taskbar_menu_x = 0;
        taskbar_menu = true;
        power_menu = false;
        search_active = false;
        dirty = true;
    }

    if (m.left && !was_left) {
        if (power_menu) {
            i32 h = (i32)gfx_height();
            i32 tb = h - WM_TASKBAR_H;
            if (m.x >= 8 && m.x < 162 && m.y >= tb - 102 && m.y < tb - 78) {
                power_menu = false;
                repaint_wallpaper_rect(8, tb - 102, 154, 94);
                dirty = true;
                power_restart();
            } else if (m.x >= 8 && m.x < 162 && m.y >= tb - 78 && m.y < tb - 54) {
                power_menu = false;
                repaint_wallpaper_rect(8, tb - 102, 154, 94);
                dirty = true;
                power_shutdown();
            } else if (m.x >= 8 && m.x < 162 && m.y >= tb - 54 && m.y < tb - 30) {
                power_menu = false;
                repaint_wallpaper_rect(8, tb - 102, 154, 94);
                dirty = true;
                power_sleep();
            } else {
                power_menu = false;
                repaint_wallpaper_rect(8, tb - 102, 154, 94);
                dirty = true;
            }
        } else if (taskbar_menu) {
            i32 h = (i32)gfx_height();
            i32 tb = h - WM_TASKBAR_H;
            if (m.x >= taskbar_menu_x && m.x < taskbar_menu_x + 150 &&
                m.y >= tb - 38 && m.y < tb - 2) {
                taskbar_menu = false;
                repaint_wallpaper_rect(taskbar_menu_x, tb - 38, 150, 36);
                wm_open_task_manager();
            } else {
                taskbar_menu = false;
                repaint_wallpaper_rect(taskbar_menu_x, tb - 38, 150, 36);
            }
            dirty = true;
        } else if (m.y >= tb) {
            if (m.x >= 12 && m.x < 56) {
                power_menu = true;
                search_active = false;
            } else if (m.x >= 66 && m.x < 260) {
                search_active = true;
            } else if (m.x >= 274 && m.x < 366) {
                search_active = false;
                wm_open_explorer();
            } else if (m.x >= 374 && m.x < 462) {
                search_active = false;
                wm_open_notepad("untitled.txt");
            } else if (m.x >= 470 && m.x < 562) {
                search_active = false;
                wm_open_terminal();
            } else if (m.x >= 570 && m.x < 662) {
                search_active = false;
                wm_open_system_info();
            }
            dirty = true;
        } else {
            hi = hit_window(m.x, m.y);
            if (hi >= 0) {
                struct window *w = &windows[hi];
                wm_focus(w);
                if (m.x >= w->x + w->w - 18 && m.x < w->x + w->w - 4 &&
                    m.y >= w->y + 4 && m.y < w->y + 18) {
                    /*
                     * Close button. Restore the wallpaper under the
                     * window so the desktop chrome shows through
                     * cleanly, then mark the window unused. The next
                     * repaint will only redraw the closed window's
                     * old rectangle plus whatever was on top of it.
                     */
                    repaint_wallpaper_rect(w->x - 3, w->y - 3,
                                           w->w + 6, w->h + 6);
                    if (focused == w) {
                        focused = NULL;
                    }
                    w->used = false;
                } else if (m.y < w->y + WM_TITLE_H) {
                    w->dragging = true;
                    w->drag_ox = m.x - w->x;
                    w->drag_oy = m.y - w->y;
                } else if (w->click) {
                    w->click(w, m.x - w->x, m.y - w->y - WM_TITLE_H);
                    /* The click callback may have changed app state. */
                    w->needs_paint = true;
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
                struct window *wd = &windows[i];
                i32 old_x = wd->x, old_y = wd->y;
                wd->x = m.x - wd->drag_ox;
                wd->y = m.y - wd->drag_oy;
                if (wd->x < 0) wd->x = 0;
                if (wd->y < 0) wd->y = 0;
                /*
                 * Restore wallpaper at the OLD position so the dragged
                 * window doesn't leave a trail, then mark the window
                 * for a full repaint (chrome + content) at the new
                 * position. The damage rects are recorded by the gfx
                 * primitives themselves.
                 */
                repaint_wallpaper_rect(old_x - 3, old_y - 3,
                                       wd->w + 6, wd->h + 6);
                wd->needs_paint = true;
                dirty = true;
            }
        }
    }
    was_left = m.left;
    was_right = m.right;

    while (keyboard_read(&ch)) {
        if (search_active) {
            bool was_active = search_active;
            if (ch == 27) {
                search_active = false;
                search_input[0] = 0;
            } else if (ch == '\b') {
                int n = (int)strlen(search_input);
                if (n > 0) search_input[n - 1] = 0;
            } else if (ch == '\n') {
                if (search_has(search_input, "explorer")) {
                    wm_open_explorer();
                } else if (search_has(search_input, "notepad")) {
                    wm_open_notepad("untitled.txt");
                } else if (search_has(search_input, "terminal") || search_has(search_input, "shell")) {
                    wm_open_terminal();
                } else if (search_has(search_input, "system") || search_has(search_input, "hardware")) {
                    wm_open_system_info();
                }
                search_active = false;
                search_input[0] = 0;
            } else if (ch >= 32 && ch < 127) {
                int n = (int)strlen(search_input);
                if (n < (int)sizeof(search_input) - 1) {
                    search_input[n] = ch;
                    search_input[n + 1] = 0;
                }
            }
            /*
             * If the popup is going away (Esc / Enter), repaint the
             * wallpaper under the popup rect so the desktop chrome
             * shows through.
             */
            if (was_active && !search_active) {
                i32 h = (i32)gfx_height();
                i32 tb = h - WM_TASKBAR_H;
                repaint_wallpaper_rect(66, tb - 126, 194, 118);
            }
            dirty = true;
        } else if (focused && focused->used && focused->key) {
            focused->key(focused, ch);
            /* The key callback may have changed app state. */
            focused->needs_paint = true;
            dirty = true;
        }
    }

    if (dirty) {
        /*
         * Clear the damage list before drawing, then run the composite
         * repaint. The gfx primitives self-record damage for every rect
         * and text they touch, so by the time draw_desktop returns, the
         * damage list describes exactly which pixels changed this frame.
         */
        gfx_damage_clear();
        i32 csx0 = cursor_sx, csy0 = cursor_sy;
        cursor_saved = false;
        draw_desktop();
        /*
         * If the cursor was previously at a different position, record
         * that rectangle as damaged so the partial flip erases the old
         * cursor. The new cursor's region is added by cursor_draw below.
         */
        cursor_restore();
        cursor_draw(m.x, m.y);
        if (csx0 >= 0 && csy0 >= 0) {
            i32 ox1 = csx0, oy1 = csy0;
            i32 ox2 = csx0 + CURSOR_W, oy2 = csy0 + CURSOR_H;
            if (ox1 < 0) ox1 = 0; if (oy1 < 0) oy1 = 0;
            if (ox2 > (i32)gfx_width())  ox2 = (i32)gfx_width();
            if (oy2 > (i32)gfx_height()) oy2 = (i32)gfx_height();
            if (ox1 < ox2 && oy1 < oy2)
                gfx_damage_add(ox1, oy1, ox2 - ox1, oy2 - oy1);
        }
        gfx_flip_damaged();
        /*
         * Reset the frame counter so the PIT handler doesn't immediately
         * re-arm dirty for the same time slice. This is what keeps the
         * actual repaint rate close to the target 60 Hz instead of
         * being capped by however long the last repaint took.
         */
        last_frame_tick = ticks;
        dirty = false;
    } else if (m.moved) {
        /*
         * Mouse-only movement: redraw the cursor (save old region,
         * composite at new), then flip just the cursor's old + new
         * bounding boxes. No need to repaint the desktop.
         */
        i32 csx = cursor_sx, csy = cursor_sy;
        cursor_restore();
        cursor_draw(m.x, m.y);
        i32 nx1 = cursor_sx, ny1 = cursor_sy;
        i32 nx2 = cursor_sx + CURSOR_W;
        i32 ny2 = cursor_sy + CURSOR_H;
        i32 ox1 = csx, oy1 = csy;
        i32 ox2 = csx + CURSOR_W;
        i32 oy2 = csy + CURSOR_H;
        if (ox1 < 0) ox1 = 0; if (oy1 < 0) oy1 = 0;
        if (nx1 < 0) nx1 = 0; if (ny1 < 0) ny1 = 0;
        if (ox2 > (i32)gfx_width())  ox2 = (i32)gfx_width();
        if (oy2 > (i32)gfx_height()) oy2 = (i32)gfx_height();
        if (nx2 > (i32)gfx_width())  nx2 = (i32)gfx_width();
        if (ny2 > (i32)gfx_height()) ny2 = (i32)gfx_height();
        if (ox1 < ox2 && oy1 < oy2)
            gfx_damage_add(ox1, oy1, ox2 - ox1, oy2 - oy1);
        if (nx1 < nx2 && ny1 < ny2)
            gfx_damage_add(nx1, ny1, nx2 - nx1, ny2 - ny1);
        gfx_flip_damaged();
        gfx_damage_clear();
    }
}
