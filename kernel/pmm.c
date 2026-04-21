#include "pmm.h"

#define EFI_CONVENTIONAL_MEMORY 7

typedef struct {
    uint64_t base;
    uint64_t pages_total;
} PMM_REGION;

typedef struct {
    PMM_REGION regions[PMM_MAX_REGIONS];
    uint64_t region_count;

    uint64_t total_pages;
    uint64_t free_pages;

    uint64_t managed_pages;
    uint64_t bitmap_bytes;
    uint64_t alloc_hint;

    uint8_t *usable_bitmap;
    uint8_t *used_bitmap;
} PMM_STATE;

typedef struct {
    uint32_t type;
    uint32_t pad;              // ← ДОБАВИТЬ!
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} KERNEL_EFI_MEMORY_DESCRIPTOR;

typedef struct {
    uint64_t start;
    uint64_t end;
} PMM_RESERVED_RANGE;

static PMM_STATE g_pmm;

static void memfill_u8(uint8_t *dst, uint8_t value, uint64_t size) {
    if (!dst) {
        return;
    }

    for (uint64_t i = 0; i < size; i++) {
        dst[i] = value;
    }
}

static uint64_t align_down_u64(uint64_t value, uint64_t align) {
    if (align == 0) {
        return value;
    }

    return value & ~(align - 1);
}

static uint64_t align_up_u64(uint64_t value, uint64_t align) {
    if (align == 0) {
        return value;
    }

    return (value + align - 1) & ~(align - 1);
}

static void pmm_add_region(PMM_STATE *pmm, uint64_t base, uint64_t pages) {
    if (!pmm || pages == 0) {
        return;
    }

    if (pmm->region_count >= PMM_MAX_REGIONS) {
        return;
    }

    pmm->regions[pmm->region_count].base = base;
    pmm->regions[pmm->region_count].pages_total = pages;
    pmm->region_count++;
}

static void bitmap_set(uint8_t *bitmap, uint64_t bit_index) {
    bitmap[bit_index >> 3] |= (uint8_t)(1u << (bit_index & 7u));
}

static void bitmap_clear(uint8_t *bitmap, uint64_t bit_index) {
    bitmap[bit_index >> 3] &= (uint8_t)~(1u << (bit_index & 7u));
}

static int bitmap_test(uint8_t *bitmap, uint64_t bit_index) {
    return (bitmap[bit_index >> 3] & (uint8_t)(1u << (bit_index & 7u))) != 0;
}

static void pmm_mark_usable(PMM_STATE *pmm, uint64_t start_addr, uint64_t end_addr) {
    if (!pmm || end_addr <= start_addr) {
        return;
    }

    uint64_t start_page = align_up_u64(start_addr, PMM_PAGE_SIZE) / PMM_PAGE_SIZE;
    uint64_t end_page   = align_down_u64(end_addr, PMM_PAGE_SIZE) / PMM_PAGE_SIZE;

    if (end_page <= start_page) {
        return;
    }

    if (start_page >= pmm->managed_pages) {
        return;
    }

    if (end_page > pmm->managed_pages) {
        end_page = pmm->managed_pages;
    }

    for (uint64_t page = start_page; page < end_page; page++) {
        if (!bitmap_test(pmm->usable_bitmap, page)) {
            bitmap_set(pmm->usable_bitmap, page);
            bitmap_clear(pmm->used_bitmap, page);
            pmm->total_pages++;
            pmm->free_pages++;
        }
    }
}

