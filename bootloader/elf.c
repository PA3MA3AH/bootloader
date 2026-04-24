#include "elf.h"

static UINTN StrLen(const CHAR16 *s) {
    UINTN i = 0;
    while (s[i]) i++;
    return i;
}

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

#define EFI_DP_TYPE_END                 0x7F
#define EFI_DP_SUBTYPE_END_ENTIRE       0xFF
#define EFI_DP_TYPE_MEDIA               0x04
#define EFI_DP_SUBTYPE_MEDIA_FILEPATH   0x04

static UINTN char16_len_local(const CHAR16 *s) {
    UINTN n = 0;

    if (!s) {
        return 0;
    }

    while (s[n]) {
        n++;
    }

    return n;
}

static UINTN device_path_node_len(const EFI_DEVICE_PATH_PROTOCOL *node) {
    return (UINTN)node->Length[0] | ((UINTN)node->Length[1] << 8);
}

static int device_path_is_end(const EFI_DEVICE_PATH_PROTOCOL *node) {
    return node->Type == EFI_DP_TYPE_END && node->SubType == EFI_DP_SUBTYPE_END_ENTIRE;
}

static UINTN device_path_size_with_end(const EFI_DEVICE_PATH_PROTOCOL *path) {
    UINTN total = 0;
    const EFI_DEVICE_PATH_PROTOCOL *node = path;

    if (!path) {
        return 0;
    }

    for (;;) {
        UINTN len = device_path_node_len(node);
        if (len < sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
            return 0;
        }

        total += len;

        if (device_path_is_end(node)) {
            break;
        }

        node = (const EFI_DEVICE_PATH_PROTOCOL*)((const UINT8*)node + len);
    }

    return total;
}

static EFI_STATUS build_file_device_path(
    EFI_SYSTEM_TABLE *st,
    EFI_HANDLE image_handle,
    CHAR16 *kernel_path,
    EFI_DEVICE_PATH_PROTOCOL **out_path
) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_DEVICE_PATH_PROTOCOL *parent_path = 0;
    EFI_STATUS status;
    UINTN parent_size;
    UINTN parent_without_end;
    UINTN path_chars;
    UINTN file_node_size;
    UINTN end_node_size;
    UINTN total_size;
    UINT8 *buffer = 0;
    EFI_DEVICE_PATH_PROTOCOL *file_node;
    EFI_DEVICE_PATH_PROTOCOL *end_node;
    CHAR16 *dst_name;

    if (!st || !st->BootServices || !image_handle || !kernel_path || !out_path) {
        return EFI_INVALID_PARAMETER;
    }

    *out_path = 0;

    status = st->BootServices->HandleProtocol(
        image_handle,
        &EFI_LOADED_IMAGE_PROTOCOL_GUID,
        (VOID**)&loaded
    );
    if (status != EFI_SUCCESS || !loaded) {
        return status != EFI_SUCCESS ? status : EFI_LOAD_ERROR;
    }

    status = st->BootServices->HandleProtocol(
        loaded->DeviceHandle,
        &EFI_DEVICE_PATH_PROTOCOL_GUID,
        (VOID**)&parent_path
    );
    if (status != EFI_SUCCESS || !parent_path) {
        return status != EFI_SUCCESS ? status : EFI_LOAD_ERROR;
    }

    parent_size = device_path_size_with_end(parent_path);
    if (parent_size < sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
        return EFI_LOAD_ERROR;
    }

    parent_without_end = parent_size - sizeof(EFI_DEVICE_PATH_PROTOCOL);
    path_chars = char16_len_local(kernel_path);
    file_node_size = sizeof(EFI_DEVICE_PATH_PROTOCOL) + ((path_chars + 1) * sizeof(CHAR16));
    end_node_size = sizeof(EFI_DEVICE_PATH_PROTOCOL);
    total_size = parent_without_end + file_node_size + end_node_size;

    status = st->BootServices->AllocatePool(EfiLoaderData, total_size, (VOID**)&buffer);
    if (status != EFI_SUCCESS || !buffer) {
        return status != EFI_SUCCESS ? status : EFI_OUT_OF_RESOURCES;
    }

    memcpy_local(buffer, parent_path, parent_without_end);

    file_node = (EFI_DEVICE_PATH_PROTOCOL*)(buffer + parent_without_end);
    file_node->Type = EFI_DP_TYPE_MEDIA;
    file_node->SubType = EFI_DP_SUBTYPE_MEDIA_FILEPATH;
    file_node->Length[0] = (UINT8)(file_node_size & 0xFF);
    file_node->Length[1] = (UINT8)((file_node_size >> 8) & 0xFF);

    dst_name = (CHAR16*)((UINT8*)file_node + sizeof(EFI_DEVICE_PATH_PROTOCOL));
    for (UINTN i = 0; i < path_chars; i++) {
        dst_name[i] = kernel_path[i];
    }
    dst_name[path_chars] = 0;

    end_node = (EFI_DEVICE_PATH_PROTOCOL*)((UINT8*)file_node + file_node_size);
    end_node->Type = EFI_DP_TYPE_END;
    end_node->SubType = EFI_DP_SUBTYPE_END_ENTIRE;
    end_node->Length[0] = (UINT8)(end_node_size & 0xFF);
    end_node->Length[1] = (UINT8)((end_node_size >> 8) & 0xFF);

    *out_path = (EFI_DEVICE_PATH_PROTOCOL*)buffer;
    return EFI_SUCCESS;
}

