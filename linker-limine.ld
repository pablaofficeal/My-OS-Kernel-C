/* linker-limine.ld - Higher half for Limine (GOP) */
ENTRY(_start)
SECTIONS
{
    . = 0xffffffff80000000;
    .requests : AT(ADDR(.requests) - 0xffffffff80000000 + 0x100000) {
        KEEP(*(.requests))
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests_end_marker))
    }
    .text : AT(ADDR(.text) - 0xffffffff80000000 + 0x100000) {
        *(.text .text.*)
    }
    .rodata : AT(ADDR(.rodata) - 0xffffffff80000000 + 0x100000) {
        *(.rodata .rodata.*)
    }
    .data : AT(ADDR(.data) - 0xffffffff80000000 + 0x100000) {
        *(.data .data.*)
    }
    .bss : AT(ADDR(.bss) - 0xffffffff80000000 + 0x100000) {
        *(COMMON)
        *(.bss .bss.*)
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
