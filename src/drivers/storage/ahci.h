#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "storage_types.h"

void ahci_set_address_mapping(uint64_t hhdm_offset, uint64_t kernel_physical_base,
                              uint64_t kernel_virtual_base);
bool ahci_init(uint32_t linux_name_base);
uint32_t ahci_device_count(void);
bool ahci_get_device_info(uint32_t index, struct storage_device_info *info);
