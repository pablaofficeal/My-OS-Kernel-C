#include "net_service.h"
#include "net_device.h"
#include "ethernet.h"
#include "arp.h"
#include "../drivers/net/e1000_82540em.h"
#include "../drivers/interrupts/timer.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/process/scheduler.h"

#define NET_POLL_BUDGET 32U
#define NET_POLL_INTERVAL_MS 1U

static bool ready;

bool net_service_init(void){
    net_device_registry_init();
    ethernet_init();
    if(!arp_init()){
        klog(KLOG_ERROR,"net: cannot register ARP Ethernet handler");
        ready=false;
        return false;
    }
    ready=e1000_82540em_init();
    if(!ready) klog(KLOG_WARN,"net: no supported network adapter found");
    return ready;
}

bool net_service_is_ready(void){ return ready; }

void net_service_thread(void *argument){
    (void)argument;
    struct net_frame frame;
    uint64_t last_housekeeping=timer_ticks();
    for(;;){
        net_device_poll_all(NET_POLL_BUDGET);
        for(uint32_t device_index=0;device_index<net_device_count();device_index++){
            struct net_device *device=net_device_get(device_index);
            for(uint32_t frame_index=0;frame_index<NET_POLL_BUDGET;frame_index++){
                if(!net_device_dequeue(device,&frame)) break;
                (void)ethernet_receive(device,frame.data,frame.length);
            }
        }
        uint64_t now=timer_ticks();
        if(now-last_housekeeping>=1000){
            arp_poll(now);
            last_housekeeping=now;
        }
        scheduler_sleep(NET_POLL_INTERVAL_MS);
    }
}
