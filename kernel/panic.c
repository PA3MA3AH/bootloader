#include "panic.h"
#include "console.h"
#include "crashlog.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static CONSOLE *g_panic_console = NULL;
static const char *g_panic_stage = "unknown";

void panic_set_console(CONSOLE *con) {
    g_panic_console = con;
}

void panic_set_stage(const char *stage) {
    g_panic_stage = stage ? stage : "unknown";
    crashlog_set_stage(g_panic_stage);
}

static void panic_disable_interrupts(void) {
    __asm__ volatile ("cli");
}

static __attribute__((noreturn)) void panic_halt_forever(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void panic_draw_banner(void) {
    if (!g_panic_console) {
        return;
    }

    console_printf(g_panic_console, "\n");
    console_printf(g_panic_console, "========================================\n");
    console_printf(g_panic_console, "              KERNEL PANIC              \n");
    console_printf(g_panic_console, "========================================\n");
    console_printf(g_panic_console, "Stage: %s\n", g_panic_stage);
    console_printf(g_panic_console, "----------------------------------------\n");
}

__attribute__((noreturn))
void panic(const char *message) {
    panic_disable_interrupts();
    crashlog_mark_panic(message ? message : "unknown panic");

    if (g_panic_console) {
        panic_draw_banner();
        console_printf(g_panic_console, "%s\n", message ? message : "unknown panic");
        console_printf(g_panic_console, "\nSystem halted.\n");
    }

    panic_halt_forever();
}

__attribute__((noreturn))
void panicf(const char *fmt, ...) {
    panic_disable_interrupts();
    crashlog_mark_panic("formatted kernel panic");

    if (g_panic_console) {
        va_list args;

        panic_draw_banner();

        va_start(args, fmt);
        console_vprintf(g_panic_console, fmt, args);
        va_end(args);

        console_printf(g_panic_console, "\n\nSystem halted.\n");
    }

    panic_halt_forever();
}
