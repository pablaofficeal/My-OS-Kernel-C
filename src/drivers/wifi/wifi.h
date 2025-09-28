// src/drivers/wifi/wifi.h
#ifndef WIFI_H
#define WIFI_H

#include "../pci/pci.h"
#include "../../lib/stddef.h"

#define WIFI_DEBUG 1

// Wi-Fi стандарты
typedef enum {
    WIFI_STANDARD_80211A = 0,
    WIFI_STANDARD_80211B,
    WIFI_STANDARD_80211G,
    WIFI_STANDARD_80211N,
    WIFI_STANDARD_80211AC,
    WIFI_STANDARD_80211AX
} wifi_standard_t;

// Состояния Wi-Fi адаптера
typedef enum {
    WIFI_STATE_DISABLED = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

// Структура Wi-Fi сети
typedef struct {
    char ssid[32];
    uint8_t bssid[6];
    int signal_strength;  // dBm
    wifi_standard_t standard;
    int channel;
    int encryption;  // 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3
} wifi_network_t;

// Структура Wi-Fi адаптера
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t base_addr;
    wifi_state_t state;
    uint8_t mac_address[6];
    wifi_network_t current_network;
    int networks_found;
    wifi_network_t networks[20];
} wifi_adapter_t;

// Основные функции Wi-Fi
int wifi_init(void);
int wifi_scan_networks(void);
int wifi_connect(const char* ssid, const char* password);
int wifi_disconnect(void);
int wifi_get_status(wifi_adapter_t* status);
void wifi_print_networks(void);

// Intel AX210 специфичные функции
int ax210_detect(void);
int ax210_init(void);
int ax210_send_command(uint16_t command, void* data, size_t len);
int ax210_receive_response(void* buffer, size_t len);

#endif