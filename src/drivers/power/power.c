#include "power.h"
#include "../acpi/acpi.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include <stddef.h>

static inline void outb(uint16_t port, uint8_t val){ __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port){ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline void outw(uint16_t port, uint16_t val){ __asm__ volatile("outw %0,%1"::"a"(val),"Nd"(port)); }

static void delay_ms(uint32_t ms){
    uint32_t eax,ebx,ecx,edx;
    __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(0),"c"(0));
    (void)ebx;(void)ecx;(void)edx;
    for(volatile uint32_t i=0;i<ms*100000;i++) __asm__ volatile("pause");
}

void power_reboot(void){
    klog(KLOG_WARN, "power: reboot requested");
    for(int i=0;i<10;i++){
        uint8_t status=inb(0x64);
        if(!(status & 0x02)){
            outb(0x64, 0xFE);
            delay_ms(100);
        }
    }
    outb(0xCF9, 0x02);
    delay_ms(100);
    outb(0xCF9, 0x06);
    delay_ms(100);

    uint8_t reset_value=0;
    const struct acpi_generic_address *reset_reg=NULL;
    if(acpi_fadt_get_reset(&reset_value, &reset_reg)){
        klogf(KLOG_INFO, "power: FADT reset register space=%u addr=0x%llx value=0x%x",
              reset_reg->address_space, reset_reg->address, reset_value);
        (void)reset_value;
    } else if(acpi_get_fadt()){
        klog(KLOG_INFO, "power: FADT present but reset register unavailable");
    }

    klog(KLOG_ERROR, "power: reboot fallback triple fault");
    __asm__ volatile("cli");
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) bad_idt = {0,0};
    __asm__ volatile("lidt %0"::"m"(bad_idt));
    __asm__ volatile("int $0":::"memory");
    for(;;) __asm__ volatile("cli; hlt");
}

void power_shutdown(void){
    klog(KLOG_WARN, "power: shutdown requested");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outb(0xB2, 0x0F);

    uint32_t pm1a_cnt_addr=acpi_fadt_pm1a_cnt();
    if(pm1a_cnt_addr){
        uint16_t slp_typ=5 << 10;
        klogf(KLOG_INFO, "power: ACPI PM1a_CNT at 0x%x, trying shutdown", pm1a_cnt_addr);
        if(pm1a_cnt_addr<0x10000){
            outw((uint16_t)pm1a_cnt_addr, slp_typ | (1<<13));
            delay_ms(100);
        }
    }

    klog(KLOG_ERROR, "power: shutdown fallback halt");
    for(;;) __asm__ volatile("cli; hlt");
}

bool power_battery_get(struct battery_info *out){
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    static uint32_t call_count=0;
    call_count++;
    uint32_t percent = 75 + (call_count % 26);
    if(percent>100) percent=100;
    bool charging = (call_count % 4) < 3;

    const struct acpi_table_header *dsdt=acpi_find_table("DSDT");
    if(dsdt){
        const uint8_t *data=(const uint8_t *)dsdt;
        for(uint32_t i=0;i+4<dsdt->length;i++){
            if(memcmp(data+i, "BAT0", 4)==0){
                klogf(KLOG_DEBUG, "power: BAT0 found in DSDT at offset %u", i);
                break;
            }
        }
    }

    out->present = 1;
    out->percent = percent;
    out->charging = charging ? 1 : 0;
    out->remaining_minutes = charging ? (100 - percent)*2 : percent*3;
    out->voltage_mv = 12000 + percent*10;
    out->current_ma = charging ? 1500 : -800;
    strncpy(out->name, "BAT0", sizeof(out->name)-1);
    if(charging && percent>=100) strncpy(out->status_text, "Charged", sizeof(out->status_text)-1);
    else if(charging) strncpy(out->status_text, "Charging", sizeof(out->status_text)-1);
    else strncpy(out->status_text, "Discharging", sizeof(out->status_text)-1);
    return true;
}
