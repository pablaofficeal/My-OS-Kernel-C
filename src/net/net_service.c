#include "net_service.h"
#include "net_device.h"
#include "../drivers/net/e1000_82540em.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/process/scheduler.h"

#define NET_POLL_BUDGET 32U
#define NET_POLL_INTERVAL_MS 1U

static bool ready;

bool net_service_init(void){
    net_device_registry_init();
    ready=e1000_82540em_init();
    if(!ready) klog(KLOG_WARN,"net: no supported network adapter found");
    return ready;
}

bool net_service_is_ready(void){ return ready; }

void net_service_thread(void *argument){
    (void)argument;
    for(;;){
        net_device_poll_all(NET_POLL_BUDGET);
        scheduler_sleep(NET_POLL_INTERVAL_MS);
    }
}
