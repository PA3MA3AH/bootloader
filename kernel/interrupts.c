#include "interrupts.h"
#include "idt.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "exceptions.h"
#include <stddef.h>
#include <stdint.h>

extern void *isr_stub_table[256];

static irq_handler_t irq_handlers[16];

void interrupts_set_console(CONSOLE *con) {
    exceptions_set_console(con);
}

void interrupts_enable(void) {
    __asm__ __volatile__("sti");
}

void interrupts_disable(void) {
    __asm__ __volatile__("cli");
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_unregister_handler(uint8_t irq) {
    if (irq < 16) {
        irq_handlers[irq] = 0;
    }
}

void irq_mask(uint8_t irq) {
    pic_irq_mask(irq);
}

void irq_unmask(uint8_t irq) {
    pic_irq_unmask(irq);
}

void timer_init(uint32_t hz) {
    pit_init(hz);
}

void timer_start(uint32_t hz) {
    pit_start(hz);
}

void timer_stop(void) {
    pit_stop();
}

uint64_t timer_ticks(void) {
    return pit_ticks();
}

uint32_t timer_hz(void) {
    return pit_hz();
}

void interrupts_init(void) {
    int i;

    idt_init();

    for (i = 0; i < 16; i++) {
        irq_handlers[i] = 0;
    }

    for (i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, (uint64_t)(uintptr_t)isr_stub_table[i], 0x8E);
    }

    pic_remap_all_masked();

    /*
     * Только каскад PIC оставляем доступным.
     * IRQ0 включается вручную из shell через piton.
     */
    pic_irq_unmask(2);

    idt_load();
}

void interrupt_dispatch(INTERRUPT_FRAME *frame) {
    uint64_t vec;

    if (!frame) {
        panic_set_stage("interrupt dispatch");
        panic("interrupt_dispatch received NULL frame");
    }

    vec = frame->vector;

    if (vec < 32) {
        handle_exception(frame);
    }

    if (vec >= 32 && vec < 48) {
        uint8_t irq = (uint8_t)(vec - 32);

        if (irq == 0) {
            pit_handle_irq();
        }

        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }

        pic_send_eoi(irq);
        return;
    }
}
