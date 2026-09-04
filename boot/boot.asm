; cursorOS Multiboot-1 entry
; Flags: page-align modules, pass memory map, request linear framebuffer.

MBALIGN     equ 1 << 0
MEMINFO     equ 1 << 1
VIDINFO     equ 1 << 2
FLAGS       equ MBALIGN | MEMINFO | VIDINFO
MAGIC       equ 0x1BADB002
CHECKSUM    equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0                    ; header_addr
    dd 0                    ; load_addr
    dd 0                    ; load_end_addr
    dd 0                    ; bss_end_addr
    dd 0                    ; entry_addr
    dd 0                    ; mode_type: 0 = linear graphics
    dd 1024                 ; width
    dd 768                  ; height
    dd 32                   ; depth

section .bss
align 16
stack_bottom:
    resb 32768
stack_top:

section .text
global _start
extern kmain

_start:
    mov esp, stack_top
    xor ebp, ebp
    push ebx                ; multiboot info pointer
    push eax                ; magic
    cli
    call kmain
.hang:
    cli
    hlt
    jmp .hang
