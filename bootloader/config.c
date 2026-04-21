#include "config.h"

static void zero_config(BOOT_CONFIG *config) {
    UINT8 *p = (UINT8*)config;
    for (UINTN i = 0; i < sizeof(BOOT_CONFIG); i++) {
        p[i] = 0;
    }
}

static int ascii_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int ascii_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void ascii_to_char16(CHAR16 *dst, UINTN dst_cap, const char *src, UINTN src_len) {
    UINTN n = 0;

    if (dst_cap == 0) {
        return;
    }

    while (n + 1 < dst_cap && n < src_len && src[n] != '\0') {
        dst[n] = (CHAR16)(UINT8)src[n];
        n++;
    }

    dst[n] = 0;
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

EFI_STATUS load_config(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *st,
    BOOT_CONFIG *config
) {
    zero_config(config);
    config->default_entry = 0;
    config->timeout = 3;

    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *file = 0;

    EFI_STATUS status = open_root_dir(image, st, &root);
    if (status != EFI_SUCCESS) {
        return status;
    }

    status = root->Open(
        root,
        &file,
        L"\\EFI\\BOOT\\BOOT.CFG",
        EFI_FILE_MODE_READ,
        0
    );
    if (status != EFI_SUCCESS || !file) {
        return status;
    }

    UINT64 file_size64 = 0;
    status = get_file_size(st, file, &file_size64);
    if (status != EFI_SUCCESS) {
        return status;
    }

    if (file_size64 == 0 || file_size64 > 65535) {
        return EFI_INVALID_PARAMETER;
    }

    UINTN file_size = (UINTN)file_size64;
    char *buffer = 0;

    status = st->BootServices->AllocatePool(
        EfiLoaderData,
        file_size + 1,
        (VOID**)&buffer
    );
    if (status != EFI_SUCCESS || !buffer) {
        return status;
    }

    UINTN read_size = file_size;
    status = file->Read(file, &read_size, buffer);
    if (status != EFI_SUCCESS) {
        return status;
    }

    if (read_size > file_size) {
        return EFI_INVALID_PARAMETER;
    }

    buffer[read_size] = '\0';

    UINTN i = 0;
    BOOT_ENTRY *current = 0;

    while (i < read_size) {
        while (i < read_size && ascii_is_space(buffer[i])) {
            i++;
        }

        if (i >= read_size) {
            break;
        }

        if (buffer[i] == '#') {
            while (i < read_size && buffer[i] != '\n') {
                i++;
            }
            continue;
        }

        if (i + 8 <= read_size &&
            buffer[i+0] == 'd' &&
            buffer[i+1] == 'e' &&
            buffer[i+2] == 'f' &&
            buffer[i+3] == 'a' &&
            buffer[i+4] == 'u' &&
            buffer[i+5] == 'l' &&
            buffer[i+6] == 't' &&
            buffer[i+7] == '=') {

            i += 8;

            UINTN value = 0;
            while (i < read_size && ascii_is_digit(buffer[i])) {
                value = value * 10 + (UINTN)(buffer[i] - '0');
                i++;
            }

            config->default_entry = value;
            continue;
        }

        if (i + 8 <= read_size &&
            buffer[i+0] == 't' &&
            buffer[i+1] == 'i' &&
            buffer[i+2] == 'm' &&
            buffer[i+3] == 'e' &&
            buffer[i+4] == 'o' &&
            buffer[i+5] == 'u' &&
            buffer[i+6] == 't' &&
            buffer[i+7] == '=') {

            i += 8;

            UINTN value = 0;
            while (i < read_size && ascii_is_digit(buffer[i])) {
                value = value * 10 + (UINTN)(buffer[i] - '0');
                i++;
            }

            config->timeout = value;
            continue;
        }

        if (i + 5 <= read_size &&
            buffer[i+0] == 'e' &&
            buffer[i+1] == 'n' &&
            buffer[i+2] == 't' &&
            buffer[i+3] == 'r' &&
            buffer[i+4] == 'y') {

            i += 5;

            while (i < read_size && ascii_is_space(buffer[i])) {
                i++;
            }

            if (i >= read_size || buffer[i] != '"') {
                continue;
            }

            if (config->entry_count >= MAX_ENTRIES) {
                return EFI_BUFFER_TOO_SMALL;
            }

            current = &config->entries[config->entry_count];
            config->entry_count++;

            i++;

            UINTN name_start = i;
            while (i < read_size && buffer[i] != '"') {
                i++;
            }

            if (i > name_start) {
                ascii_to_char16(
                    current->name,
                    64,
                    &buffer[name_start],
                    i - name_start
                );
            }

            while (i < read_size && buffer[i] != '{') {
                i++;
            }

            if (i < read_size && buffer[i] == '{') {
                i++;
            }

            continue;
        }

        if (i + 7 <= read_size &&
            buffer[i+0] == 'k' &&
            buffer[i+1] == 'e' &&
            buffer[i+2] == 'r' &&
            buffer[i+3] == 'n' &&
            buffer[i+4] == 'e' &&
            buffer[i+5] == 'l' &&
            buffer[i+6] == '=') {

            i += 7;

            while (i < read_size && ascii_is_space(buffer[i])) {
                i++;
            }

            if (current) {
                UINTN path_start = i;

                while (i < read_size &&
                       buffer[i] != '\r' &&
                       buffer[i] != '\n' &&
                       buffer[i] != '}') {
                    i++;
                }

                UINTN path_end = i;
                while (path_end > path_start && ascii_is_space(buffer[path_end - 1])) {
                    path_end--;
                }

                if (path_end > path_start) {
                    ascii_to_char16(
                        current->kernel_path,
                        128,
                        &buffer[path_start],
                        path_end - path_start
                    );
                }
            }

            continue;
        }

        i++;
    }

    if (config->entry_count == 0) {
        return EFI_NOT_FOUND;
    }

    if (config->default_entry >= config->entry_count) {
        config->default_entry = 0;
    }

    return EFI_SUCCESS;
}
