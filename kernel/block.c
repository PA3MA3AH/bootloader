#include "block.h"
#include "panic.h"

#include <stdint.h>
#include <stddef.h>

static BLOCK_DEVICE g_block_devices[BLOCK_MAX_DEVICES];
static uint32_t g_block_device_count = 0;
static int g_block_initialized = 0;

static void block_mem_zero(void *dst, uint64_t size) {
    uint8_t *p = (uint8_t*)dst;
    while (size--) {
        *p++ = 0;
    }
}

static void block_mem_copy(void *dst, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;

    while (size--) {
        *d++ = *s++;
    }
}

static int block_str_eq(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

void block_init(void) {
    block_mem_zero(g_block_devices, sizeof(g_block_devices));
    g_block_device_count = 0;
    g_block_initialized = 1;
}

int block_is_initialized(void) {
    return g_block_initialized;
}

uint32_t block_get_count(void) {
    return g_block_device_count;
}

BLOCK_DEVICE *block_get_device(uint32_t index) {
    if (!g_block_initialized) {
        return NULL;
    }

    if (index >= g_block_device_count) {
        return NULL;
    }

    if (!g_block_devices[index].present) {
        return NULL;
    }

    return &g_block_devices[index];
}

BLOCK_DEVICE *block_find_by_name(const char *name) {
    uint32_t i;

    if (!g_block_initialized || !name || !*name) {
        return NULL;
    }

    for (i = 0; i < g_block_device_count; i++) {
        if (g_block_devices[i].present && block_str_eq(g_block_devices[i].name, name)) {
            return &g_block_devices[i];
        }
    }

    return NULL;
}

int block_register_device(const BLOCK_DEVICE *device) {
    BLOCK_DEVICE *dst;

    if (!g_block_initialized || !device || !device->present || !device->ops || !device->ops->read) {
        return 0;
    }

    if (g_block_device_count >= BLOCK_MAX_DEVICES) {
        return 0;
    }

    dst = &g_block_devices[g_block_device_count];
    block_mem_zero(dst, sizeof(*dst));
    block_mem_copy(dst, device, sizeof(*dst));
    dst->index = g_block_device_count;
    dst->present = 1;
    g_block_device_count++;
    return 1;
}

int block_read(BLOCK_DEVICE *dev, uint64_t lba, uint32_t count, void *out_buf) {
    uint64_t end_lba;

    if (!dev || !dev->present || !dev->ops || !dev->ops->read || !out_buf) {
        return 0;
    }

    if (count == 0) {
        return 0;
    }

    end_lba = lba + (uint64_t)count;
    if (end_lba < lba) {
        return 0;
    }

    if (end_lba > dev->total_sectors) {
        return 0;
    }

    return dev->ops->read(dev, lba, count, out_buf);
}

int block_write(BLOCK_DEVICE *dev, uint64_t lba, uint32_t count, const void *in_buf) {
    uint64_t end_lba;

    if (!dev || !dev->present || !dev->ops || !dev->ops->write || !in_buf) {
        return 0;
    }

    if (dev->flags & BLOCK_FLAG_READ_ONLY) {
        return 0;
    }

    if (count == 0) {
        return 0;
    }

    end_lba = lba + (uint64_t)count;
    if (end_lba < lba) {
        return 0;
    }

    if (end_lba > dev->total_sectors) {
        return 0;
    }

    return dev->ops->write(dev, lba, count, in_buf);
}

int block_flush(BLOCK_DEVICE *dev) {
    if (!dev || !dev->present || !dev->ops || !dev->ops->flush) {
        return 1;
    }

    return dev->ops->flush(dev);
}

void block_print_device_info(CONSOLE *con, const BLOCK_DEVICE *dev) {
    if (!con || !dev || !dev->present) {
        return;
    }

    console_printf(con, "block device %u:\n", (unsigned int)dev->index);
    console_printf(con, "  name:          %s\n", dev->name[0] ? dev->name : "(unnamed)");
    console_printf(con, "  model:         %s\n", dev->model[0] ? dev->model : "(unknown)");
    console_printf(con, "  driver:        %s\n", dev->driver_name ? dev->driver_name : "(none)");
    console_printf(con, "  sector size:   %u\n", (unsigned int)dev->sector_size);
    console_printf(con, "  total sectors: %u\n", (unsigned int)(dev->total_sectors & 0xFFFFFFFFULL));
    console_printf(con, "  total bytes:   %u\n", (unsigned int)(dev->total_bytes & 0xFFFFFFFFULL));
    console_printf(con, "  flags:         %x\n", (unsigned int)dev->flags);
}

void block_print_devices(CONSOLE *con) {
    uint32_t i;

    if (!con) {
        return;
    }

    if (!g_block_initialized) {
        console_printf(con, "block: not initialized\n");
        return;
    }

    console_printf(con, "Block devices: %u\n", (unsigned int)g_block_device_count);

    for (i = 0; i < g_block_device_count; i++) {
        BLOCK_DEVICE *dev = &g_block_devices[i];

        if (!dev->present) {
            continue;
        }

        console_printf(con,
                       "  [%u] %s  driver=%s  sectors=%u  sector_size=%u",
                       (unsigned int)dev->index,
                       dev->name[0] ? dev->name : "(unnamed)",
                       dev->driver_name ? dev->driver_name : "(none)",
                       (unsigned int)(dev->total_sectors & 0xFFFFFFFFULL),
                       (unsigned int)dev->sector_size);

        if (dev->model[0]) {
            console_printf(con, "  model=%s", dev->model);
        }

        console_printf(con, "\n");
    }
}
