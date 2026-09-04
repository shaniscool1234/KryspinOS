#ifndef CURSOROS_IDT_H
#define CURSOROS_IDT_H

#include <types.h>

struct idt_entry {
    u16 base_low;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 base_high;
} PACKED;

struct idt_ptr {
    u16 limit;
    u32 base;
} PACKED;

void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags);
void idt_init(void);

#endif
