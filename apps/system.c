#include <apps.h>
#include <window.h>
#include <gfx.h>
#include <rtc.h>
#include <ata.h>
#include <string.h>
#include <heap.h>

static struct multiboot_info *boot_info;

void apps_set_boot_info(struct multiboot_info *mb) {
    boot_info = mb;
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

static void system_paint(struct window *w) {
    char line[32];
    u32 ram_mb = 512;
    u32 fb_type = 0;
    struct rtc_time now;
    if (boot_info && (boot_info->flags & MULTIBOOT_INFO_MEMORY)) {
        ram_mb = (boot_info->mem_upper / 1024) + 1;
    }
    if (boot_info) {
        fb_type = boot_info->framebuffer_type;
    }
    rtc_read(&now);
    gfx_rect(w->x + 1, w->y + WM_TITLE_H, w->w - 2, w->h - WM_TITLE_H - 1,
             COLOR_RGB(245, 247, 250));
    gfx_text(w->x + 16, w->y + WM_TITLE_H + 14, "KryspinOS hardware", COLOR_RGB(30, 52, 78), 0xFFFFFFFF);

    gfx_rect(w->x + 14, w->y + WM_TITLE_H + 38, w->w - 28, 46, COLOR_RGB(229, 237, 247));
    gfx_text_transparent(w->x + 26, w->y + WM_TITLE_H + 48, "PROCESSOR", COLOR_RGB(50, 98, 150));
    gfx_text_transparent(w->x + 26, w->y + WM_TITLE_H + 64, "i386-compatible / 32-bit protected mode", COLOR_RGB(25, 35, 48));

    gfx_rect(w->x + 14, w->y + WM_TITLE_H + 92, w->w - 28, 46, COLOR_RGB(235, 242, 234));
    gfx_text_transparent(w->x + 26, w->y + WM_TITLE_H + 102, "MEMORY", COLOR_RGB(57, 120, 72));
    decimal(ram_mb, line, sizeof(line));
    gfx_text_transparent(w->x + 26, w->y + WM_TITLE_H + 118, line, COLOR_RGB(25, 35, 48));
    gfx_text_transparent(w->x + 58, w->y + WM_TITLE_H + 118, "MiB reported by GRUB", COLOR_RGB(25, 35, 48));

    gfx_rect(w->x + 14, w->y + WM_TITLE_H + 146, w->w - 28, 46, COLOR_RGB(246, 237, 226));
    gfx_text_transparent(w->x + 26, w->y + WM_TITLE_H + 156, "DISPLAY", COLOR_RGB(168, 100, 42));
    decimal(gfx_width(), line, sizeof(line));
    gfx_text_transparent(w->x + 26, w->y + WM_TITLE_H + 172, line, COLOR_RGB(25, 35, 48));
    gfx_text_transparent(w->x + 66, w->y + WM_TITLE_H + 172, "x", COLOR_RGB(25, 35, 48));
    decimal(gfx_height(), line, sizeof(line));
    gfx_text_transparent(w->x + 74, w->y + WM_TITLE_H + 172, line, COLOR_RGB(25, 35, 48));
    gfx_text_transparent(w->x + 114, w->y + WM_TITLE_H + 172, "linear framebuffer", COLOR_RGB(25, 35, 48));

    gfx_text_transparent(w->x + 16, w->y + w->h - 34,
                         ata_present() ? "Storage: ATA + CursorFS" : "Storage: ramdisk + CursorFS",
                         COLOR_RGB(54, 68, 86));
    gfx_text_transparent(w->x + 16, w->y + w->h - 20,
                         fb_type == 1 ? "Framebuffer: RGB packed pixels" : "Framebuffer: indexed-compatible",
                         COLOR_RGB(54, 68, 86));
    (void)now;
}

void system_setup(struct window *w) {
    w->paint = system_paint;
    w->key = NULL;
    w->click = NULL;
}