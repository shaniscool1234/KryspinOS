#include <gdt.h>

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} PACKED;

struct gdt_ptr {
    u16 limit;
    u32 base;
} PACKED;

static struct gdt_entry gdt[3];
static struct gdt_ptr   gp;

extern void gdt_flush(u32 gp_addr);

static void gdt_set(int idx, u32 base, u32 limit, u8 access, u8 gran) {
    gdt[idx].base_low    = (u16)(base & 0xFFFF);
    gdt[idx].base_mid    = (u8)((base >> 16) & 0xFF);
    gdt[idx].base_high   = (u8)((base >> 24) & 0xFF);
    gdt[idx].limit_low   = (u16)(limit & 0xFFFF);
    gdt[idx].granularity = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[idx].access      = access;
}

void gdt_init(void) {
    gp.limit = (u16)(sizeof(gdt) - 1);
    gp.base  = (u32)&gdt;

    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF); /* ring 0 code, 4K gran, 32-bit */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF); /* ring 0 data, 4K gran, 32-bit */

    gdt_flush((u32)&gp);
}
