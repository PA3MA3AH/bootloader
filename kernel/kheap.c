#include "kheap.h"
#include <stdint.h>
#include <stddef.h>

#define KHEAP_ALIGNMENT 16ULL
#define KHEAP_MIN_SPLIT 32ULL

typedef struct heap_block {
    uint64_t size;
    uint32_t free;
    uint32_t reserved;
    struct heap_block *next;
    struct heap_block *prev;
} HEAP_BLOCK;

static HEAP_BLOCK *g_heap_head = 0;
static uint64_t g_heap_base = 0;
static uint64_t g_heap_size = 0;
static int g_heap_initialized = 0;

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    uint64_t rem;

    if (alignment == 0) {
        return value;
    }

    rem = value % alignment;
    if (rem == 0) {
        return value;
    }

    return value + (alignment - rem);
}

static uint64_t block_header_size(void) {
    return align_up_u64((uint64_t)sizeof(HEAP_BLOCK), KHEAP_ALIGNMENT);
}

static void memory_zero(void *ptr, uint64_t size) {
    uint8_t *p = (uint8_t*)ptr;
    uint64_t i;

    for (i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void memory_copy(void *dst, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    uint64_t i;

    for (i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void split_block(HEAP_BLOCK *block, uint64_t wanted_size) {
    uint64_t header_size;
    uint64_t remaining_size;
    HEAP_BLOCK *new_block;
    uint8_t *block_bytes;

    if (!block) {
        return;
    }

    header_size = block_header_size();

    if (block->size <= wanted_size + header_size + KHEAP_MIN_SPLIT) {
        return;
    }

    remaining_size = block->size - wanted_size - header_size;
    block_bytes = (uint8_t*)block;

    new_block = (HEAP_BLOCK*)(void*)(block_bytes + header_size + wanted_size);
    new_block->size = remaining_size;
    new_block->free = 1;
    new_block->reserved = 0;
    new_block->next = block->next;
    new_block->prev = block;

    if (new_block->next) {
        new_block->next->prev = new_block;
    }

    block->size = wanted_size;
    block->next = new_block;
}

static void coalesce_with_next(HEAP_BLOCK *block) {
    uint64_t header_size;
    HEAP_BLOCK *next;

    if (!block) {
        return;
    }

    next = block->next;
    if (!next || !next->free) {
        return;
    }

    header_size = block_header_size();

    block->size += header_size + next->size;
    block->next = next->next;

    if (block->next) {
        block->next->prev = block;
    }
}

static void coalesce_block(HEAP_BLOCK *block) {
    if (!block) {
        return;
    }

    coalesce_with_next(block);

    if (block->prev && block->prev->free) {
        coalesce_with_next(block->prev);
    }
}

void kheap_init(uint64_t heap_base, uint64_t heap_size) {
    uint64_t aligned_base;
    uint64_t aligned_size;
    uint64_t header_size;
    HEAP_BLOCK *head;

    header_size = block_header_size();

    aligned_base = align_up_u64(heap_base, KHEAP_ALIGNMENT);

    if (heap_size <= (aligned_base - heap_base)) {
        g_heap_initialized = 0;
        return;
    }

    aligned_size = heap_size - (aligned_base - heap_base);
    aligned_size = aligned_size & ~(KHEAP_ALIGNMENT - 1ULL);

    if (aligned_size <= header_size + KHEAP_MIN_SPLIT) {
        g_heap_initialized = 0;
        return;
    }

    g_heap_base = aligned_base;
    g_heap_size = aligned_size;

    head = (HEAP_BLOCK*)(uintptr_t)aligned_base;
    head->size = aligned_size - header_size;
    head->free = 1;
    head->reserved = 0;
    head->next = 0;
    head->prev = 0;

    g_heap_head = head;
    g_heap_initialized = 1;
}

int kheap_is_initialized(void) {
    return g_heap_initialized;
}

void *kmalloc(size_t size) {
    uint64_t wanted_size;
    HEAP_BLOCK *block;
    uint64_t header_size;

    if (!g_heap_initialized || size == 0) {
        return 0;
    }

    wanted_size = align_up_u64((uint64_t)size, KHEAP_ALIGNMENT);
    header_size = block_header_size();

    block = g_heap_head;
    while (block) {
        if (block->free && block->size >= wanted_size) {
            split_block(block, wanted_size);
            block->free = 0;
            return (void*)((uint8_t*)block + header_size);
        }

        block = block->next;
    }

    return 0;
}

void kfree(void *ptr) {
    HEAP_BLOCK *block;
    uint64_t header_size;

    if (!g_heap_initialized || !ptr) {
        return;
    }

    header_size = block_header_size();
    block = (HEAP_BLOCK*)(void*)((uint8_t*)ptr - header_size);

    block->free = 1;
    coalesce_block(block);
}

void *kcalloc(size_t count, size_t size) {
    uint64_t total;
    void *ptr;

    if (count == 0 || size == 0) {
        return 0;
    }

    total = (uint64_t)count * (uint64_t)size;
    if ((uint64_t)size != 0 && total / (uint64_t)size != (uint64_t)count) {
        return 0;
    }

    ptr = kmalloc((size_t)total);
    if (!ptr) {
        return 0;
    }

    memory_zero(ptr, total);
    return ptr;
}

void *krealloc(void *ptr, size_t new_size) {
    HEAP_BLOCK *block;
    uint64_t header_size;
    uint64_t old_size;
    void *new_ptr;

    if (!ptr) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }

    header_size = block_header_size();
    block = (HEAP_BLOCK*)(void*)((uint8_t*)ptr - header_size);
    old_size = block->size;

    if (old_size >= (uint64_t)new_size) {
        return ptr;
    }

    new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return 0;
    }

    memory_copy(new_ptr, ptr, old_size);
    kfree(ptr);

    return new_ptr;
}

void kheap_get_stats(KHEAP_STATS *stats) {
    HEAP_BLOCK *block;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t allocation_count;
    uint64_t free_block_count;

    if (!stats) {
        return;
    }

    stats->heap_base = g_heap_base;
    stats->heap_size = g_heap_size;
    stats->used_bytes = 0;
    stats->free_bytes = 0;
    stats->allocation_count = 0;
    stats->free_block_count = 0;

    if (!g_heap_initialized) {
        return;
    }

    used_bytes = 0;
    free_bytes = 0;
    allocation_count = 0;
    free_block_count = 0;

    block = g_heap_head;
    while (block) {
        if (block->free) {
            free_bytes += block->size;
            free_block_count++;
        } else {
            used_bytes += block->size;
            allocation_count++;
        }

        block = block->next;
    }

    stats->used_bytes = used_bytes;
    stats->free_bytes = free_bytes;
    stats->allocation_count = allocation_count;
    stats->free_block_count = free_block_count;
}