static void pmm_mark_reserved(PMM_STATE *pmm, uint64_t start_addr, uint64_t end_addr) {
    if (!pmm || end_addr <= start_addr) {
        return;
    }

    uint64_t start_page = align_down_u64(start_addr, PMM_PAGE_SIZE) / PMM_PAGE_SIZE;
    uint64_t end_page   = align_up_u64(end_addr, PMM_PAGE_SIZE) / PMM_PAGE_SIZE;

    if (start_page >= pmm->managed_pages) {
        return;
    }

    if (end_page > pmm->managed_pages) {
        end_page = pmm->managed_pages;
    }

    for (uint64_t page = start_page; page < end_page; page++) {
        if (bitmap_test(pmm->usable_bitmap, page)) {
            bitmap_clear(pmm->usable_bitmap, page);
            bitmap_clear(pmm->used_bitmap, page);

            if (pmm->total_pages > 0) {
                pmm->total_pages--;
            }

            if (pmm->free_pages > 0) {
                pmm->free_pages--;
            }
        }
    }
}

static uint64_t pmm_region_free_pages_internal(PMM_STATE *pmm, PMM_REGION *region) {
    if (!pmm || !region) {
        return 0;
    }

    uint64_t start_page = region->base / PMM_PAGE_SIZE;
    uint64_t free_pages = 0;

    for (uint64_t i = 0; i < region->pages_total; i++) {
        uint64_t page = start_page + i;

        if (page >= pmm->managed_pages) {
            break;
        }

        if (bitmap_test(pmm->usable_bitmap, page) && !bitmap_test(pmm->used_bitmap, page)) {
            free_pages++;
        }
    }

    return free_pages;
}

