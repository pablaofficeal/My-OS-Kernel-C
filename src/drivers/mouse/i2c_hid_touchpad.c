#include "i2c_hid_touchpad.h"
#include "ps2_mouse.h"
#include "../gop.h"
#include "../serial.h"

// ASUS Vivobook X1404ZA DSDT: \_SB.PC00.I2C1.ETPD
#define ELAN_I2C_ADDR  0x15
#define HID_DESC_REG   0x0001

#define DW_IC_CON           0x00
#define DW_IC_TAR           0x04
#define DW_IC_DATA_CMD      0x10
#define DW_IC_INTR_MASK     0x30
#define DW_IC_CLR_INTR      0x40
#define DW_IC_CLR_TX_ABRT   0x54
#define DW_IC_ENABLE         0x6C
#define DW_IC_STATUS         0x70
#define DW_IC_RXFLR          0x78
#define DW_IC_ENABLE_STATUS  0x9C

#define DW_STATUS_TFNF       (1U << 1)
#define DW_STATUS_ACTIVITY   (1U << 5)
#define DW_DATA_CMD_READ     (1U << 8)
#define DW_DATA_CMD_STOP     (1U << 9)
#define DW_DATA_CMD_RESTART  (1U << 10)

struct i2c_hid_descriptor {
    uint16_t length;
    uint16_t version;
    uint16_t report_desc_length;
    uint16_t report_desc_register;
    uint16_t input_register;
    uint16_t max_input_length;
    uint16_t output_register;
    uint16_t max_output_length;
    uint16_t command_register;
    uint16_t data_register;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version_id;
    uint32_t reserved;
} __attribute__((packed));

static volatile uint32_t *controller;
static struct i2c_hid_descriptor descriptor;
static volatile struct i2c_hid_debug_state debug_state;

static inline uint32_t readl(uint32_t reg){ return controller[reg / 4]; }
static inline void writel(uint32_t reg, uint32_t value){ controller[reg / 4] = value; }
static uint16_t le16(const uint8_t *value){ return (uint16_t)value[0] | ((uint16_t)value[1] << 8); }
static inline void outl(uint16_t port, uint32_t value){ __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port)); }
static inline uint32_t inl(uint16_t port){ uint32_t value; __asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port)); return value; }

static uint32_t pci_read32(uint8_t device, uint8_t function, uint8_t reg){
    outl(0xCF8, 0x80000000U | ((uint32_t)device << 11) | ((uint32_t)function << 8) | (reg & 0xFC));
    return inl(0xCFC);
}

static void pci_write32(uint8_t device, uint8_t function, uint8_t reg, uint32_t value){
    outl(0xCF8, 0x80000000U | ((uint32_t)device << 11) | ((uint32_t)function << 8) | (reg & 0xFC));
    outl(0xCFC, value);
}

static void draw_hex(uint32_t x, uint32_t y, uint32_t value, int digits){
    char text[9]; const char *hex="0123456789ABCDEF";
    for(int i=digits-1;i>=0;i--){ text[i]=hex[value & 0xF]; value >>= 4; }
    text[digits]=0;
    gop_draw_text_at(x, y, text, 0xCDD6F4, 0x313244);
}

static void draw_debug(void){
    if(!gop_is_available()) return;
    const uint32_t x=320, y=100, bg=0x313244;
    gop_draw_rect(x, y, 310, 62, bg);
    gop_draw_text_at(x+6, y+5, "I2C HID DEBUG", 0xA6E3A1, bg);
    gop_draw_text_at(x+6, y+17, "CTRL DESC READY ERR", 0xCDD6F4, bg);
    draw_hex(x+6,   y+27, debug_state.controller_ready, 2);
    draw_hex(x+46,  y+27, debug_state.descriptor_ready, 2);
    draw_hex(x+86,  y+27, debug_state.device_ready, 2);
    draw_hex(x+136, y+27, debug_state.transfer_errors, 8);
    gop_draw_text_at(x+6, y+39, "VID  PID  MAXIN", 0xCDD6F4, bg);
    draw_hex(x+6,  y+49, debug_state.vendor_id, 4);
    draw_hex(x+46, y+49, debug_state.product_id, 4);
    draw_hex(x+96, y+49, debug_state.max_input_length, 4);
}

static bool wait_for(uint32_t reg, uint32_t mask, bool set){
    for(uint32_t i=0;i<1000000;i++){
        if(((readl(reg) & mask) != 0) == set) return true;
    }
    debug_state.transfer_errors++;
    return false;
}

static bool controller_enable(bool enabled){
    writel(DW_IC_ENABLE, enabled ? 1 : 0);
    return wait_for(DW_IC_ENABLE_STATUS, 1, enabled);
}

