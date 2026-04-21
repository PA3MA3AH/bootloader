#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "../common/bootinfo.h"
#include "console.h"

#define PMM_MAX_REGIONS   128
#define PMM_PAGE_SIZE     4096ULL

void pmm_init(CONSOLE *con, BOOT_INFO *boot_info);
void *pmm_alloc_page(void);
void *pmm_alloc_pages(uint64_t count);
void pmm_free_page(void *ptr);
void pmm_dump(CONSOLE *con);

uint64_t pmm_region_count(void);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_used_pages(void);
uint64_t pmm_managed_pages(void);
uint64_t pmm_bitmap_bytes(void);

#endif
