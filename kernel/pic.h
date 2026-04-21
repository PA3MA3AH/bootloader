#ifndef PIC_H
#define PIC_H

#include <stdint.h>

void pic_remap_all_masked(void);
void pic_send_eoi(uint8_t irq);

void pic_irq_mask(uint8_t irq);
void pic_irq_unmask(uint8_t irq);

#endif
