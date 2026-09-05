/* C wrappers for the 32-bit mem primitives in libc/mem32.asm.
 *
 * These are the workhorse functions used by the framebuffer flip and
 * by the gfx layer's solid-fill fast paths. They are also used by the
 * regular libc memcpy/memset when both buffers are 4-byte aligned and
 * the size is a whole number of words -- a quick check that turns a
 * ~50 ms byte loop on a 3 MiB backbuffer into ~3 ms.
 */
#include <string.h>
#include <types.h>

extern void *memcpy32(void *dst, const void *src, u32 count_words);
extern void *memset32(void *dst, u32 value, u32 count_words);
extern void *memmove32(void *dst, const void *src, u32 count_words);

void *memcpy_fast(void *dst, const void *src, size_t n) {
    uintptr_t a = (uintptr_t)dst;
    uintptr_t b = (uintptr_t)src;
    /* Whole dwords, both ends 4-byte aligned. */
    if ((n >= 4) && ((a | b) & 3u) == 0) {
        return memcpy32(dst, src, (u32)(n >> 2));
    }
    return memcpy(dst, src, n);
}

void *memset_fast(void *dst, int c, size_t n) {
    uintptr_t a = (uintptr_t)dst;
    if ((n >= 4) && (a & 3u) == 0) {
        return memset32(dst, (u32)(u8)c, (u32)(n >> 2));
    }
    return memset(dst, c, n);
}
