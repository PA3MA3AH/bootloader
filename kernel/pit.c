#include "pit.h"
#include "io.h"
#include "pic.h"

#define PIT_COMMAND        0x43
#define PIT_CHANNEL0       0x40
#define PIT_BASE_FREQUENCY 1193182U

static volatile uint64_t g_pit_ticks = 0;
static uint32_t g_pit_hz = 0;

void pit_init(uint32_t hz) {
    uint16_t divisor;

    if (hz == 0) {
        hz = 100;
    }

    divisor = (uint16_t)(PIT_BASE_FREQUENCY / hz);
    g_pit_hz = hz;
    g_pit_ticks = 0;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_start(uint32_t hz) {
    pit_init(hz);
    pic_irq_unmask(0);
}

void pit_stop(void) {
    pic_irq_mask(0);
    g_pit_hz = 0;
}

uint64_t pit_ticks(void) {
    return g_pit_ticks;
}

uint32_t pit_hz(void) {
    return g_pit_hz;
}

void pit_handle_irq(void) {
    g_pit_ticks++;
}
