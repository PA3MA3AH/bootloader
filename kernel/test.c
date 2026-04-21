#include <stdint.h>
#include "../common/bootinfo.h"
#include "console.h"

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info) {
    CONSOLE con;
    console_init(&con, boot_info, 0x0000FFFF, 0x00000000);
    console_clear(&con);

    console_printf(&con, "TEST kernel\n");
    console_printf(&con, "Framebuffer console online.\n");
    console_printf(&con, "Resolution: %u x %u\n",
                   (unsigned int)boot_info->framebuffer_width,
                   (unsigned int)boot_info->framebuffer_height);

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
