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
void gfx_text_blit(i32 x, i32 y, const char *s, u32 fg, u32 bg);
void gfx_text_blit_transparent(i32 x, i32 y, const char *s, u32 fg);
void gfx_flip(void);  /* Double buffering: flip back buffer to screen */
void gfx_clear_dirty(void);  /* Clear dirty rectangles for partial updates */

/* ----- damage-rectangle API (Kryspin OS #4) ----- */
#define GFX_MAX_DAMAGE 32

void gfx_damage_clear(void);
void gfx_damage_add(i32 x, i32 y, i32 w, i32 h);
void gfx_damage_add_window(i32 x, i32 y, i32 w, i32 h);
void gfx_flip_damaged(void);   /* Flip only the union of damage rects */
bool gfx_has_damage(void);

/* ----- BMP image loading ----- */
bool gfx_load_bmp(const u8 *data, u32 size);
void gfx_draw_bmp(i32 x, i32 y);

/* ----- PNG image loading ----- */
bool gfx_load_png(const u8 *data, u32 size);
void gfx_draw_png(i32 x, i32 y);

/* ----- Wallpaper system ----- */
bool gfx_load_wallpaper(const char *path);
void gfx_draw_wallpaper(void);

/* ----- Wallpaper generation ----- */
void gfx_set_gradient_wallpaper(u32 color1, u32 color2);

#endif
