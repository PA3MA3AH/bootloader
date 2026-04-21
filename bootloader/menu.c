#include "menu.h"

#define SCAN_UP     0x0001
#define SCAN_DOWN   0x0002
#define SCAN_ESC    0x0017
#define CHAR_CR     0x000D

static void print(EFI_SYSTEM_TABLE *st, CHAR16 *s) {
    st->ConOut->OutputString(st->ConOut, s);
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

static void draw_menu(EFI_SYSTEM_TABLE *st, BOOT_CONFIG *config, UINTN selected) {
    st->ConOut->ClearScreen(st->ConOut);

    print(st, L"MyOS bootloader\r\n");
    print(st, L"Boot menu\r\n\r\n");

    for (UINTN i = 0; i < config->entry_count; i++) {
        if (i == selected) {
            print(st, L"> ");
        } else {
            print(st, L"  ");
        }

        print_dec(st, i);
        print(st, L": ");
        print(st, config->entries[i].name);
        print(st, L"\r\n");
    }

    print(st, L"\r\n");
    print(st, L"Up/Down - select\r\n");
    print(st, L"Enter   - boot\r\n");
    print(st, L"Esc     - boot default\r\n\r\n");

    print(st, L"Current selection: ");
    print_dec(st, selected);
    print(st, L"\r\n");
}

UINTN run_menu(
    EFI_SYSTEM_TABLE *st,
    BOOT_CONFIG *config
) {
    UINTN selected = config->default_entry;

    if (config->entry_count == 0) {
        return 0;
    }

    if (selected >= config->entry_count) {
        selected = 0;
    }

    draw_menu(st, config, selected);

    for (;;) {
        UINTN event_index = 0;
        EFI_STATUS status = st->BootServices->WaitForEvent(
            1,
            &st->ConIn->WaitForKey,
            &event_index
        );

        if (status != EFI_SUCCESS) {
            return config->default_entry;
        }

        EFI_INPUT_KEY key;
        status = st->ConIn->ReadKeyStroke(st->ConIn, &key);
        if (status != EFI_SUCCESS) {
            continue;
        }

        if (key.ScanCode == SCAN_UP) {
            if (selected == 0) {
                selected = config->entry_count - 1;
            } else {
                selected--;
            }

            draw_menu(st, config, selected);
            continue;
        }

        if (key.ScanCode == SCAN_DOWN) {
            selected++;
            if (selected >= config->entry_count) {
                selected = 0;
            }

            draw_menu(st, config, selected);
            continue;
        }

        if (key.UnicodeChar == CHAR_CR) {
            st->ConOut->ClearScreen(st->ConOut);
            print(st, L"MyOS bootloader\r\n");
            print(st, L"Boot menu\r\n\r\n");
            print(st, L"Booting selected entry...\r\n\r\n");
            return selected;
        }

        if (key.ScanCode == SCAN_ESC) {
            st->ConOut->ClearScreen(st->ConOut);
            print(st, L"MyOS bootloader\r\n");
            print(st, L"Boot menu\r\n\r\n");
            print(st, L"Escape pressed, booting default entry...\r\n\r\n");
            return config->default_entry;
        }
    }
}
