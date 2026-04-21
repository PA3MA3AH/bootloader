#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t heap_base;
    uint64_t heap_size;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t allocation_count;
    uint64_t free_block_count;
} KHEAP_STATS;

void kheap_init(uint64_t heap_base, uint64_t heap_size);
int kheap_is_initialized(void);

void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t new_size);

void kheap_get_stats(KHEAP_STATS *stats);

#endif
