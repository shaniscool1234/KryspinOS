; 32-bit memory primitives used by the gfx layer and large memcpys.
;
; These are exposed as memcpy32 / memset32 with the same semantics as the
; byte versions in libc/string.c, but operate on u32 words. They are an
; order of magnitude faster than the byte loops the kernel otherwise uses,
; and the speedup is what makes 60 Hz repaints possible on a framebuffer
; that would otherwise have to be cleared 786K pixels at a time.
;
; ABI: cdecl. ECX is the standard rep count register and is not preserved
; by the rep instructions, so we don't bother saving it.

bits 32
section .text

global memcpy32
global memset32
global memmove32

; void *memcpy32(void *dst, const void *src, u32 count_words)
;   Copies `count_words` u32s from src to dst. Both buffers must be
;   4-byte aligned. Returns dst.
memcpy32:
    push edi
    push esi
    mov edi, [esp + 12]   ; dst
    mov esi, [esp + 16]   ; src
    mov ecx, [esp + 20]   ; count (in dwords)
    rep movsd
    pop esi
    pop edi
    mov eax, [esp + 4]    ; return dst (pops restored the cdecl frame)
    ret

; void *memset32(void *dst, u32 value, u32 count_words)
;   Writes the u32 `value` `count_words` times at dst. dst must be
;   4-byte aligned. Returns dst.
memset32:
    push edi
    mov edi, [esp + 8]    ; dst
    mov eax, [esp + 12]   ; value
    mov ecx, [esp + 16]   ; count (in dwords)
    rep stosd
    pop edi
    mov eax, [esp + 4]    ; return dst
    ret

; void *memmove32(void *dst, const void *src, u32 count_words)
;   Like memcpy32 but handles overlap. Both buffers 4-byte aligned.
memmove32:
    push ebx
    push edi
    push esi
    mov edi, [esp + 16]   ; dst
    mov esi, [esp + 20]   ; src
    mov ecx, [esp + 24]   ; count
    mov eax, edi
    cmp eax, esi
    je .done
    jb .forward
    ; dst > src: copy backwards.
    lea edi, [edi + ecx * 4 - 4]
    lea esi, [esi + ecx * 4 - 4]
    std
    rep movsd
    cld
    jmp .done
.forward:
    cld
    rep movsd
.done:
    pop esi
    pop edi
    pop ebx
    mov eax, [esp + 4]    ; return dst
    ret
