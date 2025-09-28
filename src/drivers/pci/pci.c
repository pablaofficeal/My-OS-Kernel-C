#include "pci.h"
#include "../screen.h"

static pci_device_t pci_devices[32];
static int pci_device_count = 0;

// Базовая эмуляция PCI
int pci_scan_bus(void) {
    printf("PCI: Scanning bus...\n");
    
    // Эмулируем несколько устройств
    pci_device_count = 2;
    
    // Эмулируем Intel AX210
    pci_devices[0].vendor_id = 0x8086;
    pci_devices[0].device_id = 0x2725;
    pci_devices[0].class_code = 0x02;  // Network controller
    pci_devices[0].subclass = 0x80;    // Other
    pci_devices[0].base_addresses[0] = 0xFEB00000;
    
    // Эмулируем видеокарту
    pci_devices[1].vendor_id = 0x1234;
    pci_devices[1].device_id = 0x1111;
    pci_devices[1].class_code = 0x03;  // Display controller
    
    printf("PCI: Found %d devices\n", pci_device_count);
    return pci_device_count;
}

pci_device_t* pci_get_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && 
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}