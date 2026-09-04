#ifndef CURSOROS_ISR_H
#define CURSOROS_ISR_H

#include <types.h>

struct regs {
    u32 ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags, useresp, ss;
};

typedef void (*irq_handler_t)(struct regs *r);

void isr_install(void);
void irq_install(void);
void irq_register(u8 irq, irq_handler_t handler);
void irq_ack(u8 irq);

void isr_handler(struct regs *r);
void irq_handler(struct regs *r);

#endif
