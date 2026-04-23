#include "fat32.h"

#include <stddef.h>
#include <stdint.h>

#define FAT32_SECTOR_SIZE              512U
#define FAT32_DIRENT_SIZE              32U
#define FAT32_MAX_PATH_COMPONENTS      16U
#define FAT32_MAX_COMPONENT_LEN        16U
#define FAT32_MAX_FILE_BYTES_TO_PRINT  (128U * 1024U)

typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} FAT32_BPB;

typedef struct __attribute__((packed)) {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} FAT32_RAW_DIRENT;

static void fat_mem_zero(void *dst, uint64_t size) {
    uint8_t *p = (uint8_t*)dst;
    while (size--) {
        *p++ = 0;
    }
}

static void fat_mem_copy(void *dst, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (size--) {
        *d++ = *s++;
    }
}

static int fat_mem_eq(const void *a, const void *b, uint32_t size) {
    const uint8_t *pa = (const uint8_t*)a;
    const uint8_t *pb = (const uint8_t*)b;
    uint32_t i;
    for (i = 0; i < size; i++) {
        if (pa[i] != pb[i]) {
            return 0;
        }
    }
    return 1;
}

static int fat_str_eq(const char *a, const char *b) {
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

static void fat_copy_trim_ascii(char *dst, uint32_t dst_size, const uint8_t *src, uint32_t src_len) {
    uint32_t end = src_len;
    uint32_t i;
    uint32_t pos = 0;

    if (!dst || dst_size == 0) {
        return;
    }

    while (end > 0 && src[end - 1] == ' ') {
        end--;
    }

    for (i = 0; i < end && pos + 1 < dst_size; i++) {
        uint8_t c = src[i];
        dst[pos++] = (c >= 32 && c <= 126) ? (char)c : '.';
    }
    dst[pos] = '\0';
}

static uint16_t fat_read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t fat_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int fat_is_eoc(uint32_t cluster) {
    return (cluster & FAT32_CLUSTER_MASK) >= FAT32_CLUSTER_EOC;
}

static int fat_is_valid_data_cluster(const FAT32_FS *fs, uint32_t cluster) {
    if (!fs) {
        return 0;
    }
    return cluster >= 2U && cluster < (fs->cluster_count + 2U);
}

static uint32_t fat_cluster_to_lba(const FAT32_FS *fs, uint32_t cluster) {
    return fs->first_data_sector + ((cluster - 2U) * (uint32_t)fs->sectors_per_cluster);
}

static int fat_read_partition_sector(const FAT32_FS *fs, uint64_t lba, uint8_t *buf) {
    if (!fs || !buf) {
        return 0;
    }
    return partition_read(fs->partition, lba, 1, buf);
}

static int fat_read_fat_entry(const FAT32_FS *fs, uint32_t cluster, uint32_t *out_next) {
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t sector_offset;
    uint8_t sector[FAT32_SECTOR_SIZE];

    if (!fs || !out_next) {
        return 0;
    }

    fat_offset = cluster * 4U;
    fat_sector = fs->first_fat_sector + (fat_offset / (uint32_t)fs->bytes_per_sector);
    sector_offset = fat_offset % (uint32_t)fs->bytes_per_sector;

    if (!fat_read_partition_sector(fs, fat_sector, sector)) {
        return 0;
    }

    if (sector_offset + 4U > (uint32_t)fs->bytes_per_sector) {
        return 0;
    }

    *out_next = fat_read_u32_le(&sector[sector_offset]) & FAT32_CLUSTER_MASK;
    return 1;
}

static void fat_short_name_to_string(const uint8_t raw[11], char *out, uint32_t out_size) {
    char base[9];
    char ext[4];
    uint32_t pos = 0;
    uint32_t i;

    if (!out || out_size == 0) {
        return;
    }

    fat_copy_trim_ascii(base, sizeof(base), raw, 8);
    fat_copy_trim_ascii(ext, sizeof(ext), raw + 8, 3);

    out[0] = '\0';

    for (i = 0; base[i] && pos + 1 < out_size; i++) {
        out[pos++] = base[i];
    }

    if (ext[0] && pos + 1 < out_size) {
        out[pos++] = '.';
        for (i = 0; ext[i] && pos + 1 < out_size; i++) {
            out[pos++] = ext[i];
        }
    }

    out[pos] = '\0';
}

static int fat_component_to_short_name(const char *component, uint8_t out[11]) {
    uint32_t i = 0;
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    int seen_dot = 0;

    if (!component || !component[0]) {
        return 0;
    }

    for (i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    for (i = 0; component[i]; i++) {
        char c = component[i];

        if (c == '/') {
            return 0;
        }

        if (c == '.') {
            if (seen_dot) {
                return 0;
            }
            seen_dot = 1;
            continue;
        }

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }

        if (!((c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '$' || c == '~' || c == '-' )) {
            return 0;
        }

        if (!seen_dot) {
            if (base_len >= 8U) {
                return 0;
            }
            out[base_len++] = (uint8_t)c;
        } else {
            if (ext_len >= 3U) {
                return 0;
            }
            out[8U + ext_len++] = (uint8_t)c;
        }
    }

    return base_len > 0U;
}

static int fat_parse_path_components(const char *path,
                                     char components[FAT32_MAX_PATH_COMPONENTS][FAT32_MAX_COMPONENT_LEN],
                                     uint32_t *out_count) {
    uint32_t i = 0;
    uint32_t count = 0;
    uint32_t pos = 0;

    if (!path || !out_count) {
        return 0;
    }

    *out_count = 0;

    while (path[i] == ' ') {
        i++;
    }

    if (path[i] == '\0' || (path[i] == '/' && path[i + 1] == '\0')) {
        return 1;
    }

    if (path[i] == '/') {
        i++;
    }

    while (path[i]) {
        if (count >= FAT32_MAX_PATH_COMPONENTS) {
            return 0;
        }

        pos = 0;
        while (path[i] && path[i] != '/') {
            if (pos + 1 >= FAT32_MAX_COMPONENT_LEN) {
                return 0;
            }
            components[count][pos++] = path[i++];
        }
        components[count][pos] = '\0';

        if (pos == 0) {
            if (path[i] == '/') {
                i++;
                continue;
            }
            break;
        }

        count++;
        if (path[i] == '/') {
            i++;
        }
    }

    *out_count = count;
    return 1;
}

static int fat_fill_entry_info(const FAT32_RAW_DIRENT *raw, FAT32_DIR_ENTRY *out_entry) {
    uint32_t cluster;

    if (!raw || !out_entry) {
        return 0;
    }

    fat_mem_zero(out_entry, sizeof(*out_entry));
    fat_short_name_to_string(raw->name, out_entry->name, sizeof(out_entry->name));
    out_entry->attr = raw->attr;
    cluster = ((uint32_t)raw->first_cluster_hi << 16) | (uint32_t)raw->first_cluster_lo;
    out_entry->first_cluster = cluster;
    out_entry->size = raw->file_size;
    out_entry->is_dir = (raw->attr & FAT32_ATTR_DIRECTORY) ? 1 : 0;
    return 1;
}

static int fat_find_entry_in_directory(const FAT32_FS *fs,
                                       uint32_t dir_cluster,
                                       const char *component,
                                       FAT32_DIR_ENTRY *out_entry) {
    uint8_t target_name[11];
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t cluster = dir_cluster;
    uint32_t next_cluster;
    uint32_t s;
    uint32_t e;

    if (!fs || !component || !out_entry) {
        return 0;
    }

    if (!fat_component_to_short_name(component, target_name)) {
        return 0;
    }

    while (fat_is_valid_data_cluster(fs, cluster)) {
        uint32_t base_lba = fat_cluster_to_lba(fs, cluster);

        for (s = 0; s < fs->sectors_per_cluster; s++) {
            if (!fat_read_partition_sector(fs, base_lba + s, sector)) {
                return 0;
            }

            for (e = 0; e < FAT32_SECTOR_SIZE / FAT32_DIRENT_SIZE; e++) {
                FAT32_RAW_DIRENT *raw = (FAT32_RAW_DIRENT*)&sector[e * FAT32_DIRENT_SIZE];

                if (raw->name[0] == 0x00) {
                    return 0;
                }
                if (raw->name[0] == 0xE5) {
                    continue;
                }
                if (raw->attr == FAT32_ATTR_LFN) {
                    continue;
                }
                if (raw->attr & FAT32_ATTR_VOLUME_ID) {
                    continue;
                }
                if (fat_mem_eq(raw->name, target_name, 11)) {
                    return fat_fill_entry_info(raw, out_entry);
                }
            }
        }

        if (!fat_read_fat_entry(fs, cluster, &next_cluster)) {
            return 0;
        }
        if (fat_is_eoc(next_cluster)) {
            break;
        }
        if (next_cluster == cluster) {
            return 0;
        }
        cluster = next_cluster;
    }

    return 0;
}

static int fat_lookup_path(const FAT32_FS *fs, const char *path, FAT32_DIR_ENTRY *out_entry) {
    char components[FAT32_MAX_PATH_COMPONENTS][FAT32_MAX_COMPONENT_LEN];
    uint32_t component_count = 0;
    uint32_t i;
    FAT32_DIR_ENTRY current;
    uint32_t cluster = 0;

    if (!fs || !path || !out_entry) {
        return 0;
    }

    if (!fat_parse_path_components(path, components, &component_count)) {
        return 0;
    }

    if (component_count == 0) {
        fat_mem_zero(&current, sizeof(current));
        current.name[0] = '/';
        current.name[1] = '\0';
        current.attr = FAT32_ATTR_DIRECTORY;
        current.first_cluster = fs->root_cluster;
        current.is_dir = 1;
        *out_entry = current;
        return 1;
    }

    cluster = fs->root_cluster;
    for (i = 0; i < component_count; i++) {
        if (!fat_find_entry_in_directory(fs, cluster, components[i], &current)) {
            return 0;
        }

        if (i + 1 < component_count) {
            if (!current.is_dir) {
                return 0;
            }
            cluster = current.first_cluster ? current.first_cluster : fs->root_cluster;
        }
    }

    *out_entry = current;
    return 1;
}

int fat32_mount(PARTITION_INFO *part, FAT32_FS *out_fs) {
    uint8_t sector[FAT32_SECTOR_SIZE];
    FAT32_BPB *bpb;
    FAT32_FS fs;
    uint32_t total_sectors;
    uint32_t fat_size;

    if (!part || !out_fs || !part->present) {
        return 0;
    }

    if (!part->parent || part->parent->sector_size != FAT32_SECTOR_SIZE) {
        return 0;
    }

    if (!partition_read(part, 0, 1, sector)) {
        return 0;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return 0;
    }

    bpb = (FAT32_BPB*)sector;

    if (bpb->bytes_per_sector != FAT32_SECTOR_SIZE) {
        return 0;
    }
    if (bpb->sectors_per_cluster == 0 || bpb->num_fats == 0) {
        return 0;
    }
    if (bpb->fat_size_32 == 0 || bpb->root_cluster < 2U) {
        return 0;
    }

    fat_mem_zero(&fs, sizeof(fs));
    fs.partition = part;
    fs.bytes_per_sector = bpb->bytes_per_sector;
    fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fs.reserved_sector_count = bpb->reserved_sector_count;
    fs.num_fats = bpb->num_fats;
    fs.fat_size_sectors = bpb->fat_size_32;
    fs.root_cluster = bpb->root_cluster;

    total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    fat_size = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;

    fs.total_sectors = total_sectors;
    fs.first_fat_sector = bpb->reserved_sector_count;
    fs.first_data_sector = bpb->reserved_sector_count + ((uint32_t)bpb->num_fats * fat_size);
    fs.data_sectors = total_sectors - fs.first_data_sector;
    fs.cluster_count = fs.data_sectors / bpb->sectors_per_cluster;

    fat_copy_trim_ascii(fs.oem_name, sizeof(fs.oem_name), bpb->oem_name, 8);
    fat_copy_trim_ascii(fs.volume_label, sizeof(fs.volume_label), bpb->volume_label, 11);
    fat_copy_trim_ascii(fs.fs_type, sizeof(fs.fs_type), bpb->fs_type, 8);

    if (fs.cluster_count < 65525U) {
        return 0;
    }

    *out_fs = fs;
    return 1;
}

void fat32_print_info(CONSOLE *con, const FAT32_FS *fs) {
    if (!con || !fs || !fs->partition) {
        return;
    }

    console_printf(con, "FAT32 info (%s):\n", fs->partition->name);
    console_printf(con, "  parent:              %s\n", fs->partition->parent_name);
    console_printf(con, "  OEM:                 %s\n", fs->oem_name[0] ? fs->oem_name : "<none>");
    console_printf(con, "  volume label:        %s\n", fs->volume_label[0] ? fs->volume_label : "<none>");
    console_printf(con, "  fs type field:       %s\n", fs->fs_type[0] ? fs->fs_type : "<none>");
    console_printf(con, "  bytes/sector:        %u\n", (unsigned int)fs->bytes_per_sector);
    console_printf(con, "  sectors/cluster:     %u\n", (unsigned int)fs->sectors_per_cluster);
    console_printf(con, "  reserved sectors:    %u\n", (unsigned int)fs->reserved_sector_count);
    console_printf(con, "  FAT count:           %u\n", (unsigned int)fs->num_fats);
    console_printf(con, "  FAT size sectors:    %u\n", (unsigned int)fs->fat_size_sectors);
    console_printf(con, "  total sectors:       %u\n", (unsigned int)fs->total_sectors);
    console_printf(con, "  first FAT sector:    %u\n", (unsigned int)fs->first_fat_sector);
    console_printf(con, "  first data sector:   %u\n", (unsigned int)fs->first_data_sector);
    console_printf(con, "  root cluster:        %u\n", (unsigned int)fs->root_cluster);
    console_printf(con, "  cluster count:       %u\n", (unsigned int)fs->cluster_count);
}

int fat32_list_directory(CONSOLE *con, PARTITION_INFO *part, const char *path) {
    FAT32_FS fs;
    FAT32_DIR_ENTRY dir;
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t cluster;
    uint32_t next_cluster;
    uint32_t base_lba;
    uint32_t s;
    uint32_t e;
    uint32_t shown = 0;

    if (!con || !part || !path) {
        return 0;
    }

    if (!fat32_mount(part, &fs)) {
        return 0;
    }

    if (!fat_lookup_path(&fs, path, &dir)) {
        return 0;
    }

    if (!dir.is_dir) {
        return 0;
    }

    cluster = dir.first_cluster ? dir.first_cluster : fs.root_cluster;

    console_printf(con, "Directory listing for %s:%s\n", part->name, path);

    while (fat_is_valid_data_cluster(&fs, cluster)) {
        base_lba = fat_cluster_to_lba(&fs, cluster);

        for (s = 0; s < fs.sectors_per_cluster; s++) {
            if (!fat_read_partition_sector(&fs, base_lba + s, sector)) {
                return 0;
            }

            for (e = 0; e < FAT32_SECTOR_SIZE / FAT32_DIRENT_SIZE; e++) {
                FAT32_RAW_DIRENT *raw = (FAT32_RAW_DIRENT*)&sector[e * FAT32_DIRENT_SIZE];
                FAT32_DIR_ENTRY entry;

                if (raw->name[0] == 0x00) {
                    console_printf(con, "entries: %u\n", (unsigned int)shown);
                    return 1;
                }
                if (raw->name[0] == 0xE5) {
                    continue;
                }
                if (raw->attr == FAT32_ATTR_LFN) {
                    continue;
                }
                if (!fat_fill_entry_info(raw, &entry)) {
                    continue;
                }

                if (entry.attr & FAT32_ATTR_VOLUME_ID) {
                    continue;
                }

                console_printf(con, "  %s  %s  cluster=%u  size=%u\n",
                               entry.name,
                               entry.is_dir ? "<DIR>" : "<FILE>",
                               (unsigned int)entry.first_cluster,
                               (unsigned int)entry.size);
                shown++;
            }
        }

        if (!fat_read_fat_entry(&fs, cluster, &next_cluster)) {
            return 0;
        }
        if (fat_is_eoc(next_cluster)) {
            break;
        }
        if (next_cluster == cluster) {
            return 0;
        }
        cluster = next_cluster;
    }

    console_printf(con, "entries: %u\n", (unsigned int)shown);
    return 1;
}

int fat32_cat_file(CONSOLE *con, PARTITION_INFO *part, const char *path) {
    FAT32_FS fs;
    FAT32_DIR_ENTRY file;
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t cluster;
    uint32_t next_cluster;
    uint32_t base_lba;
    uint32_t s;
    uint32_t remaining;
    uint32_t printed = 0;

    if (!con || !part || !path) {
        return 0;
    }

    if (!fat32_mount(part, &fs)) {
        return 0;
    }

    if (!fat_lookup_path(&fs, path, &file)) {
        return 0;
    }

    if (file.is_dir) {
        return 0;
    }

    console_printf(con, "File %s:%s (%u bytes)\n",
                   part->name,
                   path,
                   (unsigned int)file.size);

    if (file.size == 0) {
        return 1;
    }

    cluster = file.first_cluster;
    remaining = file.size;

    while (remaining > 0 && fat_is_valid_data_cluster(&fs, cluster)) {
        base_lba = fat_cluster_to_lba(&fs, cluster);

        for (s = 0; s < fs.sectors_per_cluster && remaining > 0; s++) {
            uint32_t i;
            uint32_t to_print;

            if (!fat_read_partition_sector(&fs, base_lba + s, sector)) {
                return 0;
            }

            to_print = remaining;
            if (to_print > FAT32_SECTOR_SIZE) {
                to_print = FAT32_SECTOR_SIZE;
            }

            for (i = 0; i < to_print; i++) {
                uint8_t c = sector[i];
                console_putchar(con, (c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t' ? (char)c : '.');
            }

            remaining -= to_print;
            printed += to_print;

            if (printed >= FAT32_MAX_FILE_BYTES_TO_PRINT && remaining > 0) {
                console_printf(con, "\n[fatcat] output truncated at %u bytes\n",
                               (unsigned int)printed);
                return 1;
            }
        }

        if (remaining == 0) {
            break;
        }

        if (!fat_read_fat_entry(&fs, cluster, &next_cluster)) {
            return 0;
        }
        if (fat_is_eoc(next_cluster)) {
            break;
        }
        if (next_cluster == cluster) {
            return 0;
        }
        cluster = next_cluster;
    }

    if (printed > 0) {
        console_printf(con, "\n");
    }

    return remaining == 0;
}
