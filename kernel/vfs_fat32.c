#include "vfs.h"
#include "fat32.h"
#include "partition.h"
#include "kheap.h"
#include "panic.h"
#include <stddef.h>

/* FAT32 filesystem implementation for VFS */

typedef struct {
    PARTITION_INFO *partition;
    FAT32_FS fs;
    char device_name[32];
} fat32_fs_data_t;

typedef struct {
    FAT32_DIR_ENTRY entry;
    uint8_t *cached_data;
    uint32_t cached_size;
} fat32_node_data_t;

/* Forward declarations */
static int fat32_node_open(vfs_node_t *node, uint32_t flags);
static int fat32_node_close(vfs_node_t *node);
static int64_t fat32_node_read(vfs_node_t *node, void *buf, uint64_t offset, uint64_t size);
static int64_t fat32_node_write(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t size);
static vfs_node_t* fat32_node_finddir(vfs_node_t *node, const char *name);
static int fat32_node_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *out);
static int fat32_node_stat(vfs_node_t *node, vfs_stat_t *out);

static vfs_node_ops_t g_fat32_node_ops = {
    .open = fat32_node_open,
    .close = fat32_node_close,
    .read = fat32_node_read,
    .write = fat32_node_write,
    .finddir = fat32_node_finddir,
    .readdir = fat32_node_readdir,
    .stat = fat32_node_stat,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL
};

/* Node operations implementation */

static int fat32_node_open(vfs_node_t *node, uint32_t flags) {
    (void)node;
    (void)flags;
    /* Nothing special to do for FAT32 */
    return 0;
}

static int fat32_node_close(vfs_node_t *node) {
    fat32_node_data_t *data;
    
    if (!node || !node->private_data) {
        return -1;
    }
    
    data = (fat32_node_data_t*)node->private_data;
    
    if (data->cached_data) {
        kfree(data->cached_data);
        data->cached_data = NULL;
    }
    
    return 0;
}

static int64_t fat32_node_read(vfs_node_t *node, void *buf, uint64_t offset, uint64_t size) {
    fat32_node_data_t *node_data;
    fat32_fs_data_t *fs_data;
    uint8_t *file_buf;
    uint32_t file_size;
    char path[VFS_PATH_MAX];
    uint64_t to_read;
    
    if (!node || !buf || !node->private_data || !node->fs) {
        return -1;
    }
    
    node_data = (fat32_node_data_t*)node->private_data;
    fs_data = (fat32_fs_data_t*)node->fs->private_data;
    
    if (!fs_data) {
        return -1;
    }
    
    /* Check if already cached */
    if (!node_data->cached_data) {
        /* Build path - for now just use node name as path */
        path[0] = '/';
        {
            int i;
            for (i = 0; node->name[i] && i < VFS_PATH_MAX - 2; i++) {
                path[i + 1] = node->name[i];
            }
            path[i + 1] = '\0';
        }
        
        /* Read file into cache */
        if (!fat32_read_file(fs_data->partition, path, &file_buf, &file_size)) {
            return -1;
        }
        
        node_data->cached_data = file_buf;
        node_data->cached_size = file_size;
    }
    
    /* Check bounds */
    if (offset >= node_data->cached_size) {
        return 0;
    }
    
    to_read = size;
    if (offset + to_read > node_data->cached_size) {
        to_read = node_data->cached_size - offset;
    }
    
    /* Copy data */
    {
        uint64_t i;
        uint8_t *src = node_data->cached_data + offset;
        uint8_t *dst = (uint8_t*)buf;
        for (i = 0; i < to_read; i++) {
            dst[i] = src[i];
        }
    }
    
    return (int64_t)to_read;
}

static int64_t fat32_node_write(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t size) {
    (void)node;
    (void)buf;
    (void)offset;
    (void)size;
    /* Write not yet implemented */
    return -1;
}

static vfs_node_t* fat32_node_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    (void)name;
    /* Directory traversal not yet implemented */
    return NULL;
}

static int fat32_node_readdir(vfs_node_t *node, uint32_t index, vfs_dirent_t *out) {
    (void)node;
    (void)index;
    (void)out;
    /* Directory reading not yet implemented */
    return -1;
}

static int fat32_node_stat(vfs_node_t *node, vfs_stat_t *out) {
    fat32_node_data_t *data;
    
    if (!node || !out || !node->private_data) {
        return -1;
    }
    
    data = (fat32_node_data_t*)node->private_data;
    
    out->type = data->entry.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    out->size = data->entry.size;
    out->mode = 0644;
    out->inode = data->entry.first_cluster;
    out->atime = 0;
    out->mtime = 0;
    out->ctime = 0;
    
    return 0;
}

/* Filesystem operations */

static int fat32_vfs_mount(vfs_filesystem_t *fs, void *device) {
    PARTITION_INFO *part;
    fat32_fs_data_t *data;
    const char *dev_name = (const char*)device;
    
    if (!fs || !device) {
        return -1;
    }
    
    /* Find partition */
    part = partition_find_by_name(dev_name);
    if (!part) {
        return -1;
    }
    
    /* Allocate private data */
    data = (fat32_fs_data_t*)kmalloc(sizeof(fat32_fs_data_t));
    if (!data) {
        return -1;
    }
    
    /* Mount FAT32 */
    if (!fat32_mount(part, &data->fs)) {
        kfree(data);
        return -1;
    }
    
    data->partition = part;
    
    /* Copy device name */
    {
        int i;
        for (i = 0; dev_name[i] && i < 31; i++) {
            data->device_name[i] = dev_name[i];
        }
        data->device_name[i] = '\0';
    }
    
    fs->private_data = data;
    fs->mounted = 1;
    
    return 0;
}

static int fat32_umount(vfs_filesystem_t *fs) {
    if (!fs || !fs->private_data) {
        return -1;
    }
    
    kfree(fs->private_data);
    fs->private_data = NULL;
    fs->mounted = 0;
    
    return 0;
}

static vfs_node_t* fat32_get_root(vfs_filesystem_t *fs) {
    vfs_node_t *root;
    fat32_node_data_t *node_data;
    fat32_fs_data_t *fs_data;
    
    if (!fs || !fs->private_data) {
        return NULL;
    }
    
    fs_data = (fat32_fs_data_t*)fs->private_data;
    
    /* Allocate root node */
    root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) {
        return NULL;
    }
    
    /* Allocate node data */
    node_data = (fat32_node_data_t*)kmalloc(sizeof(fat32_node_data_t));
    if (!node_data) {
        kfree(root);
        return NULL;
    }
    
    /* Initialize node data */
    node_data->entry.name[0] = '/';
    node_data->entry.name[1] = '\0';
    node_data->entry.attr = FAT32_ATTR_DIRECTORY;
    node_data->entry.first_cluster = fs_data->fs.root_cluster;
    node_data->entry.size = 0;
    node_data->entry.is_dir = 1;
    node_data->cached_data = NULL;
    node_data->cached_size = 0;
    
    /* Initialize root node */
    root->name[0] = '/';
    root->name[1] = '\0';
    root->type = VFS_TYPE_DIR;
    root->flags = 0;
    root->size = 0;
    root->inode = fs_data->fs.root_cluster;
    root->fs = fs;
    root->ops = &g_fat32_node_ops;
    root->private_data = node_data;
    root->refcount = 1;
    
    return root;
}

static vfs_fs_ops_t g_fat32_fs_ops = {
    .mount = fat32_vfs_mount,
    .umount = fat32_umount,
    .get_root = fat32_get_root
};

/* Public API */

int vfs_fat32_register(void) {
    return vfs_register_filesystem("fat32", &g_fat32_fs_ops);
}
