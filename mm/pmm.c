#include <pmm.h>
#include <string.h>
#include <kstdio.h>

extern u32 kernel_start;
extern u32 kernel_end;

#define PMM_MAX_PAGES (1024u * 1024u / 8u) /* bitmap for 4GiB */

static u8  bitmap[PMM_MAX_PAGES];
static u32 total_pages;
static u32 used_pages;
static u32 bitmap_pages;

static void pmm_set(u32 page) {
    bitmap[page / 8] |= (u8)(1u << (page % 8));
}

static void pmm_unset(u32 page) {
    bitmap[page / 8] &= (u8)~(1u << (page % 8));
}

static bool pmm_test(u32 page) {
    return (bitmap[page / 8] & (u8)(1u << (page % 8))) != 0;
}

static void pmm_mark_used_range(u64 addr, u64 len) {
    u64 start = addr / PAGE_SIZE;
    u64 end = (addr + len + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 i;
    if (end > total_pages) {
        end = total_pages;
    }
    for (i = start; i < end; i++) {
        if (!pmm_test((u32)i)) {
            pmm_set((u32)i);
            used_pages++;
        }
    }
}

void pmm_init(struct multiboot_info *mb) {
    u32 i;
    u32 mem_kb = 1024 + mb->mem_upper;
    u8 *mmap;

    total_pages = (mem_kb * 1024) / PAGE_SIZE;
    if (total_pages > PMM_MAX_PAGES * 8) {
        total_pages = PMM_MAX_PAGES * 8;
    }
    bitmap_pages = (total_pages + 7) / 8;
    memset(bitmap, 0xFF, sizeof(bitmap)); /* used by default */
    used_pages = total_pages;

    if (mb->flags & MULTIBOOT_INFO_MMAP) {
        mmap = (u8 *)mb->mmap_addr;
        while ((u32)mmap < mb->mmap_addr + mb->mmap_length) {
            struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)mmap;
            if (e->type == 1) {
                u64 addr = e->addr;
                u64 len = e->len;
                u64 p, end;
                p = (addr + PAGE_SIZE - 1) / PAGE_SIZE;
                end = (addr + len) / PAGE_SIZE;
                for (; p < end && p < total_pages; p++) {
                    if (pmm_test((u32)p)) {
                        pmm_unset((u32)p);
                        used_pages--;
                    }
                }
            }
            mmap += e->size + sizeof(e->size);
        }
    } else {
        for (i = 256; i < total_pages; i++) { /* skip first 1MiB */
            if (pmm_test(i)) {
                pmm_unset(i);
                used_pages--;
            }
        }
    }

    pmm_mark_used_range(0, 0x100000);
    pmm_mark_used_range((u32)&kernel_start, (u32)&kernel_end - (u32)&kernel_start);
    pmm_mark_used_range((u32)bitmap, sizeof(bitmap));

    kprintf("pmm: %u pages (%u KiB), %u free\n",
            total_pages, total_pages * 4, (total_pages - used_pages) * 4);
}

void *pmm_alloc(void) {
    u32 i;
    for (i = 0; i < total_pages; i++) {
        if (!pmm_test(i)) {
            pmm_set(i);
            used_pages++;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free(void *page) {
    u32 p = (u32)page / PAGE_SIZE;
    if (p >= total_pages || !pmm_test(p)) {
        return;
    }
    pmm_unset(p);
    used_pages--;
}

u32 pmm_free_pages(void) {
    return total_pages - used_pages;
}

u32 pmm_total_pages(void) {
    return total_pages;
}
