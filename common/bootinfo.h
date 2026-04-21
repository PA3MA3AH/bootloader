#ifndef BOOTINFO_H
#define BOOTINFO_H

#include <stdint.h>

typedef struct {
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint64_t memory_descriptor_version;

    uint64_t usable_memory_bytes;

    uint64_t chosen_region_base;
    uint64_t chosen_region_size;

    uint64_t kernel_phys_base;
    uint64_t kernel_reserved_size;
    uint64_t kernel_file_size;

    uint64_t bootinfo_phys;
    uint64_t bootinfo_size;

    uint64_t scratch_phys;
    uint64_t scratch_size;

    uint64_t kernel_stack_bottom;
    uint64_t kernel_stack_top;

    uint64_t heap_base;
    uint64_t heap_size;

    uint64_t crash_info_phys;
    uint64_t crash_info_size;

    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pixels_per_scanline;
    uint32_t framebuffer_format;
} BOOT_INFO;

#endif
