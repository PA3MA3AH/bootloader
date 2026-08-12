#include "fat32_vfs.h"
#include "fat32.h"
#include "vfs.h"
#include "kheap.h"
#include "partition.h"
#include <stddef.h>

typedef struct {
    FAT32_FS fs;
    PARTITION_INFO *partition;
} fat32_vfs_private_t;

typedef struct {
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t dir_index;
} fat32_node_private_t;

/* Helper: copy string safely */
static void safe_strcpy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;
    if (!dst || !src || max_len == 0) return;
    while (src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Mount operation */
static int fat32_vfs_op_mount(vfs_filesystem_t *vfs_fs, void *device) {
    PARTITION_INFO *part = (PARTITION_INFO*)device;
    fat32_vfs_private_t *priv;
    
    if (!vfs_fs || !part) {
        return 0;
    }
    
    priv = (fat32_vfs_private_t*)kmalloc(sizeof(fat32_vfs_private_t));
    if (!priv) {
        return 0;
    }
    
    if (!fat32_mount(part, &priv->fs)) {
        kfree(priv);
        return 0;
    }
    
    priv->partition = part;
    vfs_fs->private_data = priv;
    
    return 1;
}

/* Unmount operation */
static int fat32_vfs_op_umount(vfs_filesystem_t *vfs_fs) {
    if (!vfs_fs || !vfs_fs->private_data) {
        return 0;
    }
    
    kfree(vfs_fs->private_data);
    vfs_fs->private_data = NULL;
    
    return 1;
}

/* Node open operation */
static int fat32_node_op_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    return 1;
}

/* Node close operation */
static int fat32_node_op_close(vfs_node_t *node) {
    (void)node;
    return 1;
}

/* Node read operation */
static int64_t fat32_node_op_read(vfs_node_t *node, void *buf, uint64_t offset, uint64_t size) {
    fat32_vfs_private_t *fs_priv;
    fat32_node_private_t *node_priv;
    uint8_t *full_buf = NULL;
    uint32_t file_size;
    uint64_t to_read;
    
    if (!node || !node->fs || !buf) {
        return -1;
    }
    
    fs_priv = (fat32_vfs_private_t*)node->fs->private_data;
    node_priv = (fat32_node_private_t*)node->private_data;
    
    if (!fs_priv || !node_priv) {
        return -1;
    }
    
    /* For now, read entire file and copy the requested portion */
    /* TODO: Optimize to read only needed clusters */
    FAT32_DIR_ENTRY entry;
    entry.first_cluster = node_priv->first_cluster;
    entry.size = (uint32_t)node->size;
    entry.is_dir = 0;
    
    if (offset >= node->size) {
        return 0;
    }
    
    to_read = size;
    if (offset + to_read > node->size) {
        to_read = node->size - offset;
    }
    
    /* Read entire file */
    uint8_t *file_buf = NULL;
    uint32_t read_size = 0;
    
    /* Use fat32 internal read function */
    uint8_t sector[512];
    uint32_t cluster = node_priv->first_cluster;
    uint32_t next_cluster;
    uint32_t remaining = (uint32_t)node->size;
    uint32_t bytes_read = 0;
    uint32_t s;
    
    file_size = (uint32_t)node->size;
    full_buf = (uint8_t*)kmalloc(file_size + 1);
    if (!full_buf) {
        return -1;
    }
    
    /* Read file cluster by cluster */
    while (remaining > 0 && cluster >= 2 && cluster < fs_priv->fs.cluster_count + 2) {
        uint32_t base_lba = fs_priv->fs.first_data_sector + 
                            ((cluster - 2) * fs_priv->fs.sectors_per_cluster);
        
        for (s = 0; s < fs_priv->fs.sectors_per_cluster && remaining > 0; s++) {
            uint32_t to_copy = (remaining > 512) ? 512 : remaining;
            
            if (!partition_read(fs_priv->partition, base_lba + s, 1, sector)) {
                kfree(full_buf);
                return -1;
            }
            
            /* Copy to buffer */
            uint32_t i;
            for (i = 0; i < to_copy; i++) {
                full_buf[bytes_read + i] = sector[i];
            }
            
            bytes_read += to_copy;
            remaining -= to_copy;
        }
        
        if (remaining == 0) {
            break;
        }
        
        /* Read FAT entry for next cluster */
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs_priv->fs.first_fat_sector + 
                              (fat_offset / fs_priv->fs.bytes_per_sector);
        uint32_t sector_offset = fat_offset % fs_priv->fs.bytes_per_sector;
        
        if (!partition_read(fs_priv->partition, fat_sector, 1, sector)) {
            kfree(full_buf);
            return -1;
        }
        
        next_cluster = ((uint32_t)sector[sector_offset] |
                       ((uint32_t)sector[sector_offset + 1] << 8) |
                       ((uint32_t)sector[sector_offset + 2] << 16) |
                       ((uint32_t)sector[sector_offset + 3] << 24)) & 0x0FFFFFFF;
        
        if (next_cluster >= 0x0FFFFFF8) {
            break;
        }
        
        if (next_cluster == cluster) {
            break;
        }
        
        cluster = next_cluster;
    }
    
    /* Copy requested portion */
    uint64_t i;
    for (i = 0; i < to_read; i++) {
        ((uint8_t*)buf)[i] = full_buf[offset + i];
    }
    
    kfree(full_buf);
    return (int64_t)to_read;
}

