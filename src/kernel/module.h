#pragma once
#include <stdbool.h>
#include <stdint.h>
void modules_init(void);
bool module_load_from_memory(const void *data, uint64_t size);
bool module_load_from_limine(void);
