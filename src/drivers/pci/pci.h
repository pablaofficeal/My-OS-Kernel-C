#pragma once

#include <stdint.h>

struct pci_device_info {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t revision;
};

typedef void (*pci_device_visitor)(const struct pci_device_info *device, void *context);

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset);
uint64_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t function,
                      uint8_t bar_index);
void pci_enumerate(pci_device_visitor visitor, void *context);