/* Node write operation */
static int64_t fat32_node_op_write(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t size) {
    /* TODO: Implement write support */
    (void)node;
    (void)buf;
    (void)offset;
    (void)size;
    return -1;
}

/* Find directory entry */
static vfs_node_t* fat32_node_op_finddir(vfs_node_t *node, const char *name) {
    fat32_vfs_private_t *fs_priv;
    fat32_node_private_t *node_priv;
    fat32_node_private_t *new_node_priv;
    vfs_node_t *new_node;
    uint8_t sector[512];
    uint32_t cluster;
    uint32_t next_cluster;
    uint32_t s, e;
    
    if (!node || !name || !node->fs) {
        return NULL;
    }
    
    fs_priv = (fat32_vfs_private_t*)node->fs->private_data;
    node_priv = (fat32_node_private_t*)node->private_data;
    
    if (!fs_priv || !node_priv) {
        return NULL;
    }
    
    cluster = node_priv->first_cluster;
    if (cluster == 0) {
        cluster = fs_priv->fs.root_cluster;
    }
    
    /* Search through directory */
    while (cluster >= 2 && cluster < fs_priv->fs.cluster_count + 2) {
        uint32_t base_lba = fs_priv->fs.first_data_sector + 
                            ((cluster - 2) * fs_priv->fs.sectors_per_cluster);
        
        for (s = 0; s < fs_priv->fs.sectors_per_cluster; s++) {
            if (!partition_read(fs_priv->partition, base_lba + s, 1, sector)) {
                return NULL;
            }
            
            for (e = 0; e < 512 / 32; e++) {
                uint8_t *raw = &sector[e * 32];
                
                if (raw[0] == 0x00) {
                    return NULL;
                }
                
                if (raw[0] == 0xE5) {
                    continue;
                }
                
                if (raw[11] == 0x0F) {
                    continue;
                }
                
                if (raw[11] & 0x08) {
                    continue;
                }
                
                /* Parse short name */
                char entry_name[64];
                uint32_t pos = 0;
                uint32_t i;
                
                for (i = 0; i < 8 && raw[i] != ' '; i++) {
                    entry_name[pos++] = (raw[i] >= 'A' && raw[i] <= 'Z') ? 
                                        (raw[i] + 32) : raw[i];
                }
                
                if (raw[8] != ' ') {
                    entry_name[pos++] = '.';
                    for (i = 8; i < 11 && raw[i] != ' '; i++) {
                        entry_name[pos++] = (raw[i] >= 'A' && raw[i] <= 'Z') ? 
                                            (raw[i] + 32) : raw[i];
                    }
                }
                
                entry_name[pos] = '\0';
                
                /* Compare names (case-insensitive) */
                uint32_t match = 1;
                for (i = 0; entry_name[i] && name[i]; i++) {
                    char c1 = entry_name[i];
                    char c2 = name[i];
                    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                    if (c1 != c2) {
                        match = 0;
                        break;
                    }
                }
                
                if (match && entry_name[i] == '\0' && name[i] == '\0') {
                    /* Found it */
                    uint32_t entry_cluster = ((uint32_t)raw[21] << 24) |
                                             ((uint32_t)raw[20] << 16) |
                                             ((uint32_t)raw[27] << 8) |
                                             ((uint32_t)raw[26]);
                    
                    uint32_t entry_size = ((uint32_t)raw[31] << 24) |
                                         ((uint32_t)raw[30] << 16) |
                                         ((uint32_t)raw[29] << 8) |
                                         ((uint32_t)raw[28]);
                    
                    uint8_t attr = raw[11];
                    
                    new_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
                    if (!new_node) {
                        return NULL;
                    }
                    
                    new_node_priv = (fat32_node_private_t*)kmalloc(sizeof(fat32_node_private_t));
                    if (!new_node_priv) {
                        kfree(new_node);
                        return NULL;
                    }
                    
                    safe_strcpy(new_node->name, entry_name, VFS_NAME_MAX);
                    new_node->type = (attr & 0x10) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                    new_node->flags = 0;
                    new_node->size = entry_size;
                    new_node->inode = entry_cluster;
                    new_node->fs = node->fs;
                    new_node->ops = node->ops;
                    new_node->refcount = 1;
                    
                    new_node_priv->first_cluster = entry_cluster;
                    new_node_priv->current_cluster = entry_cluster;
                    new_node_priv->dir_index = 0;
                    
                    new_node->private_data = new_node_priv;
                    
                    return new_node;
                }
            }
        }
        
        /* Read FAT entry for next cluster */
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs_priv->fs.first_fat_sector + 
                              (fat_offset / fs_priv->fs.bytes_per_sector);
        uint32_t sector_offset = fat_offset % fs_priv->fs.bytes_per_sector;
        
        if (!partition_read(fs_priv->partition, fat_sector, 1, sector)) {
            return NULL;
        }
        
        next_cluster = ((uint32_t)sector[sector_offset] |
                       ((uint32_t)sector[sector_offset + 1] << 8) |
                       ((uint32_t)sector[sector_offset + 2] << 16) |
                       ((uint32_t)sector[sector_offset + 3] << 24)) & 0x0FFFFFFF;
        
        if (next_cluster >= 0x0FFFFFF8) {
            break;
        }
        
        if (next_cluster == cluster) {
            return NULL;
        }
        
        cluster = next_cluster;
    }
    
    return NULL;
}

