#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include "console.h"

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t vector;
    uint64_t error_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} INTERRUPT_FRAME;

typedef void (*irq_handler_t)(void);

void interrupts_set_console(CONSOLE *con);
void interrupts_init(void);
void interrupts_enable(void);
void interrupts_disable(void);

void irq_register_handler(uint8_t irq, irq_handler_t handler);
void irq_unregister_handler(uint8_t irq);

/* Backward-compatible IRQ mask helpers. */
void irq_mask(uint8_t irq);
void irq_unmask(uint8_t irq);

/* Backward-compatible timer API. */
void timer_init(uint32_t hz);
void timer_start(uint32_t hz);
void timer_stop(void);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);

void interrupt_dispatch(INTERRUPT_FRAME *frame);

#endif
