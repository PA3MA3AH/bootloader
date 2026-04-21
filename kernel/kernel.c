#include <stdint.h>
#include "../common/bootinfo.h"
#include "console.h"
#include "memory_map.h"
#include "pmm.h"
#include "keyboard.h"
#include "shell.h"
#include "interrupts.h"
#include "panic.h"
#include "crashlog.h"
#include "kheap.h"

#define KERNEL_PIT_HZ 100

static void kernel_print_banner(CONSOLE *con) {
    console_printf(con, "========================================\n");
    console_printf(con, "MyOS kernel started\n");
    console_printf(con, "Early kernel initialization\n");
    console_printf(con, "========================================\n\n");
}

static void kernel_print_boot_info(CONSOLE *con, BOOT_INFO *boot_info) {
    console_printf(con, "Boot info:\n");
    console_printf(con, "  framebuffer base:         %p\n", (void*)(uintptr_t)boot_info->framebuffer_base);
    console_printf(con, "  framebuffer size:         %u\n", (unsigned int)boot_info->framebuffer_size);
    console_printf(con, "  resolution:               %u x %u\n",
                   (unsigned int)boot_info->framebuffer_width,
                   (unsigned int)boot_info->framebuffer_height);
    console_printf(con, "  pixels per scanline:      %u\n",
                   (unsigned int)boot_info->framebuffer_pixels_per_scanline);
    console_printf(con, "  usable memory bytes:      %u\n",
                   (unsigned int)boot_info->usable_memory_bytes);
    console_printf(con, "  chosen region base:       %p\n",
                   (void*)(uintptr_t)boot_info->chosen_region_base);
    console_printf(con, "  chosen region size:       %u\n",
                   (unsigned int)boot_info->chosen_region_size);
    console_printf(con, "  kernel phys base:         %p\n",
                   (void*)(uintptr_t)boot_info->kernel_phys_base);
    console_printf(con, "  kernel reserved size:     %u\n",
                   (unsigned int)boot_info->kernel_reserved_size);
    console_printf(con, "  kernel file size:         %u\n",
                   (unsigned int)boot_info->kernel_file_size);
    console_printf(con, "  bootinfo phys:            %p\n",
                   (void*)(uintptr_t)boot_info->bootinfo_phys);
    console_printf(con, "  bootinfo size:            %u\n",
                   (unsigned int)boot_info->bootinfo_size);
    console_printf(con, "  scratch phys:             %p\n",
                   (void*)(uintptr_t)boot_info->scratch_phys);
    console_printf(con, "  scratch size:             %u\n",
                   (unsigned int)boot_info->scratch_size);
    console_printf(con, "  kernel stack bottom:      %p\n",
                   (void*)(uintptr_t)boot_info->kernel_stack_bottom);
    console_printf(con, "  kernel stack top:         %p\n",
                   (void*)(uintptr_t)boot_info->kernel_stack_top);
    console_printf(con, "  heap base:                %p\n",
                   (void*)(uintptr_t)boot_info->heap_base);
    console_printf(con, "  heap size:                %u\n",
                   (unsigned int)boot_info->heap_size);
    console_printf(con, "  crash info phys:          %p\n",
                   (void*)(uintptr_t)boot_info->crash_info_phys);
    console_printf(con, "  crash info size:          %u\n",
                   (unsigned int)boot_info->crash_info_size);
    console_printf(con, "  memory map:               %p\n",
                   (void*)(uintptr_t)boot_info->memory_map);
    console_printf(con, "  memory map size:          %u\n",
                   (unsigned int)boot_info->memory_map_size);
    console_printf(con, "  descriptor size:          %u\n",
                   (unsigned int)boot_info->memory_descriptor_size);
    console_printf(con, "  descriptor version:       %u\n\n",
                   (unsigned int)boot_info->memory_descriptor_version);
}

static void kernel_print_runtime_plan(CONSOLE *con) {
    console_printf(con, "Runtime mode:\n");
    console_printf(con, "  IDT:                      enabled\n");
    console_printf(con, "  exceptions:               enabled\n");
    console_printf(con, "  external IRQs:            IRQ1 keyboard enabled, IRQ0 optional\n");
    console_printf(con, "  keyboard input:           IRQ1 + ring buffer\n");
    console_printf(con, "  PIT timer:                prepared but not enabled\n");
    console_printf(con, "  kernel heap:              enabled (kmalloc/kfree)\n\n");
}