/* Read directory entry by index */
static int fat32_node_op_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *out) {
    fat32_vfs_private_t *fs_priv;
    fat32_node_private_t *node_priv;
    uint8_t sector[512];
    uint32_t cluster;
    uint32_t next_cluster;
    uint32_t s, e;
    uint32_t current_index = 0;
    
    if (!node || !out || !node->fs) {
        return 0;
    }
    
    fs_priv = (fat32_vfs_private_t*)node->fs->private_data;
    node_priv = (fat32_node_private_t*)node->private_data;
    
    if (!fs_priv || !node_priv) {
        return 0;
    }
    
    cluster = node_priv->first_cluster;
    if (cluster == 0) {
        cluster = fs_priv->fs.root_cluster;
    }
    
    /* Iterate through directory */
    while (cluster >= 2 && cluster < fs_priv->fs.cluster_count + 2) {
        uint32_t base_lba = fs_priv->fs.first_data_sector + 
                            ((cluster - 2) * fs_priv->fs.sectors_per_cluster);
        
        for (s = 0; s < fs_priv->fs.sectors_per_cluster; s++) {
            if (!partition_read(fs_priv->partition, base_lba + s, 1, sector)) {
                return 0;
            }
            
            for (e = 0; e < 512 / 32; e++) {
                uint8_t *raw = &sector[e * 32];
                
                if (raw[0] == 0x00) {
                    return 0;
                }
                
                if (raw[0] == 0xE5) {
                    continue;
                }
                
                if (raw[11] == 0x0F) {
                    continue;
                }
                
                if (raw[11] & 0x08) {
                    continue;
                }
                
                if (current_index == index) {
                    /* Parse and return this entry */
                    char entry_name[64];
                    uint32_t pos = 0;
                    uint32_t i;
                    
                    for (i = 0; i < 8 && raw[i] != ' '; i++) {
                        entry_name[pos++] = (raw[i] >= 'A' && raw[i] <= 'Z') ? 
                                            (raw[i] + 32) : raw[i];
                    }
                    
                    if (raw[8] != ' ') {
                        entry_name[pos++] = '.';
                        for (i = 8; i < 11 && raw[i] != ' '; i++) {
                            entry_name[pos++] = (raw[i] >= 'A' && raw[i] <= 'Z') ? 
                                                (raw[i] + 32) : raw[i];
                        }
                    }
                    
                    entry_name[pos] = '\0';
                    
                    uint32_t entry_cluster = ((uint32_t)raw[21] << 24) |
                                             ((uint32_t)raw[20] << 16) |
                                             ((uint32_t)raw[27] << 8) |
                                             ((uint32_t)raw[26]);
                    
                    uint32_t entry_size = ((uint32_t)raw[31] << 24) |
                                         ((uint32_t)raw[30] << 16) |
                                         ((uint32_t)raw[29] << 8) |
                                         ((uint32_t)raw[28]);
                    
                    uint8_t attr = raw[11];
                    
                    safe_strcpy(out->name, entry_name, VFS_NAME_MAX);
                    out->type = (attr & 0x10) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                    out->size = entry_size;
                    out->inode = entry_cluster;
                    out->mode = 0644;
                    
                    return 1;
                }
                
                current_index++;
            }
        }
        
        /* Read FAT entry for next cluster */
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs_priv->fs.first_fat_sector + 
                              (fat_offset / fs_priv->fs.bytes_per_sector);
        uint32_t sector_offset = fat_offset % fs_priv->fs.bytes_per_sector;
        
        if (!partition_read(fs_priv->partition, fat_sector, 1, sector)) {
            return 0;
        }
        
        next_cluster = ((uint32_t)sector[sector_offset] |
                       ((uint32_t)sector[sector_offset + 1] << 8) |
                       ((uint32_t)sector[sector_offset + 2] << 16) |
                       ((uint32_t)sector[sector_offset + 3] << 24)) & 0x0FFFFFFF;
        
        if (next_cluster >= 0x0FFFFFF8) {
            break;
        }
        
        if (next_cluster == cluster) {
            return 0;
        }
        
        cluster = next_cluster;
    }
    
    return 0;
}

