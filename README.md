# KryspinOS

A small 32-bit x86 hobby operating system with a graphical desktop, a window
manager, a custom filesystem (CFS) and built-in apps (Notepad, File Explorer,
Terminal, and System Information). The kernel identifies itself on boot as
**KryspinOS**.

This document covers what KryspinOS is, how it's wired together, how to
build it, and how to run it (QEMU and VirtualBox). For minimum and
recommended hardware specs (RAM, storage, display, hypervisor), see
[HR.md](HR.md).

---

## Table of Contents

1. [Features](#features)
2. [Project Layout](#project-layout)
3. [Boot Sequence](#boot-sequence)
4. [Architecture](#architecture)
   - [CPU: GDT / IDT / ISR / IRQ / PIC](#cpu)
   - [Memory: PMM / Paging / Heap](#memory)
   - [Drivers: VGA / Keyboard / Mouse / ATA](#drivers)
   - [Filesystem: CFS + VFS](#filesystem)
   - [Graphics + GUI](#graphics)
   - [Apps](#apps)
5. [Building](#building)
   - [Toolchain (Linux)](#toolchain-linux)
   - [Toolchain (Windows / MSYS2)](#toolchain-windows)
   - [Building the ISO](#building-the-iso)
   - [Cleaning](#cleaning)
6. [Running](#running)
   - [QEMU](#qemu)
   - [VirtualBox](#virtualbox)
7. [Troubleshooting](#troubleshooting)

---

## Features

- **Multiboot-1 bootable kernel** loaded by GRUB.
- **i686 flat memory model** with its own GDT, IDT, ISR and IRQ handlers.
- **8259A PIC** remapped to vectors 32–47.
- **Physical memory manager** backed by a bitmap for up to 4 GiB.
- **4 MiB-page paging** with PSE, identity-mapping the kernel and the
  framebuffer (the framebuffer region is mapped uncacheable so writes
  actually reach the MMIO — see `mm/paging.c`).
- **1 MiB kernel heap** with first-fit allocation, magic-tagged blocks and
  on-free coalescing.
- **VGA text driver** (fallback console) plus a custom **kprintf**.
- **PS/2 keyboard & mouse** drivers routed through the IRQ subsystem.
- **ATA PIO driver** (primary master, LBA28).
- **CursorFS** — a custom flat filesystem with a `CURS` magic superblock,
  32 file slots × 8 KiB each, persisted to ATA or kept in a ramdisk.
- **VFS** POSIX-ish wrapper with up to 16 open file descriptors.
- **Linear framebuffer graphics**: 24/32-bpp primitives, 8×8 bitmap font,
  rectangles, text. The 32-bpp fast paths (`gfx_fill`, `gfx_rect`,
  `gfx_flip`) use `rep stosd` / `rep movsd` via the primitives in
  `libc/mem32.asm` and write into a static 4 MiB BSS backbuffer.
- **Window manager**: draggable windows, close button, focus by Z-order,
  searchable taskbar with pinned apps and a live RTC clock/date. The PIT
  runs at 250 Hz and the desktop repaint is paced at ~62.5 Hz; a
  static 4 MiB BSS backbuffer plus rep stosd / rep movsd primitives
  in `libc/mem32.asm` keep each frame well inside the 16 ms budget.
- **Built-in apps**: Notepad (4 KiB buffer, save/open via CFS), File Explorer
  (lists CFS root, opens files in Notepad), Terminal (basic shell commands),
  and System Information (hardware and framebuffer details).

There is no userspace, no paging beyond 32 MiB + the framebuffer, no
ACPI, no APIC, no PCI enumeration, and no networking — it's a hobby
kernel, not a general-purpose OS.

---

## Project Layout

```
KryspinOS/
├── Makefile              # build driver
├── linker.ld             # ELF32 layout, kernel at 0x00100000
├── boot/
│   └── boot.asm          # Multiboot-1 header + _start
├── grub/
│   └── grub.cfg          # GRUB menu, sets gfxpayload
├── cpu/
│   ├── gdt.c             # GDT setup
│   ├── gdt_flush.asm     # lgdt + segment reload
│   ├── idt.c             # IDT programming
│   ├── isr.asm           # ISR0-31 + IRQ0-15 stubs
│   ├── isr.c             # Exception names + handler
│   ├── irq.c             # IRQ dispatch table
│   └── pic.c             # 8259A PIC remap / mask / EOI
├── mm/
│   ├── pmm.c             # Bitmap physical page allocator
│   ├── paging.c          # 4 MiB-page directory, identity map
│   └── heap.c            # First-fit kernel heap
├── drivers/
│   ├── vga.c             # 80×25 text console
│   ├── keyboard.c        # PS/2 IRQ1, ringbuffer
│   ├── mouse.c           # PS/2 IRQ12, 3-byte packets
│   └── ata.c             # ATA PIO read/write/IDENTIFY
├── fs/
│   ├── cfs.c             # CursorFS implementation
│   └── vfs.c             # POSIX-ish VFS over CFS
├── libc/
│   ├── string.c          # memcpy/memset/strcmp/...
│   ├── mem32.asm         # rep stosd / rep movsd primitives
│   ├── mem32.c           # memcpy_fast / memset_fast dispatchers
│   └── kstdio.c          # kputc/kputs/kprintf
├── gfx/
│   ├── font.c            # 8×8 ASCII bitmap font
│   └── graphics.c        # Framebuffer primitives + 4 MiB BSS backbuffer
├── gui/
│   └── wm.c              # Window manager (PIT 250 Hz, ~62.5 Hz repaint)
├── apps/
│   ├── notepad.c         # Text editor (CFS-backed)
│   ├── explorer.c        # File list (CFS-backed)
│   ├── terminal.c        # Built-in shell (ls/cat/cd/grep/...)
│   ├── system.c          # System Information
│   └── taskmgr.c         # Task Manager (process / CPU / mem)
├── include/
│   └── *.h               # All public headers
├── HR.md                 # Hardware requirements (RAM, storage, display)
```

---

## Boot Sequence

1. **GRUB** loads `boot/boot.asm` at the multiboot entry point.
2. `_start` sets up a 32 KiB stack, pushes `magic` and `mb_info`, and
   calls `kmain(magic, mb)`.
3. `kmain` runs the subsystem init order:

```
vga_init          (early console so we can kprintf)
gdt_init           (flat 32-bit model)
idt_init / isr_install / irq_install
   └── PIC remap to 0x20 / 0x28
pmm_init(mb)      (bitmap allocator from multiboot memory map)
paging_init(mb)   (identity-map first 32 MiB + framebuffer, enable PSE+PG)
heap_init         (1 MiB first-fit arena)
keyboard_init     (IRQ1)
mouse_init        (IRQ12)
ata_init          (IDENTIFY primary master)
vfs_init          (mount / format CFS)
gfx_init(mb)      (consume mb framebuffer fields)
sti
```

4. If `gfx_ready()` returns true, `wm_init` launches and the WM loop
   drives the desktop. Otherwise, we fall back to a VGA text shell that
   echoes whatever the keyboard produces.

---

## Architecture

### CPU

- **GDT** (`cpu/gdt.c`): three entries — null, ring-0 code (access 0x9A,
  granularity 0xCF, 4 KiB gran, 32-bit), ring-0 data (0x92). The
  selectors are `KERNEL_CS = 0x08` and `KERNEL_DS = 0x10`.
- **IDT** (`cpu/idt.c`): 256 entries. Each gate is `{base_low, selector,
  zero, flags, base_high}`. `idt_init` zeroes the table and runs `lidt`.
- **ISRs** (`cpu/isr.asm`, `cpu/isr.c`): macros generate `isr0..31`
  stubs. Some push a dummy error code, others push the CPU-supplied
  one. The common stub `isr_common` does `pusha`, swaps to data selector
  0x10, pushes `esp`, calls C, then restores and `iret`.
- **IRQs** (`cpu/irq.c`): same pattern for `irq0..15` mapped to vectors
  32–47. The C dispatcher looks up `irq_handlers[int_no-32]` and calls
  it, then sends EOI. Devices register via `irq_register(n, handler)`
  and the handler runs at the registered IRQ.
- **PIC** (`cpu/pic.c`): standard 8259A ICW1..ICW4 dance, then
  mask/unmask/EOI helpers. `pic_unmask` preserves the IRQ2 cascade so
  slave PIC IRQs still get through.

### Memory

- **PMM** (`mm/pmm.c`): a 128 KiB bitmap (1 bit per 4 KiB page = 4 GiB).
  `pmm_init` reads the multiboot memory map, clears the bitmap, then
  sets bits for *usable* ranges (type=1) back to free. The first MiB,
  the kernel image (`kernel_start..kernel_end`), and the bitmap itself
  are marked used. Allocation is a linear scan — fine for a hobby kernel.
- **Paging** (`mm/paging.c`): a single 4 KiB-aligned page directory. The
  first 32 MiB are identity-mapped using 4 MiB pages (PSE bit set). The
  framebuffer region is **also mapped, but with PWT (write-through) and
  PCD (cache-disable)** — framebuffer memory is MMIO, and if it sits in
  the CPU cache, your writes never reach the screen and you see black.
- **Heap** (`mm/heap.c`): a static 1 MiB arena, split into
  `struct block { magic=0xC0DE0001, size, free, next }`. `kmalloc`
  rounds up to 8 bytes, splits when the remainder is large enough;
  `kfree` coalesces adjacent free blocks.

### Drivers

- **VGA** (`drivers/vga.c`): 80×25 text buffer at `0xB8000`. Cursor via
  ports `0x3D4`/`0x3D5`. Handles `\n \r \t \b` and scrolls.
- **Keyboard** (`drivers/keyboard.c`): reads scancode from port `0x60`
  on IRQ1. Has two 128-entry keymaps (unshifted/shifted), tracks
  shift/caps, and pushes characters into a 64-byte ringbuffer. The
  caller drains via `keyboard_read`.
- **Mouse** (`drivers/mouse.c`): PS/2 initialization (`0xA8` enable aux,
  `0x20/0x60` set compaq status, `0xF6` defaults, `0xF4` enable stream),
  then assembles 3-byte packets on IRQ12 with sign-extension and Y
  inversion. Position is clamped to a configured bounding box.
- **ATA** (`drivers/ata.c`): PIO only, primary master on `0x1F0`.
  `IDENTIFY (0xEC)` reads 256 words and pulls the sector count from
  words 60/61. `READ (0x20)` / `WRITE (0x30)` use `rep insw/outsw`
  after BSY/DRQ polling.

### Filesystem

- **CursorFS** (`fs/cfs.c`, `include/cfs.h`): a flat filesystem whose
  superblock magic is `'CURS'` (0x53525543). Layout:

  ```
  sector 0..CFS_DATA_START-1   superblock + file entries (32 slots)
  CFS_DATA_START..             per-file data area
  ```

  - Up to 32 files (`CFS_MAX_FILES`).
  - Each file gets `CFS_MAX_FILE_SECTORS = 16` sectors (8 KiB) of
    contiguous data, for a max ~256 KiB volume.
  - On mount, if the ATA primary master is present and the superblock
    reads back with `CFS_MAGIC`, we mount from ATA. Otherwise we format
    in-memory with `/`, `/readme.txt`, `/hello.txt`.
  - Every write persists the superblock and the affected file sectors
    back to disk (when on ATA).

- **VFS** (`fs/vfs.c`): a 16-entry open-file cache wrapping CFS. Modes
  `r/w/a`. Missing files auto-create on write modes. `vfs_fopen(path, "w")`
  truncates; `vfs_fopen(path, "a")` seeks to the end.

### Graphics

- **Font** (`gfx/font.c`): 96 glyphs (ASCII 32..127), 8×8 pixels, one
  byte per row, MSB = leftmost pixel.
- **Graphics** (`gfx/graphics.c`): linear framebuffer primitives built on
  a per-pixel `plot`/`sample`. Provides `gfx_fill`, `gfx_rect`,
  `gfx_rect_border`, `gfx_char`, `gfx_text`, `gfx_text_transparent`.
  `COLOR_RGB` packs `0xFF000000|R<<16|G<<8|B` so the high byte (used as
  the "no background" sentinel) is always 0xFF. The 32-bpp fast path
  in `gfx_fill` kicks in when `pitch == width * 4` and now goes
  through `memset_fast` (`rep stosd` in `libc/mem32.asm`). The
  backbuffer is a 4 MiB static BSS slice (big enough for any
  32-bpp mode up to 1024×1024) and `gfx_flip` uses `rep movsd`
  for the backbuffer → framebuffer blit.

### GUI

- **Window manager** (`gui/wm.c`):
  - PIT @ 250 Hz (IRQ0) drives a `ticks` counter. The handler sets
    `dirty = true` when `ticks - last_frame_tick >= 4` (so a
    repaint is requested every ~16 ms / ~62.5 Hz). The repaint
    itself resets `last_frame_tick` so a long frame does not queue
    up the next one.
  - Up to 8 windows, each with `paint`/`key`/`click` callbacks and
    arbitrary `data`.
  - Click → taskbar hit-test or window hit-test (focus, close, drag,
    or `click(lx, ly)`).
  - The classic 12×19 arrow cursor is drawn by saving the underlying
    pixels from the backbuffer, then compositing a 2-tone sprite.
  - Default desktop: branded KryspinOS workspace with search, pinned
    Explorer/Notepad/Terminal/System launchers, and live time/date.

### Apps

- **Notepad** (`apps/notepad.c`): 4 KiB buffer, insert/delete
  with `memmove`, click "Save" → `vfs_fopen(path, "w")`. Opening a file
  reads into the buffer and parks the cursor at the end.
- **File Explorer** (`apps/explorer.c`): lists CFS root with
  `vfs_list`, refresh button re-runs the listing, click on `[FILE]` row
  calls `wm_open_notepad(filename)`.
- **Terminal** (`apps/terminal.c`): accepts `help`, `ls`, `cat`, `date`,
  `hardware`, `clear`, `about`, and `echo` commands.
- **System Information** (`apps/system.c`): shows the processor mode,
  GRUB-reported memory, framebuffer dimensions, and storage mode.

---

## Building

KryspinOS is built in freestanding 32-bit mode, plus `nasm` for assembly
and `grub-mkrescue` (from `grub-common` / `grub2-common`) to produce the
bootable ISO. On Replit, the native compiler's multilib support is used,
so an i686-ELF cross compiler is not required.
The Makefile also disables SSE/MMX code generation because the kernel does
not enable those CPU extensions before entering protected-mode C code.

### Toolchain (Linux)

```sh
sudo apt install \
    build-essential bison flex libgmp-dev libmpfr-dev libisl-dev \
    libmpc-dev texinfo wget nasm xorriso grub-pc-bin qemu-system-x86
```

For a standard Linux installation, the classic cross-compiler recipe is
at <https://wiki.osdev.org/GCC_Cross-Compiler>. Alternatively, on a
Replit environment with native multilib support installed, skip the
cross-compiler setup and run the build directly.

### Toolchain (Windows / MSYS2)

```sh
pacman -S --needed \
    mingw-w64-x86_64-gcc nasm xorriso grub \
    mingw-w64-x86_64-tools
```

You'll still need an `i686-elf` cross GCC. Either build one from source
following the OSDev guide, or use the same approach as Linux in an
MSYS2 / WSL shell. If you don't have one, an alternative is to swap the
Makefile to use the host `gcc -m32 -ffreestanding` plus a freestanding
libgcc — that works for many hobby OSes, but you'll need to point the
Makefile at the host `gcc` instead of `i686-elf-gcc`.

### Building the ISO

From the project root:

```sh
make
```

That's it. The Makefile:

1. Builds every `.c` and `.asm` source into `build/.../*.o`.
2. Links them with `linker.ld` into `build/kernel.bin` (ELF32, loaded
   at `0x00100000`).
3. Copies the kernel and `grub/grub.cfg` into `build/iso/boot/...`.
4. Runs `grub-mkrescue -o KryspinOS.iso build/iso`.

The final artifact is **`KryspinOS.iso`** at the project root. Boot it
with any BIOS-mode multiboot loader (GRUB, qemu-direct, etc.).

You can also stop at intermediate steps:

```sh
make kernel   # produces build/kernel.bin
make iso      # produces KryspinOS.iso
```

### Cleaning

```sh
make clean
```

Removes `build/` and `KryspinOS.iso`.

---

## Running

### QEMU

```sh
qemu-system-i386 -cdrom KryspinOS.iso -m 512 -serial stdio
```

`-m 512` is plenty. For a faster boot pass `-no-reboot -no-shutdown`.

To run the kernel directly without going through GRUB (skip the boot
loader entirely, useful when iterating on early code):

```sh
qemu-system-i386 -kernel build/kernel.bin -m 512
```

This only works for the multiboot case if QEMU can load multiboot images
natively — most distros' QEMU doesn't, so prefer the ISO path.

### VirtualBox

1. **Create a new VM** (Type: Other / Version: Other/Unknown (32-bit)).
   Give it at least 512 MB of RAM.
2. **Storage**: attach `KryspinOS.iso` to the IDE controller as the
   optical drive.
3. **Display**: set Video Memory to 16 MB or more. Switch the
   graphics controller to **VBoxVGA** if you have the option — VBoxSVGA
   and VMSVGA sometimes don't honor the multiboot header's requested
   mode.
4. Boot the VM. GRUB should pick 1024×768×32 and you should see the
   KryspinOS desktop with the taskbar and pinned apps.

If the screen stays black, see [Troubleshooting](#troubleshooting).

---

## Troubleshooting

### Black screen on VirtualBox (or any BIOS-mode VM)

There are three common culprits; the kernel prints diagnostics for each.

1. **Framebuffer wasn't accepted.** GRUB sets
   `MULTIBOOT_INFO_FRAMEBUFFER` and provides a linear framebuffer, but
   the kernel might reject it if `bpp < 24` or `framebuffer_type` is
   something it doesn't recognize. The kernel logs the exact reason and
   falls back to VGA text mode.

   Modern GRUB + VirtualBox has been observed to report
   `framebuffer_type = 0` (indexed) even when the surface is RGB with
   `bpp >= 24`. The kernel now accepts both `0` and `1`; if it still
   rejects yours, the `kprintf` line on the VGA console will tell you
   which check failed.

2. **Framebuffer was cached.** Framebuffer memory is MMIO — if it
   lives in the CPU's write-back cache, your pixels never reach the
   display. The fix is in `mm/paging.c`: the framebuffer's 4 MiB pages
   are mapped with `PWT` (write-through) **and** `PCD` (cache
   disable). Verify you see
   `paging: mapped framebuffer 0xXXXXXXXX (N MiB, UC/WT)` early in
   boot.

3. **GRUB picked the wrong mode.** If GRUB falls back to text mode or
   a low-bpp mode, the kernel can't draw. The `grub/grub.cfg` file
   sets `gfxpayload=1024x768x32,1024x768x24,800x600x32,auto` to force
   a sane default — make sure you're booting from the ISO produced by
   `make`, not a stale one.

4. **The VM reports "Guru Meditation" immediately.** This usually means
   the kernel faulted before drawing. Rebuild with the supplied Makefile;
   it disables SSE/MMX instructions that a native compiler may otherwise
   emit before the kernel has enabled those CPU extensions. In VirtualBox,
   keep EFI disabled and use a 32-bit BIOS VM.

4. **Diagnostic breadcrumbs.** The VGA console always prints the
   multiboot framebuffer fields when graphics fails:
   ```
   mb flags=00002000  type=1  w=1024 h=768 bpp=32 addr=0xE0000000
   ```
   `flags` should include bit 12 (`0x1000`). `type` should be 0 or 1.
   `bpp` should be 24 or 32. `addr` should be a non-zero MMIO address.

### "Invalid multiboot magic" or no VGA output at all

You're not booted via multiboot. Make sure the ISO is produced by
`grub-mkrescue` against the `build/iso/` tree that contains
`boot/kernel.bin` and `boot/grub/grub.cfg`, and that the VM is set to
boot from CD.

### ATA: "ata: no primary master"

There's no disk on the primary IDE controller. The kernel still boots
into CFS in ramdisk mode with `/`, `/readme.txt`, `/hello.txt` seeded.
To use ATA, attach a virtual disk with at least a few hundred KiB of
free space and re-run.

### Mouse doesn't move / clicks don't register

Make sure the VM has the PS/2 mouse enabled. On VirtualBox this is on
by default. In QEMU add `-device ps2-mouse`.

### Build fails: `i686-elf-gcc: command not found`

You don't have a cross toolchain. See
[Building — Toolchain](#toolchain-linux).

### Build fails: `grub-mkrescue: command not found`

Install `grub-pc-bin` (Linux) or `grub` (MSYS2). On macOS `brew
install grub xorriso` and ensure the right `grub-mkrescue` is on PATH
(the Homebrew keg ships one — sometimes named `grub-mkrescue-efi` or
under `$(brew --prefix)/opt/grub/bin/`).

---

## License

This is a personal hobby project. No license is granted by default —
add one if you intend to redistribute.