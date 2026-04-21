#include "idt.h"

static IDT_ENTRY g_idt[256];
static IDT_PTR g_idt_ptr;

static uint16_t idt_read_cs(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        g_idt[i].offset_low = 0;
        g_idt[i].selector = 0;
        g_idt[i].ist = 0;
        g_idt[i].type_attr = 0;
        g_idt[i].offset_mid = 0;
        g_idt[i].offset_high = 0;
        g_idt[i].zero = 0;
    }

    g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
    g_idt_ptr.base = (uint64_t)(uintptr_t)&g_idt[0];
}

void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t type_attr) {
    uint16_t current_cs = idt_read_cs();

    g_idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    g_idt[vector].selector    = current_cs;
    g_idt[vector].ist         = 0;
    g_idt[vector].type_attr   = type_attr;
    g_idt[vector].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    g_idt[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    g_idt[vector].zero        = 0;
}

void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(g_idt_ptr));
}
