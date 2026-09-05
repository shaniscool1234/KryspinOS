# KryspinOS — Hardware Requirements (HR)

This document lists what KryspinOS needs to run, broken into three
tiers: the **minimum** the kernel will boot on, the **recommended**
target that is what the developers actually test on, and the
**optimal** profile for a comfortable desktop experience.

The numbers below reflect the actual resources the kernel reserves
at boot, plus the real-world cost of the desktop repaint. They are
not arbitrary marketing floors.

---

## 1. Quick reference

| Resource | Minimum | Recommended | Optimal |
|----------|---------|-------------|---------|
| CPU      | i386 (Pentium-class or newer) | i686 / Pentium Pro / Athlon | x86-64 host running a 32-bit VM |
| RAM      | 64 MiB | 256 MiB | 512 MiB or more |
| Storage  | 32 MiB disk (or none — pure ramdisk) | 64 MiB ATA / SATA / virtual disk | 128 MiB+ for filesystem experiments |
| Display  | 800x600x24 VBE linear framebuffer | 1024x768x32 VBE linear framebuffer | 1024x768x32 with no row padding |
| Input    | PS/2 keyboard (IRQ1) | PS/2 keyboard + PS/2 mouse (IRQ12) | PS/2 keyboard + PS/2 mouse + QEMU/VBox PS/2 emulation |
| BIOS     | Multiboot-1 compliant (GRUB, QEMU `-kernel`, etc.) | GRUB via ISO | GRUB via ISO + 32-bit BIOS-mode VM |

---

## 2. CPU

KryspinOS is built as **i386 freestanding 32-bit** (no userspace, no
SSE, no MMX). The Makefile passes:

    -m32 -march=i386 -mno-sse -mno-sse2 -mno-mmx -msoft-float
    -fno-tree-vectorize -ffreestanding -O2

so the kernel does not need a 486, a Pentium, or any FPU/SIMD
extension. The first usable target is therefore **any i386-class
processor** — original 386, 486, Pentium, Pentium Pro, Pentium II,
K6, Athlon, or anything 32-bit-x86-capable after that.

The PIT runs at **250 Hz** (channel 0, divisor 4772 ≈ 1.19318 MHz /
250). The kernel reads CMOS RTC for the live clock.

### What is not supported
* 64-bit-only (x86_64 without 32-bit long mode). The kernel expects
  to be loaded in 32-bit protected mode by a multiboot-1 loader.
* APIC / IOAPIC / SMP. Only the legacy 8259A PIC is wired up
  (vectors 32–47). On a modern multi-core machine you will end up
  on BSP and that is fine.
* PAE. The page directory uses 4 MiB pages via PSE, identity-mapping
  the first 32 MiB plus the framebuffer.
* UEFI. KryspinOS boots via Multiboot-1, which is a BIOS-era
  protocol. On a UEFI machine you need a 32-bit BIOS-mode VM
  (VirtualBox) or QEMU's default SeaBIOS firmware.

### TSC / HPET / Local APIC timer
The kernel does not enable these. Everything time-related goes
through the PIT. The PIT is the only timed IRQ that drives the
desktop repaint.

---

## 3. RAM

The kernel reserves its memory at boot from the multiboot memory
map. The reservation breakdown at runtime, for a default
1024x768x32 build:

| What                     | Size          | Where             |
|--------------------------|---------------|-------------------|
| Kernel image (.text + .rodata + .data) | ~150 KiB | low memory, identity-mapped |
| Page directory (one 4 KiB page) | 4 KiB | BSS |
| Kernel heap (`mm/heap.c`) | 1 MiB | static BSS |
| CFS ramdisk (32 files × 8 KiB) | 256 KiB | static BSS |
| Framebuffer backbuffer | 4 MiB (BSS) | static BSS, identity-mapped |
| Free physical pages (PMM bitmap covers 4 GiB) | bitmap = 128 KiB | BSS |
| Multiboot mmap + kernel stack | ~80 KiB | low memory |

The **practical minimum RAM** the kernel can boot with is therefore
~32 MiB: 5.5 MiB of kernel BSS + a few MiB of free physical pages
for `kmalloc` to hand out + enough to leave a working set for the
window manager. Below 32 MiB, the multiboot memory map still
reports usable memory but the heap and PMM start starving.

| RAM tier | What works                                     |
|----------|------------------------------------------------|
| 32 MiB   | Boots. The desktop will fit, but opening a few apps may exhaust the heap. |
| 64 MiB   | Comfortable for the desktop + 2–3 apps.        |
| 256 MiB  | The development baseline. No memory pressure.  |
| 512 MiB+ | Anything you want to do in the WM is fine.     |

