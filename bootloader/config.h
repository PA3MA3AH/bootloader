#ifndef CONFIG_H
#define CONFIG_H

#include "efi.h"

#define MAX_ENTRIES 8

typedef enum {
    BOOT_ENTRY_TYPE_ELF = 1,
    BOOT_ENTRY_TYPE_LINUX_EFI = 2,
} BOOT_ENTRY_TYPE;

typedef struct {
    BOOT_ENTRY_TYPE type;
    CHAR16 name[64];
    CHAR16 kernel_path[128];
} BOOT_ENTRY;

typedef struct {
    UINTN entry_count;
    UINTN default_entry;
    UINTN timeout;
    BOOT_ENTRY entries[MAX_ENTRIES];
} BOOT_CONFIG;

EFI_STATUS load_config(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *st,
    BOOT_CONFIG *config
);

#endif
