#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_ENTRY_NULL      0
#define GDT_ENTRY_KERNEL_CS 1
#define GDT_ENTRY_KERNEL_DS 2
#define GDT_ENTRY_USER_CS   3
#define GDT_ENTRY_USER_DS   4
#define GDT_ENTRY_TSS       5

#define GDT_KERNEL_CS (GDT_ENTRY_KERNEL_CS * 8)
#define GDT_KERNEL_DS (GDT_ENTRY_KERNEL_DS * 8)
#define GDT_USER_CS   (GDT_ENTRY_USER_CS * 8 | 3)
#define GDT_USER_DS   (GDT_ENTRY_USER_DS * 8 | 3)

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} tss_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

void gdt_init(void);
void gdt_load(void);
void tss_set_kernel_stack(uint64_t stack);

#endif
