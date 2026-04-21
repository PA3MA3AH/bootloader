#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(uint32_t hz);
void pit_start(uint32_t hz);
void pit_stop(void);
uint64_t pit_ticks(void);
uint32_t pit_hz(void);
void pit_handle_irq(void);

#endif
