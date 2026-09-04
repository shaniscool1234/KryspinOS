#include <isr.h>
#include <idt.h>
#include <pic.h>
#include <string.h>

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

static irq_handler_t irq_handlers[16];

void irq_install(void) {
    u32 stubs[16] = {
        (u32)irq0,  (u32)irq1,  (u32)irq2,  (u32)irq3,
        (u32)irq4,  (u32)irq5,  (u32)irq6,  (u32)irq7,
        (u32)irq8,  (u32)irq9,  (u32)irq10, (u32)irq11,
        (u32)irq12, (u32)irq13, (u32)irq14, (u32)irq15
    };
    int i;
    memset(irq_handlers, 0, sizeof(irq_handlers));
    pic_remap(0x20, 0x28);
    pic_mask_all();
    for (i = 0; i < 16; i++) {
        idt_set_gate((u8)(32 + i), stubs[i], KERNEL_CS, 0x8E);
    }
}

void irq_register(u8 irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

void irq_ack(u8 irq) {
    pic_eoi(irq);
}

void irq_handler(struct regs *r) {
    u8 irq = (u8)(r->int_no - 32);
    if (irq_handlers[irq]) {
        irq_handlers[irq](r);
    }
    pic_eoi(irq);
}