/* Stat operation */
static int fat32_node_op_stat(vfs_node_t *node, vfs_stat_t *out) {
    if (!node || !out) {
        return 0;
    }
    
    out->type = node->type;
    out->size = node->size;
    out->inode = node->inode;
    out->mode = 0644;
    out->atime = out->mtime = out->ctime = 0;
    
    return 1;
}

static vfs_node_ops_t fat32_node_ops = {
    .open = fat32_node_op_open,
    .close = fat32_node_op_close,
    .read = fat32_node_op_read,
    .write = fat32_node_op_write,
    .finddir = fat32_node_op_finddir,
    .readdir = fat32_node_op_readdir,
    .stat = fat32_node_op_stat,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL
};

/* Get root node */
static vfs_node_t* fat32_vfs_op_get_root(vfs_filesystem_t *vfs_fs) {
    fat32_vfs_private_t *priv;
    vfs_node_t *root;
    fat32_node_private_t *root_priv;
    
    if (!vfs_fs || !vfs_fs->private_data) {
        return NULL;
    }
    
    priv = (fat32_vfs_private_t*)vfs_fs->private_data;
    
    root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) {
        return NULL;
    }
    
    root_priv = (fat32_node_private_t*)kmalloc(sizeof(fat32_node_private_t));
    if (!root_priv) {
        kfree(root);
        return NULL;
    }
    
    root->name[0] = '/';
    root->name[1] = '\0';
    root->type = VFS_TYPE_DIR;
    root->flags = 0;
    root->size = 0;
    root->inode = priv->fs.root_cluster;
    root->fs = vfs_fs;
    root->ops = &fat32_node_ops;
    root->refcount = 1;
    
    root_priv->first_cluster = priv->fs.root_cluster;
    root_priv->current_cluster = priv->fs.root_cluster;
    root_priv->dir_index = 0;
    
    root->private_data = root_priv;
    
    return root;
}

static vfs_fs_ops_t fat32_vfs_ops = {
    .mount = fat32_vfs_op_mount,
    .umount = fat32_vfs_op_umount,
    .get_root = fat32_vfs_op_get_root
};

/* Register FAT32 with VFS */
int fat32_vfs_register(void) {
    return vfs_register_filesystem("fat32", &fat32_vfs_ops);
}

/* Mount a FAT32 partition */
int fat32_vfs_mount(PARTITION_INFO *part, const char *mount_point) {
    if (!part || !mount_point) {
        return 0;
    }
    
    return vfs_mount("fat32", mount_point, part);
}
