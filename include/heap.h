#ifndef CURSOROS_HEAP_H
#define CURSOROS_HEAP_H

#include <types.h>

void  heap_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t n, size_t size);
void  kfree(void *ptr);

#endif
