#include "vmm.h"
#include "pmm.h"
#include "panic.h"

#define PAGE_SIZE_2M  0x200000ULL
#define VMM_LARGE     (1ULL << 7)

static uint64_t *g_pml4 = 0;
static uint64_t *g_pdpt = 0;
static uint64_t *g_pd0  = 0;
static uint64_t *g_pd1  = 0;
static uint64_t *g_pd2  = 0;
static uint64_t *g_pd3  = 0;

static inline void write_cr3(uint64_t val) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(val) : "memory");
}

static uint64_t *alloc_table_zeroed(void) {
    uint64_t *tbl = (uint64_t*)pmm_alloc_page();
    if (!tbl) {
        panic("VMM bootstrap: out of memory");
    }

    for (uint64_t i = 0; i < 512; i++) {
        tbl[i] = 0;
    }

    return tbl;
}

static void fill_pd_2m_identity(uint64_t *pd, uint64_t base_addr) {
    if (!pd) {
        panic("VMM bootstrap: null PD");
    }

    for (uint64_t i = 0; i < 512; i++) {
        uint64_t addr = base_addr + i * PAGE_SIZE_2M;
        pd[i] = (addr & ~0x1FFFFFULL) | VMM_PRESENT | VMM_WRITE | VMM_LARGE;
    }
}

void vmm_init(void) {
    g_pml4 = alloc_table_zeroed();
    g_pdpt = alloc_table_zeroed();
    g_pd0  = alloc_table_zeroed();
    g_pd1  = alloc_table_zeroed();
    g_pd2  = alloc_table_zeroed();
    g_pd3  = alloc_table_zeroed();

    // PML4[0] -> PDPT
    g_pml4[0] = ((uint64_t)(uintptr_t)g_pdpt & ~0xFFFULL) | VMM_PRESENT | VMM_WRITE;

    // PDPT entries: each covers 1 GB
    g_pdpt[0] = ((uint64_t)(uintptr_t)g_pd0 & ~0xFFFULL) | VMM_PRESENT | VMM_WRITE; // 0..1GB
    g_pdpt[1] = ((uint64_t)(uintptr_t)g_pd1 & ~0xFFFULL) | VMM_PRESENT | VMM_WRITE; // 1..2GB
    g_pdpt[2] = ((uint64_t)(uintptr_t)g_pd2 & ~0xFFFULL) | VMM_PRESENT | VMM_WRITE; // 2..3GB
    g_pdpt[3] = ((uint64_t)(uintptr_t)g_pd3 & ~0xFFFULL) | VMM_PRESENT | VMM_WRITE; // 3..4GB

    fill_pd_2m_identity(g_pd0, 0x00000000ULL);
    fill_pd_2m_identity(g_pd1, 0x40000000ULL);
    fill_pd_2m_identity(g_pd2, 0x80000000ULL);
    fill_pd_2m_identity(g_pd3, 0xC0000000ULL);

    write_cr3((uint64_t)(uintptr_t)g_pml4);
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    (void)virt;
    (void)phys;
    (void)flags;
    panic("VMM bootstrap: vmm_map_page not implemented yet");
}

void vmm_unmap_page(uint64_t virt) {
    (void)virt;
    panic("VMM bootstrap: vmm_unmap_page not implemented yet");
}

uint64_t vmm_translate(uint64_t virt) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;

    if (pml4_i != 0) {
        return 0;
    }

    uint64_t *pd = 0;

    switch (pdpt_i) {
        case 0: pd = g_pd0; break;
        case 1: pd = g_pd1; break;
        case 2: pd = g_pd2; break;
        case 3: pd = g_pd3; break;
        default: return 0;
    }

    uint64_t entry = pd[pd_i];
    if (!(entry & VMM_PRESENT)) {
        return 0;
    }

    if (!(entry & VMM_LARGE)) {
        return 0;
    }

    return (entry & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
}
