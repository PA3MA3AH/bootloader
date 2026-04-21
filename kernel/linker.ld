ENTRY(kernel_main)

SECTIONS
{
    . = 0x0000000001800000;

    .text : ALIGN(4096) {
        *(.text*)
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(.bss*)
        *(COMMON)
    }
}
