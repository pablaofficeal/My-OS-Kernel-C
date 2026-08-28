#include "audio_hda.h"
#include "pci/pci.h"
#include "../arch/x86_64/mmio.h"
#include "../kernel/klog.h"
#include "../lib/string.h"

#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_HDA 0x03
#define HDA_MMIO_SIZE 0x4000
#define HDA_TIMEOUT 1000000U
#define HDA_GCTL 0x08
#define HDA_STATESTS 0x0E
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP 0x48
#define HDA_CORBRP 0x4A
#define HDA_CORBCTL 0x4C
#define HDA_CORBSIZE 0x4E
#define HDA_RIRBLBASE 0x50
#define HDA_RIRBUBASE 0x54
#define HDA_RIRBWP 0x58
#define HDA_RINTCNT 0x5A
#define HDA_RIRBCTL 0x5C
#define HDA_RIRBSTS 0x5D
#define HDA_STREAM_BASE 0x80
#define HDA_STREAM_DESCRIPTOR_SIZE 0x20
#define HDA_STREAM_CTL 0x00
#define HDA_STREAM_CBL 0x08
#define HDA_STREAM_LVI 0x0C
#define HDA_STREAM_FMT 0x12
#define HDA_STREAM_BDPL 0x18
#define HDA_STREAM_BDPU 0x1C
#define HDA_GCTL_RESET 0x01
#define HDA_STREAM_RESET 0x01
#define HDA_STREAM_RUN 0x02
#define HDA_WIDGET_AUDIO_OUTPUT 0x00
#define HDA_WIDGET_PIN 0x04
#define HDA_PARAMETER_SUBNODES 0x04
#define HDA_PARAMETER_FUNCTION_GROUP_TYPE 0x05
#define HDA_PARAMETER_WIDGET_CAPS 0x09
#define HDA_PARAMETER_PIN_CAPS 0x0C
#define HDA_PARAMETER_CONNECTION_LIST_LENGTH 0x0E
#define HDA_FUNCTION_GROUP_AUDIO 0x01
#define HDA_PIN_CAP_OUTPUT 0x10
#define HDA_VERB_GET_PARAMETER 0x0F00
#define HDA_VERB_SET_CONNECTION_SELECT 0x701
#define HDA_VERB_SET_STREAM_FORMAT 0x200
#define HDA_VERB_SET_CONVERTER_STREAM_CHANNEL 0x706
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707
#define HDA_VERB_SET_EAPD_BTL 0x70C
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300
#define HDA_PCM_FORMAT 0x4011
#define HDA_STREAM_TAG 1
#define HDA_PCM_RATE 44100U
#define HDA_PCM_SAMPLES 4096U
#define HDA_PCM_BYTES (HDA_PCM_SAMPLES * 4U)

struct hda_bdl_entry {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed));

static struct hda_controller_info controller;
static volatile uint8_t *regs;
static bool present;
static bool pcm_ready;
static bool mapping_ready;
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static uint8_t codec_address;
static uint8_t dac_node;
static uint8_t pin_node;
static uint32_t corb[256] __attribute__((aligned(4096)));
static uint64_t rirb[256] __attribute__((aligned(4096)));
static struct hda_bdl_entry bdl[2] __attribute__((aligned(128)));
static uint8_t pcm_buffer[HDA_PCM_BYTES * 2U] __attribute__((aligned(4096)));

static uint16_t read16(uint32_t offset) {
    return *(volatile uint16_t *)(regs + offset);
}

static uint8_t read8(uint32_t offset) {
    return *(volatile uint8_t *)(regs + offset);
}

static uint32_t read32(uint32_t offset) {
    return *(volatile uint32_t *)(regs + offset);
}

static void write16(uint32_t offset, uint16_t value) {
    *(volatile uint16_t *)(regs + offset) = value;
    klogf(KLOG_DEBUG, "audio: HDA MMIO write16 off=0x%03x value=0x%04x",
          offset, value);
}

static void write8(uint32_t offset, uint8_t value) {
    *(volatile uint8_t *)(regs + offset) = value;
    klogf(KLOG_DEBUG, "audio: HDA MMIO write8 off=0x%03x value=0x%02x",
          offset, value);
}

