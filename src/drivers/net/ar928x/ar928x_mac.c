#include "ar928x_mac.h"
#include "ar928x.h"
#include "ar928x_reg.h"
#include "ar928x_hw.h"
#include "ar928x_eeprom.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
void ar928x_mac_generate_fallback(uint8_t out[6]){
    out[0]=0x02; out[1]=0xA9; out[2]=g_ar928x.pci.bus; out[3]=g_ar928x.pci.slot; out[4]=(uint8_t)(g_ar928x.pci.device_id&0xFF); out[5]=(uint8_t)(g_ar928x.pci.device_id>>8);
    out[0]&=~1U; out[0]|=0x02;
}
bool ar928x_mac_program(const uint8_t mac[6]){
    if(!g_ar928x.regs || !mac) return false;
    uint32_t low=((uint32_t)mac[0])|((uint32_t)mac[1]<<8)|((uint32_t)mac[2]<<16)|((uint32_t)mac[3]<<24);
    uint32_t high=((uint32_t)mac[4])|((uint32_t)mac[5]<<8);
    ar928x_reg_write(AR928X_REG_STA_ID0,low);
    uint32_t cur=ar928x_reg_read(AR928X_REG_STA_ID1);
    cur=(cur & ~0xFFFFU) | (high & 0xFFFFU);
    ar928x_reg_write(AR928X_REG_STA_ID1,cur);
    return true;
}
bool ar928x_mac_setup(void){
    uint8_t mac[6];
    if(!ar928x_eeprom_read_mac(mac)) return false;
    memcpy(g_ar928x.mac,mac,6);
    ar928x_mac_program(mac);
    klogf(KLOG_OK,"ar928x: MAC %02x:%02x:%02x:%02x:%02x:%02x ready",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return true;
}
