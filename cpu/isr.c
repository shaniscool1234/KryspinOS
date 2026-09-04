#include <isr.h>
#include <idt.h>
#include <kstdio.h>
#include <vga.h>

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Security Exception",
    "Reserved"
};

void isr_install(void) {
    u32 stubs[32] = {
        (u32)isr0,  (u32)isr1,  (u32)isr2,  (u32)isr3,
        (u32)isr4,  (u32)isr5,  (u32)isr6,  (u32)isr7,
        (u32)isr8,  (u32)isr9,  (u32)isr10, (u32)isr11,
        (u32)isr12, (u32)isr13, (u32)isr14, (u32)isr15,
        (u32)isr16, (u32)isr17, (u32)isr18, (u32)isr19,
        (u32)isr20, (u32)isr21, (u32)isr22, (u32)isr23,
        (u32)isr24, (u32)isr25, (u32)isr26, (u32)isr27,
        (u32)isr28, (u32)isr29, (u32)isr30, (u32)isr31
    };
    int i;
    for (i = 0; i < 32; i++) {
        idt_set_gate((u8)i, stubs[i], KERNEL_CS, 0x8E);
    }
}

void isr_handler(struct regs *r) {
    vga_set_color(0x0C, 0x00);
    kprintf("\n*** EXCEPTION: %s (%u) err=%x eip=%p ***\n",
            exception_messages[r->int_no], r->int_no, r->err_code, r->eip);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
