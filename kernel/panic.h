#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include "console.h"
#include <stdint.h>

void panic_set_console(CONSOLE *con);
void panic_set_stage(const char *stage);

__attribute__((noreturn)) void panic(const char *message);
__attribute__((noreturn)) void panicf(const char *fmt, ...);

#define ASSERT(expr)                                                         \
    do {                                                                     \
        if (!(expr)) {                                                       \
            panicf("ASSERT FAILED: %s at %s:%d", #expr, __FILE__, __LINE__); \
        }                                                                    \
    } while (0)

#define PANIC(msg) panic(msg)

#endif
