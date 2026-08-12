#include "gdt.h"
#include <stdint.h>

#define GDT_ENTRIES 6

static gdt_entry_t gdt[GDT_ENTRIES];
static tss_t tss;
static gdt_ptr_t gdt_ptr;

extern void gdt_flush(uint64_t gdt_ptr_addr);
extern void tss_flush(uint16_t tss_selector);

static void gdt_set_entry(uint32_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[index].base_low = (base & 0xFFFF);
    gdt[index].base_mid = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    
    gdt[index].limit_low = (limit & 0xFFFF);
    gdt[index].granularity = (limit >> 16) & 0x0F;
    gdt[index].granularity |= gran & 0xF0;
    gdt[index].access = access;
}

static void tss_set_entry(uint32_t index, uint64_t base, uint32_t limit) {
    tss_entry_t *tss_ent = (tss_entry_t*)&gdt[index];
    
    tss_ent->base_low = base & 0xFFFF;
    tss_ent->base_mid = (base >> 16) & 0xFF;
    tss_ent->base_high = (base >> 24) & 0xFF;
    tss_ent->base_upper = (base >> 32) & 0xFFFFFFFF;
    
    tss_ent->limit_low = limit & 0xFFFF;
    tss_ent->granularity = (limit >> 16) & 0x0F;
    
    tss_ent->access = 0x89;  /* Present, Ring 0, TSS Available */
    tss_ent->granularity |= 0x00;  /* Byte granularity */
    tss_ent->reserved = 0;
}

static void tss_init(void) {
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    
    /* Clear TSS */
    uint8_t *ptr = (uint8_t*)&tss;
    for (uint32_t i = 0; i < sizeof(tss); i++) {
        ptr[i] = 0;
    }
    
    /* Setup TSS entry in GDT */
    tss_set_entry(GDT_ENTRY_TSS, tss_base, tss_limit);
    
    /* Set default kernel stack (will be updated during task switches) */
    tss.rsp0 = 0;
    tss.iomap_base = sizeof(tss);
}

void gdt_init(void) {
    /* NULL descriptor */
    gdt_set_entry(GDT_ENTRY_NULL, 0, 0, 0, 0);
    
    /* Kernel code segment: base=0, limit=0xFFFFFFFF, access=0x9A, gran=0xA0 */
    gdt_set_entry(GDT_ENTRY_KERNEL_CS, 0, 0xFFFFFFFF, 0x9A, 0xA0);
    
    /* Kernel data segment: base=0, limit=0xFFFFFFFF, access=0x92, gran=0xC0 */
    gdt_set_entry(GDT_ENTRY_KERNEL_DS, 0, 0xFFFFFFFF, 0x92, 0xC0);
    
    /* User code segment: base=0, limit=0xFFFFFFFF, access=0xFA, gran=0xA0 */
    gdt_set_entry(GDT_ENTRY_USER_CS, 0, 0xFFFFFFFF, 0xFA, 0xA0);
    
    /* User data segment: base=0, limit=0xFFFFFFFF, access=0xF2, gran=0xC0 */
    gdt_set_entry(GDT_ENTRY_USER_DS, 0, 0xFFFFFFFF, 0xF2, 0xC0);
    
    /* TSS */
    tss_init();
    
    /* Setup GDT pointer */
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) + sizeof(tss_entry_t) - 1;
    gdt_ptr.base = (uint64_t)&gdt[0];
}

void gdt_load(void) {
    gdt_flush((uint64_t)&gdt_ptr);
    tss_flush(GDT_ENTRY_TSS * 8);
}

void tss_set_kernel_stack(uint64_t stack) {
    tss.rsp0 = stack;
}
