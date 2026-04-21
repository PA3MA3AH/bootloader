#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} IDT_ENTRY;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} IDT_PTR;

void idt_init(void);
void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t type_attr);
void idt_load(void);

#endif
