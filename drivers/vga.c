#include <vga.h>
#include <ports.h>
#include <string.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   0xB8000

static u16 *vga_buf = (u16 *)VGA_ADDR;
static u8 vga_color = 0x0F;
static u8 vga_row;
static u8 vga_col;

static u16 vga_entry(char c, u8 color) {
    return (u16)c | ((u16)color << 8);
}

static void vga_update_cursor(void) {
    u16 pos = (u16)vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    u32 r, c;
    for (r = 1; r < VGA_HEIGHT; r++) {
        for (c = 0; c < VGA_WIDTH; c++) {
            vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
        }
    }
    for (c = 0; c < VGA_WIDTH; c++) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = vga_entry(' ', vga_color);
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_set_color(u8 fg, u8 bg) {
    vga_color = (bg << 4) | (fg & 0x0F);
}

void vga_clear(void) {
    u32 i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buf[i] = vga_entry(' ', vga_color);
    }
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_init(void) {
    vga_set_color(0x0F, 0x00);
    vga_clear();
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (u8)((vga_col + 4) & ~3);
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga_buf[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color);
        }
    } else {
        vga_buf[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
        vga_col++;
    }

    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    vga_update_cursor();
}

void vga_write(const char *s) {
    while (*s) {
        vga_putc(*s++);
    }
}
