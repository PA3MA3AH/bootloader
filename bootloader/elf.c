#include "elf.h"

static void memzero_local(VOID *ptr, UINTN size) {
    UINT8 *p = (UINT8*)ptr;
    for (UINTN i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void memcpy_local(VOID *dst, const VOID *src, UINTN size) {
    UINT8 *d = (UINT8*)dst;
    const UINT8 *s = (const UINT8*)src;
    for (UINTN i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static EFI_STATUS open_root_dir(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *st,
    EFI_FILE_PROTOCOL **out_root
) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;

    EFI_STATUS status = st->BootServices->HandleProtocol(
        image_handle,
        &EFI_LOADED_IMAGE_PROTOCOL_GUID,
        (VOID**)&loaded_image
    );
    if (status != EFI_SUCCESS || !loaded_image) {
        return status;
    }

    status = st->BootServices->HandleProtocol(
        loaded_image->DeviceHandle,
        &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
        (VOID**)&fs
    );
    if (status != EFI_SUCCESS || !fs) {
        return status;
    }

    status = fs->OpenVolume(fs, &root);
    if (status != EFI_SUCCESS || !root) {
        return status;
    }

    *out_root = root;
    return EFI_SUCCESS;
}

static EFI_STATUS get_file_size(
    EFI_SYSTEM_TABLE *st,
    EFI_FILE_PROTOCOL *file,
    UINT64 *out_file_size
) {
    UINTN info_size = 0;
    EFI_FILE_INFO *info = 0;

    EFI_STATUS status = file->GetInfo(file, &EFI_FILE_INFO_GUID, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL || info_size == 0) {
        return status;
    }

    status = st->BootServices->AllocatePool(EfiLoaderData, info_size, (VOID**)&info);
    if (status != EFI_SUCCESS || !info) {
        return status;
    }

    status = file->GetInfo(file, &EFI_FILE_INFO_GUID, &info_size, info);
    if (status != EFI_SUCCESS) {
        return status;
    }

    *out_file_size = info->FileSize;
    return EFI_SUCCESS;
}

static int validate_elf64(ELF64_EHDR *eh) {
    if (!eh) {
        return 0;
    }

    if (eh->e_ident[0] != ELF64_MAGIC_0 ||
        eh->e_ident[1] != ELF64_MAGIC_1 ||
        eh->e_ident[2] != ELF64_MAGIC_2 ||
        eh->e_ident[3] != ELF64_MAGIC_3) {
        return 0;
    }

    if (eh->e_ident[4] != ELFCLASS64) {
        return 0;
    }

    if (eh->e_ident[5] != ELFDATA2LSB) {
        return 0;
    }

    if (eh->e_machine != EM_X86_64) {
        return 0;
    }

    if (eh->e_phentsize != sizeof(ELF64_PHDR)) {
        return 0;
    }

    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        return 0;
    }

    return 1;
}

EFI_STATUS load_kernel_elf_from_path(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *st,
    CHAR16 *kernel_path,
    EFI_PHYSICAL_ADDRESS reserved_base,
    UINT64 reserved_size,
    ELF_LOAD_RESULT *result
) {
    if (!result) {
        return EFI_INVALID_PARAMETER;
    }

    result->image_base = 0;
    result->image_end = 0;
    result->entry_point = 0;
    result->loadable_segments = 0;

    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *kernel = 0;

    EFI_STATUS status = open_root_dir(image_handle, st, &root);
    if (status != EFI_SUCCESS) {
        return status;
    }

    status = root->Open(root, &kernel, kernel_path, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || !kernel) {
        return status;
    }

    UINT64 file_size64 = 0;
    status = get_file_size(st, kernel, &file_size64);
    if (status != EFI_SUCCESS) {
        return status;
    }

    if (file_size64 < sizeof(ELF64_EHDR)) {
        return EFI_LOAD_ERROR;
    }

    UINTN file_size = (UINTN)file_size64;
    VOID *file_buffer = 0;

    status = st->BootServices->AllocatePool(
        EfiLoaderData,
        file_size,
        &file_buffer
    );
    if (status != EFI_SUCCESS || !file_buffer) {
        return status;
    }

    UINTN read_size = file_size;
    status = kernel->Read(kernel, &read_size, file_buffer);
    if (status != EFI_SUCCESS || read_size != file_size) {
        return EFI_LOAD_ERROR;
    }

    ELF64_EHDR *eh = (ELF64_EHDR*)file_buffer;
    if (!validate_elf64(eh)) {
        return EFI_LOAD_ERROR;
    }

    if (eh->e_phoff + ((UINT64)eh->e_phnum * sizeof(ELF64_PHDR)) > file_size64) {
        return EFI_LOAD_ERROR;
    }

    ELF64_PHDR *phdrs = (ELF64_PHDR*)((UINT8*)file_buffer + eh->e_phoff);

    UINT64 low = ~0ULL;
    UINT64 high = 0;
    UINT64 segments = 0;

    for (UINT16 i = 0; i < eh->e_phnum; i++) {
        ELF64_PHDR *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) {
            continue;
        }

        if (ph->p_memsz == 0) {
            continue;
        }

        if (ph->p_offset + ph->p_filesz > file_size64) {
            return EFI_LOAD_ERROR;
        }

        if (ph->p_paddr < low) {
            low = ph->p_paddr;
        }

        if (ph->p_paddr + ph->p_memsz > high) {
            high = ph->p_paddr + ph->p_memsz;
        }

        segments++;
    }

    if (segments == 0) {
        return EFI_LOAD_ERROR;
    }

    if (low < reserved_base) {
        return EFI_LOAD_ERROR;
    }

    if (high > reserved_base + reserved_size) {
        return EFI_BUFFER_TOO_SMALL;
    }

    memzero_local((VOID*)(UINTN)reserved_base, (UINTN)reserved_size);

    for (UINT16 i = 0; i < eh->e_phnum; i++) {
        ELF64_PHDR *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }

        VOID *src = (UINT8*)file_buffer + ph->p_offset;
        VOID *dst = (VOID*)(UINTN)ph->p_paddr;

        if (ph->p_filesz > 0) {
            memcpy_local(dst, src, (UINTN)ph->p_filesz);
        }

        if (ph->p_memsz > ph->p_filesz) {
            memzero_local(
                (UINT8*)dst + ph->p_filesz,
                (UINTN)(ph->p_memsz - ph->p_filesz)
            );
        }
    }

    result->image_base = low;
    result->image_end = high;
    result->entry_point = eh->e_entry;
    result->loadable_segments = segments;

    return EFI_SUCCESS;
}
