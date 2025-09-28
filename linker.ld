/* linker.ld - для Multiboot ISO */
ENTRY(_start)

SECTIONS
{
    . = 0x100000; /* Загрузка по адресу 1MB */

    /* Multiboot header должен быть в первых 8KB */
    .text : {
        *(.multiboot)
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}