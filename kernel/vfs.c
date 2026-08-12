#include "vfs.h"
#include "kheap.h"
#include "panic.h"
#include <stddef.h>

static int vfs_initialized = 0;
static vfs_filesystem_t *registered_filesystems[VFS_MAX_FILESYSTEMS];
static uint32_t registered_fs_count = 0;
static vfs_filesystem_t *mounted_filesystems[VFS_MAX_FILESYSTEMS];
static uint32_t mounted_fs_count = 0;
static vfs_file_t open_files[VFS_MAX_OPEN_FILES];

/* Helper: string operations */
static int vfs_strlen(const char *s) {
    int len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

static int vfs_strcmp(const char *a, const char *b) {
    if (!a || !b) return (a == b) ? 0 : 1;
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

static void vfs_strcpy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;
    if (!dst || !src || max_len == 0) return;
    while (src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int vfs_strncmp(const char *a, const char *b, uint32_t n) {
    uint32_t i = 0;
    if (!a || !b) return (a == b) ? 0 : 1;
    while (i < n && a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    if (i == n) return 0;
    return a[i] - b[i];
}

/* Initialize VFS */
void vfs_init(void) {
    uint32_t i;
    
    for (i = 0; i < VFS_MAX_FILESYSTEMS; i++) {
        registered_filesystems[i] = NULL;
        mounted_filesystems[i] = NULL;
    }
    
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].node = NULL;
        open_files[i].flags = 0;
        open_files[i].offset = 0;
        open_files[i].in_use = 0;
    }
    
    registered_fs_count = 0;
    mounted_fs_count = 0;
    vfs_initialized = 1;
}

int vfs_is_initialized(void) {
    return vfs_initialized;
}

/* Register a filesystem type */
int vfs_register_filesystem(const char *name, vfs_fs_ops_t *ops) {
    vfs_filesystem_t *fs;
    
    if (!vfs_initialized || !name || !ops) {
        return 0;
    }
    
    if (registered_fs_count >= VFS_MAX_FILESYSTEMS) {
        return 0;
    }
    
    fs = (vfs_filesystem_t*)kmalloc(sizeof(vfs_filesystem_t));
    if (!fs) {
        return 0;
    }
    
    vfs_strcpy(fs->name, name, VFS_NAME_MAX);
    fs->ops = ops;
    fs->root = NULL;
    fs->private_data = NULL;
    fs->mounted = 0;
    fs->mount_point[0] = '\0';
    
    registered_filesystems[registered_fs_count++] = fs;
    return 1;
}

/* Mount a filesystem */
int vfs_mount(const char *fs_name, const char *mount_point, void *device) {
    vfs_filesystem_t *fs_template = NULL;
    vfs_filesystem_t *fs_instance;
    uint32_t i;
    
    if (!vfs_initialized || !fs_name || !mount_point) {
        return 0;
    }
    
    /* Find registered filesystem */
    for (i = 0; i < registered_fs_count; i++) {
        if (vfs_strcmp(registered_filesystems[i]->name, fs_name) == 0) {
            fs_template = registered_filesystems[i];
            break;
        }
    }
    
    if (!fs_template) {
        return 0;
    }
    
    if (mounted_fs_count >= VFS_MAX_FILESYSTEMS) {
        return 0;
    }
    
    /* Create mounted instance */
    fs_instance = (vfs_filesystem_t*)kmalloc(sizeof(vfs_filesystem_t));
    if (!fs_instance) {
        return 0;
    }
    
    vfs_strcpy(fs_instance->name, fs_name, VFS_NAME_MAX);
    vfs_strcpy(fs_instance->mount_point, mount_point, VFS_PATH_MAX);
    fs_instance->ops = fs_template->ops;
    fs_instance->private_data = NULL;
    fs_instance->root = NULL;
    fs_instance->mounted = 0;
    
    /* Call filesystem mount operation */
    if (fs_instance->ops->mount && fs_instance->ops->mount(fs_instance, device)) {
        if (fs_instance->ops->get_root) {
            fs_instance->root = fs_instance->ops->get_root(fs_instance);
        }
        fs_instance->mounted = 1;
        mounted_filesystems[mounted_fs_count++] = fs_instance;
        return 1;
    }
    
    kfree(fs_instance);
    return 0;
}

/* Unmount a filesystem */
int vfs_umount(const char *mount_point) {
    uint32_t i, j;
    vfs_filesystem_t *fs = NULL;
    
    if (!vfs_initialized || !mount_point) {
        return 0;
    }
    
    /* Find mounted filesystem */
    for (i = 0; i < mounted_fs_count; i++) {
        if (vfs_strcmp(mounted_filesystems[i]->mount_point, mount_point) == 0) {
            fs = mounted_filesystems[i];
            break;
        }
    }
    
    if (!fs) {
        return 0;
    }
    
    /* Call filesystem umount operation */
    if (fs->ops->umount) {
        fs->ops->umount(fs);
    }
    
    /* Remove from mounted list */
    for (j = i; j < mounted_fs_count - 1; j++) {
        mounted_filesystems[j] = mounted_filesystems[j + 1];
    }
    mounted_filesystems[mounted_fs_count - 1] = NULL;
    mounted_fs_count--;
    
    kfree(fs);
    return 1;
}

/* Resolve a path to a VFS node */
vfs_node_t* vfs_resolve_path(const char *path) {
    vfs_filesystem_t *fs = NULL;
    vfs_node_t *current = NULL;
    char component[VFS_NAME_MAX];
    uint32_t path_idx = 0;
    uint32_t comp_idx = 0;
    uint32_t i;
    int mount_len = 0;
    int best_match_len = 0;
    
    if (!vfs_initialized || !path || path[0] != '/') {
        return NULL;
    }
    
    /* Find best matching mount point */
    for (i = 0; i < mounted_fs_count; i++) {
        mount_len = vfs_strlen(mounted_filesystems[i]->mount_point);
        if (vfs_strncmp(path, mounted_filesystems[i]->mount_point, mount_len) == 0) {
            if (mount_len > best_match_len) {
                best_match_len = mount_len;
                fs = mounted_filesystems[i];
            }
        }
    }
    
    if (!fs || !fs->root) {
        return NULL;
    }
    
    current = fs->root;
    path_idx = best_match_len;
    
    /* Skip trailing slash of mount point */
    if (path[path_idx] == '/') {
        path_idx++;
    }
    
    /* If path equals mount point, return root */
    if (path[path_idx] == '\0') {
        return current;
    }
    
    /* Walk path components */
    while (path[path_idx] != '\0') {
        comp_idx = 0;
        
        /* Extract component */
        while (path[path_idx] != '\0' && path[path_idx] != '/' && comp_idx < VFS_NAME_MAX - 1) {
            component[comp_idx++] = path[path_idx++];
        }
        component[comp_idx] = '\0';
        
        /* Skip multiple slashes */
        while (path[path_idx] == '/') {
            path_idx++;
        }
        
        /* Skip empty components */
        if (comp_idx == 0) {
            continue;
        }
        
        /* Find child node */
        if (!current || !current->ops || !current->ops->finddir) {
            return NULL;
        }
        
        current = current->ops->finddir(current, component);
        if (!current) {
            return NULL;
        }
    }
    
    return current;
}

/* Allocate file descriptor */
static int vfs_alloc_fd(void) {
    uint32_t i;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) {
            return (int)i;
        }
    }
    return -1;
}

