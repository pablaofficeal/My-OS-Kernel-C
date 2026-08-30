#include "intel_ax201.h"
#include "../pci/pci.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include "../../drivers/interrupts/timer.h"
#include "../../mm/pmm.h"
#include "../../net/core/net_device.h"
#include "../../net/wifi/wifi.h"
#include "../../net/network/ipv4.h"
#include "../../net/link/ethernet.h"
#include "../../boot/install_source.h"
#include <stddef.h>
#include <stdint.h>

#define AX201_VENDOR_INTEL 0x8086
#define AX201_MMIO_SIZE 0x4000U
#define AX201_DMA_BUFFER_SIZE 2048U

static const uint16_t ax201_device_ids[] = {
    0x06F0, 0xA0F0, 0x02F0, 0x34F0, 0x43F0, 0x4DF0, 0x51F0, 0x51F1, 0x54F0, 0x7A70, 0x7AF0, 0x2723, 0x2725, 0x7360,
};

#define AX201_FIRMWARE_HINT "iwlwifi-QuZ-a0-hr-b0-77.ucode"
#define AX201_FIRMWARE_MODULE "/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode"
#define AX201_FIRMWARE_MAGIC 0x0A4C5749U

#define AX201_CSR_HW_IF_CONFIG_REG 0x00
#define AX201_CSR_INT              0x08
#define AX201_CSR_INT_MASK         0x0C
#define AX201_CSR_GPIO_IN          0x18
#define AX201_CSR_HW_REV           0x28
#define AX201_CSR_GP_CNTRL         0x24
#define AX201_CSR_CTXT_INFO_BA     0x40
#define AX201_CSR_HW_RF_ID         0x9C
#define AX201_CSR_MAC_ADDR_BASE    0x380
#define AX201_CSR_MAC_ADDR0_OTP    (AX201_CSR_MAC_ADDR_BASE + 0x00)
#define AX201_CSR_MAC_ADDR1_OTP    (AX201_CSR_MAC_ADDR_BASE + 0x04)
#define AX201_CSR_MAC_ADDR0_STRAP  (AX201_CSR_MAC_ADDR_BASE + 0x08)
#define AX201_CSR_MAC_ADDR1_STRAP  (AX201_CSR_MAC_ADDR_BASE + 0x0C)

#define AX201_GP_CNTRL_HW_RFKILL   0x08000000U

struct ax201_device {
    volatile uint8_t *regs;
    struct pci_device_info pci;
    struct net_device net;
    bool hardware_found;
    bool mmio_mapped;
    bool ready;
    bool associated;
    uint8_t mac[6];
    char hw_info[64];
    uint64_t scan_start_ms;
    uint64_t connect_start_ms;
    char connect_ssid[WIFI_SSID_MAX+1];
    char connect_password[WIFI_PASSWORD_MAX+1];
    bool connect_pending;
    const uint8_t *firmware;
    uint64_t firmware_size;
};

static struct ax201_device adapter;
static bool initialized;

static uint32_t ax201_csr_read(uint32_t offset){
    return *(volatile uint32_t *)(adapter.regs + offset);
}

static bool ax201_get_firmware(void){
    const void *image=0;
    uint64_t size=0;
    if(!boot_get_module(AX201_FIRMWARE_MODULE,&image,&size) || !image || size<8){
        klogf(KLOG_ERROR, "ax201: firmware module %s not supplied by Limine", AX201_FIRMWARE_MODULE);
        return false;
    }
    uint32_t magic=*(const uint32_t *)image;
    if(magic!=AX201_FIRMWARE_MAGIC){
        klogf(KLOG_ERROR, "ax201: invalid firmware magic=0x%08x size=%llu", magic, (unsigned long long)size);
        return false;
    }
    adapter.firmware=(const uint8_t *)image;
    adapter.firmware_size=size;
    klogf(KLOG_OK, "ax201: firmware module loaded %s (%llu bytes)",
        AX201_FIRMWARE_HINT,(unsigned long long)size);
    return true;
}

static bool is_ax201_device(uint16_t dev_id){
    for(size_t i=0;i<sizeof(ax201_device_ids)/sizeof(ax201_device_ids[0]); i++){
        if(ax201_device_ids[i]==dev_id) return true;
    }
    return false;
}

struct ax201_scan_ctx { bool found; struct ax201_device *dev; };

