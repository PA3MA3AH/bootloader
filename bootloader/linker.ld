OUTPUT_FORMAT("pei-x86-64")
ENTRY(efi_main)

SECTIONS
{
    . = 0;

    .text : {
        *(.text*)
        *(.rdata*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.rela*)
        *(.rel*)
    }
}
