#include "memory_map.h"

#define EFI_RESERVED_MEMORY_TYPE       0
#define EFI_LOADER_CODE                1
#define EFI_LOADER_DATA                2
#define EFI_BOOT_SERVICES_CODE         3
#define EFI_BOOT_SERVICES_DATA         4
#define EFI_RUNTIME_SERVICES_CODE      5
#define EFI_RUNTIME_SERVICES_DATA      6
#define EFI_CONVENTIONAL_MEMORY        7
#define EFI_UNUSABLE_MEMORY            8
#define EFI_ACPI_RECLAIM_MEMORY        9
#define EFI_ACPI_MEMORY_NVS           10
#define EFI_MEMORY_MAPPED_IO          11
#define EFI_MEMORY_MAPPED_IO_PORT     12
#define EFI_PAL_CODE                  13
#define EFI_PERSISTENT_MEMORY         14

#define PAGE_SIZE 4096ULL

typedef struct {
    uint32_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} KERNEL_EFI_MEMORY_DESCRIPTOR;

const char *memory_type_name(uint32_t type) {
    switch (type) {
        case EFI_RESERVED_MEMORY_TYPE:  return "Reserved";
        case EFI_LOADER_CODE:           return "LoaderCode";
        case EFI_LOADER_DATA:           return "LoaderData";
        case EFI_BOOT_SERVICES_CODE:    return "BootSrvCode";
        case EFI_BOOT_SERVICES_DATA:    return "BootSrvData";
        case EFI_RUNTIME_SERVICES_CODE: return "RtSrvCode";
        case EFI_RUNTIME_SERVICES_DATA: return "RtSrvData";
        case EFI_CONVENTIONAL_MEMORY:   return "Conventional";
        case EFI_UNUSABLE_MEMORY:       return "Unusable";
        case EFI_ACPI_RECLAIM_MEMORY:   return "ACPIReclaim";
        case EFI_ACPI_MEMORY_NVS:       return "ACPINVS";
        case EFI_MEMORY_MAPPED_IO:      return "MMIO";
        case EFI_MEMORY_MAPPED_IO_PORT: return "MMIOPort";
        case EFI_PAL_CODE:              return "PalCode";
        case EFI_PERSISTENT_MEMORY:     return "Persistent";
        default:                        return "Other";
    }
}

void dump_memory_map(CONSOLE *con, BOOT_INFO *boot_info) {
    if (!con || !boot_info || boot_info->memory_map == 0 || boot_info->memory_descriptor_size == 0) {
        console_printf(con, "Memory map unavailable.\n");
        return;
    }

    uint64_t entry_count = boot_info->memory_map_size / boot_info->memory_descriptor_size;
    uint64_t total_pages = 0;
    uint64_t total_bytes = 0;
    uint64_t usable_pages = 0;
    uint64_t usable_bytes = 0;

    console_printf(con, "=== Kernel memory map dump ===\n");
    console_printf(con, "Map base: %p\n", (void*)(uintptr_t)boot_info->memory_map);
    console_printf(con, "Map size: %u bytes\n", (unsigned int)boot_info->memory_map_size);
    console_printf(con, "Desc size: %u bytes\n", (unsigned int)boot_info->memory_descriptor_size);
    console_printf(con, "Entry count: %u\n\n", (unsigned int)entry_count);

    for (uint64_t i = 0; i < entry_count; i++) {
        uint8_t *base = (uint8_t*)(uintptr_t)boot_info->memory_map;
        KERNEL_EFI_MEMORY_DESCRIPTOR *desc =
            (KERNEL_EFI_MEMORY_DESCRIPTOR*)(base + i * boot_info->memory_descriptor_size);

        uint64_t pages = desc->number_of_pages;
        uint64_t bytes = pages * PAGE_SIZE;
        uint64_t end = desc->physical_start + bytes;

        total_pages += pages;
        total_bytes += bytes;

        if (desc->type == EFI_CONVENTIONAL_MEMORY) {
            usable_pages += pages;
            usable_bytes += bytes;
        }

        console_printf(con, "[%u] ", (unsigned int)i);
        console_printf(con, "%s ", memory_type_name(desc->type));
        console_printf(con, "Base=%p ", (void*)(uintptr_t)desc->physical_start);
        console_printf(con, "End=%p ", (void*)(uintptr_t)end);
        console_printf(con, "Pages=%u ", (unsigned int)pages);
        console_printf(con, "Bytes=%u\n", (unsigned int)bytes);
    }

    console_printf(con, "\n=== Kernel memory map totals ===\n");
    console_printf(con, "Total pages:  %u\n", (unsigned int)total_pages);
    console_printf(con, "Total bytes:  %u\n", (unsigned int)total_bytes);
    console_printf(con, "Usable pages: %u\n", (unsigned int)usable_pages);
    console_printf(con, "Usable bytes: %u\n", (unsigned int)usable_bytes);
}
