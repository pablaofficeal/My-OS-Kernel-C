#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../storage/storage_types.h"

struct xhci_probe_stats {
    uint32_t controllers;
    uint32_t connected_ports;
    uint32_t addressed_devices;
    uint32_t mass_storage_devices;
    uint32_t failures;
};

void xhci_set_address_mapping(uint64_t hhdm_offset, uint64_t kernel_physical_base,
                              uint64_t kernel_virtual_base);
bool xhci_init(uint32_t linux_name_base);
uint32_t xhci_device_count(void);
bool xhci_get_device_info(uint32_t index, struct storage_device_info *info);
bool xhci_select_device(uint32_t index);
bool xhci_read_sector(uint32_t lba, void *buffer);
bool xhci_write_sector(uint32_t lba, const void *buffer);
const char *xhci_device_name(void);
void xhci_get_probe_stats(struct xhci_probe_stats *stats);
