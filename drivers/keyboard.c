#include <keyboard.h>
#include <isr.h>
#include <ports.h>

#define KBD_DATA 0x60
#define KBD_STAT 0x64

static volatile char kbd_buf[64];
static volatile u8 kbd_head;
static volatile u8 kbd_tail;
static bool shift_l;
static bool shift_r;
static bool caps;

static const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0
};

static const char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0
};

static void kbd_push(char c) {
    u8 next = (u8)((kbd_head + 1) % 64);
    if (next == kbd_tail) {
        return;
    }
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static void keyboard_irq(struct regs *r) {
    u8 sc;
    (void)r;
    sc = inb(KBD_DATA);

    if (sc == 0x2A) { shift_l = true; return; }
    if (sc == 0x36) { shift_r = true; return; }
    if (sc == 0xAA) { shift_l = false; return; }
    if (sc == 0xB6) { shift_r = false; return; }
    if (sc == 0x3A) { caps = !caps; return; }
    if (sc & 0x80) {
        return;
    }
    if (sc < 128) {
        char c;
        bool sh = shift_l || shift_r;
        c = sh ? keymap_shift[sc] : keymap[sc];
        if (c >= 'a' && c <= 'z' && caps) {
            c = (char)(c - 32);
        } else if (c >= 'A' && c <= 'Z' && caps && !sh) {
            c = (char)(c + 32);
        }
        if (c) {
            kbd_push(c);
        }
    }
}

void keyboard_init(void) {
    kbd_head = kbd_tail = 0;
    shift_l = shift_r = caps = false;
    irq_register(1, keyboard_irq);
}

int keyboard_read(char *out) {
    if (kbd_head == kbd_tail) {
        return 0;
    }
    *out = kbd_buf[kbd_tail];
    kbd_tail = (u8)((kbd_tail + 1) % 64);
    return 1;
}

bool keyboard_shift(void) {
    return shift_l || shift_r;
}