static void kernel_validate_boot_info(BOOT_INFO *boot_info) {
    panic_set_stage("boot validation");

    if (!boot_info) {
        panic("boot_info is NULL");
    }

    if (boot_info->framebuffer_base == 0) {
        panic("framebuffer_base is 0");
    }

    if (boot_info->framebuffer_width == 0 || boot_info->framebuffer_height == 0) {
        panicf("framebuffer dimensions are invalid: %u x %u",
               (unsigned int)boot_info->framebuffer_width,
               (unsigned int)boot_info->framebuffer_height);
    }

    if (boot_info->framebuffer_pixels_per_scanline == 0) {
        panic("framebuffer_pixels_per_scanline is 0");
    }

    if (boot_info->framebuffer_size == 0) {
        panic("framebuffer_size is 0");
    }

    if (boot_info->memory_map == 0) {
        panic("memory_map is missing");
    }

    if (boot_info->memory_map_size == 0) {
        panic("memory_map_size is 0");
    }

    if (boot_info->memory_descriptor_size == 0) {
        panic("memory_descriptor_size is 0");
    }

    if (boot_info->kernel_stack_bottom == 0 || boot_info->kernel_stack_top == 0) {
        panicf("kernel stack is invalid: bottom=%p top=%p",
               (void*)(uintptr_t)boot_info->kernel_stack_bottom,
               (void*)(uintptr_t)boot_info->kernel_stack_top);
    }

    if (boot_info->kernel_stack_bottom >= boot_info->kernel_stack_top) {
        panicf("kernel stack range is invalid: bottom=%p top=%p",
               (void*)(uintptr_t)boot_info->kernel_stack_bottom,
               (void*)(uintptr_t)boot_info->kernel_stack_top);
    }

    if (boot_info->heap_base == 0) {
        panic("heap_base is 0");
    }

    if (boot_info->heap_size == 0) {
        panic("heap_size is 0");
    }

    if (boot_info->usable_memory_bytes == 0) {
        panic("usable_memory_bytes is 0");
    }

    if (boot_info->kernel_phys_base == 0) {
        panic("kernel_phys_base is 0");
    }

    if (boot_info->kernel_reserved_size == 0) {
        panic("kernel_reserved_size is 0");
    }

    if (boot_info->bootinfo_phys == 0) {
        panic("bootinfo_phys is 0");
    }

    if (boot_info->bootinfo_size == 0) {
        panic("bootinfo_size is 0");
    }

    if (boot_info->crash_info_phys == 0) {
        panic("crash_info_phys is 0");
    }

    if (boot_info->crash_info_size < 4096) {
        panic("crash_info_size is too small");
    }
}

static void kernel_init_interrupt_core(CONSOLE *con) {
    panic_set_stage("interrupt core init");

    console_printf(con, "[1/5] Interrupt core init...\n");

    interrupts_disable();
    interrupts_set_console(con);
    interrupts_init();

    console_printf(con, "      IDT loaded, PIC remapped.\n");
    console_printf(con, "      External IRQ lines masked.\n\n");
}

static void kernel_init_memory(CONSOLE *con, BOOT_INFO *boot_info, PMM_STATE *pmm) {
    panic_set_stage("memory subsystem init");

    console_printf(con, "[2/5] Memory subsystem init...\n");

    dump_memory_map(con, boot_info);
    pmm_init(pmm, boot_info);
    pmm_dump(con, pmm);

    console_printf(con, "      PMM initialized successfully.\n\n");
}

static void kernel_init_heap(CONSOLE *con, BOOT_INFO *boot_info) {
    KHEAP_STATS stats;

    panic_set_stage("heap init");

    console_printf(con, "[3/5] Kernel heap init...\n");

    kheap_init(boot_info->heap_base, boot_info->heap_size);

    if (!kheap_is_initialized()) {
        panic("kheap_init failed");
    }

    kheap_get_stats(&stats);

    console_printf(con, "      Heap base: %p\n", (void*)(uintptr_t)stats.heap_base);
    console_printf(con, "      Heap size: %u bytes\n", (unsigned int)stats.heap_size);
    console_printf(con, "      Heap is ready.\n\n");
}

static void kernel_init_input(CONSOLE *con) {
    panic_set_stage("input subsystem init");

    console_printf(con, "[4/5] Input subsystem init...\n");

    keyboard_init();

    irq_register_handler(1, keyboard_irq_handler);
    irq_unmask(1);

    console_printf(con, "      PS/2 keyboard ready (IRQ1 + ring buffer).\n\n");
}

static void kernel_init_shell(CONSOLE *con,
                              BOOT_INFO *boot_info,
                              PMM_STATE *pmm,
                              SHELL *sh) {
    panic_set_stage("shell init");

    console_printf(con, "[5/5] Shell init...\n");

    shell_init(sh, con, boot_info, pmm);

    console_printf(con, "      Mini shell ready.\n\n");
}

static void kernel_prepare_timer(CONSOLE *con) {
    panic_set_stage("timer prepare");

    console_printf(con, "[timer] PIT prepare...\n");
    timer_init(KERNEL_PIT_HZ);
    console_printf(con, "        PIT configured to %u Hz but not armed.\n\n",
                   (unsigned int)KERNEL_PIT_HZ);
}

static __attribute__((noreturn))
void kernel_run_shell(SHELL *sh) {
    panic_set_stage("shell runtime");
    crashlog_mark_stable();

    console_printf(sh->con, "System is stable.\n");
    console_printf(sh->con, "Type 'help' for commands.\n");
    shell_prompt(sh);

    interrupts_enable();

    for (;;) {
        char ch = keyboard_getchar();
        shell_handle_char(sh, ch);
    }
}

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info) {
    CONSOLE con;
    PMM_STATE pmm;
    SHELL sh;

    if (!boot_info) {
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    crashlog_init(boot_info);
    crashlog_mark_booting();

    console_init(&con, boot_info, 0x00FFFFFF, 0x00000000);
    panic_set_console(&con);
    console_clear(&con);

    kernel_validate_boot_info(boot_info);

    panic_set_stage("kernel startup");

    kernel_print_banner(&con);
    kernel_print_boot_info(&con, boot_info);
    kernel_print_runtime_plan(&con);

    kernel_init_interrupt_core(&con);
    kernel_init_memory(&con, boot_info, &pmm);
    kernel_init_heap(&con, boot_info);
    kernel_init_input(&con);
    kernel_init_shell(&con, boot_info, &pmm, &sh);
    kernel_prepare_timer(&con);

    kernel_run_shell(&sh);
}
