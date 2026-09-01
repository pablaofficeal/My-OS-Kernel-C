#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "drivers/pci/pci.h"
#include "net/core/net_device.h"
#include "net/wifi/wifi.h"
#include "ar928x_reg.h"
#define AR928X_VENDOR_ATHEROS 0x168c
struct ar928x_desc {
    uint32_t ds_link;
    uint32_t ds_data;
    uint32_t ds_ctl0;
    uint32_t ds_ctl1;
    uint32_t ds_hw[4];
} __attribute__((packed));
struct ar928x_device {
    volatile uint8_t *regs;
    struct pci_device_info pci;
    struct net_device net;
    bool hardware_found;
    bool mmio_mapped;
    bool ready;
    bool associated;
    bool eeprom_valid;
    uint8_t mac[6];
    char hw_info[64];
    uint16_t srev_version;
    uint8_t srev_rev;
    uint32_t srev_raw;
    uint64_t scan_start_ms;
    uint64_t connect_start_ms;
    char connect_ssid[WIFI_SSID_MAX+1];
    char connect_password[WIFI_PASSWORD_MAX+1];
    bool connect_pending;
    uint64_t tx_ring_phys;
    uint64_t rx_ring_phys;
    struct ar928x_desc *tx_ring;
    struct ar928x_desc *rx_ring;
    uint64_t tx_buf_phys[AR928X_RING_COUNT];
    uint64_t rx_buf_phys[AR928X_RING_COUNT];
    uint8_t *tx_bufs[AR928X_RING_COUNT];
    uint8_t *rx_bufs[AR928X_RING_COUNT];
    uint16_t tx_next;
    uint16_t rx_next;
    volatile bool tx_locked;
};
extern struct ar928x_device g_ar928x;
extern bool g_ar928x_initialized;
bool ar928x_module_init(void);
void ar928x_module_exit(void);
bool ar928x_has_hardware(void);
const char *ar928x_hardware_info(void);
