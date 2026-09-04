; Reload GDT and segment registers for a flat 32-bit model.
global gdt_flush

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs
.reload_cs:
    ret
