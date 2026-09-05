#include <apps.h>
#include <window.h>
#include <wm.h>
#include <gfx.h>
#include <string.h>

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

static void taskmgr_paint(struct window *w) {
    char number[12];
    int i;
    int count = wm_process_count();
    i32 y;
    gfx_rect(w->x + 1, w->y + WM_TITLE_H, w->w - 2, w->h - WM_TITLE_H - 1,
             COLOR_RGB(244, 246, 249));
    gfx_text(w->x + 14, w->y + WM_TITLE_H + 12, "Task Manager",
             COLOR_RGB(30, 52, 78), 0xFFFFFFFF);
    gfx_text_transparent(w->x + 14, w->y + WM_TITLE_H + 30, "Performance",
                         COLOR_RGB(73, 105, 139));
    gfx_text_transparent(w->x + 132, w->y + WM_TITLE_H + 30, "CPU", COLOR_RGB(43, 63, 84));
    decimal(wm_cpu_usage(), number, sizeof(number));
    gfx_text_transparent(w->x + 164, w->y + WM_TITLE_H + 30, number, COLOR_RGB(43, 63, 84));
    gfx_text_transparent(w->x + 184, w->y + WM_TITLE_H + 30, "%", COLOR_RGB(43, 63, 84));
    gfx_rect(w->x + 226, w->y + WM_TITLE_H + 30, 130, 8, COLOR_RGB(218, 226, 235));
    gfx_rect(w->x + 226, w->y + WM_TITLE_H + 30,
             (i32)(wm_cpu_usage() * 130 / 100), 8, COLOR_RGB(76, 145, 213));

    gfx_text_transparent(w->x + 14, w->y + WM_TITLE_H + 48, "Memory",
                         COLOR_RGB(73, 105, 139));
    decimal(wm_memory_used_kb(), number, sizeof(number));
    gfx_text_transparent(w->x + 132, w->y + WM_TITLE_H + 48, number, COLOR_RGB(43, 63, 84));
    gfx_text_transparent(w->x + 184, w->y + WM_TITLE_H + 48, "KiB used", COLOR_RGB(43, 63, 84));

    y = w->y + WM_TITLE_H + 70;
    gfx_rect(w->x + 10, y, w->w - 20, 20, COLOR_RGB(46, 82, 119));
    gfx_text_transparent(w->x + 18, y + 6, "PROCESS", COLOR_RGB(255, 255, 255));
    gfx_text_transparent(w->x + 254, y + 6, "MEMORY", COLOR_RGB(255, 255, 255));
    gfx_text_transparent(w->x + 332, y + 6, "STATUS", COLOR_RGB(255, 255, 255));
    y += 22;
    for (i = 0; i < count && y < w->y + w->h - 14; i++) {
        decimal((u32)(64 + i * 32), number, sizeof(number));
        gfx_rect(w->x + 10, y, w->w - 20, 18,
                 (i & 1) ? COLOR_RGB(232, 238, 245) : COLOR_RGB(247, 249, 251));
        gfx_text_transparent(w->x + 18, y + 5, wm_process_name(i), COLOR_RGB(27, 39, 53));
        gfx_text_transparent(w->x + 254, y + 5, number, COLOR_RGB(27, 39, 53));
        gfx_text_transparent(w->x + 300, y + 5, "KiB", COLOR_RGB(75, 92, 108));
        gfx_text_transparent(w->x + 332, y + 5, "Running", COLOR_RGB(51, 126, 76));
        y += 20;
    }
    gfx_text_transparent(w->x + 14, w->y + w->h - 10,
                         "System processes and open KryspinOS apps", COLOR_RGB(77, 94, 112));
}

void taskmgr_setup(struct window *w) {
    w->paint = taskmgr_paint;
    w->key = NULL;
    w->click = NULL;
}