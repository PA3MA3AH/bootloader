#include "exceptions.h"
#include "panic.h"
#include "crashlog.h"
#include <stdint.h>
#include <stddef.h>

static CONSOLE *g_console = 0;

static uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static const char *exception_name(uint64_t vec) {
    switch (vec) {
        case 14: return "Page Fault";
        case 13: return "General Protection Fault";
        case 6:  return "Invalid Opcode";
        case 0:  return "Divide Error";
        case 8:  return "Double Fault";
        default: return "Unknown Exception";
    }
}

/* 🔥 НОВОЕ: нормальный разбор PF */
static void dump_page_fault_details(uint64_t error_code) {
    uint64_t cr2 = read_cr2();

    if (!g_console) return;

    console_printf(g_console, "\n=== PAGE FAULT ANALYSIS ===\n");
    console_printf(g_console, "fault addr (CR2): %p\n", (void*)cr2);

    console_printf(g_console, "type: ");

    if (error_code & 0x1)
        console_printf(g_console, "protection violation");
    else
        console_printf(g_console, "non-present page");

    console_printf(g_console, "\naccess: ");

    if (error_code & 0x2)
        console_printf(g_console, "write");
    else
        console_printf(g_console, "read");

    console_printf(g_console, "\nmode: ");

    if (error_code & 0x4)
        console_printf(g_console, "user");
    else
        console_printf(g_console, "kernel");

    if (error_code & 0x8)
        console_printf(g_console, "\nRSVD bit violation");

    if (error_code & 0x10)
        console_printf(g_console, "\ninstruction fetch");

    console_printf(g_console, "\n===========================\n");

    /* 🔥 логируем в crashlog */
    crashlog_set_action("page fault", "invalid memory access");
}

/* 🔥 НОВОЕ: проверка типичных причин */
static void analyze_common_faults(uint64_t addr) {
    if (!g_console) return;

    if (addr == 0) {
        console_printf(g_console, "HINT: NULL pointer dereference\n");
    }

    if (addr < 0x1000) {
        console_printf(g_console, "HINT: low memory access (likely bug)\n");
    }

    if (addr >= 0x80000000ULL && addr < 0x100000000ULL) {
        console_printf(g_console, "HINT: high memory (MMIO / framebuffer?)\n");
    }
}

void exceptions_set_console(CONSOLE *con) {
    g_console = con;
}

__attribute__((noreturn))
void handle_exception(INTERRUPT_FRAME *frame) {
    uint64_t vec;

    panic_set_stage("cpu exception");

    if (!frame) {
        panic("NULL exception frame");
    }

    vec = frame->vector;

    if (g_console) {
        console_printf(g_console, "\n*** CPU EXCEPTION ***\n");
        console_printf(g_console, "vector: %u\n", (unsigned int)vec);
        console_printf(g_console, "name:   %s\n", exception_name(vec));

        console_printf(g_console, "rip:    %p\n", (void*)frame->rip);
        console_printf(g_console, "rsp:    %p\n", (void*)frame->rbp);
        console_printf(g_console, "error:  %p\n", (void*)frame->error_code);
    }

    if (vec == 14) {
        uint64_t addr = read_cr2();

        dump_page_fault_details(frame->error_code);
        analyze_common_faults(addr);

        panicf("PAGE FAULT at %p", (void*)addr);
    }

    panicf("Unhandled CPU exception %u (%s)",
           (unsigned int)vec,
           exception_name(vec));
}
