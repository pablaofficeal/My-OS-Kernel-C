/* linker.ld - 64-bit higher-half Limine kernel */
ENTRY(_start)

SECTIONS
{
    . = 0xffffffff80000000;

    .requests : {
        KEEP(*(.requests))
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests_end_marker))
    }

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
