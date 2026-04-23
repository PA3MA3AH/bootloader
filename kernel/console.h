#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stdarg.h>
#include "framebuffer.h"

typedef struct {
    FRAMEBUFFER fb;

    uint32_t fg_color;
    uint32_t bg_color;

    uint32_t cursor_x;
    uint32_t cursor_y;

    uint32_t cols;
    uint32_t rows;

    uint32_t origin_x;
    uint32_t origin_y;

    uint32_t cell_w;
    uint32_t cell_h;
} CONSOLE;

void console_init(CONSOLE *con, BOOT_INFO *boot_info, uint32_t fg, uint32_t bg);
void console_clear(CONSOLE *con);
void console_set_colors(CONSOLE *con, uint32_t fg, uint32_t bg);

void console_putchar(CONSOLE *con, char c);
void console_write(CONSOLE *con, const char *s);
void console_write_len(CONSOLE *con, const char *s, uint32_t len);
void console_backspace(CONSOLE *con);

void console_write_hex(CONSOLE *con, uint64_t value);
void console_write_dec(CONSOLE *con, uint64_t value);
void console_write_udec(CONSOLE *con, uint64_t value);
void console_write_ptr(CONSOLE *con, const void *ptr);

void console_vprintf(CONSOLE *con, const char *fmt, va_list args);
void console_printf(CONSOLE *con, const char *fmt, ...);

#endif

