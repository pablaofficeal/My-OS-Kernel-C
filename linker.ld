/* linker.ld - скрипт для flat binary с Multiboot */
ENTRY(_start)

SECTIONS
{
    /* Начинаем с адреса 0x100000 (1MB) - стандартный адрес загрузки */
    . = 0x100000;

    /* Multiboot заголовок ДОЛЖЕН быть в первых 8KB от начала файла */
    .multiboot : {
        KEEP(*(.multiboot))
    }
    
    /* Multiboot2 заголовок для UEFI */
    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    /* Код и данные */
    .text : {
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