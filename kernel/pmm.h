#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "../common/bootinfo.h"
#include "console.h"

#define PMM_MAX_REGIONS   128
#define PMM_PAGE_SIZE     4096ULL

typedef struct {
    uint64_t base;
    uint64_t pages_total;
} PMM_REGION;

typedef struct {
    PMM_REGION regions[PMM_MAX_REGIONS];
    uint64_t region_count;

    uint64_t total_pages;      /* allocatable pages */
    uint64_t free_pages;       /* currently free allocatable pages */

    uint64_t managed_pages;    /* bitmap coverage in pages, from physical page 0 */
    uint64_t bitmap_bytes;     /* bytes per bitmap */
    uint64_t alloc_hint;       /* next-fit hint */

    uint8_t *usable_bitmap;    /* 1 = page may be allocated by PMM */
    uint8_t *used_bitmap;      /* 1 = page currently allocated */
} PMM_STATE;

void pmm_init(PMM_STATE *pmm, BOOT_INFO *boot_info);
void *pmm_alloc_page(PMM_STATE *pmm);
void *pmm_alloc_pages(PMM_STATE *pmm, uint64_t count);
void pmm_free_page(PMM_STATE *pmm, void *ptr);
void pmm_dump(CONSOLE *con, PMM_STATE *pmm);

#endif
