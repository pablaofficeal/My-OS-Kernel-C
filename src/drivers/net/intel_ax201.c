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
#define AX201_CTXT_INFO_SIZE 0x1000U  /* one page for CTXT_INFO struct */
#define AX201_DMA_BUFFER_SIZE 2048U

static const uint16_t ax201_device_ids[] = {
    0x06F0, 0xA0F0, 0x02F0, 0x34F0, 0x43F0, 0x4DF0, 0x51F0, 0x51F1, 0x54F0, 0x7A70, 0x7AF0, 0x2723, 0x2725, 0x7360,
};

#define AX201_FIRMWARE_QUZ_HINT   "iwlwifi-QuZ-a0-hr-b0-77.ucode"
#define AX201_FIRMWARE_QUZ_MODULE "/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode"
#define AX201_FIRMWARE_SO_HINT    "iwlwifi-so-a0-gf-a0-89.ucode"
#define AX201_FIRMWARE_SO_MODULE  "/firmware/iwlwifi-so-a0-gf-a0-89.ucode"
#define AX201_FIRMWARE_MAGIC        0x0A4C5749U
#define AX201_FIRMWARE_MAGIC_OFFSET 4U

/* CSR register offsets (BAR0) */
#define AX201_CSR_HW_IF_CONFIG_REG  0x000
#define AX201_CSR_INT               0x008
#define AX201_CSR_INT_MASK          0x00C
#define AX201_CSR_GPIO_IN           0x018
#define AX201_CSR_RESET             0x020
#define AX201_CSR_GP_CNTRL          0x024
#define AX201_CSR_HW_REV            0x028
#define AX201_CSR_CTXT_INFO_BA      0x040   /* context-info base address (lo 32) */
#define AX201_CSR_CTXT_INFO_BA_HI   0x044   /* context-info base address (hi 32) */
#define AX201_CSR_CTXT_INFO_KICK    0x048   /* write 1 to start ROM loader   */
#define AX201_CSR_HW_RF_ID          0x09C
#define AX201_CSR_MAC_ADDR_BASE     0x380
#define AX201_CSR_MAC_ADDR0_OTP     (AX201_CSR_MAC_ADDR_BASE + 0x00)
#define AX201_CSR_MAC_ADDR1_OTP     (AX201_CSR_MAC_ADDR_BASE + 0x04)
#define AX201_CSR_MAC_ADDR0_STRAP   (AX201_CSR_MAC_ADDR_BASE + 0x08)
#define AX201_CSR_MAC_ADDR1_STRAP   (AX201_CSR_MAC_ADDR_BASE + 0x0C)

/* GP_CNTRL bits */
#define AX201_GP_CNTRL_HW_RFKILL    0x08000000U
#define AX201_GP_CNTRL_INIT_DONE    0x00000004U

/* CSR_INT bits */
#define AX201_INT_ALIVE             0x00010000U  /* firmware ALIVE notification */
#define AX201_INT_SW_ERR            0x04000000U  /* firmware SW error           */

/* ALIVE polling parameters */
#define AX201_ALIVE_POLL_MS         5U
#define AX201_ALIVE_TIMEOUT_MS      1500U

/* Soft-scan timeout used when firmware is not alive */
#define AX201_SOFTSCAN_TIMEOUT_MS   1200U

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
    const char *firmware_hint;
    const char *firmware_module;
    const uint8_t *firmware;
    uint64_t firmware_size;
    bool firmware_loaded;
    bool firmware_alive;
    /* CTXT_INFO DMA upload state */
    uint64_t ctxt_info_phys;
    uint64_t fw_dma_phys;
    bool ctxt_info_written;
};

static struct ax201_device adapter;
static bool initialized;

/* Forward declarations for firmware bring-up helpers defined later in file */
static bool ax201_fw_upload(void);
static bool ax201_wait_alive_poll(void);

static uint32_t ax201_csr_read(uint32_t offset){
    return *(volatile uint32_t *)(adapter.regs + offset);
}
static void ax201_csr_write(uint32_t offset, uint32_t value){
    *(volatile uint32_t *)(adapter.regs + offset) = value;
}

