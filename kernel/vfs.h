#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX           256
#define VFS_PATH_MAX           1024
#define VFS_MAX_FILESYSTEMS    16
#define VFS_MAX_OPEN_FILES     256

#define VFS_TYPE_FILE          1
#define VFS_TYPE_DIR           2
#define VFS_TYPE_SYMLINK       3
#define VFS_TYPE_MOUNTPOINT    4

#define VFS_O_RDONLY           0x0001
#define VFS_O_WRONLY           0x0002
#define VFS_O_RDWR             0x0004
#define VFS_O_CREAT            0x0008
#define VFS_O_APPEND           0x0010
#define VFS_O_TRUNC            0x0020

#define VFS_SEEK_SET           0
#define VFS_SEEK_CUR           1
#define VFS_SEEK_END           2

typedef struct vfs_node vfs_node_t;
typedef struct vfs_filesystem vfs_filesystem_t;
typedef struct vfs_file vfs_file_t;

typedef struct {
    char name[VFS_NAME_MAX];
    uint32_t type;
    uint64_t size;
    uint32_t mode;
    uint64_t inode;
} vfs_dirent_t;

typedef struct {
    uint32_t type;
    uint64_t size;
    uint32_t mode;
    uint64_t inode;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
} vfs_stat_t;

/* Filesystem operations */
typedef struct {
    int (*mount)(vfs_filesystem_t *fs, void *device);
    int (*umount)(vfs_filesystem_t *fs);
    vfs_node_t* (*get_root)(vfs_filesystem_t *fs);
} vfs_fs_ops_t;

/* Node operations */
typedef struct {
    int (*open)(vfs_node_t *node, uint32_t flags);
    int (*close)(vfs_node_t *node);
    int64_t (*read)(vfs_node_t *node, void *buf, uint64_t offset, uint64_t size);
    int64_t (*write)(vfs_node_t *node, const void *buf, uint64_t offset, uint64_t size);
    vfs_node_t* (*finddir)(vfs_node_t *node, const char *name);
    int (*readdir)(vfs_node_t *node, uint32_t index, vfs_dirent_t *out);
    int (*stat)(vfs_node_t *node, vfs_stat_t *out);
    int (*create)(vfs_node_t *parent, const char *name, uint32_t mode);
    int (*unlink)(vfs_node_t *parent, const char *name);
    int (*mkdir)(vfs_node_t *parent, const char *name, uint32_t mode);
    int (*rmdir)(vfs_node_t *parent, const char *name);
} vfs_node_ops_t;

struct vfs_node {
    char name[VFS_NAME_MAX];
    uint32_t type;
    uint32_t flags;
    uint64_t size;
    uint64_t inode;
    
    vfs_filesystem_t *fs;
    vfs_node_ops_t *ops;
    void *private_data;
    
    uint32_t refcount;
};

struct vfs_filesystem {
    char name[VFS_NAME_MAX];
    char mount_point[VFS_PATH_MAX];
    
    vfs_fs_ops_t *ops;
    vfs_node_t *root;
    void *private_data;
    
    int mounted;
};

struct vfs_file {
    vfs_node_t *node;
    uint32_t flags;
    uint64_t offset;
    int in_use;
};

/* VFS core functions */
void vfs_init(void);
int vfs_is_initialized(void);

/* Filesystem registration */
int vfs_register_filesystem(const char *name, vfs_fs_ops_t *ops);
int vfs_mount(const char *fs_name, const char *mount_point, void *device);
int vfs_umount(const char *mount_point);

/* File operations */
int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);
int64_t vfs_read(int fd, void *buf, uint64_t size);
int64_t vfs_write(int fd, const void *buf, uint64_t size);
int64_t vfs_seek(int fd, int64_t offset, int whence);
int64_t vfs_tell(int fd);

/* Directory operations */
int vfs_readdir(int fd, vfs_dirent_t *out);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_rmdir(const char *path);

/* Path operations */
int vfs_stat(const char *path, vfs_stat_t *out);
int vfs_unlink(const char *path);
int vfs_create(const char *path, uint32_t mode);

/* Node resolution */
vfs_node_t* vfs_resolve_path(const char *path);

#endif