EFI_STATUS load_linux_efi_from_path(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *st,
    CHAR16 *kernel_path
) {
    if (!st || !st->BootServices || !kernel_path) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *linux_file = 0;

    EFI_STATUS status = open_root_dir(image_handle, st, &root);
    if (status != EFI_SUCCESS || !root) {
        return status;
    }

    status = root->Open(root, &linux_file, kernel_path, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || !linux_file) {
        root->Close(root);
        return status != EFI_SUCCESS ? status : EFI_NOT_FOUND;
    }

    linux_file->Close(linux_file);
    root->Close(root);

    EFI_DEVICE_PATH_PROTOCOL *linux_device_path = 0;
    status = build_file_device_path(st, image_handle, kernel_path, &linux_device_path);
    if (status != EFI_SUCCESS || !linux_device_path) {
        return status != EFI_SUCCESS ? status : EFI_LOAD_ERROR;
    }

    EFI_HANDLE linux_handle = 0;

    status = st->BootServices->LoadImage(
        0,
        image_handle,
        linux_device_path,
        0,
        0,
        &linux_handle
    );

    st->BootServices->FreePool(linux_device_path);

    if (status != EFI_SUCCESS || !linux_handle) {
        return status != EFI_SUCCESS ? status : EFI_LOAD_ERROR;
    }

    EFI_LOADED_IMAGE_PROTOCOL *linux_loaded = 0;
    status = st->BootServices->HandleProtocol(
        linux_handle,
        &EFI_LOADED_IMAGE_PROTOCOL_GUID,
        (VOID**)&linux_loaded
    );
    
    if (status != EFI_SUCCESS || !linux_loaded) {
        return status != EFI_SUCCESS ? status : EFI_LOAD_ERROR;
    }
    
    /* 👇 ВОТ СЮДА ВСТАВЛЯЕШЬ */
    static CHAR16 *cmdline =
    L"console=ttyS0 earlyprintk=ttyS0 loglevel=7 init=/init mem=256M";
    
    linux_loaded->LoadOptions = cmdline;
    linux_loaded->LoadOptionsSize =
        (UINT32)((StrLen(cmdline) + 1) * sizeof(CHAR16));

    EFI_FILE_PROTOCOL *initrd_file = 0;
    VOID *initrd_buffer = 0;
    UINTN initrd_size = 0;
    
    /* открыть initramfs */
    status = open_root_dir(image_handle, st, &root);
    if (status == EFI_SUCCESS) {
        status = root->Open(root, &initrd_file,
            L"\\EFI\\COREFORGE\\KERNELS\\initramfs.cpio",
            EFI_FILE_MODE_READ, 0);
    }
    
    if (status == EFI_SUCCESS && initrd_file) {
        UINT64 sz = 0;
        get_file_size(st, initrd_file, &sz);
    
        initrd_size = (UINTN)sz;
    
        st->BootServices->AllocatePool(EfiLoaderData, initrd_size, &initrd_buffer);
    
        UINTN rs = initrd_size;
        initrd_file->Read(initrd_file, &rs, initrd_buffer);
    
        initrd_file->Close(initrd_file);
    
        /* передаём Linux */
        linux_loaded->LoadOptions = cmdline;
        linux_loaded->LoadOptionsSize =
            (UINT32)((StrLen(cmdline)+1)*sizeof(CHAR16));
    
        linux_loaded->ImageBase = initrd_buffer;
    }

    UINTN exit_data_size = 0;
    CHAR16 *exit_data = 0;

    status = st->BootServices->StartImage(
        linux_handle,
        &exit_data_size,
        &exit_data
    );

    return status;
}

