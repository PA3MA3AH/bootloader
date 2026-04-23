#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>
#include "console.h"
#include "block.h"

#define PARTITION_MAX_PARTITIONS 32
#define PARTITION_NAME_MAX       24

#define PARTITION_SCHEME_NONE    0
#define PARTITION_SCHEME_MBR     1

typedef struct {
    int present;
    uint32_t index;

    char name[PARTITION_NAME_MAX];
    char parent_name[BLOCK_NAME_MAX];

    BLOCK_DEVICE *parent;
    uint32_t parent_index;
    uint32_t partition_number;

    uint32_t scheme;
    uint8_t type;
    uint8_t bootable;

    uint64_t lba_start;
    uint64_t sector_count;
    uint64_t total_bytes;
} PARTITION_INFO;

void partition_init(void);
int partition_is_initialized(void);

uint32_t partition_scan_all(void);

uint32_t partition_get_count(void);
PARTITION_INFO *partition_get(uint32_t index);
PARTITION_INFO *partition_find_by_name(const char *name);

int partition_read(PARTITION_INFO *part, uint64_t lba, uint32_t count, void *out_buf);
int partition_write(PARTITION_INFO *part, uint64_t lba, uint32_t count, const void *in_buf);

const char *partition_type_name(uint8_t type);

void partition_print_all(CONSOLE *con);
void partition_print_info(CONSOLE *con, const PARTITION_INFO *part);

#endif