void pmm_init(CONSOLE *con, BOOT_INFO *boot_info) {
    PMM_STATE *pmm = &g_pmm;

    for (uint64_t i = 0; i < PMM_MAX_REGIONS; i++) {
        pmm->regions[i].base = 0;
        pmm->regions[i].pages_total = 0;
    }

    pmm->region_count   = 0;
    pmm->total_pages    = 0;
    pmm->free_pages     = 0;
    pmm->managed_pages  = 0;
    pmm->bitmap_bytes   = 0;
    pmm->alloc_hint     = 0;
    pmm->usable_bitmap  = (uint8_t*)0;
    pmm->used_bitmap    = (uint8_t*)0;

    if (!boot_info) {
        return;
    }

    if (boot_info->memory_map == 0 ||
        boot_info->memory_map_size == 0 ||
        boot_info->memory_descriptor_size == 0 ||
        boot_info->scratch_phys == 0 ||
        boot_info->scratch_size == 0) {
        return;
    }

    uint64_t entry_count = boot_info->memory_map_size / boot_info->memory_descriptor_size;
    if (entry_count == 0) {
        return;
    }

    uint8_t *map_base = (uint8_t*)(uintptr_t)boot_info->memory_map;
    uint64_t highest_page_plus_one = 0;

    for (uint64_t i = 0; i < entry_count; i++) {
        KERNEL_EFI_MEMORY_DESCRIPTOR *desc =
            (KERNEL_EFI_MEMORY_DESCRIPTOR*)(map_base + i * boot_info->memory_descriptor_size);
    
        if (i < 8) {
            console_printf(con,
                "[PMM-DESC %u] type=%u phys=%p pages=%u attr=%p\n",
                (unsigned int)i,
                (unsigned int)desc->type,
                (void*)(uintptr_t)desc->physical_start,
                (unsigned int)desc->number_of_pages,
                (void*)(uintptr_t)desc->attribute);
        }
    
        if (!desc) {
            continue;
        }
    
        if (desc->type != 7 || desc->number_of_pages == 0) {
            continue;
        }
    
        uint64_t start_page = desc->physical_start / PMM_PAGE_SIZE;
        uint64_t end_page   = start_page + desc->number_of_pages;
    
        if (end_page > highest_page_plus_one) {
            highest_page_plus_one = end_page;
        }
    }

    if (highest_page_plus_one == 0) {
        return;
    }

    uint64_t required_bitmap_bytes = (highest_page_plus_one + 7) / 8;
    if (required_bitmap_bytes == 0) {
        return;
    }
    
    uint64_t scratch_base = boot_info->scratch_phys;
    uint64_t scratch_end  = boot_info->scratch_phys + boot_info->scratch_size;
    
    uint64_t bitmap_base = scratch_base;
    
    // если memory map лежит в scratch, кладём bitmap ПОСЛЕ неё
    if (boot_info->memory_map >= scratch_base && boot_info->memory_map < scratch_end) {
        uint64_t map_end = boot_info->memory_map + boot_info->memory_map_size;
        bitmap_base = align_up_u64(map_end, 16);
    }
    
    uint64_t total_bitmap_bytes = required_bitmap_bytes * 2;
    if (bitmap_base + total_bitmap_bytes > scratch_end) {
        return;
    }
    
    pmm->managed_pages = highest_page_plus_one;
    pmm->bitmap_bytes  = required_bitmap_bytes;
    pmm->usable_bitmap = (uint8_t*)(uintptr_t)bitmap_base;
    pmm->used_bitmap   = pmm->usable_bitmap + required_bitmap_bytes;
    pmm->alloc_hint    = 0;
    
    memfill_u8(pmm->usable_bitmap, 0x00, required_bitmap_bytes);
    memfill_u8(pmm->used_bitmap,   0x00, required_bitmap_bytes);

    for (uint64_t i = 0; i < entry_count; i++) {
        KERNEL_EFI_MEMORY_DESCRIPTOR *desc =
            (KERNEL_EFI_MEMORY_DESCRIPTOR*)(map_base + i * boot_info->memory_descriptor_size);
    
        if (!desc) {
            continue;
        }
    
        if (desc->type != 7 || desc->number_of_pages == 0) {
            continue;
        }
    
        uint64_t region_start = desc->physical_start;
        uint64_t region_end   = region_start + desc->number_of_pages * PMM_PAGE_SIZE;
    
        pmm_add_region(pmm, region_start, desc->number_of_pages);
        pmm_mark_usable(pmm, region_start, region_end);
    }

    PMM_RESERVED_RANGE reserved[] = {
        {
            boot_info->kernel_phys_base,
            boot_info->kernel_phys_base + boot_info->kernel_reserved_size
        },
        {
            boot_info->bootinfo_phys,
            boot_info->bootinfo_phys + boot_info->bootinfo_size
        },
        {
            boot_info->scratch_phys,
            boot_info->scratch_phys + boot_info->scratch_size
        },
        {
            boot_info->kernel_stack_bottom,
            boot_info->kernel_stack_top
        },
        {
            boot_info->heap_base,
            boot_info->heap_base + boot_info->heap_size
        },
        {
            boot_info->crash_info_phys,
            boot_info->crash_info_phys + boot_info->crash_info_size
        }
    };

    uint64_t reserved_count = sizeof(reserved) / sizeof(reserved[0]);
    for (uint64_t i = 0; i < reserved_count; i++) {
        if (reserved[i].end > reserved[i].start) {
            pmm_mark_reserved(pmm, reserved[i].start, reserved[i].end);
        }
    }
}

void *pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

void *pmm_alloc_pages(uint64_t count) {
    PMM_STATE *pmm = &g_pmm;

    if (!pmm->usable_bitmap || !pmm->used_bitmap || count == 0) {
        return (void*)0;
    }

    if (count > pmm->free_pages || count > pmm->managed_pages) {
        return (void*)0;
    }

    uint64_t pass_start[2];
    uint64_t pass_end[2];

    pass_start[0] = pmm->alloc_hint;
    pass_end[0]   = pmm->managed_pages;

    pass_start[1] = 0;
    pass_end[1]   = pmm->alloc_hint;

    for (int pass = 0; pass < 2; pass++) {
        uint64_t start_limit = pass_start[pass];
        uint64_t end_limit   = pass_end[pass];

        if (end_limit <= start_limit) {
            continue;
        }

        if ((end_limit - start_limit) < count) {
            continue;
        }

        for (uint64_t start_page = start_limit; start_page + count <= end_limit; start_page++) {
            int ok = 1;

            for (uint64_t i = 0; i < count; i++) {
                uint64_t page = start_page + i;

                if (!bitmap_test(pmm->usable_bitmap, page) ||
                    bitmap_test(pmm->used_bitmap, page)) {
                    ok = 0;
                    start_page += i;
                    break;
                }
            }

            if (!ok) {
                continue;
            }

            for (uint64_t i = 0; i < count; i++) {
                bitmap_set(pmm->used_bitmap, start_page + i);
            }

            pmm->free_pages -= count;
            pmm->alloc_hint = start_page + count;

            if (pmm->alloc_hint >= pmm->managed_pages) {
                pmm->alloc_hint = 0;
            }

            return (void*)(uintptr_t)(start_page * PMM_PAGE_SIZE);
        }
    }

    return (void*)0;
}

