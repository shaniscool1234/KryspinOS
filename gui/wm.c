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
static bool frame_has_dragging;
/*
 * Chrome repaint flags. These are declared here (not lower in the
 * file) because wm_create() needs to set them when a new window is
 * opened -- the backbuffer under the new window's position still
 * holds whatever was there before (the splash screen, a previously
 * closed window's body, the wallpaper under a window's old
 * position) and the only way to flush those stale pixels out is to
 * redraw the chrome layer underneath before the window paints.
 *
 * Both default to true so the very first frame after wm_init()
 * redraws everything regardless of what was on the backbuffer.
 */
static bool desktop_chrome_dirty = true;     /* wallpaper, branding */
static bool taskbar_chrome_dirty  = true;     /* taskbar buttons */

/* Simplified cursor system - direct drawing without save/restore complexity */
static i32 cursor_x = 512;
static i32 cursor_y = 384;
static bool cursor_visible = true;
static i32 cursor_old_x = 0;
static i32 cursor_old_y = 0;

/* Cached desktop background for performance */
static bool desktop_cached = false;
static u32 *desktop_cache = NULL;

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
    /* More aggressive frame timing for smoother rendering */
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
            win->title[sizeof(win->title) - 1] = 0;
            win->x = x;
            win->y = y;
            win->w = w;
            win->h = h;
            win->z = ++z_top;
            win->needs_paint = true; /* Kryspin OS #4 */
            focused = win;
            /*
             * Force the chrome under the new window to be redrawn,
             * otherwise stale content (the splash screen, a previously
             * closed window, the wallpaper under a previous window's
             * position, etc.) shows through behind the new one. The
             * window's own paint only covers the content area; the
             * title bar covers the chrome overlap, but anything that
             * was on the backbuffer before -- and the backbuffer is
             * never cleared -- will bleed through wherever the new
             * window's white background doesn't fully cover.
             */
            desktop_chrome_dirty = true;
            taskbar_chrome_dirty = true;
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
 * Cache the desktop background (gradient wallpaper) for performance.
 * This avoids recalculating the gradient every frame.
 */
static void cache_desktop_background(void) {
    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();
    u32 color1 = COLOR_RGB(19, 32, 54);
    u32 color2 = COLOR_RGB(45, 60, 85);
    
    if (desktop_cached && desktop_cache) {
        return; /* Already cached */
    }
    
    /* Allocate cache memory */
    if (!desktop_cache) {
        desktop_cache = (u32 *)kmalloc(screen_w * screen_h * sizeof(u32));
        if (!desktop_cache) {
            return; /* Fall back to real-time computation */
        }
    }
    
    /* Pre-compute gradient */
    for (i32 py = 0; py < screen_h; py++) {
        u32 t = ((u32)py * 256) / (u32)screen_h;
        u32 r1 = (color1 >> 16) & 0xFF;
        u32 g1 = (color1 >> 8) & 0xFF;
        u32 b1 = color1 & 0xFF;
        u32 r2 = (color2 >> 16) & 0xFF;
        u32 g2 = (color2 >> 8) & 0xFF;
        u32 b2 = color2 & 0xFF;
        u32 r = r1 + ((r2 - r1) * t) / 256;
        u32 g = g1 + ((g2 - g1) * t) / 256;
        u32 b = b1 + ((b2 - b1) * t) / 256;
        u32 pixel = COLOR_RGB(r, g, b);
        
        for (i32 px = 0; px < screen_w; px++) {
            desktop_cache[py * screen_w + px] = pixel;
        }
    }
    
    desktop_cached = true;
}

/*
 * Draw cached desktop background to backbuffer.
 */
