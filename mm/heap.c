#include <heap.h>
#include <string.h>
#include <kstdio.h>

#define HEAP_MAGIC 0xC0DE0001u
#define HEAP_ARENA 0x100000

struct block {
    u32 magic;
    u32 size;
    u32 free;
    struct block *next;
};

static u8 heap_arena[HEAP_ARENA] ALIGN(16);
static struct block *heap_head;

void heap_init(void) {
    heap_head = (struct block *)heap_arena;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size = HEAP_ARENA - sizeof(struct block);
    heap_head->free = 1;
    heap_head->next = NULL;
    kprintf("heap: %p (%u bytes)\n", heap_arena, HEAP_ARENA);
}

static void split(struct block *b, size_t size) {
    struct block *n;
    if (b->size < size + sizeof(struct block) + 16) {
        return;
    }
    n = (struct block *)((u8 *)b + sizeof(struct block) + size);
    n->magic = HEAP_MAGIC;
    n->size = b->size - (u32)size - sizeof(struct block);
    n->free = 1;
    n->next = b->next;
    b->size = (u32)size;
    b->next = n;
}

void *kmalloc(size_t size) {
    struct block *b;
    if (size == 0) {
        return NULL;
    }
    size = (size + 7u) & ~7u;
    for (b = heap_head; b; b = b->next) {
        if (b->magic != HEAP_MAGIC) {
            return NULL;
        }
        if (b->free && b->size >= size) {
            split(b, size);
            b->free = 0;
            return (u8 *)b + sizeof(struct block);
        }
    }
    return NULL;
}

void *kcalloc(size_t n, size_t size) {
    size_t total = n * size;
    void *p = kmalloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void kfree(void *ptr) {
    struct block *b;
    struct block *c;
    if (!ptr) {
        return;
    }
    b = (struct block *)((u8 *)ptr - sizeof(struct block));
    if (b->magic != HEAP_MAGIC) {
        return;
    }
    b->free = 1;
    for (c = heap_head; c && c->next; ) {
        if (c->free && c->next->free) {
            c->size += sizeof(struct block) + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
}