The PMM bitmap itself covers the full **4 GiB** physical address
space, but the kernel only identity-maps the **first 32 MiB** plus
the framebuffer region. Any physical page above 32 MiB is tracked
by the bitmap but not directly addressable; this is a hard
limitation in the current `mm/paging.c` and the reason KryspinOS
still asks for `qemu -m 512` in the README.

---

## 4. Storage

KryspinOS ships in three storage modes, in order of preference:

1. **ATA PIO primary master, LBA28** (drivers/ata.c). If a disk
   is present and identifies, the kernel mounts CursorFS from it.
2. **Pure ramdisk**. If no disk is present, the kernel formats
   CursorFS in memory and boots with the seed files (`/`,
   `/Documents`, `/Windows`, `/Windows/System32`, `readme.txt`,
   `hello.txt`, `notes.txt`). Nothing persists across reboots.
3. **ISO boot**. The `KryspinOS.iso` produced by `make` is the
   standard delivery; it contains the kernel, GRUB, and the GRUB
   menu but no on-disk volume. Pair the ISO with a separate
   virtual disk in QEMU if you want ATA persistence.

### Disk size

| Mode                    | Minimum | Recommended |
|-------------------------|---------|-------------|
| Pure ramdisk (no disk)  | 0       | 0           |
| ATA / virtual disk      | 32 MiB  | 64 MiB      |
| ISO + virtual disk      | 32 MiB disk + a few MB ISO image | 64 MiB disk + ISO |

The on-disk CursorFS volume is small — `CFS_MAX_FILES = 32`,
`CFS_MAX_FILE_SECTORS = 16` per file, so the maximum addressable
volume is about **256 KiB of file data plus the superblock**, or
roughly 33 KiB after the 32-entry table. A 32 MiB virtual disk is
grossly over-provisioned; the kernel only uses the first ~33 KiB.

### Filesystems
KryspinOS speaks exactly one: **CursorFS (CFS)**, magic `'CURS'`
(`0x53525543`). It is a flat filesystem with a 32-entry table,
8 KiB max per file, hierarchical via the `parent` index field in
each entry. There is no FAT, no ext2, no UFS, no MBR parser, no
partitioning. The ATA driver treats the first sectors of the disk
as a single CFS volume.

---

## 5. Display

KryspinOS renders the desktop through GRUB's multiboot
framebuffer. It does not program any GPU; the bootloader must
hand it a linear framebuffer in the multiboot info structure.

### Resolution
The kernel asks GRUB for **1024x768x32** in the multiboot header
(`boot/boot.asm`); the `gfxpayload` line in `grub/grub.cfg`
favours that, with a fallback ladder of `1024x768x24`, `800x600x32`,
then `auto`.

| Resolution | What works                            |
|------------|---------------------------------------|
| 800x600x24 | Minimum. Layout uses 1024-wide coordinates and may be clipped on the right. |
| 1024x768x24 | All desktop widgets visible. The fast paths in `gfx_rect` still hit; only the fill path takes the slow route. |
| 1024x768x32 | Native target. The 32-bpp fast paths in `gfx_fill`, `gfx_rect`, and `gfx_flip` all engage. |
| 1280x1024  | **Not supported** — the static BSS backbuffer is reserved for 1024x1024x32. The kernel will refuse to init and fall back to VGA text mode. |

### Color depth
`bpp < 24` is rejected by `gfx_init`. The kernel targets
**packed 24-bit or 32-bit RGB** (BGRX on x86, with the high byte
unused as a "no background" sentinel — see the `COLOR_RGB` macro
in `include/gfx.h`).

### Row padding
The fast paths require `pitch == width * 4` for 32-bpp. Most
BIOS-mode VBE implementations honor this; if a hypervisor or
firmware pads the row, the kernel still draws correctly but
drops to the per-pixel `plot` path for `gfx_rect`, which is
roughly 10x slower.

### Cache behavior
`mm/paging.c` maps the framebuffer region with **PWT (write-through)
and PCD (cache-disable)** set. Framebuffer memory is MMIO; if it
sits in the CPU's write-back cache, pixels never reach the screen
and you see black. The kernel logs the framebuffer mapping at boot:

    paging: mapped framebuffer 0xXXXXXXXX (N MiB, UC/WT)

