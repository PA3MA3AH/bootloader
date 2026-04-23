#include "fat32.h"
#include "kheap.h"
#include "keyboard.h"

#include <stddef.h>
#include <stdint.h>

#define FAT32_SECTOR_SIZE              512U
#define FAT32_DIRENT_SIZE              32U
#define FAT32_MAX_PATH_COMPONENTS      16U
#define FAT32_MAX_COMPONENT_LEN        16U
#define FAT32_MAX_FILE_BYTES_TO_PRINT  (8U * 1024U)
#define FAT32_TEXT_PREVIEW_BYTES       (256U)
#define FAT32_VIEW_PAGE_LINES          (20U)
#define FAT32_VIEW_LINE_NUMBER_WIDTH   4
#define FAT32_DUMP_DEFAULT_BYTES       (256U)

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


static int fat_is_printable_text_byte(uint8_t c) {
    return (c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t';
}

static int fat_buffer_looks_text(const uint8_t *buf, uint32_t size) {
    uint32_t sample;
    uint32_t bad = 0;
    uint32_t i;

    if (!buf || size == 0) {
        return 1;
    }

    sample = size;
    if (sample > FAT32_TEXT_PREVIEW_BYTES) {
        sample = FAT32_TEXT_PREVIEW_BYTES;
    }

    for (i = 0; i < sample; i++) {
        if (!fat_is_printable_text_byte(buf[i])) {
            bad++;
        }
    }

    return bad <= (sample / 8U + 1U);
}

static void fat_print_attr_flags(CONSOLE *con, uint8_t attr) {
    if (attr & FAT32_ATTR_DIRECTORY) console_write(con, "DIR ");
    if (attr & FAT32_ATTR_READ_ONLY) console_write(con, "RO ");
    if (attr & FAT32_ATTR_HIDDEN)    console_write(con, "HID ");
    if (attr & FAT32_ATTR_SYSTEM)    console_write(con, "SYS ");
    if (attr & FAT32_ATTR_ARCHIVE)   console_write(con, "ARC ");
}

static int fat_read_file_into_buffer(const FAT32_FS *fs,
                                     const FAT32_DIR_ENTRY *file,
                                     uint8_t **out_buf,
                                     uint32_t *out_size) {
    uint8_t *buf;
    uint8_t sector[FAT32_SECTOR_SIZE];
    uint32_t cluster;
    uint32_t next_cluster;
    uint32_t remaining;
    uint32_t written = 0;
    uint32_t s;

    if (!fs || !file || !out_buf || !out_size || file->is_dir) {
        return 0;
    }

    *out_buf = 0;
    *out_size = 0;

    if (file->size == 0) {
        buf = (uint8_t*)kmalloc(1);
        if (!buf) {
            return 0;
        }
        buf[0] = 0;
        *out_buf = buf;
        *out_size = 0;
        return 1;
    }

    buf = (uint8_t*)kmalloc(file->size + 1U);
    if (!buf) {
        return 0;
    }

    cluster = file->first_cluster;
    remaining = file->size;

    while (remaining > 0 && fat_is_valid_data_cluster(fs, cluster)) {
        uint32_t base_lba = fat_cluster_to_lba(fs, cluster);

        for (s = 0; s < fs->sectors_per_cluster && remaining > 0; s++) {
            uint32_t to_copy = remaining;
            if (to_copy > FAT32_SECTOR_SIZE) {
                to_copy = FAT32_SECTOR_SIZE;
            }

            if (!fat_read_partition_sector(fs, base_lba + s, sector)) {
                kfree(buf);
                return 0;
            }

            fat_mem_copy(buf + written, sector, to_copy);
            written += to_copy;
            remaining -= to_copy;
        }

        if (remaining == 0) {
            break;
        }

        if (!fat_read_fat_entry(fs, cluster, &next_cluster)) {
            kfree(buf);
            return 0;
        }
        if (fat_is_eoc(next_cluster) || next_cluster == cluster) {
            break;
        }
        cluster = next_cluster;
    }

    if (written < file->size) {
        kfree(buf);
        return 0;
    }

    buf[file->size] = 0;
    *out_buf = buf;
    *out_size = file->size;
    return 1;
}

static void fat_print_hex_dump(CONSOLE *con, const uint8_t *buf, uint32_t bytes) {
    uint32_t i;
    uint32_t j;

    for (i = 0; i < bytes; i += 16U) {
        console_printf(con, "%08x  ", (unsigned int)i);

        for (j = 0; j < 16U; j++) {
            if (i + j < bytes) {
                uint8_t b = buf[i + j];
                console_printf(con, "%c%c ",
                               "0123456789ABCDEF"[b >> 4],
                               "0123456789ABCDEF"[b & 0x0F]);
            } else {
                console_write(con, "   ");
            }
        }

        console_write(con, " |");
        for (j = 0; j < 16U && (i + j) < bytes; j++) {
            uint8_t c = buf[i + j];
            console_putchar(con, fat_is_printable_text_byte(c) && c != '\n' && c != '\r' ? (char)c : '.');
        }
        console_write(con, "|\n");
    }
}

static void fat_wait_viewer_key(CONSOLE *con) {
    console_write(con, "\n[Space/Enter: next page, Q/Esc: quit]");
}

static uint32_t fat_view_print_page(CONSOLE *con,
                                    const uint8_t *buf,
                                    uint32_t size,
                                    uint32_t *offset_io,
                                    uint32_t *line_no_io,
                                    uint32_t max_lines) {
    uint32_t lines_left = max_lines;
    uint32_t text_width;
    uint32_t offset = *offset_io;
    uint32_t line_no = *line_no_io;

    if (con->cols > (FAT32_VIEW_LINE_NUMBER_WIDTH + 3U)) {
        text_width = con->cols - FAT32_VIEW_LINE_NUMBER_WIDTH - 3U;
    } else {
        text_width = 40U;
    }

    while (offset < size && lines_left > 0) {
        uint32_t written = 0;

        line_no++;
        
        {
            uint32_t tmp = line_no;
            uint32_t digits = 1;
        
            while (tmp >= 10) {
                tmp /= 10;
                digits++;
            }
        
            while (digits < FAT32_VIEW_LINE_NUMBER_WIDTH) {
                console_putchar(con, ' ');
                digits++;
            }
        }
        
        console_printf(con, "%u | ", (unsigned int)line_no);

        while (offset < size && written < text_width) {
            uint8_t c = buf[offset++];

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                break;
            }

            console_putchar(con, fat_is_printable_text_byte(c) ? (char)c : '.');
            written++;
        }

        console_putchar(con, '\n');

        if (offset < size && written >= text_width) {
            while (offset < size && buf[offset] != '\n') {
                offset++;
            }
        }

        if (offset < size && buf[offset] == '\n') {
            offset++;
        }

        lines_left--;
    }

    *offset_io = offset;
    *line_no_io = line_no;
    return offset < size;
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
    uint8_t *buf;
    uint32_t size;
    uint32_t i;
    uint32_t print_bytes;

    if (!con || !part || !path) {
        return 0;
    }

    if (!fat32_mount(part, &fs)) {
        return 0;
    }

    if (!fat_lookup_path(&fs, path, &file) || file.is_dir) {
        return 0;
    }

    if (!fat_read_file_into_buffer(&fs, &file, &buf, &size)) {
        return 0;
    }

    console_printf(con, "File %s:%s (%u bytes)\n",
                   part->name,
                   path,
                   (unsigned int)size);

    if (size == 0) {
        kfree(buf);
        return 1;
    }

    if (!fat_buffer_looks_text(buf, size)) {
        console_printf(con,
                       "[fatcat] binary-looking file; use fatdump %s %s\n",
                       part->name,
                       path);
        kfree(buf);
        return 1;
    }

    print_bytes = size;
    if (print_bytes > FAT32_MAX_FILE_BYTES_TO_PRINT) {
        print_bytes = FAT32_MAX_FILE_BYTES_TO_PRINT;
    }

    for (i = 0; i < print_bytes; i++) {
        uint8_t c = buf[i];
        console_putchar(con, fat_is_printable_text_byte(c) ? (char)c : '.');
    }

    if (print_bytes < size) {
        console_printf(con,
                       "\n[fatcat] truncated at %u bytes; use fatview %s %s\n",
                       (unsigned int)print_bytes,
                       part->name,
                       path);
    } else if (print_bytes > 0 && buf[print_bytes - 1] != '\n') {
        console_putchar(con, '\n');
    }

    kfree(buf);
    return 1;
}

