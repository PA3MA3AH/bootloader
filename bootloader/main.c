#include "efi.h"
#include "config.h"
#include "menu.h"
#include "elf.h"
#include "../common/bootinfo.h"
#include "../common/crashinfo.h"

#define PAGE_SIZE 4096ULL
#define SECTOR_SIZE 512ULL
#define KiB (1024ULL)
#define MiB (1024ULL * 1024ULL)

#define KERNEL_RESERVED_SIZE    (2ULL * MiB)
#define BOOTINFO_RESERVED_SIZE  (64ULL * KiB)
#define SCRATCH_RESERVED_SIZE   (256ULL * KiB)
#define STACK_RESERVED_SIZE     (64ULL * KiB)
#define CRASHINFO_RESERVED_SIZE (4ULL * KiB)

typedef struct {
    UINT64 usable_regions;
    UINT64 total_usable_bytes;
    EFI_PHYSICAL_ADDRESS chosen_region_base;
    UINT64 chosen_region_size;
    EFI_PHYSICAL_ADDRESS kernel_phys_base;
    UINT64 kernel_reserved_size;
    EFI_PHYSICAL_ADDRESS bootinfo_phys;
    UINT64 bootinfo_reserved_size;
    EFI_PHYSICAL_ADDRESS scratch_phys;
    UINT64 scratch_reserved_size;
    EFI_PHYSICAL_ADDRESS crash_info_phys;
    UINT64 crash_info_reserved_size;
    EFI_PHYSICAL_ADDRESS kernel_stack_bottom;
    EFI_PHYSICAL_ADDRESS kernel_stack_top;
    EFI_PHYSICAL_ADDRESS future_heap_base;
    UINT64 future_heap_size;
} MEMORY_LAYOUT_PLAN;

static void print(EFI_SYSTEM_TABLE *st, CHAR16 *str) {
    st->ConOut->OutputString(st->ConOut, str);
}