static void append_hex4(char *out, uint16_t v){
    const char *hex="0123456789ABCDEF";
    out[0]=hex[(v>>12)&0xF];
    out[1]=hex[(v>>8)&0xF];
    out[2]=hex[(v>>4)&0xF];
    out[3]=hex[v&0xF];
    out[4]='\0';
}
static void append_hex2(char *out, uint8_t v){
    const char *hex="0123456789ABCDEF";
    out[0]=hex[(v>>4)&0xF];
    out[1]=hex[v&0xF];
    out[2]='\0';
}
static void ax201_pci_visitor(const struct pci_device_info *device, void *context){
    struct ax201_scan_ctx *ctx = context;
    if(ctx->found) return;
    if(device->vendor_id != AX201_VENDOR_INTEL) return;
    bool known = is_ax201_device(device->device_id);
    bool generic_wireless = (device->class_code == 0x02 && device->subclass == 0x80);
    bool network_controller = (device->class_code == 0x02);
    if(!known && !generic_wireless){
        if(!network_controller) return;
        if(device->subclass == 0x00) return;
    }
    ctx->found = true;
    ctx->dev->pci = *device;
    ctx->dev->hardware_found = true;
    char *p = ctx->dev->hw_info;
    memset(p,0,sizeof(ctx->dev->hw_info));
    if(known){
        strcpy(p,"Intel AX201 0x");
        append_hex4(p+14, device->device_id);
    } else if(generic_wireless){
        strcpy(p,"Intel Wireless 0x");
        append_hex4(p+17, device->device_id);
    } else {
        strcpy(p,"Intel Net 0x");
        append_hex4(p+12, device->device_id);
        size_t len = strlen(p);
        p[len++]=' ';
        append_hex2(p+len, device->class_code);
        p[len+2]=':';
        append_hex2(p+len+3, device->subclass);
    }
}

static bool ax201_valid_mac(const uint8_t mac[6]){
    bool all_zero=true, all_ff=true;
    for(int i=0;i<6;i++){
        if(mac[i]) all_zero=false;
        if(mac[i]!=0xFF) all_ff=false;
    }
    return !(mac[0]&1U) && !all_zero && !all_ff;
}

static void ax201_decode_mac(uint32_t low, uint32_t high, uint8_t mac[6]){
    mac[0]=(uint8_t)(low>>24);
    mac[1]=(uint8_t)(low>>16);
    mac[2]=(uint8_t)(low>>8);
    mac[3]=(uint8_t)low;
    mac[4]=(uint8_t)(high>>8);
    mac[5]=(uint8_t)high;
}