/* Open a file */
int vfs_open(const char *path, uint32_t flags) {
    vfs_node_t *node;
    int fd;
    
    if (!vfs_initialized || !path) {
        return -1;
    }
    
    node = vfs_resolve_path(path);
    if (!node) {
        return -1;
    }
    
    fd = vfs_alloc_fd();
    if (fd < 0) {
        return -1;
    }
    
    if (node->ops && node->ops->open) {
        if (!node->ops->open(node, flags)) {
            return -1;
        }
    }
    
    open_files[fd].node = node;
    open_files[fd].flags = flags;
    open_files[fd].offset = 0;
    open_files[fd].in_use = 1;
    
    return fd;
}

/* Close a file */
int vfs_close(int fd) {
    vfs_node_t *node;
    
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        return 0;
    }
    
    if (!open_files[fd].in_use) {
        return 0;
    }
    
    node = open_files[fd].node;
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }
    
    open_files[fd].node = NULL;
    open_files[fd].flags = 0;
    open_files[fd].offset = 0;
    open_files[fd].in_use = 0;
    
    return 1;
}

/* Read from file */
int64_t vfs_read(int fd, void *buf, uint64_t size) {
    vfs_node_t *node;
    int64_t result;
    
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_OPEN_FILES || !buf) {
        return -1;
    }
    
    if (!open_files[fd].in_use) {
        return -1;
    }
    
    node = open_files[fd].node;
    if (!node || !node->ops || !node->ops->read) {
        return -1;
    }
    
    result = node->ops->read(node, buf, open_files[fd].offset, size);
    if (result > 0) {
        open_files[fd].offset += result;
    }
    
    return result;
}