__attribute__((noreturn))
static void halt_forever(void) {
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

static void memzero(VOID *ptr, UINTN size) {
    UINT8 *p = (UINT8*)ptr;
    for (UINTN i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static UINT64 align_up(UINT64 value, UINT64 alignment) {
    if (alignment == 0) {
        return value;
    }

    UINT64 remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }

    return value + (alignment - remainder);
}

static UINT64 align_down(UINT64 value, UINT64 alignment) {
    if (alignment == 0) {
        return value;
    }

    return value - (value % alignment);
}

static UINTN pages_for_size(UINT64 size) {
    return (UINTN)((size + PAGE_SIZE - 1) / PAGE_SIZE);
}

static UINT64 bytes_to_sectors(UINT64 bytes) {
    return bytes / SECTOR_SIZE;
}

static void u64_to_hex(UINT64 value, CHAR16 *buffer) {
    const CHAR16 *hex = L"0123456789ABCDEF";

    buffer[0] = L'0';
    buffer[1] = L'x';

    for (int i = 0; i < 16; i++) {
        UINTN shift = (UINTN)(15 - i) * 4;
        buffer[2 + i] = hex[(value >> shift) & 0xF];
    }

    buffer[18] = L'\0';
}

static void print_hex(EFI_SYSTEM_TABLE *st, UINT64 value) {
    CHAR16 buf[19];
    u64_to_hex(value, buf);
    print(st, buf);
}

static void u64_to_dec(UINT64 value, CHAR16 *buffer) {
    CHAR16 temp[32];
    UINTN i = 0;
    UINTN j = 0;

    if (value == 0) {
        buffer[0] = L'0';
        buffer[1] = L'\0';
        return;
    }

    while (value > 0) {
        temp[i++] = (CHAR16)(L'0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        buffer[j++] = temp[--i];
    }

    buffer[j] = L'\0';
}

static void print_dec(EFI_SYSTEM_TABLE *st, UINT64 value) {
    CHAR16 buf[32];
    u64_to_dec(value, buf);
    print(st, buf);
}

static void print_spaces(EFI_SYSTEM_TABLE *st, int count) {
    for (int i = 0; i < count; i++) {
        print(st, L" ");
    }
}

static int u64_digits(UINT64 v) {
    int d = 1;
    while (v >= 10) {
        v /= 10;
        d++;
    }
    return d;
}

static int str16_len(CHAR16 *s) {
    int len = 0;
    while (s[len] != 0) {
        len++;
    }
    return len;
}

static UINTN ascii_len(const char *s) {
    UINTN len = 0;
    if (!s) {
        return 0;
    }

    while (s[len]) {
        len++;
    }

    return len;
}

static void print_ascii(EFI_SYSTEM_TABLE *st, const char *s) {
    CHAR16 buf[256];
    UINTN i = 0;

    if (!s) {
        return;
    }

    while (s[i] && i < 255) {
        buf[i] = (CHAR16)(UINT8)s[i];
        i++;
    }

    buf[i] = 0;
    print(st, buf);
}

static CHAR16 *memory_type_name(UINT32 type) {
    switch (type) {
        case EfiReservedMemoryType:      return L"Reserved";
        case EfiLoaderCode:              return L"LoaderCode";
        case EfiLoaderData:              return L"LoaderData";
        case EfiBootServicesCode:        return L"BootSrvCode";
        case EfiBootServicesData:        return L"BootSrvData";
        case EfiRuntimeServicesCode:     return L"RtSrvCode";
        case EfiRuntimeServicesData:     return L"RtSrvData";
        case EfiConventionalMemory:      return L"Conventional";
        case EfiUnusableMemory:          return L"Unusable";
        case EfiACPIReclaimMemory:       return L"ACPIReclaim";
        case EfiACPIMemoryNVS:           return L"ACPINVS";
        case EfiMemoryMappedIO:          return L"MMIO";
        case EfiMemoryMappedIOPortSpace: return L"MMIOPort";
        case EfiPalCode:                 return L"PalCode";
        case EfiPersistentMemory:        return L"Persistent";
        default:                         return L"Other";
    }
}

static CHAR16 *crash_status_name(UINT64 status) {
    switch (status) {
        case CRASH_STATUS_BOOTING:      return L"booting";
        case CRASH_STATUS_ACTION_ARMED: return L"armed action";
        case CRASH_STATUS_PANIC:        return L"panic";
        case CRASH_STATUS_STABLE:       return L"stable";
        default:                        return L"clear";
    }
}

static CHAR16 *crash_likely_reason(UINT64 status) {
    switch (status) {
        case CRASH_STATUS_ACTION_ARMED:
            return L"triple fault / broken IRQ entry path";
        case CRASH_STATUS_PANIC:
            return L"kernel panic";
        case CRASH_STATUS_BOOTING:
            return L"early kernel boot failure";
        default:
            return L"unknown";
    }
}

static void wait_for_enter(EFI_SYSTEM_TABLE *st) {
    for (;;) {
        UINTN event_index = 0;
        EFI_STATUS status = st->BootServices->WaitForEvent(
            1,
            &st->ConIn->WaitForKey,
            &event_index
        );

        if (status != EFI_SUCCESS) {
            return;
        }

        EFI_INPUT_KEY key;
        status = st->ConIn->ReadKeyStroke(st->ConIn, &key);
        if (status != EFI_SUCCESS) {
            continue;
        }

        if (key.UnicodeChar == 0x000D) {
            return;
        }
    }
}

static int crash_info_should_show(CRASH_INFO *info) {
    if (!info) {
        return 0;
    }

    if (info->magic != CRASH_INFO_MAGIC || info->version != CRASH_INFO_VERSION) {
        return 0;
    }

    return (info->status == CRASH_STATUS_BOOTING ||
            info->status == CRASH_STATUS_ACTION_ARMED ||
            info->status == CRASH_STATUS_PANIC);
}

static void show_previous_crash_screen(EFI_SYSTEM_TABLE *st, CRASH_INFO *info) {
    if (!crash_info_should_show(info)) {
        return;
    }

    st->ConOut->ClearScreen(st->ConOut);

    print(st, L"[ERROR]: PREVIOUS KERNEL BOOT FAILED\r\n\r\n");

    print(st, L"STATUS: ");
    print(st, crash_status_name(info->status));
    print(st, L"\r\n");

    print(st, L"LAST STAGE: ");
    print_ascii(st, info->stage);
    print(st, L"\r\n");

    print(st, L"LAST ACTION: ");
    print_ascii(st, info->action);
    print(st, L"\r\n");

    print(st, L"DETAIL: ");
    print_ascii(st, info->detail);
    print(st, L"\r\n");

    print(st, L"LIKELY REASON: ");
    print(st, crash_likely_reason(info->status));
    print(st, L"\r\n");

    print(st, L"BOOT COUNT: ");
    print_dec(st, info->boot_counter);
    print(st, L"\r\n");

    print(st, L"PANIC COUNT: ");
    print_dec(st, info->panic_counter);
    print(st, L"\r\n\r\n");

    print(st, L"PRESS ENTER TO CONTINUE...\r\n");
    wait_for_enter(st);

    st->ConOut->ClearScreen(st->ConOut);
}

static int build_memory_layout(
    EFI_MEMORY_DESCRIPTOR *memory_map,
    UINTN memory_map_size,
    UINTN descriptor_size,
    MEMORY_LAYOUT_PLAN *plan
) {
    if (!memory_map || descriptor_size == 0 || !plan) {
        return 0;
    }

    plan->usable_regions = 0;
    plan->total_usable_bytes = 0;
    plan->chosen_region_base = 0;
    plan->chosen_region_size = 0;
    plan->kernel_phys_base = 0;
    plan->kernel_reserved_size = KERNEL_RESERVED_SIZE;
    plan->bootinfo_phys = 0;
    plan->bootinfo_reserved_size = BOOTINFO_RESERVED_SIZE;
    plan->scratch_phys = 0;
    plan->scratch_reserved_size = SCRATCH_RESERVED_SIZE;
    plan->crash_info_phys = 0;
    plan->crash_info_reserved_size = CRASHINFO_RESERVED_SIZE;
    plan->kernel_stack_bottom = 0;
    plan->kernel_stack_top = 0;
    plan->future_heap_base = 0;
    plan->future_heap_size = 0;

    UINTN entry_count = memory_map_size / descriptor_size;

    EFI_PHYSICAL_ADDRESS best_base = 0;
    UINT64 best_size = 0;

    for (UINTN i = 0; i < entry_count; i++) {
        EFI_MEMORY_DESCRIPTOR *desc =
            (EFI_MEMORY_DESCRIPTOR*)((UINT8*)memory_map + i * descriptor_size);

        if (desc->Type != EfiConventionalMemory) {
            continue;
        }

        UINT64 region_size = desc->NumberOfPages * PAGE_SIZE;

        plan->usable_regions++;
        plan->total_usable_bytes += region_size;

        if (region_size > best_size) {
            best_size = region_size;
            best_base = desc->PhysicalStart;
        }
    }

    if (best_size == 0) {
        return 0;
    }

    plan->chosen_region_base = best_base;
    plan->chosen_region_size = best_size;

    UINT64 region_start = best_base;
    UINT64 region_end = best_base + best_size;

    UINT64 kernel_base = align_up(region_start, 2ULL * MiB);
    UINT64 kernel_end = kernel_base + KERNEL_RESERVED_SIZE;

    UINT64 bootinfo_base = align_up(kernel_end, PAGE_SIZE);
    UINT64 bootinfo_end = bootinfo_base + BOOTINFO_RESERVED_SIZE;

    UINT64 scratch_base = align_up(bootinfo_end, PAGE_SIZE);
    UINT64 scratch_end = scratch_base + SCRATCH_RESERVED_SIZE;

    UINT64 crash_base = align_up(scratch_end, PAGE_SIZE);
    UINT64 crash_end = crash_base + CRASHINFO_RESERVED_SIZE;

    UINT64 stack_top = align_down(region_end, 16ULL);
    UINT64 stack_bottom = stack_top - STACK_RESERVED_SIZE;

    if (kernel_base < region_start) {
        return 0;
    }

    if (crash_end > stack_bottom) {
        return 0;
    }

    plan->kernel_phys_base = kernel_base;
    plan->bootinfo_phys = bootinfo_base;
    plan->scratch_phys = scratch_base;
    plan->crash_info_phys = crash_base;
    plan->kernel_stack_bottom = stack_bottom;
    plan->kernel_stack_top = stack_top;
    plan->future_heap_base = crash_end;
    plan->future_heap_size = stack_bottom - crash_end;

    return 1;
}

static EFI_STATUS reserve_region(
    EFI_SYSTEM_TABLE *st,
    EFI_PHYSICAL_ADDRESS base,
    UINT64 size,
    EFI_MEMORY_TYPE type
) {
    EFI_PHYSICAL_ADDRESS addr = base;
    UINTN pages = pages_for_size(size);
    return st->BootServices->AllocatePages(AllocateAddress, type, pages, &addr);
}

static EFI_STATUS get_gop_info(
    EFI_SYSTEM_TABLE *st,
    BOOT_INFO *boot_info
) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;

    EFI_STATUS status = st->BootServices->LocateProtocol(
        &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
        0,
        (VOID**)&gop
    );

    if (status != EFI_SUCCESS || !gop || !gop->Mode || !gop->Mode->Info) {
        return status;
    }

    boot_info->framebuffer_base = gop->Mode->FrameBufferBase;
    boot_info->framebuffer_size = gop->Mode->FrameBufferSize;
    boot_info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    boot_info->framebuffer_height = gop->Mode->Info->VerticalResolution;
    boot_info->framebuffer_pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    boot_info->framebuffer_format = gop->Mode->Info->PixelFormat;

    return EFI_SUCCESS;
}

static void print_memory_map_table(
    EFI_SYSTEM_TABLE *st,
    EFI_MEMORY_DESCRIPTOR *memory_map,
    UINTN memory_map_size,
    UINTN descriptor_size
) {
    UINTN entry_count = memory_map_size / descriptor_size;

    UINT64 total_pages = 0;
    UINT64 total_bytes = 0;
    UINT64 usable_pages = 0;
    UINT64 usable_bytes = 0;

    print(st, L"\r\n==== Final UEFI Memory Map ====\r\n");

    for (UINTN i = 0; i < entry_count; i++) {
        EFI_MEMORY_DESCRIPTOR *desc =
            (EFI_MEMORY_DESCRIPTOR*)((UINT8*)memory_map + i * descriptor_size);

        UINT64 pages = desc->NumberOfPages;
        UINT64 bytes = pages * PAGE_SIZE;
        UINT64 end = desc->PhysicalStart + bytes;

        total_pages += pages;
        total_bytes += bytes;

        if (desc->Type == EfiConventionalMemory) {
            usable_pages += pages;
            usable_bytes += bytes;
        }

        print(st, L"[");
        print_dec(st, i);
        print(st, L"] ");
        {
            int pad = 4 - u64_digits(i);
            if (pad > 0) {
                print_spaces(st, pad);
            }
        }

        CHAR16 *name = memory_type_name(desc->Type);
        print(st, name);
        {
            int name_len = str16_len(name);
            int pad = 14 - name_len;
            if (pad > 0) {
                print_spaces(st, pad);
            }
        }

        print(st, L"  Base=");
        print_hex(st, desc->PhysicalStart);

        print(st, L"  End=");
        print_hex(st, end);

        print(st, L"  Pg=");
        print_dec(st, pages);
        {
            int pad = 8 - u64_digits(pages);
            if (pad > 0) {
                print_spaces(st, pad);
            }
        }

        print(st, L"  Bytes=");
        print_dec(st, bytes);
        {
            int pad = 12 - u64_digits(bytes);
            if (pad > 0) {
                print_spaces(st, pad);
            }
        }

        print(st, L"  Sec=");
        print_dec(st, bytes_to_sectors(bytes));
        print(st, L"\r\n");
    }

    print(st, L"\r\nMemory map totals:\r\n");
    print(st, L"  All pages:    ");
    print_dec(st, total_pages);
    print(st, L"\r\n");

    print(st, L"  All bytes:    ");
    print_dec(st, total_bytes);
    print(st, L"\r\n");

    print(st, L"  All sectors:  ");
    print_dec(st, bytes_to_sectors(total_bytes));
    print(st, L"\r\n");

    print(st, L"  Usable pages: ");
    print_dec(st, usable_pages);
    print(st, L"\r\n");

    print(st, L"  Usable bytes: ");
    print_dec(st, usable_bytes);
    print(st, L"\r\n");

    print(st, L"  Usable sects: ");
    print_dec(st, bytes_to_sectors(usable_bytes));
    print(st, L"\r\n");
}

__attribute__((noreturn))
static void handoff_to_kernel(UINT64 entry_point, UINT64 stack_top, BOOT_INFO *boot_info) {
    stack_top &= ~0xFULL;

    __asm__ __volatile__ (
        "mov %0, %%rsp\n"
        "mov %1, %%rdi\n"
        "jmp *%2\n"
        :
        : "r"(stack_top), "r"(boot_info), "r"(entry_point)
        : "rsp", "rdi", "memory"
    );

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    if (!SystemTable || !SystemTable->ConOut || !SystemTable->BootServices) {
        return 1;
    }

    SystemTable->ConOut->Reset(SystemTable->ConOut, 1);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

    print(SystemTable, L"MyOS bootloader\r\n");
    print(SystemTable, L"UEFI loader + boot.cfg + ELF kernel + jump to entry\r\n\r\n");

    UINTN memory_map_capacity = 0;
    UINTN memory_map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;

    EFI_STATUS status = SystemTable->BootServices->GetMemoryMap(
        &memory_map_capacity,
        0,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );

    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0) {
        print(SystemTable, L"Initial GetMemoryMap failed.\r\n");
        halt_forever();
    }

    memory_map_capacity += descriptor_size * 64;

    EFI_MEMORY_DESCRIPTOR *memory_map = 0;
    status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData,
        memory_map_capacity,
        (VOID**)&memory_map
    );
    if (status != EFI_SUCCESS || !memory_map) {
        print(SystemTable, L"AllocatePool for memory map failed.\r\n");
        halt_forever();
    }

    memory_map_size = memory_map_capacity;
    status = SystemTable->BootServices->GetMemoryMap(
        &memory_map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"Second GetMemoryMap failed.\r\n");
        halt_forever();
    }

    MEMORY_LAYOUT_PLAN plan;
    if (!build_memory_layout(memory_map, memory_map_size, descriptor_size, &plan)) {
        print(SystemTable, L"Failed to build memory layout.\r\n");
        halt_forever();
    }

    print(SystemTable, L"Chosen usable region base: ");
    print_hex(SystemTable, plan.chosen_region_base);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"Chosen usable region size: ");
    print_dec(SystemTable, plan.chosen_region_size);
    print(SystemTable, L" bytes (");
    print_dec(SystemTable, bytes_to_sectors(plan.chosen_region_size));
    print(SystemTable, L" sectors)\r\n\r\n");

    print(SystemTable, L"Planned layout:\r\n");

    print(SystemTable, L"  Kernel   base=");
    print_hex(SystemTable, plan.kernel_phys_base);
    print(SystemTable, L" size=");
    print_dec(SystemTable, plan.kernel_reserved_size);
    print(SystemTable, L" bytes (");
    print_dec(SystemTable, bytes_to_sectors(plan.kernel_reserved_size));
    print(SystemTable, L" sectors)\r\n");

    print(SystemTable, L"  BootInfo base=");
    print_hex(SystemTable, plan.bootinfo_phys);
    print(SystemTable, L" size=");
    print_dec(SystemTable, plan.bootinfo_reserved_size);
    print(SystemTable, L" bytes (");
    print_dec(SystemTable, bytes_to_sectors(plan.bootinfo_reserved_size));
    print(SystemTable, L" sectors)\r\n");

    print(SystemTable, L"  Scratch  base=");
    print_hex(SystemTable, plan.scratch_phys);
    print(SystemTable, L" size=");
    print_dec(SystemTable, plan.scratch_reserved_size);
    print(SystemTable, L" bytes (");
    print_dec(SystemTable, bytes_to_sectors(plan.scratch_reserved_size));
    print(SystemTable, L" sectors)\r\n");

    print(SystemTable, L"  CrashInf base=");
    print_hex(SystemTable, plan.crash_info_phys);
    print(SystemTable, L" size=");
    print_dec(SystemTable, plan.crash_info_reserved_size);
    print(SystemTable, L" bytes (");
    print_dec(SystemTable, bytes_to_sectors(plan.crash_info_reserved_size));
    print(SystemTable, L" sectors)\r\n");

    print(SystemTable, L"  Stack    base=");
    print_hex(SystemTable, plan.kernel_stack_bottom);
    print(SystemTable, L" size=");
    print_dec(SystemTable, plan.kernel_stack_top - plan.kernel_stack_bottom);
    print(SystemTable, L" bytes (");
    print_dec(SystemTable, bytes_to_sectors(plan.kernel_stack_top - plan.kernel_stack_bottom));
    print(SystemTable, L" sectors)\r\n\r\n");

    status = reserve_region(
        SystemTable,
        plan.kernel_phys_base,
        plan.kernel_reserved_size,
        EfiLoaderData
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"AllocatePages for kernel failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    status = reserve_region(
        SystemTable,
        plan.bootinfo_phys,
        plan.bootinfo_reserved_size,
        EfiLoaderData
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"AllocatePages for BootInfo failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    status = reserve_region(
        SystemTable,
        plan.scratch_phys,
        plan.scratch_reserved_size,
        EfiLoaderData
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"AllocatePages for scratch failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    status = reserve_region(
        SystemTable,
        plan.crash_info_phys,
        plan.crash_info_reserved_size,
        EfiLoaderData
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"AllocatePages for crash info failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    status = reserve_region(
        SystemTable,
        plan.kernel_stack_bottom,
        plan.kernel_stack_top - plan.kernel_stack_bottom,
        EfiLoaderData
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"AllocatePages for stack failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    CRASH_INFO *crash_info = (CRASH_INFO*)(UINTN)plan.crash_info_phys;
    show_previous_crash_screen(SystemTable, crash_info);

    BOOT_CONFIG config;
    UINTN selected = 0;
    CHAR16 *selected_kernel_path = L"\\EFI\\COREFORGE\\KERNELS\\KERNEL.ELF";
    CHAR16 *selected_entry_name = L"Fallback entry";

    status = load_config(ImageHandle, SystemTable, &config);
    if (status == EFI_SUCCESS) {
        selected = run_menu(SystemTable, &config);

        if (selected < config.entry_count) {
            selected_kernel_path = config.entries[selected].kernel_path;
            selected_entry_name = config.entries[selected].name;
        }

        print(SystemTable, L"Selected entry: ");
        print_dec(SystemTable, selected);
        print(SystemTable, L"\r\n");

        print(SystemTable, L"Selected name:  ");
        print(SystemTable, selected_entry_name);
        print(SystemTable, L"\r\n");

        print(SystemTable, L"Selected kernel: ");
        print(SystemTable, selected_kernel_path);
        print(SystemTable, L"\r\n\r\n");
    } else {
        print(SystemTable, L"BOOT.CFG not loaded, using fallback kernel path:\r\n");
        print(SystemTable, selected_kernel_path);
        print(SystemTable, L"\r\n\r\n");
    }

    ELF_LOAD_RESULT elf_result;
    memzero(&elf_result, sizeof(ELF_LOAD_RESULT));

    BOOT_ENTRY *selected_entry = 0;

    if (status == EFI_SUCCESS && selected < config.entry_count) {
        selected_entry = &config.entries[selected];
    }

    if (selected_entry && selected_entry->type == BOOT_ENTRY_TYPE_LINUX_EFI) {
        print(SystemTable, L"Loading Linux EFI kernel...\r\n");

        status = load_linux_efi_from_path(
            ImageHandle,
            SystemTable,
            selected_entry->kernel_path
        );

        print(SystemTable, L"Linux EFI image returned or failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    status = load_kernel_elf_from_path(
        ImageHandle,
        SystemTable,
        selected_kernel_path,
        plan.kernel_phys_base,
        plan.kernel_reserved_size,
        &elf_result
    );

    if (status != EFI_SUCCESS) {
        print(SystemTable, L"Failed to load selected ELF kernel. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    print(SystemTable, L"Selected ELF kernel loaded successfully\r\n");
    print(SystemTable, L"  Entry name:    ");
    print(SystemTable, selected_entry_name);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  File path:     ");
    print(SystemTable, selected_kernel_path);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Image base:    ");
    print_hex(SystemTable, elf_result.image_base);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Image end:     ");
    print_hex(SystemTable, elf_result.image_end);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Entry point:   ");
    print_hex(SystemTable, elf_result.entry_point);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Segments:      ");
    print_dec(SystemTable, elf_result.loadable_segments);
    print(SystemTable, L"\r\n\r\n");

    BOOT_INFO *boot_info = (BOOT_INFO*)(UINTN)plan.bootinfo_phys;
    memzero(boot_info, sizeof(BOOT_INFO));

    boot_info->usable_memory_bytes = plan.total_usable_bytes;
    boot_info->chosen_region_base = plan.chosen_region_base;
    boot_info->chosen_region_size = plan.chosen_region_size;
    boot_info->kernel_phys_base = plan.kernel_phys_base;
    boot_info->kernel_reserved_size = plan.kernel_reserved_size;
    boot_info->kernel_file_size = elf_result.image_end - elf_result.image_base;
    boot_info->bootinfo_phys = plan.bootinfo_phys;
    boot_info->bootinfo_size = sizeof(BOOT_INFO);
    boot_info->scratch_phys = plan.scratch_phys;
    boot_info->scratch_size = plan.scratch_reserved_size;
    boot_info->kernel_stack_bottom = plan.kernel_stack_bottom;
    boot_info->kernel_stack_top = plan.kernel_stack_top;
    boot_info->heap_base = plan.future_heap_base;
    boot_info->heap_size = plan.future_heap_size;
    boot_info->crash_info_phys = plan.crash_info_phys;
    boot_info->crash_info_size = plan.crash_info_reserved_size;

    status = get_gop_info(SystemTable, boot_info);
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"Failed to get GOP/framebuffer info.\r\n");
        halt_forever();
    }

    EFI_MEMORY_DESCRIPTOR *final_map = (EFI_MEMORY_DESCRIPTOR*)(UINTN)plan.scratch_phys;
    UINTN final_map_size = (UINTN)plan.scratch_reserved_size;
    UINTN final_map_key = 0;
    UINTN final_descriptor_size = 0;
    UINT32 final_descriptor_version = 0;

    status = SystemTable->BootServices->GetMemoryMap(
        &final_map_size,
        final_map,
        &final_map_key,
        &final_descriptor_size,
        &final_descriptor_version
    );
    if (status != EFI_SUCCESS) {
        print(SystemTable, L"Final GetMemoryMap failed. Status=");
        print_hex(SystemTable, status);
        print(SystemTable, L"\r\n");
        halt_forever();
    }

    boot_info->memory_map = (UINT64)(UINTN)final_map;
    boot_info->memory_map_size = final_map_size;
    boot_info->memory_descriptor_size = final_descriptor_size;
    boot_info->memory_descriptor_version = final_descriptor_version;

    print(SystemTable, L"BOOT_INFO ready\r\n");
    print(SystemTable, L"  BootInfo at:   ");
    print_hex(SystemTable, plan.bootinfo_phys);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  MemoryMap at:  ");
    print_hex(SystemTable, (UINT64)(UINTN)final_map);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  CrashInfo at:  ");
    print_hex(SystemTable, plan.crash_info_phys);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Map size:      ");
    print_dec(SystemTable, final_map_size);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Desc size:     ");
    print_dec(SystemTable, final_descriptor_size);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"  Desc version:  ");
    print_dec(SystemTable, final_descriptor_version);
    print(SystemTable, L"\r\n");

    print(SystemTable, L"\r\nExiting UEFI Boot Services...\r\n");

    status = SystemTable->BootServices->ExitBootServices(
        ImageHandle,
        final_map_key
    );

    if (status != EFI_SUCCESS) {
        /*
         * Иногда карта памяти успевает измениться между GetMemoryMap и ExitBootServices.
         * В этом случае обновляем её ещё раз и пробуем повторно.
         */
        final_map_size = (UINTN)plan.scratch_reserved_size;
        status = SystemTable->BootServices->GetMemoryMap(
            &final_map_size,
            final_map,
            &final_map_key,
            &final_descriptor_size,
            &final_descriptor_version
        );

        if (status != EFI_SUCCESS) {
            print(SystemTable, L"Retry GetMemoryMap before ExitBootServices failed.\r\n");
            halt_forever();
        }

        boot_info->memory_map = (UINT64)(UINTN)final_map;
        boot_info->memory_map_size = final_map_size;
        boot_info->memory_descriptor_size = final_descriptor_size;
        boot_info->memory_descriptor_version = final_descriptor_version;

        status = SystemTable->BootServices->ExitBootServices(
            ImageHandle,
            final_map_key
        );

        if (status != EFI_SUCCESS) {
            print(SystemTable, L"ExitBootServices failed!\r\n");
            halt_forever();
        }
    }

    handoff_to_kernel(
        elf_result.entry_point,
        plan.kernel_stack_top,
        boot_info
    );
}
