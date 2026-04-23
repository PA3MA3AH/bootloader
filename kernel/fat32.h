#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "console.h"
#include "partition.h"

#define FAT32_NAME_MAX            64
#define FAT32_ATTR_READ_ONLY      0x01
#define FAT32_ATTR_HIDDEN         0x02
#define FAT32_ATTR_SYSTEM         0x04
#define FAT32_ATTR_VOLUME_ID      0x08
#define FAT32_ATTR_DIRECTORY      0x10
#define FAT32_ATTR_ARCHIVE        0x20
#define FAT32_ATTR_LFN            0x0F

#define FAT32_CLUSTER_FREE        0x00000000U
#define FAT32_CLUSTER_BAD         0x0FFFFFF7U
#define FAT32_CLUSTER_EOC         0x0FFFFFF8U
#define FAT32_CLUSTER_MASK        0x0FFFFFFFU

typedef struct {
    PARTITION_INFO *partition;

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t total_sectors;
    uint32_t root_cluster;

    uint32_t first_fat_sector;
    uint32_t first_data_sector;
    uint32_t data_sectors;
    uint32_t cluster_count;

    char oem_name[9];
    char volume_label[12];
    char fs_type[9];
} FAT32_FS;

typedef struct {
    char name[FAT32_NAME_MAX];
    uint8_t attr;
    uint32_t first_cluster;
    uint32_t size;
    int is_dir;
} FAT32_DIR_ENTRY;

int fat32_mount(PARTITION_INFO *part, FAT32_FS *out_fs);
void fat32_print_info(CONSOLE *con, const FAT32_FS *fs);

int fat32_list_directory(CONSOLE *con, PARTITION_INFO *part, const char *path);
int fat32_cat_file(CONSOLE *con, PARTITION_INFO *part, const char *path);
int fat32_stat_path(CONSOLE *con, PARTITION_INFO *part, const char *path);
int fat32_view_file(CONSOLE *con, PARTITION_INFO *part, const char *path, uint32_t page_lines);
int fat32_dump_file(CONSOLE *con, PARTITION_INFO *part, const char *path, uint32_t max_bytes);

#endif