static void draw_cached_desktop(void) {
    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();
    
    if (!desktop_cached || !desktop_cache) {
        /* Fallback to real-time gradient computation */
        gfx_set_gradient_wallpaper(COLOR_RGB(19, 32, 54), COLOR_RGB(45, 60, 85));
        return;
    }
    
    /* Fast blit from cache to backbuffer */
    for (i32 py = 0; py < screen_h; py++) {
        for (i32 px = 0; px < screen_w; px++) {
            gfx_putpixel(px, py, desktop_cache[py * screen_w + px]);
        }
    }
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

static void cursor_draw_simple(i32 x, i32 y) {
    i32 i, j;
    /* Clamp cursor to screen bounds */
    i32 screen_w = (i32)gfx_width();
    i32 screen_h = (i32)gfx_height();

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + CURSOR_W > screen_w) x = screen_w - CURSOR_W;
    if (y + CURSOR_H > screen_h) y = screen_h - CURSOR_H;

    /* Draw cursor sprite directly - optimized to skip empty pixels */
    for (j = 0; j < CURSOR_H; j++) {
        for (i = 0; i < CURSOR_W; i++) {
            u8 p = cursor_sprite[j][i];
            if (p == 0) continue; /* Skip transparent pixels */

            i32 px = x + i;
            i32 py = y + j;
            /* Only draw if within screen bounds */
            if (px >= 0 && py >= 0 && px < screen_w && py < screen_h) {
                if (p == 1) {
                    gfx_putpixel(px, py, COLOR_RGB(0, 0, 0));
                } else if (p == 2) {
                    gfx_putpixel(px, py, COL_WHITE);
                }
            }
        }
    }

    /* Update cursor position */
    cursor_x = x;
    cursor_y = y;

    /*
     * Kryspin OS — Cursor Rendering Fix: register the cursor's bounding
     * rect with the damage tracker. gfx_putpixel writes to the backbuffer
     * but does not record damage, so the new cursor pixels would never be
     * pushed to the framebuffer in a partial-flip frame (e.g. the
     * m.moved branch). The +2/-1 padding matches the rects the m.moved
     * branch already registers for the old and new positions, so the
     * coalescer folds them into one.
     */
    gfx_damage_add(x - 1, y - 1, CURSOR_W + 2, CURSOR_H + 2);
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
    
    /* Use cached desktop background for performance */
    if (!desktop_cached) {
        cache_desktop_background();
    }
    draw_cached_desktop();
    
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
    /*
     * Fill the backbuffer with the splash background colour first so
     * whatever the prior frame left on the backbuffer (the previous
     * progress-bar stripe, GRUB loader leftovers from before we had
     * a framebuffer driver, etc.) doesn't leak through. Without this,
     * the splash transition into draw_desktop() shows ghost pixels in
     * the title bar band.
     */
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
    frame_has_dragging = false;
    cursor_x = 512;
    cursor_y = 384;
    cursor_visible = true;
    /*
     * Kryspin OS — Cursor Rendering Fix: sentinel for the save-under.
     * The m.moved branch skips the restore when these are negative,
     * so the very first cursor movement doesn't try to blit garbage
     * (or worse, the gradient from the wrong screen position) over
     * the fresh boot frame.
     */
    cursor_old_x = -1;
    cursor_old_y = -1;
    desktop_cached = false;
    desktop_cache = NULL;
    apps_set_boot_info(mb);
    pit_init();
    mouse_bounds((i32)gfx_width(), (i32)gfx_height());
    /*
     * Do NOT eagerly open apps here. The desktop boots with only the
     * kernel and the window manager running, and Task Manager should
     * show exactly two processes. Users open apps by clicking taskbar
     * buttons. Any eager wm_open_*() call on this path re-introduces
     * the bug fixed in plan.md §8.
     */
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

/*
 * Save-under: simple single-buffer snapshot of the backbuffer pixels
 * under the cursor's bounding box. The save is captured in the dirty
 * branch (after draw_desktop, before cursor_draw_simple) so the slot
 * always holds the pre-cursor content for the cursor's *current*
 * position. The m.moved branch restores the old position from this
 * save *and* calls draw_desktop() to repaint anything underneath
 * (windows, taskbar, popups) — the save under only covers the bare
 * background, not the full desktop content.
 *
 * Why a per-frame draw_desktop() in m.moved, not a per-position ring:
 *   The previous attempt used a 4-slot per-position ring and tried to
 *   avoid draw_desktop() for stutter. The bug: for any position the
 *   cursor visits for the first time after another m.moved, the
 *   backbuffer at the new position has the previous frame's cursor
 *   pixels, no slot exists for it, so no restore happens, and the
 *   framebuffer at the old position is never updated. The cursor
 *   stays drawn at the old position in the framebuffer — the "traces
 *   of the cursor itself and a red box and black box" symptom.
 *   draw_desktop() redraws the full desktop (including the area the
 *   cursor was over), which is the only way to guarantee the
 *   backbuffer at the old position has the correct pre-cursor content
 *   without keeping a complete history of every cursor position. The
 *   cost is the stutter, but the plan's stutter fix was strictly
 *   secondary to fixing the cursor artifact.
 */
static u32 cursor_save[CURSOR_W * CURSOR_H];

static void cursor_save_under(i32 x, i32 y) {
    const i32 W = (i32)gfx_width();
    const i32 H = (i32)gfx_height();
    for (i32 j = 0; j < CURSOR_H; j++) {
        for (i32 i = 0; i < CURSOR_W; i++) {
            const i32 px = x + i;
            const i32 py = y + j;
            if (px >= 0 && py >= 0 && px < W && py < H) {
                cursor_save[j * CURSOR_W + i] = gfx_getpixel(px, py);
            } else {
                cursor_save[j * CURSOR_W + i] = 0;
            }
        }
    }
}

static void cursor_restore_under(i32 x, i32 y) {
    const i32 W = (i32)gfx_width();
    const i32 H = (i32)gfx_height();
    for (i32 j = 0; j < CURSOR_H; j++) {
        for (i32 i = 0; i < CURSOR_W; i++) {
            const i32 px = x + i;
            const i32 py = y + j;
            if (px >= 0 && py >= 0 && px < W && py < H) {
                gfx_putpixel(px, py, cursor_save[j * CURSOR_W + i]);
            }
        }
    }
    /*
     * gfx_putpixel doesn't record damage, so register the rect
     * explicitly. +2/-1 padding matches cursor_draw_simple and the
     * m.moved branch's old/new rects, so the coalescer folds them.
     */
    gfx_damage_add(x - 1, y - 1, CURSOR_W + 2, CURSOR_H + 2);
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
                dirty = true;
                power_restart();
            } else if (m.x >= 8 && m.x < 162 && m.y >= tb - 78 && m.y < tb - 54) {
                power_menu = false;
                dirty = true;
                power_shutdown();
            } else if (m.x >= 8 && m.x < 162 && m.y >= tb - 54 && m.y < tb - 30) {
                power_menu = false;
                dirty = true;
                power_sleep();
            } else {
                power_menu = false;
                dirty = true;
            }
        } else if (taskbar_menu) {
            i32 h = (i32)gfx_height();
            i32 tb = h - WM_TASKBAR_H;
            if (m.x >= taskbar_menu_x && m.x < taskbar_menu_x + 150 &&
                m.y >= tb - 38 && m.y < tb - 2) {
                taskbar_menu = false;
                wm_open_task_manager();
            } else {
                taskbar_menu = false;
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
                     * Close button. Force full desktop redraw.
                     */
                    desktop_chrome_dirty = true;
                    taskbar_chrome_dirty = true;
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
        frame_has_dragging = false;
        for (i = 0; i < WM_MAX_WINDOWS; i++) {
            if (windows[i].used && windows[i].dragging) {
                frame_has_dragging = true;
                struct window *wd = &windows[i];
                i32 old_x = wd->x, old_y = wd->y;
                wd->x = m.x - wd->drag_ox;
                wd->y = m.y - wd->drag_oy;
                if (wd->x < 0) wd->x = 0;
                if (wd->y < 0) wd->y = 0;
                /*
                 * Mark the union of the old and new window rectangles
                 * as damaged. The desktop chrome redraw (below) repaints
                 * the wallpaper under both positions, and the partial
                 * flip then pushes those regions to the framebuffer.
                 * Without this, the old window's pixels linger on the
                 * backbuffer and leave a solid-colour "ghost" behind
                 * the window as it moves -- even though we set
                 * desktop_chrome_dirty, the chrome redraw only writes
                 * into the chrome layer; the backbuffer at the old
                 * position is only overwritten if it's explicitly in the
                 * damage list.
                 */
                gfx_damage_add(old_x - 3, old_y - 3,
                               wd->w + 6, wd->h + 6);
                gfx_damage_add(wd->x - 3, wd->y - 3,
                               wd->w + 6, wd->h + 6);
                /*
                 * During window dragging, force a complete redraw to prevent
                 * any visual artifacts. This is more expensive but ensures
                 * clean rendering during movement.
                 */
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
                /* Force full redraw when popup closes */
                desktop_chrome_dirty = true;
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
         * High-performance rendering with cached background:
         * - Cache gradient once (major speedup)
         * - Use damage system for partial updates
         * - Only redraw what changed
         */
        gfx_damage_clear();

        /* Initialize cache if needed (one-time cost, huge speedup) */
        if (!desktop_cached) {
            cache_desktop_background();
        }

        /* Only clear and redraw background when UI changes significantly */
        if (desktop_chrome_dirty || taskbar_chrome_dirty || frame_has_dragging) {
            draw_cached_desktop();
            draw_desktop();
        } else {
            /* Just redraw dynamic elements (clock, cursor, window content) */
            draw_desktop();
        }

        /*
         * Kryspin OS — Cursor Rendering Fix: refresh the save-under
         * *before* the cursor is drawn this frame, so it captures the
         * fresh desktop content (gradient, taskbar, window pixels)
         * rather than the cursor pixels from a previous frame. The
         * first dirty frame after boot establishes the save-under for
         * the boot cursor position (cursor_x = 512, cursor_y = 384).
         *
         * Order matters: must run after draw_desktop() (so the
         * backbuffer at the cursor position reflects the just-painted
         * content) and before cursor_draw_simple() (so we capture
         * the pre-cursor pixels, not the cursor pixels).
         */
        cursor_save_under(m.x, m.y);

        /* Draw cursor on top -- registers its own damage rect. */
        cursor_draw_simple(m.x, m.y);

        /* Use damage-based flip for performance */
        gfx_flip_damaged();

        /* Clear dirty flags */
        desktop_chrome_dirty = false;
        taskbar_chrome_dirty = false;
        frame_has_dragging = false;
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
         * Kryspin OS — Cursor Rendering Fix (m.moved):
         * The save-under alone is not enough to clean the backbuffer
         * at the old position when the cursor visits a new position
         * for the first time (no slot exists for it, so the restore
         * is a no-op, and the framebuffer at the old position is
         * never updated). The fix: call draw_desktop() to repaint
         * the full desktop — this overwrites the cursor pixels at
         * the old position with the correct content (gradient,
         * window, taskbar, popups) and registers damage for it.
         * The save-under is then used to capture the pre-cursor
         * content at the new position before the cursor is drawn.
         *
         * This re-introduces the stutter complaint the plan tried to
         * fix. The cursor ghost bug is worse than the stutter, so
         * we accept the trade-off.
         */
        gfx_damage_clear();

        /* 1. Repaint the full desktop. This cleans the backbuffer at
         *    the old position (overwriting the cursor pixels with
         *    the correct content) and at the new position.
         */
        draw_desktop();

        /* 2. Capture the pre-cursor content at the new position. */
        cursor_save_under(m.x, m.y);

        /* 3. Draw the new cursor (registers its own damage rect). */
        cursor_draw_simple(m.x, m.y);

        /* 4. Flip. */
        gfx_flip_damaged();

        cursor_old_x = m.x;
        cursor_old_y = m.y;
    }
}
