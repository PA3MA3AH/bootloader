#include "partition.h"

#include <stdint.h>
#include <stddef.h>

#define MBR_SIGNATURE_OFFSET 510
#define MBR_PARTITION_OFFSET 446
#define MBR_PARTITION_COUNT  4
#define MBR_SIGNATURE        0xAA55U

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sector_count;
} MBR_PARTITION_ENTRY;

static PARTITION_INFO g_partitions[PARTITION_MAX_PARTITIONS];
static uint32_t g_partition_count = 0;
static int g_partition_initialized = 0;

static void part_mem_zero(void *dst, uint64_t size) {
    uint8_t *p = (uint8_t*)dst;
    while (size--) {
        *p++ = 0;
    }
}

static void part_mem_copy(void *dst, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (size--) {
        *d++ = *s++;
    }
}

static uint16_t part_read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void part_u32_to_dec(char *out, uint32_t value) {
    char tmp[16];
    uint32_t pos = 0;
    uint32_t i = 0;

    if (!out) {
        return;
    }

    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }

    while (value > 0 && pos < sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (pos > 0) {
        out[i++] = tmp[--pos];
    }

    out[i] = '\0';
}

static void part_str_copy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;

    if (!dst || max_len == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static int part_str_eq(const char *a, const char *b) {
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

static void part_build_name(char *out, uint32_t out_size, const char *base, uint32_t partition_number) {
    uint32_t i = 0;
    char num[16];
    uint32_t j = 0;

    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if (!base) {
        return;
    }

    while (base[i] && i + 1 < out_size) {
        out[i] = base[i];
        i++;
    }

    if (i + 2 >= out_size) {
        out[i] = '\0';
        return;
    }

    out[i++] = 'p';
    out[i] = '\0';

    part_u32_to_dec(num, partition_number);

    while (num[j] && i + 1 < out_size) {
        out[i++] = num[j++];
    }

    out[i] = '\0';
}

void partition_init(void) {
    part_mem_zero(g_partitions, sizeof(g_partitions));
    g_partition_count = 0;
    g_partition_initialized = 1;
}

int partition_is_initialized(void) {
    return g_partition_initialized;
}

static int partition_add_mbr(BLOCK_DEVICE *dev,
                             uint32_t partition_number,
                             const MBR_PARTITION_ENTRY *entry) {
    PARTITION_INFO *part;

    if (!dev || !entry) {
        return 0;
    }

    if (g_partition_count >= PARTITION_MAX_PARTITIONS) {
        return 0;
    }

    if (entry->type == 0 || entry->sector_count == 0) {
        return 1;
    }

    if ((uint64_t)entry->lba_first + (uint64_t)entry->sector_count > dev->total_sectors) {
        return 0;
    }

    part = &g_partitions[g_partition_count];
    part_mem_zero(part, sizeof(*part));

    part->present = 1;
    part->index = g_partition_count;
    part->parent = dev;
    part->parent_index = dev->index;
    part->partition_number = partition_number;
    part->scheme = PARTITION_SCHEME_MBR;
    part->type = entry->type;
    part->bootable = (entry->status == 0x80) ? 1 : 0;
    part->lba_start = (uint64_t)entry->lba_first;
    part->sector_count = (uint64_t)entry->sector_count;
    part->total_bytes = (uint64_t)entry->sector_count * (uint64_t)dev->sector_size;

    part_str_copy(part->parent_name, dev->name, sizeof(part->parent_name));
    part_build_name(part->name, sizeof(part->name), dev->name, partition_number);

    g_partition_count++;
    return 1;
}

static void partition_scan_device_mbr(BLOCK_DEVICE *dev) {
    uint8_t sector[512];
    const MBR_PARTITION_ENTRY *entries;
    uint16_t sig;
    uint32_t i;

    if (!dev || !dev->present) {
        return;
    }

    if (dev->sector_size != 512U) {
        return;
    }

    if (!block_read(dev, 0, 1, sector)) {
        return;
    }

    sig = part_read_u16_le(&sector[MBR_SIGNATURE_OFFSET]);
    if (sig != MBR_SIGNATURE) {
        return;
    }

    entries = (const MBR_PARTITION_ENTRY*)&sector[MBR_PARTITION_OFFSET];

    for (i = 0; i < MBR_PARTITION_COUNT; i++) {
        if (!partition_add_mbr(dev, i + 1, &entries[i])) {
            return;
        }
    }
}

uint32_t partition_scan_all(void) {
    uint32_t i;
    uint32_t dev_count;

    if (!g_partition_initialized) {
        partition_init();
    }

    part_mem_zero(g_partitions, sizeof(g_partitions));
    g_partition_count = 0;

    dev_count = block_get_count();

    for (i = 0; i < dev_count; i++) {
        BLOCK_DEVICE *dev = block_get_device(i);
        if (!dev) {
            continue;
        }

        if (dev->type != BLOCK_TYPE_DISK) {
            continue;
        }

        partition_scan_device_mbr(dev);
    }

    return g_partition_count;
}

uint32_t partition_get_count(void) {
    return g_partition_count;
}

PARTITION_INFO *partition_get(uint32_t index) {
    if (!g_partition_initialized) {
        return NULL;
    }

    if (index >= g_partition_count) {
        return NULL;
    }

    if (!g_partitions[index].present) {
        return NULL;
    }

    return &g_partitions[index];
}

PARTITION_INFO *partition_find_by_name(const char *name) {
    uint32_t i;

    if (!g_partition_initialized || !name || !*name) {
        return NULL;
    }

    for (i = 0; i < g_partition_count; i++) {
        if (g_partitions[i].present && part_str_eq(g_partitions[i].name, name)) {
            return &g_partitions[i];
        }
    }

    return NULL;
}

int partition_read(PARTITION_INFO *part, uint64_t lba, uint32_t count, void *out_buf) {
    if (!part || !part->present || !part->parent || !out_buf) {
        return 0;
    }

    if ((uint64_t)count > part->sector_count) {
        return 0;
    }

    if (lba > part->sector_count) {
        return 0;
    }

    if (lba + (uint64_t)count < lba) {
        return 0;
    }

    if (lba + (uint64_t)count > part->sector_count) {
        return 0;
    }

    return block_read(part->parent, part->lba_start + lba, count, out_buf);
}

int partition_write(PARTITION_INFO *part, uint64_t lba, uint32_t count, const void *in_buf) {
    if (!part || !part->present || !part->parent || !in_buf) {
        return 0;
    }

    if ((uint64_t)count > part->sector_count) {
        return 0;
    }

    if (lba > part->sector_count) {
        return 0;
    }

    if (lba + (uint64_t)count < lba) {
        return 0;
    }

    if (lba + (uint64_t)count > part->sector_count) {
        return 0;
    }

    return block_write(part->parent, part->lba_start + lba, count, in_buf);
}

const char *partition_type_name(uint8_t type) {
    switch (type) {
        case 0x01: return "FAT12";
        case 0x04: return "FAT16<32M";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT/HPFS";
        case 0x0B: return "FAT32 CHS";
        case 0x0C: return "FAT32 LBA";
        case 0x0E: return "FAT16 LBA";
        case 0x0F: return "Extended LBA";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        default:   return "Unknown";
    }
}

void partition_print_all(CONSOLE *con) {
    uint32_t i;

    if (!con) {
        return;
    }

    if (!g_partition_initialized) {
        console_printf(con, "partition: not initialized\n");
        return;
    }

    console_printf(con, "Partitions: %u\n", (unsigned int)g_partition_count);

    for (i = 0; i < g_partition_count; i++) {
        PARTITION_INFO *part = &g_partitions[i];

        if (!part->present) {
            continue;
        }

        console_printf(con,
                       "  [%u] %s  parent=%s  start=%u  sectors=%u  type=%x (%s)",
                       (unsigned int)part->index,
                       part->name,
                       part->parent_name,
                       (unsigned int)(part->lba_start & 0xFFFFFFFFULL),
                       (unsigned int)(part->sector_count & 0xFFFFFFFFULL),
                       (unsigned int)part->type,
                       partition_type_name(part->type));

        if (part->bootable) {
            console_printf(con, "  boot");
        }

        console_printf(con, "\n");
    }
}

void partition_print_info(CONSOLE *con, const PARTITION_INFO *part) {
    if (!con || !part || !part->present) {
        return;
    }

    console_printf(con, "partition %u:\n", (unsigned int)part->index);
    console_printf(con, "  name:            %s\n", part->name);
    console_printf(con, "  parent:          %s\n", part->parent_name);
    console_printf(con, "  parent index:    %u\n", (unsigned int)part->parent_index);
    console_printf(con, "  scheme:          %s\n", part->scheme == PARTITION_SCHEME_MBR ? "MBR" : "unknown");
    console_printf(con, "  part number:     %u\n", (unsigned int)part->partition_number);
    console_printf(con, "  type:            %x (%s)\n", (unsigned int)part->type, partition_type_name(part->type));
    console_printf(con, "  bootable:        %s\n", part->bootable ? "yes" : "no");
    console_printf(con, "  lba start:       %u\n", (unsigned int)(part->lba_start & 0xFFFFFFFFULL));
    console_printf(con, "  sector count:    %u\n", (unsigned int)(part->sector_count & 0xFFFFFFFFULL));
    console_printf(con, "  total bytes:     %u\n", (unsigned int)(part->total_bytes & 0xFFFFFFFFULL));
}