static void write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(regs + offset) = value;
    klogf(KLOG_DEBUG, "audio: HDA MMIO write32 off=0x%03x value=0x%08x",
          offset, value);
}

static void trace_ring(const char *stage) {
    klogf(KLOG_DEBUG,
          "audio: HDA %s CORBWP=0x%04x CORBRP=0x%04x CORBCTL=0x%02x CORBSIZE=0x%02x RIRBWP=0x%04x RIRBCTL=0x%02x RIRBSTS=0x%02x STATESTS=0x%04x",
          stage, read16(HDA_CORBWP), read16(HDA_CORBRP), read8(HDA_CORBCTL),
          read8(HDA_CORBSIZE), read16(HDA_RIRBWP), read8(HDA_RIRBCTL),
          read8(HDA_RIRBSTS), read16(HDA_STATESTS));
}

static uint64_t physical_address(const void *pointer) {
    return (uint64_t)(uintptr_t)pointer - kernel_virtual_base + kernel_physical_base;
}

static bool wait_reset_state(bool asserted) {
    klogf(KLOG_DEBUG, "audio: HDA reset wait target=%u", asserted ? 1 : 0);
    for (uint32_t wait = 0; wait < HDA_TIMEOUT; wait++) {
        if (((read32(HDA_GCTL) & HDA_GCTL_RESET) != 0) == asserted) {
            return true;
        }
        __asm__ volatile("pause");
    }
    klogf(KLOG_ERROR, "audio: HDA reset wait timeout target=%u", asserted ? 1 : 0);
    return false;
}

static uint32_t read_stream_control(volatile uint8_t *stream) {
    uint32_t low = *(volatile uint16_t *)(stream + HDA_STREAM_CTL);
    uint32_t high = *(volatile uint8_t *)(stream + HDA_STREAM_CTL + 2);
    return low | (high << 16);
}

static void write_stream_control(volatile uint8_t *stream, uint32_t value) {
    *(volatile uint16_t *)(stream + HDA_STREAM_CTL) = (uint16_t)value;
    *(volatile uint8_t *)(stream + HDA_STREAM_CTL + 2) = (uint8_t)(value >> 16);
}