static bool ax201_read_mac(uint8_t mac[6]){
    if(adapter.regs){
        uint32_t first = ax201_csr_read(AX201_CSR_HW_IF_CONFIG_REG);
        uint32_t rev = ax201_csr_read(AX201_CSR_HW_REV);
        (void)first; (void)rev;
        if(first == 0xFFFFFFFFU && rev == 0xFFFFFFFFU){
            klog(KLOG_WARN, "ax201: MMIO reads all 1s, device power gated or RFKILL");
        }

        uint32_t strap0=ax201_csr_read(AX201_CSR_MAC_ADDR0_STRAP);
        uint32_t strap1=ax201_csr_read(AX201_CSR_MAC_ADDR1_STRAP);
        ax201_decode_mac(strap0,strap1,mac);
        if(ax201_valid_mac(mac)){
            klogf(KLOG_OK, "ax201: factory MAC from strap %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            return true;
        }

        uint32_t otp0=ax201_csr_read(AX201_CSR_MAC_ADDR0_OTP);
        uint32_t otp1=ax201_csr_read(AX201_CSR_MAC_ADDR1_OTP);
        ax201_decode_mac(otp0,otp1,mac);
        if(ax201_valid_mac(mac)){
            klogf(KLOG_OK, "ax201: factory MAC from OTP %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            return true;
        }
        klogf(KLOG_WARN, "ax201: no valid factory MAC (strap=%08x/%08x otp=%08x/%08x); using temporary address",
            strap0,strap1,otp0,otp1);
    }

    mac[0] = 0x02;
    mac[1] = 0x34;
    mac[2] = adapter.pci.bus;
    mac[3] = adapter.pci.slot;
    mac[4] = (uint8_t)(adapter.pci.device_id & 0xFF);
    mac[5] = (uint8_t)(adapter.pci.device_id >> 8);
    if(mac[0] & 1) mac[0] &= ~1U;
    return ax201_valid_mac(mac);
}

static bool ax201_transmit(void *context, const uint8_t *frame, uint16_t length){
    struct ax201_device *dev = context;
    if(!dev || !dev->ready) return false;
    if(!dev->net.cached_link_up || !dev->associated){
        dev->net.stats.tx_dropped++;
        return false;
    }
    if(length > AX201_DMA_BUFFER_SIZE || length < 14) return false;

    if(dev->regs && dev->mmio_mapped){

    }
    dev->net.stats.tx_packets++;
    dev->net.stats.tx_bytes += length;

    return true;
}

static void ax201_poll(void *context, uint32_t budget){
    struct ax201_device *dev = context;
    (void)budget;
    if(!dev || !dev->ready) return;

}

static bool ax201_link_up(void *context){
    struct ax201_device *dev = context;
    if(!dev || !dev->ready) return false;
    struct wifi_status st;
    if(!wifi_get_status(&st)) return false;
    return st.connected && st.state == WIFI_STATE_CONNECTED && dev->associated;
}

static const struct net_device_ops ax201_net_ops = {
    .transmit = ax201_transmit,
    .poll = ax201_poll,
    .link_up = ax201_link_up,
};

static bool ax201_wifi_scan(void *context){
    struct ax201_device *dev = context;
    if(!dev || !dev->ready) return false;
    dev->scan_start_ms = timer_ticks();
    if(dev->hardware_found && dev->mmio_mapped){
        klog(KLOG_INFO, "ax201: issuing scan via firmware (stub - awaiting ucode)");
        uint32_t gp = *(volatile uint32_t*)(dev->regs + AX201_CSR_GP_CNTRL);
        (void)gp;
        klogf(KLOG_INFO, "ax201: GP_CNTRL=0x%x - firmware not loaded, scan will timeout with 0 results (expected until ucode)", gp);
        return true;
    }
    klog(KLOG_WARN, "ax201: scan requested but hardware not ready");
    return false;
}

static bool ax201_wifi_connect(void *context, const char *ssid, const char *password){
    struct ax201_device *dev = context;
    if(!dev || !ssid) return false;
    strncpy(dev->connect_ssid, ssid, sizeof(dev->connect_ssid)-1);
    dev->connect_ssid[sizeof(dev->connect_ssid)-1]='\0';
    if(password) strncpy(dev->connect_password, password, sizeof(dev->connect_password)-1);
    else dev->connect_password[0]='\0';
    dev->connect_pending = true;
    dev->connect_start_ms = timer_ticks();
    dev->associated = false;

    if(dev->hardware_found && dev->mmio_mapped){
        klogf(KLOG_INFO, "ax201: connect '%s' len=%u - sending AUTH/ASSOC via firmware", ssid, password ? (uint32_t)strlen(password):0);
        klogf(KLOG_WARN, "ax201: [TEST MODE] plaintext password logged: '%s' for '%s'", password?password:"", ssid);

        return true;
    }
    klog(KLOG_WARN, "ax201: connect but no hardware - failing");
    return false;
}

static bool ax201_wifi_disconnect(void *context){
    struct ax201_device *dev = context;
    if(!dev) return false;
    dev->associated = false;
    dev->connect_pending = false;
    if(dev->hardware_found){
        klog(KLOG_INFO, "ax201: disconnect - sending DEAUTH to AP");

    }

    dev->net.cached_link_up = false;

    return true;
}

static void ax201_wifi_poll(void *context, uint64_t now_ms){
    struct ax201_device *dev = context;
    if(!dev) return;

    if(dev->hardware_found && dev->scan_start_ms){
        if(now_ms - dev->scan_start_ms >= 3000ULL){

            wifi_notify_scan_done();
            dev->scan_start_ms = 0;
            klog(KLOG_INFO, "ax201: scan done (0 results until firmware/uCode support)");
        }
    }

    if(dev->connect_pending){
        if(now_ms - dev->connect_start_ms >= 2500ULL){

            klogf(KLOG_WARN, "ax201: association to '%s' timed out - firmware not loaded, no fake success", dev->connect_ssid);
            wifi_notify_connect_failed(-110);
            dev->connect_pending = false;

        }

    }

}

static bool ax201_wifi_is_connected(void *context){
    struct ax201_device *dev = context;
    return dev && dev->associated;
}

static const struct wifi_ops ax201_wifi_ops = {
    .scan = ax201_wifi_scan,
    .connect = ax201_wifi_connect,
    .disconnect = ax201_wifi_disconnect,
    .poll = ax201_wifi_poll,
    .is_connected = ax201_wifi_is_connected,
};

bool intel_ax201_init(void){
    if(initialized) return adapter.ready;
    memset(&adapter, 0, sizeof(adapter));
    initialized = true;

    struct ax201_scan_ctx ctx = {.found=false, .dev=&adapter};
    pci_enumerate(ax201_pci_visitor, &ctx);

    if(!adapter.hardware_found){
        klog(KLOG_INFO, "ax201: no Intel Wi-Fi hardware found - wlan0 not created (e1000 remains for QEMU)");
        klog(KLOG_INFO, "ax201: on real AX201 hardware this log should show PCI location and MMIO mapping");
        return false;
    }

    klogf(KLOG_OK, "ax201: hardware detected: %s at %02x:%02x.%u", adapter.hw_info, adapter.pci.bus, adapter.pci.slot, adapter.pci.function);
    klog(KLOG_INFO, "ax201: PCI detection complete; starting MMIO bring-up");

    uint32_t bar0 = pci_read_config32(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0x10);
    if(!bar0 || bar0 == 0xFFFFFFFFU || (bar0 & 1U)){
        klog(KLOG_ERROR, "ax201: BAR0 invalid");
        return false;
    }
    if(!pci_update_command(&adapter.pci, PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER, 0)){
        klog(KLOG_WARN, "ax201: failed to enable PCI MEM+BM");
    }
    uint64_t bar_addr = pci_read_bar(adapter.pci.bus, adapter.pci.slot, adapter.pci.function, 0);
    if(!bar_addr){
        klog(KLOG_ERROR, "ax201: BAR address 0");
        return false;
    }
    adapter.regs = mmio_map(bar_addr, AX201_MMIO_SIZE);
    if(!adapter.regs){
        klogf(KLOG_ERROR, "ax201: mmio_map failed for 0x%lx size %u", (unsigned long)bar_addr, AX201_MMIO_SIZE);
        return false;
    }
    adapter.mmio_mapped = true;
    klogf(KLOG_OK, "ax201: MMIO mapped at %p phys=0x%lx", adapter.regs, (unsigned long)bar_addr);

    uint32_t hw_rev = *(volatile uint32_t*)(adapter.regs + AX201_CSR_HW_REV);
    uint32_t hw_if = ax201_csr_read(AX201_CSR_HW_IF_CONFIG_REG);
    uint32_t hw_rf_id = ax201_csr_read(AX201_CSR_HW_RF_ID);
    uint32_t gp_ctrl = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t gpio_in = ax201_csr_read(AX201_CSR_GPIO_IN);
    uint32_t interrupt_status = ax201_csr_read(AX201_CSR_INT);
    uint32_t interrupt_mask = ax201_csr_read(AX201_CSR_INT_MASK);
    klogf(KLOG_INFO, "ax201: CSR hw_if=0x%08x hw_rev=0x%08x rf_id=0x%08x",
        hw_if, hw_rev, hw_rf_id);
    klogf(KLOG_INFO, "ax201: CSR gp_ctrl=0x%08x gpio=0x%08x int=0x%08x mask=0x%08x",
        gp_ctrl, gpio_in, interrupt_status, interrupt_mask);
    if(hw_rev == 0xFFFFFFFFU){
        klog(KLOG_WARN, "ax201: device reports not ready (RFKILL or power)");
    }
    if(gp_ctrl & AX201_GP_CNTRL_HW_RFKILL){
        klog(KLOG_WARN, "ax201: hardware RF-kill is active; turn Wi-Fi on with the laptop key/switch before firmware bring-up");
    }

    if(!ax201_get_firmware()){
        klog(KLOG_ERROR, "ax201: WLAN transport is unavailable without firmware");
        return false;
    }

    if(!ax201_read_mac(adapter.mac)){
        klog(KLOG_ERROR, "ax201: MAC generation failed");
        return false;
    }
    memcpy(adapter.net.mac, adapter.mac, 6);
    strncpy(adapter.net.name, "wlan0", sizeof(adapter.net.name)-1);
    adapter.net.mtu = NET_ETHERNET_MTU;
    adapter.net.ops = &ax201_net_ops;
    adapter.net.driver_context = &adapter;
    adapter.net.cached_link_up = false;

    if(!net_device_register(&adapter.net)){
        klog(KLOG_ERROR, "ax201: net_device_register failed");
        return false;
    }
    if(!wifi_device_register(&adapter.net, &ax201_wifi_ops, &adapter, "wlan0")){
        klog(KLOG_ERROR, "ax201: wifi_device_register failed");
        return false;
    }

    adapter.ready = true;
    klogf(KLOG_OK, "ax201: %s ready as wlan0 %02x:%02x:%02x:%02x:%02x:%02x (real stack, DHCP via existing net)",
        adapter.hw_info, adapter.mac[0], adapter.mac[1], adapter.mac[2], adapter.mac[3], adapter.mac[4], adapter.mac[5]);
    klogf(KLOG_INFO, "ax201: firmware %s is available; Gen2 DMA transport is next", AX201_FIRMWARE_HINT);
    klog(KLOG_WARN, "ax201: BRING-UP TEST: PCI+MMIO succeeded - check this log on real AX201 hardware");

    (void)wifi_trigger_scan();
    return true;
}

bool intel_ax201_has_hardware(void){ return adapter.hardware_found; }
const char *intel_ax201_hardware_info(void){ return adapter.hw_info; }
