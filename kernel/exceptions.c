#include "exceptions.h"
#include "panic.h"
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
        case 0:  return "Divide Error";
        case 1:  return "Debug";
        case 2:  return "NMI";
        case 3:  return "Breakpoint";
        case 4:  return "Overflow";
        case 5:  return "BOUND Range Exceeded";
        case 6:  return "Invalid Opcode";
        case 7:  return "Device Not Available";
        case 8:  return "Double Fault";
        case 9:  return "Coprocessor Segment Overrun";
        case 10: return "Invalid TSS";
        case 11: return "Segment Not Present";
        case 12: return "Stack-Segment Fault";
        case 13: return "General Protection Fault";
        case 14: return "Page Fault";
        case 16: return "x87 Floating-Point Error";
        case 17: return "Alignment Check";
        case 18: return "Machine Check";
        case 19: return "SIMD Floating-Point Exception";
        case 20: return "Virtualization Exception";
        case 21: return "Control Protection Exception";
        case 28: return "Hypervisor Injection Exception";
        case 29: return "VMM Communication Exception";
        case 30: return "Security Exception";
        default: return "Unknown Exception";
    }
}

static void dump_page_fault_details(uint64_t error_code) {
    if (!g_console) {
        return;
    }

    console_printf(g_console, "page fault address (CR2): %p\n",
                   (void*)(uintptr_t)read_cr2());
    console_printf(g_console, "page fault flags: [");
    console_printf(g_console, "%s", (error_code & 0x01) ? "P" : "NP");
    console_printf(g_console, " %s", (error_code & 0x02) ? "WRITE" : "READ");
    console_printf(g_console, " %s", (error_code & 0x04) ? "USER" : "KERNEL");
    if (error_code & 0x08) {
        console_printf(g_console, " RSVD");
    }
    if (error_code & 0x10) {
        console_printf(g_console, " INSTR");
    }
    if (error_code & 0x20) {
        console_printf(g_console, " PK");
    }
    if (error_code & 0x40) {
        console_printf(g_console, " SS");
    }
    if (error_code & 0x8000) {
        console_printf(g_console, " SGX");
    }
    console_printf(g_console, "]\n");
}

void exceptions_set_console(CONSOLE *con) {
    g_console = con;
}

__attribute__((noreturn))
void handle_exception(INTERRUPT_FRAME *frame) {
    uint64_t vec;

    panic_set_stage("cpu exception");

    if (!frame) {
        panic("interrupt_dispatch received NULL frame");
    }

    vec = frame->vector;

    if (g_console) {
        console_printf(g_console, "\n");
        console_printf(g_console, "*** CPU EXCEPTION ***\n");
        console_printf(g_console, "vector: %u\n", (unsigned int)vec);
        console_printf(g_console, "name:   %s\n", exception_name(vec));
        console_printf(g_console, "error:  %p\n", (void*)(uintptr_t)frame->error_code);
        console_printf(g_console, "rip:    %p\n", (void*)(uintptr_t)frame->rip);
        console_printf(g_console, "cs:     %p\n", (void*)(uintptr_t)frame->cs);
        console_printf(g_console, "rflags: %p\n", (void*)(uintptr_t)frame->rflags);
        console_printf(g_console, "rax:    %p\n", (void*)(uintptr_t)frame->rax);
        console_printf(g_console, "rbx:    %p\n", (void*)(uintptr_t)frame->rbx);
        console_printf(g_console, "rcx:    %p\n", (void*)(uintptr_t)frame->rcx);
        console_printf(g_console, "rdx:    %p\n", (void*)(uintptr_t)frame->rdx);
        console_printf(g_console, "rbp:    %p\n", (void*)(uintptr_t)frame->rbp);
        console_printf(g_console, "rdi:    %p\n", (void*)(uintptr_t)frame->rdi);
        console_printf(g_console, "rsi:    %p\n", (void*)(uintptr_t)frame->rsi);
        console_printf(g_console, "r8:     %p\n", (void*)(uintptr_t)frame->r8);
        console_printf(g_console, "r9:     %p\n", (void*)(uintptr_t)frame->r9);
        console_printf(g_console, "r10:    %p\n", (void*)(uintptr_t)frame->r10);
        console_printf(g_console, "r11:    %p\n", (void*)(uintptr_t)frame->r11);
        console_printf(g_console, "r12:    %p\n", (void*)(uintptr_t)frame->r12);
        console_printf(g_console, "r13:    %p\n", (void*)(uintptr_t)frame->r13);
        console_printf(g_console, "r14:    %p\n", (void*)(uintptr_t)frame->r14);
        console_printf(g_console, "r15:    %p\n", (void*)(uintptr_t)frame->r15);

        if (vec == 14) {
            dump_page_fault_details(frame->error_code);
        }
    }

    panicf("Unhandled CPU exception %u (%s)",
           (unsigned int)vec,
           exception_name(vec));
}
