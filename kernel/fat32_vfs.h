#ifndef FAT32_VFS_H
#define FAT32_VFS_H

#include "vfs.h"
#include "partition.h"

/* Register FAT32 filesystem with VFS */
int fat32_vfs_register(void);

/* Mount a FAT32 partition into VFS */
int fat32_vfs_mount(PARTITION_INFO *part, const char *mount_point);

#endif