/* Write to file */
int64_t vfs_write(int fd, const void *buf, uint64_t size) {
    vfs_node_t *node;
    int64_t result;
    
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_OPEN_FILES || !buf) {
        return -1;
    }
    
    if (!open_files[fd].in_use) {
        return -1;
    }
    
    node = open_files[fd].node;
    if (!node || !node->ops || !node->ops->write) {
        return -1;
    }
    
    result = node->ops->write(node, buf, open_files[fd].offset, size);
    if (result > 0) {
        open_files[fd].offset += result;
    }
    
    return result;
}

/* Seek in file */
int64_t vfs_seek(int fd, int64_t offset, int whence) {
    vfs_node_t *node;
    int64_t new_offset;
    
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        return -1;
    }
    
    if (!open_files[fd].in_use) {
        return -1;
    }
    
    node = open_files[fd].node;
    if (!node) {
        return -1;
    }
    
    switch (whence) {
        case VFS_SEEK_SET:
            new_offset = offset;
            break;
        case VFS_SEEK_CUR:
            new_offset = (int64_t)open_files[fd].offset + offset;
            break;
        case VFS_SEEK_END:
            new_offset = (int64_t)node->size + offset;
            break;
        default:
            return -1;
    }
    
    if (new_offset < 0) {
        return -1;
    }
    
    open_files[fd].offset = (uint64_t)new_offset;
    return new_offset;
}

/* Get current file position */
int64_t vfs_tell(int fd) {
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        return -1;
    }
    
    if (!open_files[fd].in_use) {
        return -1;
    }
    
    return (int64_t)open_files[fd].offset;
}

/* Read directory entry */
int vfs_readdir(int fd, vfs_dirent_t *out) {
    vfs_node_t *node;
    uint32_t index;
    
    if (!vfs_initialized || fd < 0 || fd >= VFS_MAX_OPEN_FILES || !out) {
        return 0;
    }
    
    if (!open_files[fd].in_use) {
        return 0;
    }
    
    node = open_files[fd].node;
    if (!node || !node->ops || !node->ops->readdir) {
        return 0;
    }
    
    index = (uint32_t)open_files[fd].offset;
    if (node->ops->readdir(node, index, out)) {
        open_files[fd].offset++;
        return 1;
    }
    
    return 0;
}

/* Get file/directory status */
int vfs_stat(const char *path, vfs_stat_t *out) {
    vfs_node_t *node;
    
    if (!vfs_initialized || !path || !out) {
        return 0;
    }
    
    node = vfs_resolve_path(path);
    if (!node) {
        return 0;
    }
    
    if (node->ops && node->ops->stat) {
        return node->ops->stat(node, out);
    }
    
    /* Provide basic stat info */
    out->type = node->type;
    out->size = node->size;
    out->inode = node->inode;
    out->mode = 0644;
    out->atime = out->mtime = out->ctime = 0;
    
    return 1;
}

/* Create a file */
int vfs_create(const char *path, uint32_t mode) {
    /* TODO: Implement file creation by resolving parent and calling create op */
    (void)path;
    (void)mode;
    return 0;
}

/* Delete a file */
int vfs_unlink(const char *path) {
    /* TODO: Implement file deletion by resolving parent and calling unlink op */
    (void)path;
    return 0;
}

/* Create a directory */
int vfs_mkdir(const char *path, uint32_t mode) {
    /* TODO: Implement directory creation by resolving parent and calling mkdir op */
    (void)path;
    (void)mode;
    return 0;
}

/* Remove a directory */
int vfs_rmdir(const char *path) {
    /* TODO: Implement directory removal by resolving parent and calling rmdir op */
    (void)path;
    return 0;
}
