#include "framebuffer.h"

void fb_init(FRAMEBUFFER *fb, BOOT_INFO *boot_info) {
    fb->base   = (uint32_t*)(uintptr_t)boot_info->framebuffer_base;
    fb->size   = boot_info->framebuffer_size;
    fb->width  = boot_info->framebuffer_width;
    fb->height = boot_info->framebuffer_height;
    fb->pitch  = boot_info->framebuffer_pixels_per_scanline;
    fb->format = boot_info->framebuffer_format;
}

void fb_put_pixel(FRAMEBUFFER *fb, uint32_t x, uint32_t y, uint32_t color) {
    if (!fb || !fb->base) {
        return;
    }

    if (x >= fb->width || y >= fb->height) {
        return;
    }

    fb->base[y * fb->pitch + x] = color;
}

void fb_clear(FRAMEBUFFER *fb, uint32_t color) {
    if (!fb || !fb->base) {
        return;
    }

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            fb->base[y * fb->pitch + x] = color;
        }
    }
}

void fb_fill_rect(FRAMEBUFFER *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb || !fb->base) {
        return;
    }

    for (uint32_t yy = 0; yy < h; yy++) {
        for (uint32_t xx = 0; xx < w; xx++) {
            fb_put_pixel(fb, x + xx, y + yy, color);
        }
    }
}