// Polled repeated-start transfer. It avoids dependency on APIC before input
// interrupt routing is implemented for the Intel LPSS controller.
static bool transfer(const uint8_t *write, uint32_t write_len, uint8_t *read, uint32_t read_len){
    if(!controller || !controller_enable(false)) return false;
    writel(DW_IC_TAR, ELAN_I2C_ADDR);
    writel(DW_IC_INTR_MASK, 0);
    writel(DW_IC_CLR_INTR, 0);
    writel(DW_IC_CON, 0x65); // master, fast mode, restart enabled, slave disabled
    if(!controller_enable(true)) return false;

    for(uint32_t i=0;i<write_len;i++){
        if(!wait_for(DW_IC_STATUS, DW_STATUS_TFNF, true)) return false;
        uint32_t command=write[i];
        if(i+1==write_len && !read_len) command |= DW_DATA_CMD_STOP;
        writel(DW_IC_DATA_CMD, command);
    }
    for(uint32_t i=0;i<read_len;i++){
        if(!wait_for(DW_IC_STATUS, DW_STATUS_TFNF, true)) return false;
        uint32_t command=DW_DATA_CMD_READ;
        if(i==0 && write_len) command |= DW_DATA_CMD_RESTART;
        if(i+1==read_len) command |= DW_DATA_CMD_STOP;
        writel(DW_IC_DATA_CMD, command);
        if(!wait_for(DW_IC_RXFLR, 0xFF, true)) return false;
        read[i]=(uint8_t)readl(DW_IC_DATA_CMD);
    }
    if(!wait_for(DW_IC_STATUS, DW_STATUS_ACTIVITY, false)) return false;
    if(readl(DW_IC_CLR_TX_ABRT)) { debug_state.transfer_errors++; return false; }
    return true;
}

static bool write_command(uint8_t opcode, const uint8_t *args, uint32_t arg_len){
    uint8_t command[8];
    if(arg_len > 4) return false;
    command[0]=(uint8_t)descriptor.command_register;
    command[1]=(uint8_t)(descriptor.command_register >> 8);
    command[2]=opcode;
    command[3]=0;
    for(uint32_t i=0;i<arg_len;i++) command[4+i]=args[i];
    return transfer(command, 4+arg_len, 0, 0);
}

void i2c_hid_touchpad_init(uint64_t hhdm_offset){
    // PCI 00:15.1 is Intel Alder Lake LPSS I2C #1. Do not touch a fixed
    // physical address on emulators or unrelated machines.
    if(pci_read32(0x15, 1, 0x00) != 0x51E98086U){
        serial_write_string("[I2C-HID] Alder Lake I2C1 not present\n");
        draw_debug();
        return;
    }
    uint32_t pci_command=pci_read32(0x15, 1, 0x04);
    pci_write32(0x15, 1, 0x04, pci_command | 0x00000006U);
    uint64_t bar=(uint64_t)(pci_read32(0x15, 1, 0x10) & ~0xFU);
    bar|=(uint64_t)pci_read32(0x15, 1, 0x14) << 32;
    if(!bar){
        serial_write_string("[I2C-HID] I2C1 has no MMIO BAR\n");
        draw_debug();
        return;
    }
    controller=(volatile uint32_t *)(uintptr_t)(hhdm_offset + bar);
    debug_state.controller_ready=true;

    uint8_t register_address[2]={HID_DESC_REG & 0xFF, HID_DESC_REG >> 8};
    uint8_t raw[sizeof(descriptor)];
    if(!transfer(register_address, sizeof(register_address), raw, sizeof(raw))){
        serial_write_string("[I2C-HID] failed to read HID descriptor\n");
        draw_debug();
        return;
    }

    descriptor.length=le16(raw+0);
    descriptor.version=le16(raw+2);
    descriptor.report_desc_length=le16(raw+4);
    descriptor.report_desc_register=le16(raw+6);
    descriptor.input_register=le16(raw+8);
    descriptor.max_input_length=le16(raw+10);
    descriptor.output_register=le16(raw+12);
    descriptor.max_output_length=le16(raw+14);
    descriptor.command_register=le16(raw+16);
    descriptor.data_register=le16(raw+18);
    descriptor.vendor_id=le16(raw+20);
    descriptor.product_id=le16(raw+22);
    descriptor.version_id=le16(raw+24);

    if(descriptor.length != sizeof(descriptor) || descriptor.vendor_id != 0x04F3 || descriptor.product_id != 0x3289){
        debug_state.transfer_errors++;
        serial_write_string("[I2C-HID] unexpected HID descriptor\n");
        draw_debug();
        return;
    }
    debug_state.descriptor_ready=true;
    debug_state.vendor_id=descriptor.vendor_id;
    debug_state.product_id=descriptor.product_id;
    debug_state.max_input_length=descriptor.max_input_length;

    const uint8_t power_on[2]={0,0};
    if(write_command(0x08, power_on, sizeof(power_on))) debug_state.device_ready=true;
    serial_write_string(debug_state.device_ready ? "[I2C-HID] ELAN 04F3:3289 ready\n" : "[I2C-HID] failed to power on device\n");
    draw_debug();
}

void i2c_hid_touchpad_poll(void){
    if(!debug_state.device_ready) return;

    uint8_t report[64];
    if(!transfer(0, 0, report, sizeof(report))) return;
    uint16_t report_length=le16(report);
    if(report_length < 6 || report_length > sizeof(report)) return;

    // ELAN 04F3:3289 report descriptor, report ID 1:
    // buttons, relative X, relative Y, wheel, consumer pan.
    debug_state.input_reports++;
    debug_state.last_report_id=report[2];
    if(report[2] == 1)
        mouse_apply_relative((int8_t)report[4], (int8_t)report[5], report[3]);
    draw_debug();
}

bool i2c_hid_touchpad_is_active(void){ return debug_state.device_ready; }

struct i2c_hid_debug_state i2c_hid_touchpad_get_debug_state(void){
    return *(const struct i2c_hid_debug_state *)&debug_state;
}
