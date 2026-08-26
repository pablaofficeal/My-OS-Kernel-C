/* linker.ld - 64-bit Limine kernel at 1M physical = virtual (simple base) */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;

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