If that line is missing, the framebuffer was not detected and the
kernel fell back to the VGA text shell.

### Performance budget
With the static 4 MiB BSS backbuffer and the rep stosd/movsd
primitives in `libc/mem32.asm`, the desktop repaint on 1024x768x32
fits comfortably inside a 16 ms frame budget at 250 Hz PIT. The
target rate is **~62.5 Hz** (`PIT_HZ / FRAME_TICKS = 250 / 4`).
The PIT handler schedules a repaint every 4 ticks; the WM loop
clears the counter when a frame is delivered so a long repaint
does not queue up the next one. Mouse-only movement skips the
desktop repaint and only re-composites the cursor.

---

## 6. Input

### Keyboard
PS/2 keyboard on **IRQ1** (vector 33). Two 128-entry keymaps
(unshifted and shifted) and a 64-byte ringbuffer. The driver is
in `drivers/keyboard.c`.

### Mouse
PS/2 mouse on **IRQ12** (vector 44). 3-byte packets, sign-extended
delta, Y inverted, with a configurable bounding box. The driver
is in `drivers/mouse.c`. Position is clamped to the framebuffer
size at boot (`mouse_bounds(gfx_width(), gfx_height())`).

Both are wired through the standard 8259A PIC. There is no USB
support.

### Real-time clock
CMOS RTC read on demand by `drivers/rtc.c` for the live taskbar
clock and the Terminal `date` command. The kernel does not use
the RTC periodic interrupt.

---

## 7. Hypervisor / virtualizer compatibility

KryspinOS is developed against QEMU and VirtualBox. The framebuffer
quirks documented in the README's troubleshooting section all
apply: VBoxVGA is preferred over VBoxSVGA / VMSVGA, and 32-bit
BIOS-mode is required (not EFI).

| Hypervisor           | Status                          |
|----------------------|---------------------------------|
| QEMU (`-cdrom`)      | Tested. `-m 512` is plenty.     |
| QEMU (`-kernel`)     | Works but skips the GRUB menu.  |
| VirtualBox 6/7       | Tested. VBoxVGA controller. 16 MB video memory. |
| Bochs                | Should work; uses Bochs ACPI shutdown port 0xB004. |
| VMware               | Likely works; not regularly tested. |
| Hyper-V              | Not validated. |
| KVM                  | Works through QEMU.             |

---

## 8. Toolchain (build-time, not runtime)

KryspinOS is built with:

* **gcc** with `-m32 -ffreestanding` (any reasonably modern gcc
  that supports multilib)
* **nasm** for the assembly sources (`boot/boot.asm`, the CPU
  stubs, and the new `libc/mem32.asm`)
* **xorriso** and **grub-mkrescue** to produce the bootable ISO

The kernel does not require an `i686-elf-` cross compiler. The
host `gcc -m32 -ffreestanding` works on Linux with multilib and
on MSYS2 with `mingw-w64-x86_64-gcc`. An i686-elf toolchain is
not needed and is intentionally not required by the Makefile.

---

## 9. Sizing cheat sheet

The hard BSS reservations (what the kernel always consumes,
regardless of how many windows or apps you open):

* Static backbuffer:        4.00 MiB
* Kernel heap:              1.00 MiB
* CFS ramdisk (fallback):   0.25 MiB
* Page directory:           0.004 MiB
* PMM bitmap (4 GiB):       0.125 MiB
* Kernel image + sections:  ~0.15 MiB
* Total kernel-resident:    ~5.5 MiB

Plus the multiboot stack (32 KiB) and the multiboot mmap walk
buffer (a few KiB).

If you boot with `-m 32`, you are giving the kernel exactly the
room it needs. The desktop fits, but the heap is small and you
should not open a lot of apps in parallel.

---

## 10. Summary

* **Architecture**: i386, 32-bit protected mode, Multiboot-1.
* **RAM**: 64 MiB minimum, 256 MiB recommended, 512 MiB optimal.
* **Storage**: 32 MiB minimum for a real disk, or none at all
  (pure ramdisk mode).
* **Display**: 1024x768x32 linear framebuffer; below that works
  but truncates the layout.
* **Input**: PS/2 keyboard and PS/2 mouse.
* **BIOS**: 32-bit BIOS / GRUB / QEMU SeaBIOS. Not UEFI.
* **Hypervisor**: QEMU and VirtualBox are the tested targets.

If you have any of the items above but the screen is black, the
framebuffer MMIO cache-disable is almost always the cause — see
`paging_init` in `mm/paging.c`.
