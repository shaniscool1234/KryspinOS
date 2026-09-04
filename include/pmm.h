#ifndef CURSOROS_PMM_H
#define CURSOROS_PMM_H

#include <types.h>
#include <multiboot.h>

#define PAGE_SIZE 4096

void pmm_init(struct multiboot_info *mb);
void *pmm_alloc(void);
void  pmm_free(void *page);
u32   pmm_free_pages(void);
u32   pmm_total_pages(void);

#endif
