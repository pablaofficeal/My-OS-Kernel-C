#include "ar928x_eeprom.h"
#include "ar928x.h"
#include "ar928x_reg.h"
#include "ar928x_hw.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
bool ar928x_eeprom_valid_mac(const uint8_t m[6]){
    bool z=true,f=true;
    for(int i=0;i<6;i++){if(m[i]) z=false; if(m[i]!=0xFF) f=false;}
    return !z && !f && !(m[0]&1);
}
bool ar928x_eeprom_try_read(uint8_t out[6]){
    if(!g_ar928x.regs) return false;
    uint32_t eep_status=ar928x_reg_read(AR928X_REG_EEPROM);
    bool absent=(eep_status & 0x00000100U)!=0;
    bool corrupt=(eep_status & 0x00000200U)!=0;
    if(absent||corrupt){
        klogf(KLOG_WARN,"ar928x: EEPROM absent=%u corrupt=%u status=0x%08x",absent?1U:0U,corrupt?1U:0U,eep_status);
        return false;
    }
    uint16_t words[6]={0};
    bool any_valid=false;
    for(uint32_t off=0;off<6;off++){
        uint32_t reg=AR928X_REG_EEPROM_BASE + (off<<2);
        if(reg+4>AR928X_MMIO_SIZE) break;
        uint32_t v=ar928x_reg_read(reg);
        if(v==0xFFFFFFFFU || v==0) continue;
        words[off]=(uint16_t)(v & 0xFFFFU);
        if(v & 0xFFFFU) any_valid=true;
    }
    if(!any_valid) return false;
    klogf(KLOG_DEBUG,"ar928x: EEPROM words %04x %04x %04x %04x %04x %04x",words[0],words[1],words[2],words[3],words[4],words[5]);
    uint8_t cand[6];
    cand[0]=(uint8_t)(words[1]>>8); cand[1]=(uint8_t)(words[1]&0xFF);
    cand[2]=(uint8_t)(words[2]>>8); cand[3]=(uint8_t)(words[2]&0xFF);
    cand[4]=(uint8_t)(words[3]>>8); cand[5]=(uint8_t)(words[3]&0xFF);
    if(ar928x_eeprom_valid_mac(cand)){memcpy(out,cand,6); klogf(KLOG_OK,"ar928x: EEPROM MAC %02x:%02x:%02x:%02x:%02x:%02x",out[0],out[1],out[2],out[3],out[4],out[5]); return true;}
    cand[0]=(uint8_t)(words[0]>>8); cand[1]=(uint8_t)(words[0]&0xFF);
    cand[2]=(uint8_t)(words[1]>>8); cand[3]=(uint8_t)(words[1]&0xFF);
    cand[4]=(uint8_t)(words[2]>>8); cand[5]=(uint8_t)(words[2]&0xFF);
    if(ar928x_eeprom_valid_mac(cand)){memcpy(out,cand,6); return true;}
    return false;
}
bool ar928x_eeprom_read_mac(uint8_t out[6]){
    if(ar928x_eeprom_try_read(out)){
        g_ar928x.eeprom_valid=true;
        return true;
    }
    if(g_ar928x.regs){
        uint32_t id0=ar928x_reg_read(AR928X_REG_STA_ID0);
        uint32_t id1=ar928x_reg_read(AR928X_REG_STA_ID1);
        if(id0!=0 && id0!=0xFFFFFFFFU && id1!=0xFFFFFFFFU){
            uint8_t cand[6];
            cand[0]=(uint8_t)(id0&0xFF);
            cand[1]=(uint8_t)((id0>>8)&0xFF);
            cand[2]=(uint8_t)((id0>>16)&0xFF);
            cand[3]=(uint8_t)((id0>>24)&0xFF);
            cand[4]=(uint8_t)(id1&0xFF);
            cand[5]=(uint8_t)((id1>>8)&0xFF);
            if(ar928x_eeprom_valid_mac(cand)){memcpy(out,cand,6); klogf(KLOG_OK,"ar928x: MAC from STA_ID %02x:%02x:%02x:%02x:%02x:%02x",out[0],out[1],out[2],out[3],out[4],out[5]); return true;}
        }
    }
    g_ar928x.eeprom_valid=false;
    out[0]=0x02; out[1]=0xA9; out[2]=g_ar928x.pci.bus; out[3]=g_ar928x.pci.slot; out[4]=(uint8_t)(g_ar928x.pci.device_id&0xFF); out[5]=(uint8_t)(g_ar928x.pci.device_id>>8);
    out[0]&=~1U; out[0]|=0x02;
    klogf(KLOG_WARN,"ar928x: fallback MAC %02x:%02x:%02x:%02x:%02x:%02x",out[0],out[1],out[2],out[3],out[4],out[5]);
    return ar928x_eeprom_valid_mac(out);
}
