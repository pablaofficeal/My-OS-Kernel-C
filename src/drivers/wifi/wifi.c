#include "wifi.h"
#include "intel_ax210.h"
#include "../../lib/string.h"
#include "../screen.h"

static wifi_adapter_t wifi_adapter;
static int wifi_initialized = 0;

// Эмулированные сети для тестирования
static wifi_network_t test_networks[] = {
    {"Home_WiFi",        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x11}, -45, WIFI_STANDARD_80211AC, 36, 3},
    {"Free_WiFi",        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, -60, WIFI_STANDARD_80211N,  6, 0},
    {"Office_Network",   {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE}, -35, WIFI_STANDARD_80211AX, 44, 4},
    {"Guest_Network",    {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, -70, WIFI_STANDARD_80211G,  1, 2}
};
#define TEST_NETWORKS_COUNT 4

int wifi_init(void) {
    printf("WiFi: Initializing Wi-Fi subsystem...\n");
    
    // Пытаемся найти Intel AX210
    if (ax210_probe(NULL) == 0) {
        printf("WiFi: Intel AX210 found, initializing...\n");
        if (ax210_initialize() == 0) {
            printf("WiFi: Intel AX210 initialized successfully\n");
            wifi_adapter.vendor_id = INTEL_VENDOR_ID;
            wifi_adapter.device_id = AX210_DEVICE_ID;
            wifi_adapter.state = WIFI_STATE_DISCONNECTED;
            
            // Генерируем тестовый MAC адрес
            uint8_t test_mac[] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
            memcpy(wifi_adapter.mac_address, test_mac, 6);
        } else {
            printf("WiFi: Failed to initialize Intel AX210\n");
            return -1;
        }
    } else {
        printf("WiFi: No Intel AX210 found, using emulation mode\n");
        // Эмуляционный режим для тестирования
        wifi_adapter.vendor_id = 0x1234;
        wifi_adapter.device_id = 0x5678;
        wifi_adapter.state = WIFI_STATE_DISCONNECTED;
        
        uint8_t test_mac[] = {0x02, 0x00, 0x00, 0xAA, 0xBB, 0xCC};
        memcpy(wifi_adapter.mac_address, test_mac, 6);
    }
    
    wifi_initialized = 1;
    printf("WiFi: Wi-Fi subsystem ready\n");
    return 0;
}

int wifi_scan_networks(void) {
    if (!wifi_initialized) {
        printf("WiFi: Not initialized\n");
        return -1;
    }
    
    printf("WiFi: Scanning for networks...\n");
    wifi_adapter.state = WIFI_STATE_SCANNING;
    
    // Эмуляция сканирования
    for (int i = 0; i < 3; i++) {
        printf("WiFi: Scanning... %d/3\n", i + 1);
        // Имитация задержки сканирования
        for (volatile int j = 0; j < 1000000; j++);
    }
    
    // Копируем тестовые сети
    wifi_adapter.networks_found = TEST_NETWORKS_COUNT;
    for (int i = 0; i < TEST_NETWORKS_COUNT; i++) {
        memcpy(&wifi_adapter.networks[i], &test_networks[i], sizeof(wifi_network_t));
    }
    
    wifi_adapter.state = WIFI_STATE_DISCONNECTED;
    printf("WiFi: Scan complete, found %d networks\n", wifi_adapter.networks_found);
    
    return wifi_adapter.networks_found;
}

int wifi_connect(const char* ssid, const char* password) {
    if (!wifi_initialized) {
        printf("WiFi: Not initialized\n");
        return -1;
    }
    
    printf("WiFi: Connecting to '%s'...\n", ssid);
    wifi_adapter.state = WIFI_STATE_CONNECTING;
    
    // Ищем сеть в списке
    int network_index = -1;
    for (int i = 0; i < wifi_adapter.networks_found; i++) {
        if (strcmp(wifi_adapter.networks[i].ssid, ssid) == 0) {
            network_index = i;
            break;
        }
    }
    
    if (network_index == -1) {
        printf("WiFi: Network '%s' not found\n", ssid);
        wifi_adapter.state = WIFI_STATE_ERROR;
        return -1;
    }
    
    // Эмуляция процесса подключения
    printf("WiFi: Authenticating...\n");
    for (volatile int i = 0; i < 500000; i++);
    
    printf("WiFi: Getting IP address...\n");
    for (volatile int i = 0; i < 500000; i++);
    
    // Успешное подключение
    memcpy(&wifi_adapter.current_network, &wifi_adapter.networks[network_index], sizeof(wifi_network_t));
    wifi_adapter.state = WIFI_STATE_CONNECTED;
    
    printf("WiFi: Successfully connected to '%s'\n", ssid);
    printf("WiFi: Signal strength: %d dBm, Channel: %d\n", 
           wifi_adapter.current_network.signal_strength,
           wifi_adapter.current_network.channel);
    
    return 0;
}

int wifi_disconnect(void) {
    if (!wifi_initialized) {
        printf("WiFi: Not initialized\n");
        return -1;
    }
    
    if (wifi_adapter.state != WIFI_STATE_CONNECTED) {
        printf("WiFi: Not connected\n");
        return -1;
    }
    
    printf("WiFi: Disconnecting from '%s'...\n", wifi_adapter.current_network.ssid);
    
    // Эмуляция отключения
    for (volatile int i = 0; i < 100000; i++);
    
    memset(&wifi_adapter.current_network, 0, sizeof(wifi_network_t));
    wifi_adapter.state = WIFI_STATE_DISCONNECTED;
    
    printf("WiFi: Disconnected\n");
    return 0;
}

int wifi_get_status(wifi_adapter_t* status) {
    if (!wifi_initialized) {
        return -1;
    }
    
    memcpy(status, &wifi_adapter, sizeof(wifi_adapter_t));
    return 0;
}

void wifi_print_networks(void) {
    if (!wifi_initialized) {
        printf("WiFi: Not initialized\n");
        return;
    }
    
    if (wifi_adapter.networks_found == 0) {
        printf("WiFi: No networks found. Run 'wifi scan' first.\n");
        return;
    }
    
    printf("Available Wi-Fi networks:\n");
    printf("SSID               Signal  Channel  Security\n");
    printf("------------------ ------- -------- ---------\n");
    
    for (int i = 0; i < wifi_adapter.networks_found; i++) {
        wifi_network_t* net = &wifi_adapter.networks[i];
        const char* security;
        
        switch (net->encryption) {
            case 0: security = "Open"; break;
            case 1: security = "WEP"; break;
            case 2: security = "WPA"; break;
            case 3: security = "WPA2"; break;
            case 4: security = "WPA3"; break;
            default: security = "Unknown"; break;
        }
        
        printf("%-18s %3d dBm %4d     %s\n", 
               net->ssid, net->signal_strength, net->channel, security);
    }
}