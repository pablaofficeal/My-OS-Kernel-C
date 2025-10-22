/* linker.ld - исправленная версия */
ENTRY(_start)

SECTIONS
{
    . = 0x100000; /* Загрузка по адресу 1MB */

    /* Multiboot заголовок должен быть в первых 8KB */
    .multiboot BLOCK(4) : {
        *(.multiboot)
    }

    .text BLOCK(4) : {
        *(.text)
    }

    .rodata BLOCK(4) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data BLOCK(4) : {
        *(.data)
        *(.data.*)
    }

    .bss BLOCK(4) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}