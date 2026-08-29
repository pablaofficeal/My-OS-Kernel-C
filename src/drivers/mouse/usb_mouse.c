#include "usb_mouse.h"

#include "ps2_mouse.h"
#include "../usb/xhci.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../kernel/process/scheduler.h"

static struct usb_mouse_info info;
static volatile bool polling;

void usb_mouse_attach(uint16_t vendor_id, uint16_t product_id, uint8_t port){
    info.vendor_id=vendor_id;
    info.product_id=product_id;
    info.port=port;
    info.connected=true;
    klogf(KLOG_OK,"usbhid: boot mouse ready on xHCI port%u (%04x:%04x)",
          port,vendor_id,product_id);
}

void usb_mouse_detach(void){
    if(info.connected) klog(KLOG_WARN,"usbhid: mouse disconnected");
    info.connected=false;
}

void usb_mouse_report(const uint8_t *report, uint32_t length){
    if(!info.connected || !report || length<3) return;
    // HID boot mouse: byte0 bits 0-2 buttons, byte1 X, byte2 Y.
    // Buttons mask like PS/2 (3 bits), Y orientation matches screen (positive down) – не инвертим как PS/2, чтобы не ломать QEMU.
    // На реальном железе некоторые мыши шлют 4 байта (wheel в byte3) – игнорируем.
    uint8_t buttons = report[0] & 0x07;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    mouse_handle_relative(buttons, dx, dy);
    info.reports++;
}

void usb_mouse_poll(void){
    if(!info.connected) return;
    if(__atomic_test_and_set(&polling,__ATOMIC_ACQUIRE)) return;
    xhci_poll_mouse();
    __atomic_clear(&polling,__ATOMIC_RELEASE);
}

void usb_mouse_service_thread(void *arg){
    (void)arg;
    for(;;){
        usb_mouse_poll();
        scheduler_sleep(1);
    }
}

struct usb_mouse_info usb_mouse_get_info(void){ return info; }