static bool wait_stream_control(volatile uint8_t *stream, uint32_t mask,
                                bool asserted) {
    for (uint32_t wait = 0; wait < HDA_TIMEOUT; wait++) {
        if (((read_stream_control(stream) & mask) != 0) == asserted) {
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool reset_stream(volatile uint8_t *stream) {
    uint32_t control = read_stream_control(stream) & ~HDA_STREAM_RUN;
    write_stream_control(stream, control);
    if (!wait_stream_control(stream, HDA_STREAM_RUN, false)) {
        klog(KLOG_ERROR, "audio: HDA stream RUN clear timeout");
        return false;
    }
    write_stream_control(stream, control | HDA_STREAM_RESET);
    if (!wait_stream_control(stream, HDA_STREAM_RESET, true)) {
        klog(KLOG_ERROR, "audio: HDA stream reset assert timeout");
        return false;
    }
    write_stream_control(stream, control & ~HDA_STREAM_RESET);
    if (!wait_stream_control(stream, HDA_STREAM_RESET, false)) {
        klog(KLOG_ERROR, "audio: HDA stream reset deassert timeout");
        return false;
    }
    return true;
}

static bool reset_controller(void) {
    klogf(KLOG_DEBUG, "audio: HDA reset begin GCTL=0x%08x", read32(HDA_GCTL));
    write32(HDA_GCTL, read32(HDA_GCTL) & ~HDA_GCTL_RESET);
    if (!wait_reset_state(false)) {
        klogf(KLOG_ERROR, "audio: HDA reset deassert timeout GCTL=0x%08x",
              read32(HDA_GCTL));
        return false;
    }
    write32(HDA_GCTL, read32(HDA_GCTL) | HDA_GCTL_RESET);
    if (!wait_reset_state(true)) {
        klogf(KLOG_ERROR, "audio: HDA reset assert timeout GCTL=0x%08x",
              read32(HDA_GCTL));
        return false;
    }
    klogf(KLOG_DEBUG, "audio: HDA reset complete GCTL=0x%08x STATESTS=0x%04x",
          read32(HDA_GCTL), read16(HDA_STATESTS));
    return true;
}

static bool setup_command_ring(void) {
    klog(KLOG_DEBUG, "audio: HDA command ring setup begin");
    uint64_t corb_address = physical_address(corb);
    uint64_t rirb_address = physical_address(rirb);
    write8(HDA_CORBCTL, 0);
    write8(HDA_RIRBCTL, 0);
    write16(HDA_CORBRP, 0x8000);
    write16(HDA_CORBRP, 0x0000);
    write32(HDA_CORBLBASE, (uint32_t)corb_address);
    write32(HDA_CORBUBASE, (uint32_t)(corb_address >> 32));
    write32(HDA_RIRBLBASE, (uint32_t)rirb_address);
    write32(HDA_RIRBUBASE, (uint32_t)(rirb_address >> 32));
    write16(HDA_CORBWP, 0);
    write16(HDA_RIRBWP, 0x8000);
    write16(HDA_RIRBWP, 0x0000);
    write16(HDA_RINTCNT, 1);
    write8(HDA_CORBSIZE, (uint8_t)((read8(HDA_CORBSIZE) & 0xF0) | 0x02));
    write8(HDA_CORBCTL, 0x02);
    write8(HDA_RIRBCTL, 0x02);
    bool ready = (read8(HDA_CORBCTL) & 0x02) != 0
        && (read8(HDA_RIRBCTL) & 0x02) != 0;
    klogf(KLOG_DEBUG, "audio: HDA ring pointers reset corbrp=0x%04x rirbwp=0x%04x",
          read16(HDA_CORBRP), read16(HDA_RIRBWP));
    klogf(ready ? KLOG_DEBUG : KLOG_ERROR,
          "audio: HDA CORB/RIRB corb=0x%llx rirb=0x%llx size=0x%04x ctl=0x%04x/0x%04x ready=%u",
          corb_address, rirb_address, read8(HDA_CORBSIZE),
          read8(HDA_CORBCTL), read8(HDA_RIRBCTL), ready ? 1 : 0);
    return ready;
}

static bool send_verb(uint32_t verb, uint32_t *response) {
    uint16_t old_write = read16(HDA_CORBWP) & 0xFF;
    uint16_t next_write = (uint16_t)((old_write + 1) & 0xFF);
    uint16_t old_response = read16(HDA_RIRBWP) & 0xFF;
    uint8_t codec = (uint8_t)(verb >> 28);
    uint8_t node = (uint8_t)((verb >> 20) & 0x7F);
    uint16_t command = (uint16_t)((verb >> 8) & 0x0FFF);
    uint8_t payload = (uint8_t)verb;
    klogf(KLOG_DEBUG,
          "audio: HDA VERB OUT raw=0x%08x codec=%u node=0x%02x verb=0x%03x payload=0x%02x slot=%u",
          verb, codec, node, command, payload, next_write);
    trace_ring("before-submit");
    corb[next_write] = verb;
    write16(HDA_CORBWP, next_write);
    klogf(KLOG_DEBUG, "audio: HDA CORB[%u] phys=0x%llx value=0x%08x",
          next_write, physical_address(&corb[next_write]), corb[next_write]);
    trace_ring("after-submit");
    for (uint32_t wait = 0; wait < HDA_TIMEOUT; wait++) {
        uint16_t current_response = read16(HDA_RIRBWP) & 0xFF;
        if (current_response != old_response) {
            uint16_t response_index = (uint16_t)((old_response + 1) & 0xFF);
            if (response) {
                *response = (uint32_t)rirb[response_index];
                klogf(KLOG_DEBUG,
                      "audio: HDA VERB IN raw=0x%08x response_slot=%u phys=0x%llx",
                      *response, response_index,
                      physical_address(&rirb[response_index]));
            } else {
                klogf(KLOG_DEBUG, "audio: HDA VERB IN unsolicited response ignored slot=%u",
                      response_index);
            }
            trace_ring("response-received");
            return true;
        }
        __asm__ volatile("pause");
    }
    trace_ring("response-timeout");
    klogf(KLOG_ERROR,
          "audio: HDA VERB TIMEOUT raw=0x%08x codec=%u node=0x%02x verb=0x%03x payload=0x%02x expected_rirbwp=0x%04x",
          verb, codec, node, command, payload, old_response);
    return false;
}

static bool codec_parameter(uint8_t node, uint8_t parameter, uint32_t *value) {
    klogf(KLOG_DEBUG, "audio: codec%u GET_PARAMETER node=0x%02x parameter=0x%02x",
          codec_address, node, parameter);
    return send_verb(((uint32_t)codec_address << 28)
                     | ((uint32_t)node << 20)
                     | ((uint32_t)HDA_VERB_GET_PARAMETER << 8)
                     | parameter, value);
}

static bool codec_command(uint8_t node, uint16_t verb, uint16_t payload) {
    klogf(KLOG_DEBUG, "audio: codec%u command node=0x%02x verb=0x%03x payload=0x%04x",
          codec_address, node, verb, payload);
    return send_verb(((uint32_t)codec_address << 28)
                     | ((uint32_t)node << 20)
                     | ((uint32_t)verb << 8) | payload, 0);
}

static uint8_t subnode_start(uint32_t value) {
    return (uint8_t)(value >> 16);
}

static uint8_t subnode_count(uint32_t value) {
    return (uint8_t)value;
}

static bool find_audio_function_group(uint32_t root_subnodes,
                                      uint8_t *function_group) {
    uint8_t first_group = subnode_start(root_subnodes);
    uint8_t group_count = subnode_count(root_subnodes);
    for (uint8_t index = 0; index < group_count; index++) {
        uint8_t node = (uint8_t)(first_group + index);
        uint32_t type;
        if (!codec_parameter(node, HDA_PARAMETER_FUNCTION_GROUP_TYPE, &type)) {
            continue;
        }
        if ((type & 0xFF) == HDA_FUNCTION_GROUP_AUDIO) {
            *function_group = node;
            return true;
        }
    }
    return false;
}

static bool discover_codec(void) {
    klogf(KLOG_INFO, "audio: codec%u discovery begin", codec_address);
    uint32_t subnodes;
    uint32_t function_group;
    if (!codec_parameter(0, HDA_PARAMETER_SUBNODES, &subnodes)) {
        klogf(KLOG_ERROR, "audio: codec%u root subnode query failed", codec_address);
        return false;
    }
    uint8_t first_group = 0;
    uint8_t group_count = subnode_count(subnodes);
    if (group_count == 0 || !find_audio_function_group(subnodes, &first_group)
        || !codec_parameter(first_group, HDA_PARAMETER_SUBNODES, &function_group)) {
        klogf(KLOG_ERROR, "audio: codec%u function group query failed first=%u count=%u",
              codec_address, first_group, group_count);
        return false;
    }
    uint8_t first_widget = subnode_start(function_group);
    uint8_t widget_count = subnode_count(function_group);
    uint32_t vendor_id = 0;
    uint32_t revision_id = 0;
    (void)codec_parameter(0, 0x00, &vendor_id);
    (void)codec_parameter(0, 0x02, &revision_id);
    klogf(KLOG_INFO, "audio: codec%u vendor=0x%08x revision=0x%08x root=0x%08x group=0x%08x",
          codec_address, vendor_id, revision_id, subnodes, function_group);
    for (uint8_t index = 0; index < widget_count; index++) {
        uint8_t node = (uint8_t)(first_widget + index);
        uint32_t capabilities;
        if (!codec_parameter(node, HDA_PARAMETER_WIDGET_CAPS, &capabilities)) {
            klogf(KLOG_WARN, "audio: codec%u node%u widget caps query failed",
                  codec_address, node);
            continue;
        }
        uint8_t type = (uint8_t)((capabilities >> 20) & 0x0F);
        uint32_t connection_length = 0;
        (void)codec_parameter(node, HDA_PARAMETER_CONNECTION_LIST_LENGTH,
                               &connection_length);
        klogf(KLOG_DEBUG, "audio: codec%u node%u caps=0x%08x type=0x%x connections=0x%08x",
              codec_address, node, capabilities, type, connection_length);
        if (type == HDA_WIDGET_AUDIO_OUTPUT && dac_node == 0) {
            dac_node = node;
        }
        if (type == HDA_WIDGET_PIN) {
            uint32_t pin_capabilities = 0;
            if (codec_parameter(node, HDA_PARAMETER_PIN_CAPS,
                                &pin_capabilities)) {
                klogf(KLOG_DEBUG, "audio: codec%u node%u pin caps=0x%08x output=%u",
                      codec_address, node, pin_capabilities,
                      (pin_capabilities & HDA_PIN_CAP_OUTPUT) != 0 ? 1 : 0);
                if ((pin_capabilities & HDA_PIN_CAP_OUTPUT) != 0
                    && pin_node == 0) {
                    pin_node = node;
                }
            }
        }
    }
    klogf(KLOG_INFO, "audio: codec%u discovery result dac=%u pin=%u",
          codec_address, dac_node, pin_node);
    klogf(KLOG_INFO, "audio: HDA codec %u group=%u widgets=%u dac=%u pin=%u",
          codec_address, first_group, widget_count, dac_node, pin_node);
    return dac_node != 0 && pin_node != 0;
}

static bool configure_codec(void) {
    klogf(KLOG_INFO, "audio: configuring codec%u pin=%u dac=%u format=0x%04x",
          codec_address, pin_node, dac_node, HDA_PCM_FORMAT);
    if (!codec_command(pin_node, HDA_VERB_SET_PIN_WIDGET_CONTROL, 0x40)) {
        klog(KLOG_ERROR, "audio: failed to enable output pin");
        return false;
    }
    if (!codec_command(pin_node, HDA_VERB_SET_EAPD_BTL, 0x02)) {
        klog(KLOG_ERROR, "audio: failed to enable codec amplifier");
        return false;
    }
    if (!codec_command(pin_node, HDA_VERB_SET_CONNECTION_SELECT, 0)) {
        klog(KLOG_ERROR, "audio: failed to select pin connection index 0");
        return false;
    }
    if (!codec_command(dac_node, HDA_VERB_SET_CONVERTER_STREAM_CHANNEL,
                       HDA_STREAM_TAG << 4)) {
        klog(KLOG_ERROR, "audio: failed to bind DAC to output stream");
        return false;
    }
    if (!codec_command(dac_node, HDA_VERB_SET_STREAM_FORMAT, HDA_PCM_FORMAT)) {
        klog(KLOG_ERROR, "audio: failed to set DAC stream format");
        return false;
    }
    if (!codec_command(dac_node, HDA_VERB_SET_AMP_GAIN_MUTE, 0xB07F)) {
        klog(KLOG_ERROR, "audio: failed to set DAC amplifier gain");
        return false;
    }
    return true;
}

static bool configure_stream(void) {
    klog(KLOG_INFO, "audio: HDA PCM stream configuration begin");
    uint64_t bdl_address = physical_address(bdl);
    uint64_t pcm_address = physical_address(pcm_buffer);
    uint32_t stream_offset = HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    volatile uint8_t *stream = regs + stream_offset;
    uint32_t control = read_stream_control(stream);
    klogf(KLOG_DEBUG, "audio: HDA stream before ctl=0x%08x sts=0x%02x lpib=0x%08x",
          control, *(volatile uint8_t *)(stream + 0x03),
          *(volatile uint32_t *)(stream + 0x04));
    if (!reset_stream(stream)) {
        return false;
    }
    bdl[0].address = pcm_address;
    bdl[0].length = HDA_PCM_BYTES;
    bdl[0].flags = 1;
    bdl[1].address = pcm_address + HDA_PCM_BYTES;
    bdl[1].length = HDA_PCM_BYTES;
    bdl[1].flags = 1;
    *(volatile uint32_t *)(stream + HDA_STREAM_CBL) = HDA_PCM_BYTES * 2U;
    *(volatile uint16_t *)(stream + HDA_STREAM_LVI) = 1;
    *(volatile uint16_t *)(stream + HDA_STREAM_FMT) = HDA_PCM_FORMAT;
    *(volatile uint32_t *)(stream + HDA_STREAM_BDPL) = (uint32_t)bdl_address;
    *(volatile uint32_t *)(stream + HDA_STREAM_BDPU) = (uint32_t)(bdl_address >> 32);
    write_stream_control(stream, (uint32_t)HDA_STREAM_TAG << 20);
    klogf(KLOG_DEBUG, "audio: HDA stream programmed base=0x%x ctl=0x%08x cbl=%u lvi=%u fmt=0x%04x bdl=0x%llx",
          stream_offset, read_stream_control(stream),
          HDA_PCM_BYTES * 2U, 1, HDA_PCM_FORMAT, bdl_address);
    return true;
}

static bool setup_pcm(void) {
    klog(KLOG_INFO, "audio: HDA PCM setup begin");
    if (!mapping_ready || !controller.mmio_ready || controller.output_streams == 0) {
        klog(KLOG_WARN, "audio: HDA PCM prerequisites unavailable");
        return false;
    }
    if (!reset_controller() || !setup_command_ring()) {
        klog(KLOG_WARN, "audio: HDA reset or command ring failed");
        return false;
    }
    uint16_t state = read16(HDA_STATESTS);
    klogf(KLOG_INFO, "audio: HDA codec presence STATESTS=0x%04x", state);
    for (uint8_t address = 0; address < 15; address++) {
        klogf(KLOG_DEBUG, "audio: HDA checking codec address=%u present=%u",
              address, (state & (1U << address)) != 0 ? 1 : 0);
        if ((state & (1U << address)) == 0) {
            continue;
        }
        codec_address = address;
        if (discover_codec() && configure_codec() && configure_stream()) {
            pcm_ready = true;
            klog(KLOG_OK, "audio: HDA PCM output stream configured at 44.1 kHz");
            return true;
        }
        klogf(KLOG_WARN, "audio: codec%u setup attempt failed, trying next codec",
              codec_address);
        dac_node = 0;
        pin_node = 0;
    }
    klog(KLOG_WARN, "audio: HDA codecs present but no usable output route found");
    return false;
}

static void inspect_audio_device(const struct pci_device_info *device, void *context) {
    (void)context;
    if (present || device->class_code != PCI_CLASS_MULTIMEDIA
        || device->subclass != PCI_SUBCLASS_HDA) {
        return;
    }
    klogf(KLOG_INFO, "audio: HDA PCI candidate %u:%u.%u id=%04x:%04x class=%02x/%02x prog=%02x",
          device->bus, device->slot, device->function, device->vendor_id,
          device->device_id, device->class_code, device->subclass,
          device->programming_interface);
    memset(&controller, 0, sizeof(controller));
    controller.vendor_id = device->vendor_id;
    controller.device_id = device->device_id;
    controller.bus = device->bus;
    controller.slot = device->slot;
    controller.function = device->function;
    uint32_t command = pci_read_config32(device->bus, device->slot, device->function, 0x04);
    pci_write_config32(device->bus, device->slot, device->function, 0x04, command | 0x00000006);
    uint64_t bar0 = pci_read_bar(device->bus, device->slot, device->function, 0);
    if (bar0 == 0) {
        klog(KLOG_WARN, "audio: HDA BAR0 is empty");
        return;
    }
    regs = (volatile uint8_t *)mmio_map(bar0, HDA_MMIO_SIZE);
    if (!regs) {
        klog(KLOG_WARN, "audio: HDA BAR0 could not be mapped");
        return;
    }
    uint16_t capabilities = read16(0x00);
    controller.output_streams = (uint8_t)((capabilities >> 12) & 0x0F);
    controller.input_streams = (uint8_t)((capabilities >> 8) & 0x0F);
    controller.bidirectional_streams = (uint8_t)((capabilities >> 3) & 0x1F);
    controller.minor_version = *((volatile uint8_t *)(regs + 0x02));
    controller.major_version = *((volatile uint8_t *)(regs + 0x03));
    controller.mmio_ready = true;
    present = true;
    klogf(KLOG_INFO, "audio: HDA PCI %u:%u.%u command 0x%08x BAR0=0x%llx GCAP=0x%04x version=%u.%u",
          device->bus, device->slot, device->function, command | 0x00000006,
          bar0, capabilities, controller.major_version, controller.minor_version);
}

void hda_set_address_mapping(uint64_t physical_base, uint64_t virtual_base) {
    kernel_physical_base = physical_base;
    kernel_virtual_base = virtual_base;
    mapping_ready = true;
}

void hda_init(void) {
    klog(KLOG_INFO, "audio: HDA init begin");
    present = false;
    pcm_ready = false;
    regs = 0;
    memset(&controller, 0, sizeof(controller));
    pci_enumerate(inspect_audio_device, 0);
    if (!present) {
        klog(KLOG_WARN, "audio: no Intel HDA controller found on PCI");
        return;
    }
    klogf(KLOG_INFO, "audio: HDA %04x:%04x at %u:%u.%u out=%u in=%u",
          controller.vendor_id, controller.device_id, controller.bus,
          controller.slot, controller.function, controller.output_streams,
          controller.input_streams);
    setup_pcm();
    klogf(KLOG_INFO, "audio: HDA init complete present=%u pcm_ready=%u",
          present ? 1 : 0, pcm_ready ? 1 : 0);
}

bool hda_is_present(void) {
    return present;
}

bool hda_pcm_output_ready(void) {
    return pcm_ready;
}

bool hda_play_tone(uint16_t frequency_hz, uint8_t volume) {
    if (!pcm_ready || frequency_hz == 0 || volume == 0) {
        return false;
    }
    uint32_t amplitude = (32767U * volume) / 100U;
    klogf(KLOG_DEBUG, "audio: HDA PCM fill begin freq=%u volume=%u amplitude=%u bytes=%u",
          frequency_hz, volume, amplitude, HDA_PCM_BYTES * 2U);
    for (uint32_t frame = 0; frame < HDA_PCM_SAMPLES * 2U; frame++) {
        uint32_t phase = (uint32_t)(((uint64_t)frame * frequency_hz) % HDA_PCM_RATE);
        int16_t value = phase < HDA_PCM_RATE / 2U
            ? (int16_t)amplitude : (int16_t)-amplitude;
        uint32_t offset = frame * 4U;
        pcm_buffer[offset] = (uint8_t)value;
        pcm_buffer[offset + 1] = (uint8_t)(value >> 8);
        pcm_buffer[offset + 2] = (uint8_t)value;
        pcm_buffer[offset + 3] = (uint8_t)(value >> 8);
    }
    volatile uint8_t *stream = regs + HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    uint32_t control = read_stream_control(stream);
    write_stream_control(stream, control | HDA_STREAM_RUN);
    klogf(KLOG_INFO, "audio: HDA PCM stream RUN ctl=0x%08x sts=0x%02x lpib=0x%08x",
          read_stream_control(stream),
          *(volatile uint8_t *)(stream + 0x03),
          *(volatile uint32_t *)(stream + 0x04));
    return true;
}

void hda_stop_tone(void) {
    if (!pcm_ready || !regs) {
        return;
    }
    volatile uint8_t *stream = regs + HDA_STREAM_BASE
        + (uint32_t)controller.input_streams * HDA_STREAM_DESCRIPTOR_SIZE;
    uint32_t control = read_stream_control(stream);
    write_stream_control(stream, control & ~HDA_STREAM_RUN);
    klogf(KLOG_DEBUG, "audio: HDA PCM stream STOP ctl=0x%08x sts=0x%02x",
          read_stream_control(stream),
          *(volatile uint8_t *)(stream + 0x03));
}

bool hda_get_controller_info(struct hda_controller_info *out) {
    if (!out || !present) {
        return false;
    }
    *out = controller;
    return true;
}
