// lowlevel.h - Заголовочный файл для низкоуровневых функций

#ifndef LOWLEVEL_H
#define LOWLEVEL_H

#include <stdint.h>

// Простой вывод в VGA text mode
void simple_print(const char* str);

// Работа с Multiboot
void save_multiboot_info(void* mb_info);
void* get_multiboot_info_from_ebx(void);
void* get_multiboot_info(void);

// Инициализация
void init_stack(void);

// Управление процессором
void halt_cpu(void);

#endif // LOWLEVEL_H

