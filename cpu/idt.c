#include <idt.h>
#include <string.h>

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags) {
    idt[num].base_low  = (u16)(base & 0xFFFF);
    idt[num].base_high = (u16)((base >> 16) & 0xFFFF);
    idt[num].selector  = sel;
    idt[num].zero      = 0;
    idt[num].flags     = flags;
}

void idt_init(void) {
    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base  = (u32)&idt;
    memset(&idt, 0, sizeof(idt));
    __asm__ volatile("lidt %0" : : "m"(idtp));
}
