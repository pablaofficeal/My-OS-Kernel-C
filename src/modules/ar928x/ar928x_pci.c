#include "ar928x_pci.h"
#include "ar928x.h"
#include "drivers/pci/pci.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "drivers/interrupts/timer.h"
#include "arch/x86_64/mmio.h"
static const uint16_t ar928x_device_ids[] = {0x0029,0x002A,0x002B,0x002D,0x002E,0x002C};
bool ar928x_pci_is_known_id(uint16_t did){
    for(size_t i=0;i<sizeof(ar928x_device_ids)/sizeof(ar928x_device_ids[0]);i++){
        if(ar928x_device_ids[i]==did) return true;
    }
    return false;
}
struct ar928x_scan_ctx {bool found;};
static void append_hex4(char *out, uint16_t v){
    const char *hex="0123456789ABCDEF";
    out[0]=hex[(v>>12)&0xF];
    out[1]=hex[(v>>8)&0xF];
    out[2]=hex[(v>>4)&0xF];
    out[3]=hex[v&0xF];
    out[4]='\0';
}
static void ar928x_pci_visitor(const struct pci_device_info *dev, void *ctx){
    struct ar928x_scan_ctx *c=ctx;
    if(c->found) return;
    if(dev->vendor_id!=AR928X_VENDOR_ATHEROS) return;
    bool known=ar928x_pci_is_known_id(dev->device_id);
    bool wireless=(dev->class_code==0x02 && dev->subclass==0x80);
    bool net_ctrl=(dev->class_code==0x02);
    if(!known && !wireless){
        if(!net_ctrl) return;
        if(dev->subclass==0x00) return;
    }
    c->found=true;
    g_ar928x.pci=*dev;
    g_ar928x.hardware_found=true;
    char *p=g_ar928x.hw_info;
    memset(p,0,sizeof(g_ar928x.hw_info));
    if(known){
        if(dev->device_id==0x002A) strcpy(p,"Atheros AR9280 0x002A");
        else if(dev->device_id==0x0029) strcpy(p,"Atheros AR9280 0x0029");
        else if(dev->device_id==0x002B) strcpy(p,"Atheros AR9285 0x002B");
        else if(dev->device_id==0x002D) strcpy(p,"Atheros AR9287 0x002D");
        else if(dev->device_id==0x002E) strcpy(p,"Atheros AR9287 0x002E");
        else {strcpy(p,"Atheros AR928X 0x"); append_hex4(p+17,dev->device_id);}
    } else if(wireless){
        strcpy(p,"Atheros Wireless 0x");
        append_hex4(p+19,dev->device_id);
    } else {
        strcpy(p,"Atheros Net 0x");
        append_hex4(p+14,dev->device_id);
    }
}
bool ar928x_pci_probe(void){
    struct ar928x_scan_ctx ctx={.found=false};
    pci_enumerate(ar928x_pci_visitor,&ctx);
    if(!g_ar928x.hardware_found){
        klog(KLOG_INFO,"ar928x: no Atheros AR928X hardware found");
        return false;
    }
    klogf(KLOG_OK,"ar928x: hardware detected: %s at %02x:%02x.%u",g_ar928x.hw_info,g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function);
    return true;
}
bool ar928x_pci_enable(void){
    if(!pci_update_command(&g_ar928x.pci,PCI_COMMAND_MEMORY|PCI_COMMAND_BUS_MASTER,0)){
        klog(KLOG_WARN,"ar928x: failed to enable PCI MEM+BM");
        return false;
    }
    return true;
}
uint64_t ar928x_pci_bar_address(void){
    return pci_read_bar(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,0);
}
void ar928x_pci_ensure_power(void){
    uint32_t pm_cap_ptr=0;
    uint32_t cap=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,0x34)&0xFF;
    for(int i=0;i<12 && cap>=0x40 && cap<0xFF;i++){
        uint32_t hdr=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,cap);
        uint8_t cap_id=hdr&0xFF;
        uint8_t nxt=(hdr>>8)&0xFF;
        if(cap_id==0x01){pm_cap_ptr=cap; break;}
        if(!nxt) break;
        cap=nxt;
    }
    if(pm_cap_ptr){
        uint32_t pmcsr=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,pm_cap_ptr+4);
        uint8_t state=pmcsr&0x3;
        if(state!=0){
            klogf(KLOG_INFO,"ar928x: PCI PM D%u -> D0 pmcsr=0x%08x cap=0x%x",state,pmcsr,pm_cap_ptr);
            pci_write_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,pm_cap_ptr+4,pmcsr & ~0x3U);
            for(int i=0;i<5;i++) timer_sleep(10);
            uint32_t verify=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,pm_cap_ptr+4);
            klogf(KLOG_INFO,"ar928x: PCI PM after D0 pmcsr=0x%08x",verify);
        }
    }
    uint32_t exp_cap=0;
    cap=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,0x34)&0xFF;
    for(int i=0;i<12 && cap>=0x40 && cap<0xFF;i++){
        uint32_t hdr=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,cap);
        uint8_t cap_id=hdr&0xFF;
        uint8_t nxt=(hdr>>8)&0xFF;
        if(cap_id==0x10){exp_cap=cap; break;}
        if(!nxt) break;
        cap=nxt;
    }
    if(exp_cap){
        uint32_t link_ctrl=pci_read_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,exp_cap+0x10);
        if(link_ctrl & 0x3){
            klogf(KLOG_INFO,"ar928x: PCIe ASPM L0s/L1 disabled 0x%08x -> 0x%08x",link_ctrl,link_ctrl & ~0x3U);
            pci_write_config32(g_ar928x.pci.bus,g_ar928x.pci.slot,g_ar928x.pci.function,exp_cap+0x10,link_ctrl & ~0x3U);
        }
    }
}
