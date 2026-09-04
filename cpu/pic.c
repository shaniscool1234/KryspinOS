#include <pic.h>
#include <ports.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_remap(u8 offset1, u8 offset2) {
    u8 a1 = inb(PIC1_DATA);
    u8 a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, offset1);
    io_wait();
    outb(PIC2_DATA, offset2);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void pic_mask_all(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(u8 irq) {
    u16 port;
    u8 value;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq = (u8)(irq - 8);
        /* cascade on IRQ2 must stay unmasked for slave IRQs */
        value = inb(PIC1_DATA) & (u8)~(1u << 2);
        outb(PIC1_DATA, value);
    }
    value = inb(port) & (u8)~(1u << irq);
    outb(port, value);
}

void pic_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}
