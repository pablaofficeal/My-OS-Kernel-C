#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t base_addresses[6];
} pci_device_t;

// PCI функции
int pci_scan_bus(void);
pci_device_t* pci_get_device(uint16_t vendor_id, uint16_t device_id);
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

#endif