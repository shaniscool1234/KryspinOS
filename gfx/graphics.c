#include <gfx.h>
#include <font.h>
#include <string.h>
#include <kstdio.h>

static u8 *fb;
static u32 pitch;
static u32 width;
static u32 height;
static u32 bpp;
static bool ready;
static u8 red_pos, red_size;
static u8 green_pos, green_size;
static u8 blue_pos, blue_size;

void gfx_init(struct multiboot_info *mb) {
    ready = false;
    fb = NULL;
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
    fb = (u8 *)(u32)mb->framebuffer_addr;
    pitch = mb->framebuffer_pitch;
    width = mb->framebuffer_width;
    height = mb->framebuffer_height;
    bpp = mb->framebuffer_bpp;
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
    kprintf("gfx: %ux%ux%u pitch=%u fb=%p (type=%u)\n",
           width, height, bpp, pitch, fb, mb->framebuffer_type);
}

bool gfx_ready(void) { return ready; }
u32  gfx_width(void) { return width; }
u32  gfx_height(void) { return height; }

static void plot(i32 x, i32 y, u32 color) {
    u8 *p;
    u32 pixel;
    u32 r = (color >> 16) & 0xFF;
    u32 g = (color >> 8) & 0xFF;
    u32 b = color & 0xFF;
    if (!ready || x < 0 || y < 0 || (u32)x >= width || (u32)y >= height) {
        return;
    }
    p = fb + (u32)y * pitch + (u32)x * (bpp / 8);
    pixel = ((r >> (8 - red_size)) << red_pos) |
            ((g >> (8 - green_size)) << green_pos) |
            ((b >> (8 - blue_size)) << blue_pos);
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
    p = fb + (u32)y * pitch + (u32)x * (bpp / 8);
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
        u32 *p = (u32 *)fb;
        u32 n = width * height;
        for (x = 0; x < n; x++) {
            p[x] = color;
        }
        return;
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            plot((i32)x, (i32)y, color);
        }
    }
}

void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    i32 i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            plot(x + i, y + j, color);
        }
    }
}

void gfx_rect_border(i32 x, i32 y, i32 w, i32 h, u32 color) {
    i32 i;
    for (i = 0; i < w; i++) {
        plot(x + i, y, color);
        plot(x + i, y + h - 1, color);
    }
    for (i = 0; i < h; i++) {
        plot(x, y + i, color);
        plot(x + w - 1, y + i, color);
    }
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
    for (row = 0; row < FONT_H; row++) {
        for (col = 0; col < FONT_W; col++) {
                if (g[row] & (0x80u >> col)) {
                plot(x + col, y + row, fg);
            } else if (bg != 0xFFFFFFFF) {
                plot(x + col, y + row, bg);
            }
        }
    }
}

void gfx_text(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    i32 cx = x;
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
}

void gfx_text_transparent(i32 x, i32 y, const char *s, u32 fg) {
    gfx_text(x, y, s, fg, 0xFFFFFFFF);
}
