#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include "../common/bootinfo.h"

typedef struct {
    uint32_t *base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
} FRAMEBUFFER;

void fb_init(FRAMEBUFFER *fb, BOOT_INFO *boot_info);
void fb_put_pixel(FRAMEBUFFER *fb, uint32_t x, uint32_t y, uint32_t color);
void fb_clear(FRAMEBUFFER *fb, uint32_t color);
void fb_fill_rect(FRAMEBUFFER *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

#endif
