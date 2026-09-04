#include <types.h>
#include <multiboot.h>
#include <vga.h>
#include <kstdio.h>
#include <gdt.h>
#include <idt.h>
#include <isr.h>
#include <pmm.h>
#include <paging.h>
#include <heap.h>
#include <keyboard.h>
#include <mouse.h>
#include <ata.h>
#include <vfs.h>
#include <gfx.h>
#include <wm.h>
#include <string.h>

void kmain(u32 magic, struct multiboot_info *mb) {
    vga_init();
    kprintf("Welcome to cursorOS!\n");

    if (magic != MULTIBOOT_MAGIC) {
        kprintf("Invalid multiboot magic: %x\n", magic);
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    gdt_init();
    kprintf("GDT loaded\n");

    idt_init();
    isr_install();
    irq_install();
    kprintf("IDT/ISR/IRQ ready (PIC remapped 32-47)\n");

    pmm_init(mb);
    paging_init(mb);
    heap_init();

    keyboard_init();
    mouse_init();
    ata_init();
    vfs_init();

    gfx_init(mb);

    __asm__ volatile("sti");

    if (gfx_ready()) {
        gfx_fill(COLOR_RGB(24, 48, 80));
        gfx_text(40, 40, "Welcome to cursorOS!", COLOR_RGB(255, 255, 255), 0xFFFFFFFF);
        wm_init();
        for (;;) {
            wm_update();
            __asm__ volatile("hlt");
        }
    }

    kprintf("Framebuffer unavailable; staying in VGA text mode.\n");
    kprintf("  mb flags=%x  type=%u  w=%u h=%u bpp=%u addr=%p\n",
            mb->flags,
            mb->framebuffer_type,
            mb->framebuffer_width, mb->framebuffer_height,
            mb->framebuffer_bpp, (void *)(u32)mb->framebuffer_addr);
    kprintf("Type on the keyboard (IRQ1). Mouse IRQ12 is armed.\n");
    for (;;) {
        char c;
        if (keyboard_read(&c)) {
            vga_putc(c);
        }
        __asm__ volatile("hlt");
    }
}