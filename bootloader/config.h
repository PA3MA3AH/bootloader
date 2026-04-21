#ifndef CONFIG_H
#define CONFIG_H

#include "efi.h"

#define MAX_ENTRIES 8

typedef struct {
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
