#include <stdint.h>
#include "../common/bootinfo.h"
#include "console.h"

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info) {
    CONSOLE con;
    console_init(&con, boot_info, 0x00FF4444, 0x00000000);
    console_clear(&con);

    console_printf(&con, "RESCUE kernel\n");
    console_printf(&con, "Emergency framebuffer console online.\n");

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
