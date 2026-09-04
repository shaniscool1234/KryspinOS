#include <kstdio.h>
#include <vga.h>
#include <string.h>

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)

void kputc(char c) {
    vga_putc(c);
}

void kputs(const char *s) {
    vga_write(s);
}

static void print_uint(u32 v, u32 base, int width, char pad) {
    char buf[16];
    const char *digits = "0123456789ABCDEF";
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v) {
            buf[i++] = digits[v % base];
            v /= base;
        }
    }
    while (i < width) {
        kputc(pad);
        width--;
    }
    while (i--) {
        kputc(buf[i]);
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            kputc(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            kputc('%');
            fmt++;
            continue;
        }
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            kputs(s ? s : "(null)");
        } else if (*fmt == 'c') {
            kputc((char)va_arg(ap, int));
        } else if (*fmt == 'd') {
            i32 n = va_arg(ap, i32);
            if (n < 0) {
                kputc('-');
                print_uint((u32)(-n), 10, 0, ' ');
            } else {
                print_uint((u32)n, 10, 0, ' ');
            }
        } else if (*fmt == 'u') {
            print_uint(va_arg(ap, u32), 10, 0, ' ');
        } else if (*fmt == 'x') {
            print_uint(va_arg(ap, u32), 16, 0, ' ');
        } else if (*fmt == 'p') {
            kputs("0x");
            print_uint(va_arg(ap, u32), 16, 8, '0');
        }
        if (*fmt) {
            fmt++;
        }
    }
    va_end(ap);
}
