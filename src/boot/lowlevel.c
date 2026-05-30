// lowlevel.c - Обертка для низкоуровневых функций на ассемблере

#include <stdint.h>

// Глобальные переменные для Multiboot информации
static void* multiboot_info_ptr = 0;

// Объявления функций из ассемблера
extern void simple_print(const char* str);
extern void* get_multiboot_info_from_ebx(void);
extern void init_stack(void);
extern void halt_cpu(void);

// Сохранение Multiboot info pointer
void save_multiboot_info(void* mb_info) {
    multiboot_info_ptr = mb_info;
}

// Функция для получения указателя на Multiboot info
void* get_multiboot_info(void) {
    return multiboot_info_ptr;
}