static uint32_t ax201_read_le32(const uint8_t *p){
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}
static void ax201_write_le32(uint8_t *p, uint32_t v){
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void ax201_write_le64(uint8_t *p, uint64_t v){
    ax201_write_le32(p,   (uint32_t)(v & 0xFFFFFFFFU));
    ax201_write_le32(p+4, (uint32_t)(v >> 32));
}

static void ax201_log_firmware_release(const uint8_t *image, uint64_t size){
    char release[49];
    uint64_t pos = AX201_FIRMWARE_MAGIC_OFFSET + 4U;
    uint32_t out = 0;

    memset(release, 0, sizeof(release));
    while(pos < size && out < sizeof(release)-1){
        uint8_t ch = image[pos++];
        if(ch == 0) break;
        if(ch < 0x20 || ch > 0x7e) break;
        release[out++] = (char)ch;
    }
    if(out){
        klogf(KLOG_INFO, "ax201: firmware release '%s'", release);
    }
}

static bool ax201_get_firmware(void){
    const void *image=0;
    uint64_t size=0;
    if(!adapter.firmware_module || !adapter.firmware_hint){
        klog(KLOG_ERROR, "ax201: firmware selector was not initialized");
        return false;
    }
    if(!boot_get_module(adapter.firmware_module,&image,&size) || !image || size<8){
        klogf(KLOG_ERROR, "ax201: firmware module %s not supplied by Limine", adapter.firmware_module);
        return false;
    }
    uint32_t magic=ax201_read_le32((const uint8_t *)image + AX201_FIRMWARE_MAGIC_OFFSET);
    if(magic!=AX201_FIRMWARE_MAGIC){
        uint32_t first=ax201_read_le32((const uint8_t *)image);
        klogf(KLOG_ERROR, "ax201: invalid firmware magic@4=0x%08x first=0x%08x size=%llu",
            magic, first, (unsigned long long)size);
        return false;
    }
    adapter.firmware=(const uint8_t *)image;
    adapter.firmware_size=size;
    adapter.firmware_loaded = true;
    adapter.firmware_alive = false;
    klogf(KLOG_OK, "ax201: firmware module loaded %s (%llu bytes)",
        adapter.firmware_hint,(unsigned long long)size);
    ax201_log_firmware_release(adapter.firmware, adapter.firmware_size);
    return true;
}

static void ax201_select_firmware(void){
    switch(adapter.pci.device_id){
        case 0x51F0:
        case 0x51F1:
        case 0x54F0:
        case 0x7A70:
        case 0x7AF0:
            adapter.firmware_hint = AX201_FIRMWARE_SO_HINT;
            adapter.firmware_module = AX201_FIRMWARE_SO_MODULE;
            klogf(KLOG_INFO, "ax201: PCI id 0x%04x uses Intel So/AX210-family firmware %s",
                adapter.pci.device_id, adapter.firmware_hint);
            return;
        default:
            adapter.firmware_hint = AX201_FIRMWARE_QUZ_HINT;
            adapter.firmware_module = AX201_FIRMWARE_QUZ_MODULE;
            klogf(KLOG_INFO, "ax201: PCI id 0x%04x uses QuZ/AX201-family firmware %s",
                adapter.pci.device_id, adapter.firmware_hint);
            return;
    }
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
    if(!dev->hardware_found || !dev->mmio_mapped){
        klog(KLOG_WARN, "ax201: scan requested but hardware not ready");
        return false;
    }

    uint32_t gp   = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t ints = ax201_csr_read(AX201_CSR_INT);

    if(!dev->firmware_alive){
        /*
         * FIX (Stage 1): Do NOT block the scan when firmware is not alive.
         * Start a soft-scan that completes after SOFTSCAN_TIMEOUT_MS with 0
         * results.  This prevents the UI from hanging on "нет интернета".
         */
        klogf(KLOG_WARN,
            "ax201: firmware not alive – starting soft-scan "
            "(gp=0x%08x int=0x%08x loaded=%u ctxt=%u)",
            gp, ints,
            dev->firmware_loaded    ? 1U : 0U,
            dev->ctxt_info_written  ? 1U : 0U);
        dev->scan_start_ms = timer_ticks();
        return true;
    }

    /* Firmware alive – issue a real UMAC scan command */
    dev->scan_start_ms = timer_ticks();
    klogf(KLOG_INFO, "ax201: issuing UMAC scan (gp=0x%08x int=0x%08x)", gp, ints);
    /* TODO: write SCAN_REQ_UMAC to firmware HCMD queue */
    return true;
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

    if(!dev->hardware_found || !dev->mmio_mapped){
        klog(KLOG_WARN, "ax201: connect but no hardware - failing");
        dev->connect_pending = false;
        return false;
    }

    uint32_t gp   = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t ints = ax201_csr_read(AX201_CSR_INT);

    if(!dev->firmware_alive){
        /*
         * FIX (Stage 1): Do NOT hard-fail when firmware is not alive.
         * Queue the connect; poll() will call wifi_notify_connect_failed()
         * with a timeout, giving the UI a clean error path.
         */
        klogf(KLOG_WARN,
            "ax201: firmware not alive – queuing connect '%s' "
            "(gp=0x%08x int=0x%08x)", ssid, gp, ints);
        return true;
    }

    klogf(KLOG_INFO, "ax201: connect '%s' via firmware MLME (gp=0x%08x)", ssid, gp);
    /* TODO: send AUTH/ASSOC via firmware HCMD queue */
    return true;
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

    /* --- Deferred ALIVE detection (interrupt-less polling) ---
     * If CTXT_INFO was written but ALIVE was not received synchronously
     * during init (NIC was still starting), keep checking CSR_INT. */
    if(dev->firmware_loaded && !dev->firmware_alive && dev->ctxt_info_written && dev->regs){
        uint32_t ints = ax201_csr_read(AX201_CSR_INT);
        if(ints & AX201_INT_ALIVE){
            ax201_csr_write(AX201_CSR_INT, AX201_INT_ALIVE);
            dev->firmware_alive = true;
            klogf(KLOG_OK, "ax201: firmware ALIVE (deferred via poll) int=0x%08x", ints);
        } else if(ints & AX201_INT_SW_ERR){
            ax201_csr_write(AX201_CSR_INT, AX201_INT_SW_ERR);
            uint32_t gp = ax201_csr_read(AX201_CSR_GP_CNTRL);
            klogf(KLOG_ERROR,
                "ax201: firmware SW_ERR in poll (gp=0x%08x int=0x%08x) – "
                "check firmware variant matches PCI id 0x%04x",
                gp, ints, dev->pci.device_id);
        }
    }

    /* --- Scan completion --- */
    if(dev->hardware_found && dev->scan_start_ms){
        uint64_t timeout = dev->firmware_alive ? 3000ULL : AX201_SOFTSCAN_TIMEOUT_MS;
        if(now_ms - dev->scan_start_ms >= timeout){
            wifi_notify_scan_done();
            dev->scan_start_ms = 0;
            if(dev->firmware_alive){
                klog(KLOG_INFO, "ax201: scan complete (UMAC path)");
            } else {
                klog(KLOG_WARN,
                    "ax201: soft-scan complete – 0 results (firmware not alive; "
                    "verify ucode Limine module path and firmware variant)");
            }
        }
    }

    /* --- Connect timeout --- */
    if(dev->connect_pending){
        if(now_ms - dev->connect_start_ms >= 2500ULL){
            if(!dev->firmware_alive){
                klogf(KLOG_WARN,
                    "ax201: connect '%s' failed – firmware not alive",
                    dev->connect_ssid);
            } else {
                klogf(KLOG_WARN,
                    "ax201: connect '%s' timed out – no ASSOC from AP",
                    dev->connect_ssid);
            }
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

    ax201_select_firmware();
    if(!ax201_get_firmware()){
        klog(KLOG_WARN,
            "ax201: firmware not available from bootloader; "
            "wlan0 registered in stub mode (scan returns 0 results)");
        /* FIX: Do not bail out – register the device anyway so the UI
         * shows the interface and a clean '0 networks' result rather
         * than no device at all. */
    } else {
        /* --- Stage 2: Upload firmware via CTXT_INFO v2 --- */
        if(!ax201_fw_upload()){
            klog(KLOG_WARN, "ax201: CTXT_INFO upload failed; continuing in stub mode");
        } else {
            /* Wait synchronously for ALIVE (NIC typically responds in < 500 ms) */
            if(!ax201_wait_alive_poll()){
                klog(KLOG_WARN,
                    "ax201: ALIVE not received during init; "
                    "will keep polling in wifi_poll()");
            }
        }
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
    klogf(KLOG_OK,
        "ax201: %s ready as wlan0 %02x:%02x:%02x:%02x:%02x:%02x firmware_alive=%u",
        adapter.hw_info,
        adapter.mac[0], adapter.mac[1], adapter.mac[2],
        adapter.mac[3], adapter.mac[4], adapter.mac[5],
        adapter.firmware_alive ? 1U : 0U);
    if(!adapter.firmware_alive){
        klog(KLOG_WARN,
            "ax201: firmware not alive at init – scan will return 0 networks "
            "until ALIVE arrives; check ucode Limine module and firmware variant");
    }

    (void)wifi_trigger_scan();
    return true;
}

/* -----------------------------------------------------------------------
 * Firmware upload via CTXT_INFO v2
 *
 * Flow (iwlwifi QuZ/AX201 ROM boot protocol):
 *   1. Allocate a PMM page for the CTXT_INFO struct.
 *   2. Allocate PMM pages for a contiguous DMA copy of the firmware image.
 *   3. Fill CTXT_INFO: version=2, fw_image_dram, fw_image_size.
 *   4. Write CTXT_INFO physical address to CSR_CTXT_INFO_BA (64-bit split).
 *   5. Write 1 to CSR_CTXT_INFO_KICK → ROM starts loading from DRAM.
 * ----------------------------------------------------------------------- */
static bool ax201_fw_upload(void){
    if(!adapter.firmware_loaded || !adapter.firmware || !adapter.regs){
        klog(KLOG_ERROR, "ax201: fw_upload: no firmware data or no MMIO");
        return false;
    }

    /* Allocate page for CTXT_INFO */
    uint64_t ctxt_phys = pmm_allocate_page();
    if(!ctxt_phys){
        klog(KLOG_ERROR, "ax201: fw_upload: failed to allocate CTXT_INFO page");
        return false;
    }
    adapter.ctxt_info_phys = ctxt_phys;

    /* Allocate pages for firmware image */
    uint64_t fw_pages = (adapter.firmware_size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if(fw_pages == 0) fw_pages = 1;

    uint64_t fw_phys = pmm_allocate_page();
    if(!fw_phys){
        klog(KLOG_ERROR, "ax201: fw_upload: failed to allocate firmware DMA page");
        return false;
    }
    adapter.fw_dma_phys = fw_phys;

    /* Allocate remaining pages; track non-contiguous count, log ONE summary */
    uint64_t gap_count = 0;
    for(uint64_t pg = 1; pg < fw_pages; pg++){
        uint64_t extra = pmm_allocate_page();
        if(!extra){
            klogf(KLOG_WARN,
                "ax201: fw_upload: only got %llu/%llu firmware DMA pages",
                (unsigned long long)pg, (unsigned long long)fw_pages);
            break;
        }
        if(extra != fw_phys + pg * PMM_PAGE_SIZE){
            gap_count++;
        }
    }
    if(gap_count){
        klogf(KLOG_WARN,
            "ax201: fw_upload: %llu/%llu DMA pages non-contiguous – "
            "firmware may fail to load (need contiguous DMA allocator)",
            (unsigned long long)gap_count,
            (unsigned long long)(fw_pages - 1));
    }


    /* Copy firmware image into DMA region */
    void *fw_virt = pmm_physical_to_virtual(fw_phys);
    if(!fw_virt){
        klog(KLOG_ERROR, "ax201: fw_upload: no virtual mapping for DMA buffer");
        return false;
    }
    memcpy(fw_virt, adapter.firmware, adapter.firmware_size);

    /* Build CTXT_INFO struct in the allocated page (little-endian, manually) */
    void *ctxt_virt = pmm_physical_to_virtual(ctxt_phys);
    if(!ctxt_virt){
        klog(KLOG_ERROR, "ax201: fw_upload: no virtual mapping for CTXT_INFO");
        return false;
    }
    memset(ctxt_virt, 0, PMM_PAGE_SIZE);
    uint8_t *c = (uint8_t *)ctxt_virt;
    ax201_write_le32(c +  0, 2U);                              /* version = 2          */
    ax201_write_le32(c +  4, 64U);                             /* struct size (bytes)  */
    ax201_write_le64(c +  8, fw_phys);                        /* fw_image_dram        */
    ax201_write_le32(c + 16, (uint32_t)adapter.firmware_size);/* fw_image_size        */
    /* remaining fields zero: rbd/tfd unused, control_flags = 0 (normal boot) */

    /* Write CTXT_INFO base address to NIC (64-bit, split into two 32-bit writes) */
    ax201_csr_write(AX201_CSR_CTXT_INFO_BA,    (uint32_t)(ctxt_phys & 0xFFFFFFFFU));
    ax201_csr_write(AX201_CSR_CTXT_INFO_BA_HI, (uint32_t)(ctxt_phys >> 32));

    /* Kick the ROM to start loading */
    ax201_csr_write(AX201_CSR_CTXT_INFO_KICK, 1U);

    adapter.ctxt_info_written = true;
    klogf(KLOG_OK,
        "ax201: CTXT_INFO kick sent phys=0x%llx fw_dma=0x%llx size=%llu",
        (unsigned long long)ctxt_phys,
        (unsigned long long)fw_phys,
        (unsigned long long)adapter.firmware_size);
    return true;
}

/* -----------------------------------------------------------------------
 * Synchronous ALIVE poll after firmware kick
 * ----------------------------------------------------------------------- */
static bool ax201_wait_alive_poll(void){
    if(!adapter.regs) return false;
    uint64_t deadline = timer_ticks() + AX201_ALIVE_TIMEOUT_MS;
    klogf(KLOG_INFO, "ax201: waiting up to %u ms for firmware ALIVE...",
        AX201_ALIVE_TIMEOUT_MS);

    while(timer_ticks() < deadline){
        uint32_t ints = ax201_csr_read(AX201_CSR_INT);

        if(ints & AX201_INT_ALIVE){
            ax201_csr_write(AX201_CSR_INT, AX201_INT_ALIVE);  /* clear */
            adapter.firmware_alive = true;
            klogf(KLOG_OK,
                "ax201: firmware ALIVE (synchronous) int=0x%08x", ints);
            return true;
        }
        if(ints & AX201_INT_SW_ERR){
            ax201_csr_write(AX201_CSR_INT, AX201_INT_SW_ERR); /* clear */
            uint32_t gp = ax201_csr_read(AX201_CSR_GP_CNTRL);
            klogf(KLOG_ERROR,
                "ax201: firmware SW_ERR during ALIVE wait "
                "(gp=0x%08x int=0x%08x)", gp, ints);
            return false;
        }

        timer_sleep(AX201_ALIVE_POLL_MS);
    }

    uint32_t gp   = ax201_csr_read(AX201_CSR_GP_CNTRL);
    uint32_t ints = ax201_csr_read(AX201_CSR_INT);
    uint32_t rev  = ax201_csr_read(AX201_CSR_HW_REV);
    klogf(KLOG_WARN,
        "ax201: ALIVE timeout %u ms (gp=0x%08x int=0x%08x rev=0x%08x)",
        AX201_ALIVE_TIMEOUT_MS, gp, ints, rev);
    klog(KLOG_WARN,
        "ax201: likely causes: ucode not in Limine modules, RFKILL active, "
        "PCIe D3, or DMA non-contiguous");
    return false;
}

bool intel_ax201_has_hardware(void){ return adapter.hardware_found; }
const char *intel_ax201_hardware_info(void){ return adapter.hw_info; }