void pmm_free_page(void *ptr) {
    PMM_STATE *pmm = &g_pmm;

    if (!ptr || !pmm->usable_bitmap || !pmm->used_bitmap) {
        return;
    }

    uint64_t addr = (uint64_t)(uintptr_t)ptr;

    if ((addr & (PMM_PAGE_SIZE - 1)) != 0) {
        return;
    }

    uint64_t page = addr / PMM_PAGE_SIZE;

    if (page >= pmm->managed_pages) {
        return;
    }

    if (!bitmap_test(pmm->usable_bitmap, page)) {
        return;
    }

    if (!bitmap_test(pmm->used_bitmap, page)) {
        return;
    }

    bitmap_clear(pmm->used_bitmap, page);
    pmm->free_pages++;

    if (page < pmm->alloc_hint) {
        pmm->alloc_hint = page;
    }
}

void pmm_dump(CONSOLE *con) {
    PMM_STATE *pmm = &g_pmm;

    if (!con) {
        return;
    }

    console_printf(con, "=== PMM state ===\n");
    console_printf(con, "Region count:          %u\n", (unsigned int)pmm->region_count);
    console_printf(con, "Managed pages:         %u\n", (unsigned int)pmm->managed_pages);
    console_printf(con, "Bitmap bytes / map:    %u\n", (unsigned int)pmm->bitmap_bytes);
    console_printf(con, "Total alloc pages:     %u\n", (unsigned int)pmm->total_pages);
    console_printf(con, "Free alloc pages:      %u\n", (unsigned int)pmm->free_pages);
    console_printf(con, "Used alloc pages:      %u\n",
                   (unsigned int)(pmm->total_pages - pmm->free_pages));
    console_printf(con, "Next alloc hint page:  %u\n\n", (unsigned int)pmm->alloc_hint);

    for (uint64_t i = 0; i < pmm->region_count; i++) {
        PMM_REGION *r = &pmm->regions[i];
        uint64_t free_pages = pmm_region_free_pages_internal(pmm, r);
        uint64_t used_pages = (r->pages_total >= free_pages) ? (r->pages_total - free_pages) : 0;

        console_printf(con, "[%u] base=%p pages=%u free=%u used_or_reserved=%u\n",
                       (unsigned int)i,
                       (void*)(uintptr_t)r->base,
                       (unsigned int)r->pages_total,
                       (unsigned int)free_pages,
                       (unsigned int)used_pages);
    }

    console_printf(con, "\n");
}

uint64_t pmm_region_count(void) {
    return g_pmm.region_count;
}

uint64_t pmm_total_pages(void) {
    return g_pmm.total_pages;
}

uint64_t pmm_free_pages(void) {
    return g_pmm.free_pages;
}

uint64_t pmm_used_pages(void) {
    return g_pmm.total_pages - g_pmm.free_pages;
}

uint64_t pmm_managed_pages(void) {
    return g_pmm.managed_pages;
}

uint64_t pmm_bitmap_bytes(void) {
    return g_pmm.bitmap_bytes;
}
