#include "ar928x.h"
#include "ar928x_pci.h"
#include "ar928x_hw.h"
#include "ar928x_eeprom.h"
#include "ar928x_mac.h"
#include "ar928x_dma.h"
#include "ar928x_wifi.h"
#include "ar928x_reg.h"
#include "drivers/pci/pci.h"
#include "arch/x86_64/mmio.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "drivers/interrupts/timer.h"
#include "mm/pmm.h"
#include "net/core/net_device.h"
#include "net/wifi/wifi.h"
#include "net/network/ipv4.h"
#include "net/link/ethernet.h"
struct ar928x_device g_ar928x;
bool g_ar928x_initialized;
bool ar928x_has_hardware(void){return g_ar928x.hardware_found;}
const char *ar928x_hardware_info(void){return g_ar928x.hw_info[0]?g_ar928x.hw_info:"none";}
static bool ar928x_transmit(void *ctx, const uint8_t *frame, uint16_t len){
    struct ar928x_device *dev=ctx;
    if(!dev||!dev->ready) return false;
    if(!dev->net.cached_link_up||!dev->associated){dev->net.stats.tx_dropped++; return false;}
    if(len>AR928X_DMA_BUFFER_SIZE||len<14) return false;
    if(dev->regs&&dev->mmio_mapped){(void)ar928x_reg_read(AR928X_REG_CR);}
    if(__atomic_test_and_set(&dev->tx_locked,__ATOMIC_ACQUIRE)) return false;
    uint16_t idx=dev->tx_next;
    if(idx<AR928X_RING_COUNT && dev->tx_bufs[idx]){
        memcpy(dev->tx_bufs[idx],frame,len);
        dev->tx_ring[idx].ds_data=(uint32_t)dev->tx_buf_phys[idx];
        dev->tx_ring[idx].ds_ctl0=len;
        dev->tx_next=(idx+1)%AR928X_RING_COUNT;
    }
    __atomic_clear(&dev->tx_locked,__ATOMIC_RELEASE);
    dev->net.stats.tx_packets++;
    dev->net.stats.tx_bytes+=len;
    return true;
}
static void ar928x_poll(void *ctx, uint32_t budget){
    struct ar928x_device *dev=ctx;
    (void)budget;
    if(!dev||!dev->ready) return;
    if(dev->regs&&dev->mmio_mapped){
        uint32_t isr=ar928x_reg_read(AR928X_REG_ISR);
        uint32_t imr=ar928x_reg_read(AR928X_REG_IMR);
        (void)isr; (void)imr;
    }
}
static bool ar928x_link_up(void *ctx){
    struct ar928x_device *dev=ctx;
    if(!dev||!dev->ready) return false;
    struct wifi_status st;
    if(!wifi_get_status(&st)) return false;
    return st.connected && st.state==WIFI_STATE_CONNECTED && dev->associated;
}
static const struct net_device_ops ar928x_net_ops={.transmit=ar928x_transmit,.poll=ar928x_poll,.link_up=ar928x_link_up};
bool ar928x_module_init(void){
    if(g_ar928x_initialized) return g_ar928x.ready;
    memset(&g_ar928x,0,sizeof(g_ar928x));
    g_ar928x_initialized=true;
    if(!ar928x_pci_probe()) return false;
    klog(KLOG_INFO,"ar928x: PCI probe done");
    uint32_t bar0=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,0x10);
    if(!bar0||bar0==0xFFFFFFFFU||(bar0&1U)){klog(KLOG_ERROR,"ar928x: BAR0 invalid"); return false;}
    if(!ar928x_pci_enable()) klog(KLOG_WARN,"ar928x: pci enable warn");
    ar928x_pci_ensure_power();
    uint64_t bar_addr=ar928x_pci_bar_address();
    if(!bar_addr){klog(KLOG_ERROR,"ar928x: BAR addr 0"); return false;}
    g_ar928x.regs=mmio_map(bar_addr,AR928X_MMIO_SIZE);
    if(!g_ar928x.regs){klogf(KLOG_ERROR,"ar928x: mmio_map failed 0x%lx", (unsigned long)bar_addr); return false;}
    g_ar928x.mmio_mapped=true;
    klogf(KLOG_OK,"ar928x: MMIO %p phys=0x%lx",g_ar928x.regs,(unsigned long)bar_addr);
    uint32_t srev_raw=ar928x_reg_read(AR928X_REG_SREV);
    uint32_t cr=ar928x_reg_read(AR928X_REG_CR);
    uint32_t isr=ar928x_reg_read(AR928X_REG_ISR);
    uint32_t imr=ar928x_reg_read(AR928X_REG_IMR);
    uint32_t sta0=ar928x_reg_read(AR928X_REG_STA_ID0);
    uint32_t eeprom=ar928x_reg_read(AR928X_REG_EEPROM);
    klogf(KLOG_INFO,"ar928x: REG SREV=0x%08x CR=0x%08x ISR=0x%08x IMR=0x%08x",srev_raw,cr,isr,imr);
    klogf(KLOG_INFO,"ar928x: REG STA_ID0=0x%08x EEPROM=0x%08x",sta0,eeprom);
    if(srev_raw==0xFFFFFFFFU){klog(KLOG_WARN,"ar928x: device not ready");}
    ar928x_hw_read_srev();
    ar928x_hw_disable_interrupts();
    if(!ar928x_hw_reset()) klog(KLOG_WARN,"ar928x: reset warn");
    if(!ar928x_mac_setup()){klog(KLOG_ERROR,"ar928x: mac setup failed"); return false;}
    if(!ar928x_dma_allocate()) klog(KLOG_ERROR,"ar928x: dma allocate failed");
    else klog(KLOG_INFO,"ar928x: dma allocated");
    if(!ar928x_dma_init_hw()) klog(KLOG_WARN,"ar928x: dma hw init deferred");
    klog(KLOG_INFO,"ar928x: HAL deferred need initvals");
    memcpy(g_ar928x.net.mac,g_ar928x.mac,6);
    strncpy(g_ar928x.net.name,"wlan1",sizeof(g_ar928x.net.name)-1);
    g_ar928x.net.mtu=NET_ETHERNET_MTU;
    g_ar928x.net.ops=&ar928x_net_ops;
    g_ar928x.net.driver_context=&g_ar928x;
    g_ar928x.net.cached_link_up=false;
    if(!net_device_register(&g_ar928x.net)){klog(KLOG_ERROR,"ar928x: net_device_register failed"); ar928x_dma_release(); return false;}
    if(!wifi_device_register(&g_ar928x.net,ar928x_wifi_ops_ptr(),&g_ar928x,g_ar928x.net.name)){klog(KLOG_ERROR,"ar928x: wifi_device_register failed"); return false;}
    g_ar928x.ready=true;
    klogf(KLOG_OK,"ar928x: %s ready as %s %02x:%02x:%02x:%02x:%02x:%02x eeprom=%u srev=0x%08x",g_ar928x.hw_info,g_ar928x.net.name,g_ar928x.mac[0],g_ar928x.mac[1],g_ar928x.mac[2],g_ar928x.mac[3],g_ar928x.mac[4],g_ar928x.mac[5],g_ar928x.eeprom_valid?1U:0U,g_ar928x.srev_raw);
    klog(KLOG_INFO,"ar928x: ready for scan soft mode");
    (void)wifi_trigger_scan();
    return true;
}
void ar928x_module_exit(void){
    if(!g_ar928x_initialized) return;
    if(g_ar928x.net.registered) g_ar928x.net.registered=false;
    ar928x_dma_release();
    if(g_ar928x.regs) g_ar928x.regs=0;
    g_ar928x.mmio_mapped=false;
    g_ar928x.ready=false;
    g_ar928x_initialized=false;
    klog(KLOG_INFO,"ar928x: module exited");
}