int fat32_stat_path(CONSOLE *con, PARTITION_INFO *part, const char *path) {
    FAT32_FS fs;
    FAT32_DIR_ENTRY entry;

    if (!con || !part || !path) {
        return 0;
    }

    if (!fat32_mount(part, &fs)) {
        return 0;
    }

    if (!fat_lookup_path(&fs, path, &entry)) {
        return 0;
    }

    console_printf(con, "Path:         %s:%s\n", part->name, path);
    console_printf(con, "Type:         %s\n", entry.is_dir ? "directory" : "file");
    console_printf(con, "Name:         %s\n", entry.name);
    console_printf(con, "First cluster:%u\n", (unsigned int)entry.first_cluster);
    console_printf(con, "Size:         %u bytes\n", (unsigned int)entry.size);
    console_printf(con, "Attrs:        0x%x ", (unsigned int)entry.attr);
    fat_print_attr_flags(con, entry.attr);
    console_putchar(con, '\n');
    return 1;
}

int fat32_view_file(CONSOLE *con, PARTITION_INFO *part, const char *path, uint32_t page_lines) {
    FAT32_FS fs;
    FAT32_DIR_ENTRY file;
    uint8_t *buf;
    uint32_t size;
    uint32_t offset = 0;
    uint32_t line_no = 0;

    if (!con || !part || !path) {
        return 0;
    }

    if (!fat32_mount(part, &fs)) {
        return 0;
    }

    if (!fat_lookup_path(&fs, path, &file) || file.is_dir) {
        return 0;
    }

    if (!fat_read_file_into_buffer(&fs, &file, &buf, &size)) {
        return 0;
    }

    console_printf(con,
                   "View %s:%s (%u bytes)%s\n",
                   part->name,
                   path,
                   (unsigned int)size,
                   fat_buffer_looks_text(buf, size) ? "" : " [binary shown as .]");

    if (page_lines == 0) {
        while (offset < size) {
            fat_view_print_page(con, buf, size, &offset, &line_no, 1024U);
        }
        kfree(buf);
        return 1;
    }

    while (offset < size) {
        fat_view_print_page(con, buf, size, &offset, &line_no, page_lines);
    
        if (offset >= size) {
            break;
        }
    
        fat_wait_viewer_key(con);
        for (;;) {
            char ch = keyboard_getchar();
    
            if (ch == 'q' || ch == 'Q' || (unsigned char)ch == 27) {
                console_putchar(con, '\n');
                kfree(buf);
                return 1;
            }
    
            if (ch == ' ' || ch == '\n' || ch == '\r') {
                console_putchar(con, '\n');
                break;
            }
        }
    }
    
    kfree(buf);
    return 1;
}

int fat32_dump_file(CONSOLE *con, PARTITION_INFO *part, const char *path, uint32_t max_bytes) {
    FAT32_FS fs;
    FAT32_DIR_ENTRY file;
    uint8_t *buf;
    uint32_t size;

    if (!con || !part || !path) {
        return 0;
    }

    if (!fat32_mount(part, &fs)) {
        return 0;
    }

    if (!fat_lookup_path(&fs, path, &file) || file.is_dir) {
        return 0;
    }

    if (!fat_read_file_into_buffer(&fs, &file, &buf, &size)) {
        return 0;
    }

    if (max_bytes == 0 || max_bytes > FAT32_DUMP_DEFAULT_BYTES) {
        max_bytes = FAT32_DUMP_DEFAULT_BYTES;
    }

    if (size < max_bytes) {
        max_bytes = size;
    }

    console_printf(con, "Dump %s:%s (%u/%u bytes)\n",
                   part->name,
                   path,
                   (unsigned int)max_bytes,
                   (unsigned int)size);
    fat_print_hex_dump(con, buf, max_bytes);

    if (max_bytes < size) {
        console_printf(con, "[fatdump] truncated at %u bytes\n", (unsigned int)max_bytes);
    }

    kfree(buf);
    return 1;
}
