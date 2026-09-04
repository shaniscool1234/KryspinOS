#ifndef CURSOROS_GFX_H
#define CURSOROS_GFX_H

#include <types.h>
#include <multiboot.h>

#define COLOR_RGB(r, g, b) ((u32)(0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b)))

void gfx_init(struct multiboot_info *mb);
bool gfx_ready(void);
u32  gfx_width(void);
u32  gfx_height(void);
void gfx_putpixel(i32 x, i32 y, u32 color);
u32  gfx_getpixel(i32 x, i32 y);
void gfx_fill(u32 color);
void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void gfx_rect_border(i32 x, i32 y, i32 w, i32 h, u32 color);
void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg);
void gfx_text(i32 x, i32 y, const char *s, u32 fg, u32 bg);
void gfx_text_transparent(i32 x, i32 y, const char *s, u32 fg);

#endif
