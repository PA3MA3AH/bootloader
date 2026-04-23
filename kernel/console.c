#include "console.h"
#include "font.h"

static void draw_char(CONSOLE *con, uint32_t px, uint32_t py, char c) {
    if ((unsigned char)c >= 128) {
        c = '?';
    }

    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint8_t row = font8x16[(unsigned char)c][y];

        for (uint32_t x = 0; x < FONT_WIDTH; x++) {
            uint32_t color = (row & (1u << (7 - x))) ? con->fg_color : con->bg_color;
            fb_put_pixel(&con->fb, px + x, py + y, color);
        }
    }
}

static void clear_cell(CONSOLE *con, uint32_t cx, uint32_t cy) {
    uint32_t px = con->origin_x + cx * con->cell_w;
    uint32_t py = con->origin_y + cy * con->cell_h;
    fb_fill_rect(&con->fb, px, py, con->cell_w, con->cell_h, con->bg_color);
}

static void scroll_if_needed(CONSOLE *con) {
    uint32_t line_pixels;
    uint32_t pitch;
    uint32_t width_pixels;
    uint32_t height_pixels;
    uint32_t x0;
    uint32_t y0;
    uint32_t copy_rows;
    uint32_t clear_rows;
    uint32_t y;

    if (con->cursor_y < con->rows) {
        return;
    }

    line_pixels = con->cell_h;
    pitch = con->fb.pitch;
    width_pixels = con->cols * con->cell_w;
    height_pixels = con->rows * con->cell_h;
    x0 = con->origin_x;
    y0 = con->origin_y;
    copy_rows = height_pixels - line_pixels;
    clear_rows = line_pixels;

    for (y = 0; y < copy_rows; y++) {
        uint32_t *dst = &con->fb.base[(y0 + y) * pitch + x0];
        uint32_t *src = &con->fb.base[(y0 + y + line_pixels) * pitch + x0];
        uint32_t x;
        for (x = 0; x < width_pixels; x++) {
            dst[x] = src[x];
        }
    }

    for (y = 0; y < clear_rows; y++) {
        uint32_t *dst = &con->fb.base[(y0 + copy_rows + y) * pitch + x0];
        uint32_t x;
        for (x = 0; x < width_pixels; x++) {
            dst[x] = con->bg_color;
        }
    }

    con->cursor_y = con->rows - 1;
}

static void write_signed(CONSOLE *con, int64_t value) {
    if (value < 0) {
        console_putchar(con, '-');
        console_write_udec(con, (uint64_t)(-value));
        return;
    }

    console_write_udec(con, (uint64_t)value);
}

void console_init(CONSOLE *con, BOOT_INFO *boot_info, uint32_t fg, uint32_t bg) {
    fb_init(&con->fb, boot_info);

    con->fg_color = fg;
    con->bg_color = bg;

    con->cursor_x = 0;
    con->cursor_y = 0;

    con->cell_w = FONT_WIDTH;
    con->cell_h = FONT_HEIGHT;

    con->origin_x = 8;
    con->origin_y = 8;

    uint32_t usable_w = (con->fb.width > con->origin_x * 2) ? (con->fb.width - con->origin_x * 2) : con->fb.width;
    uint32_t usable_h = (con->fb.height > con->origin_y * 2) ? (con->fb.height - con->origin_y * 2) : con->fb.height;

    con->cols = usable_w / con->cell_w;
    con->rows = usable_h / con->cell_h;
}

void console_clear(CONSOLE *con) {
    fb_clear(&con->fb, con->bg_color);
    con->cursor_x = 0;
    con->cursor_y = 0;
}

void console_set_colors(CONSOLE *con, uint32_t fg, uint32_t bg) {
    con->fg_color = fg;
    con->bg_color = bg;
}

void console_backspace(CONSOLE *con) {
    if (con->cursor_x == 0) {
        if (con->cursor_y == 0) {
            return;
        }
        con->cursor_y--;
        con->cursor_x = con->cols - 1;
    } else {
        con->cursor_x--;
    }

    clear_cell(con, con->cursor_x, con->cursor_y);
}

void console_putchar(CONSOLE *con, char c) {
    if (c == '\n') {
        con->cursor_x = 0;
        con->cursor_y++;
        scroll_if_needed(con);
        return;
    }

    if (c == '\r') {
        con->cursor_x = 0;
        return;
    }

    if (c == '\t') {
        uint32_t next_tab = (con->cursor_x + 4) & ~3u;
        while (con->cursor_x < next_tab) {
            console_putchar(con, ' ');
        }
        return;
    }

    if (c == '\b') {
        console_backspace(con);
        return;
    }

    uint32_t px = con->origin_x + con->cursor_x * con->cell_w;
    uint32_t py = con->origin_y + con->cursor_y * con->cell_h;

    draw_char(con, px, py, c);

    con->cursor_x++;
    if (con->cursor_x >= con->cols) {
        con->cursor_x = 0;
        con->cursor_y++;
        scroll_if_needed(con);
    }
}

void console_write(CONSOLE *con, const char *s) {
    while (*s) {
        console_putchar(con, *s++);
    }
}

void console_write_len(CONSOLE *con, const char *s, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        console_putchar(con, s[i]);
    }
}

void console_write_hex(CONSOLE *con, uint64_t value) {
    static const char *hex = "0123456789ABCDEF";
    console_write(con, "0x");

    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (uint8_t)((value >> (i * 4)) & 0xF);
        console_putchar(con, hex[nibble]);
    }
}

void console_write_dec(CONSOLE *con, uint64_t value) {
    write_signed(con, (int64_t)value);
}

void console_write_udec(CONSOLE *con, uint64_t value) {
    char buf[32];
    int i = 0;

    if (value == 0) {
        console_putchar(con, '0');
        return;
    }

    while (value > 0) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        console_putchar(con, buf[--i]);
    }
}

void console_write_ptr(CONSOLE *con, const void *ptr) {
    console_write_hex(con, (uint64_t)(uintptr_t)ptr);
}

void console_vprintf(CONSOLE *con, const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') {
            console_putchar(con, *fmt++);
            continue;
        }

        fmt++;

        if (*fmt == '\0') {
            break;
        }

        switch (*fmt) {
            case '%':
                console_putchar(con, '%');
                break;

            case 'c': {
                int ch = va_arg(args, int);
                console_putchar(con, (char)ch);
                break;
            }

            case 's': {
                const char *s = va_arg(args, const char*);
                if (!s) {
                    console_write(con, "(null)");
                } else {
                    console_write(con, s);
                }
                break;
            }

            case 'd':
            case 'i': {
                int value = va_arg(args, int);
                write_signed(con, (int64_t)value);
                break;
            }

            case 'u': {
                unsigned int value = va_arg(args, unsigned int);
                console_write_udec(con, (uint64_t)value);
                break;
            }

            case 'x':
            case 'X': {
                unsigned int value = va_arg(args, unsigned int);
                console_write_hex(con, (uint64_t)value);
                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void*);
                console_write_ptr(con, ptr);
                break;
            }

            default:
                console_putchar(con, '%');
                console_putchar(con, *fmt);
                break;
        }

        fmt++;
    }
}

void console_printf(CONSOLE *con, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    console_vprintf(con, fmt, args);
    va_end(args);
}
