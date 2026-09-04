#include <mouse.h>
#include <isr.h>
#include <ports.h>

#define MOUSE_DATA 0x60
#define MOUSE_STAT 0x64

static volatile i32 mx = 512;
static volatile i32 my = 384;
static volatile u8  mbuttons;
static volatile bool mmoved;
static volatile bool mchanged;
static i32 max_x = 1023;
static i32 max_y = 767;

static u8 packet[3];
static u8 packet_i;
static bool mouse_ready;

static void mouse_wait_write(void) {
    u32 t = 100000;
    while (t-- && (inb(MOUSE_STAT) & 0x02)) {
    }
}

static void mouse_wait_read(void) {
    u32 t = 100000;
    while (t-- && !(inb(MOUSE_STAT) & 0x01)) {
    }
}

static void mouse_write(u8 val) {
    mouse_wait_write();
    outb(MOUSE_STAT, 0xD4);
    mouse_wait_write();
    outb(MOUSE_DATA, val);
}

static u8 mouse_read(void) {
    mouse_wait_read();
    return inb(MOUSE_DATA);
}

static void mouse_irq(struct regs *r) {
    u8 b;
    i32 dx, dy;
    (void)r;

    if (!(inb(MOUSE_STAT) & 0x01)) {
        return;
    }
    b = inb(MOUSE_DATA);
    if (!mouse_ready) {
        return;
    }

    if (packet_i == 0 && !(b & 0x08)) {
        return;
    }
    packet[packet_i++] = b;
    if (packet_i < 3) {
        return;
    }
    packet_i = 0;

    if (packet[0] & 0xC0) {
        return; /* overflow */
    }

    dx = (i32)(i8)packet[1];
    dy = (i32)(i8)packet[2];

    mx += dx;
    my -= dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > max_x) mx = max_x;
    if (my > max_y) my = max_y;

    {
        u8 nb = packet[0] & 0x07;
        if (nb != mbuttons) {
            mbuttons = nb;
            mchanged = true;
        }
    }
    mmoved = true;
}

void mouse_init(void) {
    u8 status;

    mouse_wait_write();
    outb(MOUSE_STAT, 0xA8);

    mouse_wait_write();
    outb(MOUSE_STAT, 0x20);
    mouse_wait_read();
    status = inb(MOUSE_DATA);
    status |= 0x02;
    status &= (u8)~0x20;
    mouse_wait_write();
    outb(MOUSE_STAT, 0x60);
    mouse_wait_write();
    outb(MOUSE_DATA, status);

    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();

    packet_i = 0;
    mouse_ready = true;
    irq_register(12, mouse_irq);
}

void mouse_poll(struct mouse_state *out) {
    __asm__ volatile("cli");
    out->x = mx;
    out->y = my;
    out->left = (mbuttons & 1) != 0;
    out->right = (mbuttons & 2) != 0;
    out->middle = (mbuttons & 4) != 0;
    out->moved = mmoved;
    out->buttons_changed = mchanged;
    mmoved = false;
    mchanged = false;
    __asm__ volatile("sti");
}

void mouse_bounds(i32 w, i32 h) {
    max_x = w - 1;
    max_y = h - 1;
    if (mx > max_x) mx = max_x;
    if (my > max_y) my = max_y;
}
