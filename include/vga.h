#ifndef CURSOROS_VGA_H
#define CURSOROS_VGA_H

#include <types.h>

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s);
void vga_set_color(u8 fg, u8 bg);

#endif
