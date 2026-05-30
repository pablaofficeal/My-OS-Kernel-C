// Multiboot заголовок - ДОЛЖЕН БЫТЬ ПЕРВЫМ!
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003  // ALIGN | MEM_INFO
#define MULTIBOOT_HEADER_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Multiboot заголовок структура
__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
__attribute__((used))
__attribute__((externally_visible))
const unsigned int multiboot_header[] = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    MULTIBOOT_HEADER_CHECKSUM
};

// Multiboot2 заголовок для UEFI - byte-packed
__attribute__((section(".multiboot2")))
__attribute__((aligned(8)))
__attribute__((used))
__attribute__((externally_visible))
const unsigned char multiboot2_header[] = {
    // Header
    0xd6, 0x50, 0x52, 0xe8, // magic (0xe85250d6)
    0x00, 0x00, 0x00, 0x00, // architecture (0)
    0x20, 0x00, 0x00, 0x00, // header_length (32)
    0x2a, 0xaf, 0xad, 0x17, // checksum (-(magic+arch+length))
    // Console tag (type 2, size 12, flags 3)
    0x02, 0x00, 0x00, 0x00, // tag_type = 2
    0x0c, 0x00, 0x00, 0x00, // tag_size = 12
    0x03, 0x00, 0x00, 0x00, // flags = 3
    // End tag (type 0, size 8)
    0x00, 0x00, 0x00, 0x00, // tag_type = 0
    0x08, 0x00, 0x00, 0x00  // tag_size = 8
};

// Точка входа для ядра ОС

extern void kernel_main(void);
#include "boot/lowlevel.h"

// Функция _start - точка входа, требуемая линковщиком
void _start(void) {
    // ВАЖНО: Сначала инициализируем стек, иначе вызовы функций не будут работать!
    // Сохраняем Multiboot info pointer из EBX перед изменением стека
    void* mb_info = get_multiboot_info_from_ebx();
    
    // Инициализация стека (должна быть первой!)
    init_stack();
    
    // Теперь можно вызывать функции
    save_multiboot_info(mb_info);
    simple_print("_start called!\n");
    simple_print("Stack initialized\n");
    
    // Вызываем основную функцию ядра
    simple_print("Calling kernel_main...\n");
    kernel_main();
    
    simple_print("kernel_main returned!\n");
    
    // Бесконечный цикл после завершения kernel_main
    while(1) {
        halt_cpu();
    }
}