/* linker.ld - скрипт для flat binary с Multiboot */
ENTRY(_start)

SECTIONS
{
    /* Начинаем с адреса 0x100000 (1MB) - стандартный адрес загрузки */
    . = 0x100000;

    /* Multiboot заголовок ДОЛЖЕН быть в первых 8KB от начала файла */
    .multiboot ALIGN(4) : {
        KEEP(*(.multiboot))
    }
    
    /* Multiboot2 заголовок для UEFI */
    .multiboot2 ALIGN(8) : {
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
    
    /* Большие данные ядра */
    .kernel_data BLOCK(4) : {
        *(.kernel_data)
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