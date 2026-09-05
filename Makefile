# Native compiler in 32-bit freestanding mode. The Replit environment provides
# the required multilib support, so an external i686-elf cross compiler is not
# needed to build the ISO here.
CC      := gcc
AS      := nasm
GRUB    := grub-mkrescue

CFLAGS  := -m32 -march=i386 -mno-sse -mno-sse2 -mno-mmx -msoft-float \
           -fno-tree-vectorize -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-exceptions \
           -fno-stack-protector -fno-pic -nostdlib \
           -Iinclude
ASFLAGS := -f elf32
LDFLAGS := -m32 -T linker.ld -ffreestanding -O2 -nostdlib -lgcc

C_SOURCES := \
	kernel/kernel.c \
	libc/string.c \
	libc/kstdio.c \
	drivers/vga.c \
	drivers/keyboard.c \
	drivers/mouse.c \
	drivers/ata.c \
	drivers/rtc.c \
	cpu/gdt.c \
	cpu/idt.c \
	cpu/isr.c \
	cpu/irq.c \
	cpu/pic.c \
	mm/pmm.c \
	mm/paging.c \
	mm/heap.c \
	fs/cfs.c \
	fs/vfs.c \
	gfx/font.c \
	gfx/graphics.c \
	gui/wm.c \
	apps/explorer.c \
	apps/notepad.c \
	apps/terminal.c \
	apps/system.c \
	apps/taskmgr.c

ASM_SOURCES := \
	boot/boot.asm \
	cpu/gdt_flush.asm \
	cpu/isr.asm

C_OBJS   := $(C_SOURCES:%.c=build/%.o)
ASM_OBJS := build/boot/boot_asm.o build/cpu/gdt_flush_asm.o build/cpu/isr_asm.o
OBJS     := $(ASM_OBJS) $(C_OBJS)

.PHONY: all iso kernel clean dirs

all: iso

dirs:
	@mkdir -p build/boot build/cpu build/drivers build/kernel build/libc \
	          build/mm build/fs build/gfx build/gui build/apps \
	          build/iso/boot/grub

build/%.o: %.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

build/boot/boot_asm.o: boot/boot.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@

build/cpu/gdt_flush_asm.o: cpu/gdt_flush.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@

build/cpu/isr_asm.o: cpu/isr.asm | dirs
	$(AS) $(ASFLAGS) $< -o $@

build/kernel.bin: $(OBJS) linker.ld | dirs
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

kernel: build/kernel.bin

iso: build/kernel.bin grub/grub.cfg | dirs
	cp build/kernel.bin build/iso/boot/kernel.bin
	cp grub/grub.cfg build/iso/boot/grub/grub.cfg
	$(GRUB) -o KryspinOS.iso build/iso

clean:
	rm -rf build KryspinOS.iso
