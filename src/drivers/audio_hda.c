#include "audio_hda.h"
#include "pci/pci.h"
#include "../arch/x86_64/mmio.h"
#include "../kernel/klog.h"
#include "../lib/string.h"

#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_HDA     0x03
#define HDA_MMIO_SIZE        0x4000

static struct hda_controller_info controller;
static bool present;

static uint16_t mmio_read16(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint16_t *)(base + offset);
}

static uint8_t mmio_read8(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint8_t *)(base + offset);
}

static void inspect_audio_device(const struct pci_device_info *device, void *context) {
    (void)context;

    if (present) {
        return;
    }
    if (device->class_code != PCI_CLASS_MULTIMEDIA
        || device->subclass != PCI_SUBCLASS_HDA) {
        return;
    }

    memset(&controller, 0, sizeof(controller));
    controller.vendor_id = device->vendor_id;
    controller.device_id = device->device_id;
    controller.bus = device->bus;
    controller.slot = device->slot;
    controller.function = device->function;
    present = true;

    uint64_t bar0 = pci_read_bar(device->bus, device->slot, device->function, 0);
    if (bar0 == 0) {
        klog(KLOG_WARN, "audio: HDA BAR0 is empty");
        return;
    }
    volatile uint8_t *regs = (volatile uint8_t *)mmio_map(bar0, HDA_MMIO_SIZE);
    if (regs) {
        uint16_t capabilities = mmio_read16(regs, 0x00);
        controller.output_streams = (uint8_t)((capabilities >> 12) & 0x0F);
        controller.input_streams = (uint8_t)((capabilities >> 8) & 0x0F);
        controller.bidirectional_streams = (uint8_t)((capabilities >> 3) & 0x1F);
        controller.minor_version = mmio_read8(regs, 0x02);
        controller.major_version = mmio_read8(regs, 0x03);
        controller.mmio_ready = true;
    } else {
        klogf(KLOG_WARN, "audio: HDA BAR0 0x%llx could not be mapped", bar0);
    }
}

void hda_init(void) {
    present = false;
    memset(&controller, 0, sizeof(controller));
    pci_enumerate(inspect_audio_device, 0);

    if (!present) {
        klog(KLOG_WARN, "audio: no Intel HDA controller found on PCI");
        return;
    }

    klogf(
        KLOG_INFO,
        "audio: HDA %04x:%04x at %u:%u.%u mmio=%s out=%u in=%u bidi=%u",
        controller.vendor_id,
        controller.device_id,
        controller.bus,
        controller.slot,
        controller.function,
        controller.mmio_ready ? "ready" : "missing",
        controller.output_streams,
        controller.input_streams,
        controller.bidirectional_streams
    );
}

bool hda_is_present(void) {
    return present;
}

bool hda_pcm_output_ready(void) {
    if (present && !controller.mmio_ready) {
        klog(KLOG_WARN, "audio: HDA controller found but MMIO is unavailable");
    }
    return false;
}

bool hda_get_controller_info(struct hda_controller_info *out) {
    if (!out || !present) {
        return false;
    }

    *out = controller;
    return true;
}
