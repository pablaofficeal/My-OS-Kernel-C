/* linker.ld - исправленная версия */
ENTRY(_start)

SECTIONS
{
    . = 0x100000; /* Загрузка по адресу 1MB */

    .text : ALIGN(4) {
        *(.multiboot)
        *(.text)
    }

    .data : ALIGN(4) {
        *(.data)
    }

    .bss : ALIGN(4) {
        *(COMMON)
        *(.bss)
    }
}