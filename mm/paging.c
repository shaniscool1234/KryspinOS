#include <paging.h>
#include <pmm.h>
#include <kstdio.h>

#define PDE_PRESENT 0x001
#define PDE_RW      0x002
#define PDE_USER    0x004
#define PDE_PWT     0x008 /* write-through */
#define PDE_PCD     0x010 /* cache disable - REQUIRED for framebuffer MMIO */
#define PDE_PS      0x080 /* 4MiB page */

static u32 page_directory[1024] ALIGN(4096);

static void map_4mb(u32 virt, u32 phys, u32 flags) {
    u32 idx = virt >> 22;
    page_directory[idx] = (phys & 0xFFC00000u) | flags | PDE_PRESENT | PDE_RW | PDE_PS;
}

void paging_init(struct multiboot_info *mb) {
    u32 i;
    u32 cr0, cr4;

    for (i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }

    /* Identity-map the first 32 MiB for kernel, heap, and devices. */
    for (i = 0; i < 8; i++) {
        map_4mb(i * 0x400000u, i * 0x400000u, 0);
    }

    if ((mb->flags & MULTIBOOT_INFO_FRAMEBUFFER) && mb->framebuffer_addr) {
        u32 fb   = (u32)mb->framebuffer_addr;
        u32 size = mb->framebuffer_pitch * mb->framebuffer_height;
        u32 start = fb & 0xFFC00000u;
        u32 end   = (fb + size + 0x3FFFFFu) & 0xFFC00000u;
        u32 p;
        /*
         * Framebuffer memory is MMIO: writes must NOT sit in the CPU caches
         * or the screen stays black even though the kernel "thinks" it
         * drew something. Mark these pages write-through + cache-disabled.
         * Iterate inclusively so a framebuffer that fits in one 4 MiB slot
         * (the common 1024x768x32 case) still gets mapped.
         */
        for (p = start; p <= end; p += 0x400000u) {
            map_4mb(p, p, PDE_PWT | PDE_PCD);
        }
        kprintf("paging: mapped framebuffer %p (%u MiB, UC/WT)\n",
                fb, (end - start) / 0x100000u + 1u);
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(page_directory));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10; /* PSE */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    kprintf("paging: enabled (identity + 4MiB pages)\n");
}