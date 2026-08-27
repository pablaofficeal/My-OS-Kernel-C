#pragma once

#include <stdbool.h>
#include <stdint.h>

bool boot_get_kernel_image(const void **address, uint32_t *size);
bool boot_get_efi_loader(const void **address, uint32_t *size);
