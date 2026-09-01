#include "ar928x_hw.h"
#include "ar928x.h"
#include "ar928x_reg.h"
#include "kernel/diagnostics/klog.h"
#include "drivers/interrupts/timer.h"
uint32_t ar928x_reg_read(uint32_t off){
    if(!g_ar928x.regs) return 0xFFFFFFFFU;
    return *(volatile uint32_t *)(g_ar928x.regs+off);
}
void ar928x_reg_write(uint32_t off, uint32_t v){
    if(!g_ar928x.regs) return;
    *(volatile uint32_t *)(g_ar928x.regs+off)=v;
}
void ar928x_hw_read_srev(void){
    if(!g_ar928x.regs || !g_ar928x.mmio_mapped) return;
    uint32_t srev=ar928x_reg_read(AR928X_REG_SREV);
    g_ar928x.srev_raw=srev;
    if(srev==0xFFFFFFFFU || srev==0){
        klogf(KLOG_WARN,"ar928x: SREV read 0x%08x not ready",srev);
        return;
    }
    uint8_t ver=(srev & AR928X_SREV_VERSION_M) >> AR928X_SREV_VERSION_S;
    uint8_t rev=srev & AR928X_SREV_REVISION_M;
    g_ar928x.srev_version=ver;
    g_ar928x.srev_rev=rev;
    const char *name="unknown";
    if(ver==AR928X_SREV_VER_9280) name="AR9280";
    else if(ver==AR928X_SREV_VER_9285) name="AR9285";
    if(g_ar928x.pci.device_id==0x002D || g_ar928x.pci.device_id==0x002E) name="AR9287";
    klogf(KLOG_INFO,"ar928x: SREV raw=0x%08x ver=0x%02x rev=%u -> %s DID 0x%04x",srev,ver,rev,name,g_ar928x.pci.device_id);
}
bool ar928x_hw_reset(void){
    if(!g_ar928x.regs) return false;
    ar928x_reg_write(AR928X_REG_ISR,0xFFFFFFFFU);
    ar928x_reg_write(AR928X_REG_IMR,0);
    (void)ar928x_reg_read(AR928X_REG_ISR);
    timer_sleep(5);
    return true;
}
void ar928x_hw_disable_interrupts(void){
    ar928x_reg_write(AR928X_REG_IMR,0);
    (void)ar928x_reg_read(AR928X_REG_ISR);
    ar928x_reg_write(AR928X_REG_ISR,0xFFFFFFFFU);
}
bool ar928x_hw_check_ready(void){
    uint32_t srev=ar928x_reg_read(AR928X_REG_SREV);
    if(srev==0xFFFFFFFFU) return false;
    uint32_t cr=ar928x_reg_read(AR928X_REG_CR);
    (void)cr;
    return true;
}
