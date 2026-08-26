/* linker.ld - Working script for GRUB multiboot */
ENTRY(_start)

SECTIONS
{
    /* Start loading at 0x100000 (1MB) - standard multiboot load address */
    . = 0x100000;

    /* Multiboot header MUST be within the first 8KB of the file for GRUB to find */
    .multiboot ALIGN(4) : {
        KEEP(*(.multiboot))
    }
    
    /* Multiboot2 header for UEFI support */
    .multiboot2 ALIGN(8) : {
        KEEP(*(.multiboot2))
    }

    /* Executable code section */
    .text : {
        *(.text)
    }

    /* Read-only data section */
    .rodata BLOCK(4) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Writable data section */
    .data BLOCK(4) : {
        *(.data)
        *(.data.*)
    }
    
    /* Large kernel data section */
    .kernel_data BLOCK(4) : {
        *(.kernel_data)
    }

    /* Zero-initialized BSS section */
    .bss BLOCK(4) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Discard unnecessary sections to reduce file size */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}