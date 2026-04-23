#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include "console.h"

#define BLOCK_MAX_DEVICES     16
#define BLOCK_NAME_MAX        16
#define BLOCK_MODEL_MAX       48
#define BLOCK_SECTOR_SIZE_MIN 512U

#define BLOCK_TYPE_UNKNOWN    0
#define BLOCK_TYPE_DISK       1
#define BLOCK_TYPE_PARTITION  2

#define BLOCK_FLAG_PRESENT    (1U << 0)
#define BLOCK_FLAG_READ_ONLY  (1U << 1)
#define BLOCK_FLAG_REMOVABLE  (1U << 2)
#define BLOCK_FLAG_BOOT       (1U << 3)

struct block_device;

typedef struct {
    int (*read)(struct block_device *dev, uint64_t lba, uint32_t count, void *out_buf);
    int (*write)(struct block_device *dev, uint64_t lba, uint32_t count, const void *in_buf);
    int (*flush)(struct block_device *dev);
} BLOCK_DEVICE_OPS;

typedef struct block_device {
    int present;
    uint32_t index;
    uint32_t type;
    uint32_t flags;

    char name[BLOCK_NAME_MAX];
    char model[BLOCK_MODEL_MAX];

    uint32_t sector_size;
    uint64_t total_sectors;
    uint64_t total_bytes;

    const char *driver_name;
    void *driver_data;

    const BLOCK_DEVICE_OPS *ops;
} BLOCK_DEVICE;

void block_init(void);
int block_is_initialized(void);

uint32_t block_get_count(void);
BLOCK_DEVICE *block_get_device(uint32_t index);
BLOCK_DEVICE *block_find_by_name(const char *name);

int block_register_device(const BLOCK_DEVICE *device);

int block_read(BLOCK_DEVICE *dev, uint64_t lba, uint32_t count, void *out_buf);
int block_write(BLOCK_DEVICE *dev, uint64_t lba, uint32_t count, const void *in_buf);
int block_flush(BLOCK_DEVICE *dev);

void block_print_devices(CONSOLE *con);
void block_print_device_info(CONSOLE *con, const BLOCK_DEVICE *dev);

#endif
