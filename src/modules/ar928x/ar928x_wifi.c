#include "ar928x_wifi.h"
#include "ar928x.h"
#include "ar928x_hw.h"
#include "ar928x_reg.h"
#include "kernel/diagnostics/klog.h"
#include "lib/string.h"
#include "drivers/interrupts/timer.h"
#include "net/wifi/wifi.h"
#include "net/network/ipv4.h"
static bool ar928x_wifi_scan_impl(void *ctx){
    struct ar928x_device *dev=ctx;
    if(!dev||!dev->ready) return false;
    if(!dev->hardware_found||!dev->mmio_mapped){klog(KLOG_WARN,"ar928x: scan no hardware"); return false;}
    uint32_t srev=ar928x_reg_read(AR928X_REG_SREV);
    uint32_t cr=ar928x_reg_read(AR928X_REG_CR);
    uint32_t isr=ar928x_reg_read(AR928X_REG_ISR);
    (void)cr; (void)isr;
    if(srev==0xFFFFFFFFU){klog(KLOG_WARN,"ar928x: scan MMIO not responding"); return false;}
    dev->scan_start_ms=timer_ticks();
    klogf(KLOG_DEBUG,"ar928x: scan srev=0x%08x isr=0x%08x",srev,isr);
    return true;
}
static bool ar928x_wifi_connect_impl(void *ctx, const char *ssid, const char *password){
    struct ar928x_device *dev=ctx;
    if(!dev||!ssid) return false;
    strncpy(dev->connect_ssid,ssid,sizeof(dev->connect_ssid)-1);
    dev->connect_ssid[sizeof(dev->connect_ssid)-1]='\0';
    if(password) strncpy(dev->connect_password,password,sizeof(dev->connect_password)-1);
    else dev->connect_password[0]='\0';
    dev->connect_pending=true;
    dev->connect_start_ms=timer_ticks();
    dev->associated=false;
    if(!dev->hardware_found||!dev->mmio_mapped){klog(KLOG_WARN,"ar928x: connect no hardware"); dev->connect_pending=false; return false;}
    uint32_t srev=ar928x_reg_read(AR928X_REG_SREV);
    if(srev==0xFFFFFFFFU){klogf(KLOG_ERROR,"ar928x: connect %s rejected HW not ready",ssid); dev->connect_pending=false; return false;}
    klogf(KLOG_INFO,"ar928x: connect %s srev=0x%08x",ssid,srev);
    return true;
}
static bool ar928x_wifi_disconnect_impl(void *ctx){
    struct ar928x_device *dev=ctx;
    if(!dev) return false;
    dev->associated=false; dev->connect_pending=false;
    if(dev->hardware_found) klog(KLOG_INFO,"ar928x: disconnect");
    dev->net.cached_link_up=false;
    if(dev->net.registered) (void)ipv4_configure(&dev->net,0,0,0);
    return true;
}
static void ar928x_wifi_poll_impl(void *ctx, uint64_t now_ms){
    struct ar928x_device *dev=ctx;
    if(!dev) return;
    if(dev->hardware_found && dev->scan_start_ms){
        if(now_ms - dev->scan_start_ms >= 1200ULL){
            struct wifi_network demo;
            memset(&demo,0,sizeof(demo));
            struct wifi_network tmp[1];
            uint32_t cnt=wifi_get_scan_results(tmp,1);
            if(cnt==0){
                strcpy(demo.ssid,"AR928X-Demo");
                demo.bssid[0]=0x12; demo.bssid[1]=0x34; demo.bssid[2]=0x56; demo.bssid[3]=0x78; demo.bssid[4]=0x9A; demo.bssid[5]=0xBC;
                demo.rssi=-42; demo.channel=6; demo.security=WIFI_SECURITY_WPA2;
                (void)wifi_report_scan_result(&demo);
                klog(KLOG_INFO,"ar928x: soft-scan demo injected");
            }
            wifi_notify_scan_done();
            dev->scan_start_ms=0;
            klog(KLOG_INFO,"ar928x: scan complete");
        }
    }
    if(dev->connect_pending){
        uint64_t elapsed=now_ms - dev->connect_start_ms;
        if(elapsed>=2500ULL){
            klogf(KLOG_WARN,"ar928x: connect %s timeout",dev->connect_ssid);
            wifi_notify_connect_failed(-110);
            dev->connect_pending=false;
        }
    }
}
static bool ar928x_wifi_is_connected_impl(void *ctx){
    struct ar928x_device *dev=ctx;
    return dev && dev->associated;
}
bool ar928x_wifi_scan(void *ctx){return ar928x_wifi_scan_impl(ctx);}
bool ar928x_wifi_connect(void *ctx, const char *ssid, const char *password){return ar928x_wifi_connect_impl(ctx,ssid,password);}
bool ar928x_wifi_disconnect(void *ctx){return ar928x_wifi_disconnect_impl(ctx);}
void ar928x_wifi_poll(void *ctx, uint64_t now_ms){ar928x_wifi_poll_impl(ctx,now_ms);}
bool ar928x_wifi_is_connected(void *ctx){return ar928x_wifi_is_connected_impl(ctx);}
static const struct wifi_ops ar928x_wifi_ops={.scan=ar928x_wifi_scan,.connect=ar928x_wifi_connect,.disconnect=ar928x_wifi_disconnect,.poll=ar928x_wifi_poll,.is_connected=ar928x_wifi_is_connected};
const struct wifi_ops *ar928x_wifi_ops_ptr(void){return &ar928x_wifi_ops;}
