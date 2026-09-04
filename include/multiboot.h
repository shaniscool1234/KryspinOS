#ifndef CURSOROS_MULTIBOOT_H
#define CURSOROS_MULTIBOOT_H

#include <types.h>

#define MULTIBOOT_MAGIC 0x2BADB002

#define MULTIBOOT_INFO_MEMORY      (1u << 0)
#define MULTIBOOT_INFO_MMAP        (1u << 6)
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)

struct multiboot_mmap_entry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;
} PACKED;

struct multiboot_info {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;
    u8  color_info[6];
} PACKED;

#endif
