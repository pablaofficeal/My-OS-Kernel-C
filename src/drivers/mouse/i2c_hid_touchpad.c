#include "i2c_hid_touchpad.h"
#include "ps2_mouse.h"
#include "../../boot/limine.h"
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
static uint64_t hhdm_base_global;
static uint64_t kernel_phys_global;
static uint64_t kernel_virt_global;
static struct limine_memmap_response *memmap_global;
static uint64_t mmio_pt_pool[3][512] __attribute__((aligned(4096)));
static int mmio_pool_next;

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
    gop_draw_rect(x, y, 310, 86, bg);
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
    gop_draw_text_at(x+6, y+61, "PCI", 0xCDD6F4, bg);
    draw_hex(x+46, y+61, debug_state.pci_id, 8);
    gop_draw_text_at(x+6, y+73, "BAR", 0xCDD6F4, bg);
    draw_hex(x+46, y+73, debug_state.bar_high, 8);
    draw_hex(x+126, y+73, debug_state.bar_low, 8);
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

static uint64_t virt_to_phys(uint64_t virt){
    if(!kernel_virt_global || !kernel_phys_global) return 0;
    return virt - kernel_virt_global + kernel_phys_global;
}

static uint64_t *alloc_pt(void){
    if(mmio_pool_next >= 3) return 0;
    uint64_t *pt = mmio_pt_pool[mmio_pool_next++];
    for(int i=0;i<512;i++) pt[i]=0;
    return pt;
}

static bool map_mmio_4k(uint64_t phys, uint64_t virt){
    if(!hhdm_base_global || !kernel_virt_global || !kernel_phys_global) return false;
    uint64_t cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    uint64_t cr4; __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    if(cr4 & (1ULL<<12)) return false; // LA57 not supported in this path
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(hhdm_base_global + (cr3 & ~0xFFFULL));
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t pml4e = pml4[pml4_idx];
    uint64_t *pdpt;
    if(!(pml4e & 1)){
        pdpt = alloc_pt(); if(!pdpt) return false;
        pml4[pml4_idx] = virt_to_phys((uint64_t)(uintptr_t)pdpt) | 0x3;
        pdpt = (uint64_t *)(uintptr_t)(hhdm_base_global + virt_to_phys((uint64_t)(uintptr_t)pdpt));
    } else {
        pdpt = (uint64_t *)(uintptr_t)(hhdm_base_global + (pml4e & ~0xFFFULL));
    }
    uint64_t pdpte = pdpt[pdpt_idx];
    uint64_t *pd;
    if(!(pdpte & 1)){
        pd = alloc_pt(); if(!pd) return false;
        pdpt[pdpt_idx] = virt_to_phys((uint64_t)(uintptr_t)pd) | 0x3;
        pd = (uint64_t *)(uintptr_t)(hhdm_base_global + virt_to_phys((uint64_t)(uintptr_t)pd));
    } else {
        if(pdpte & (1ULL<<7)) return false; // 1G page
        pd = (uint64_t *)(uintptr_t)(hhdm_base_global + (pdpte & ~0xFFFULL));
    }
    uint64_t pde = pd[pd_idx];
    uint64_t *pt;
    if(!(pde & 1)){
        pt = alloc_pt(); if(!pt) return false;
        pd[pd_idx] = virt_to_phys((uint64_t)(uintptr_t)pt) | 0x3;
        pt = (uint64_t *)(uintptr_t)(hhdm_base_global + virt_to_phys((uint64_t)(uintptr_t)pt));
    } else {
        if(pde & (1ULL<<7)) return false; // 2M page
        pt = (uint64_t *)(uintptr_t)(hhdm_base_global + (pde & ~0xFFFULL));
    }
    pt[pt_idx] = (phys & ~0xFFFULL) | 0x13; // P+W+PCD for MMIO
    __asm__ volatile("invlpg (%0)"::"r"(virt):"memory");
    return true;
}

static uint64_t alloc_mmio_window(uint64_t size){
    // Prefer the exact window Linux used on this ASUS board.
    const uint64_t preferred = 0x4017001000ULL;
    if(memmap_global){
        for(uint64_t i=0;i<memmap_global->entry_count;i++){
            struct limine_memmap_entry *e = memmap_global->entries[i];
            if(e->base <= preferred && preferred + size <= e->base + e->length){
                // Do not steal usable RAM.
                if(e->type == 0) return 0; // LIMINE_MEMMAP_USABLE
            }
        }
        // Preferred is not inside usable RAM -> safe to use if still free.
        return preferred;
    }
    return preferred;
}

void i2c_hid_touchpad_init(uint64_t hhdm_offset, uint64_t kernel_phys_base, uint64_t kernel_virt_base, struct limine_memmap_response *memmap){
    hhdm_base_global = hhdm_offset;
    kernel_phys_global = kernel_phys_base;
    kernel_virt_global = kernel_virt_base;
    memmap_global = memmap;
    mmio_pool_next = 0;
    // PCI 00:15.1 is Intel Alder Lake LPSS I2C #1. Do not touch a fixed
    // physical address on emulators or unrelated machines.
    debug_state.pci_id=pci_read32(0x15, 1, 0x00);
    // Intel LPSS I2C device IDs vary between BIOS revisions. The vendor and
    // the DSDT path identify this controller; do not reject a valid revision.
    if((debug_state.pci_id & 0xFFFFU) != 0x8086U){
        serial_write_string("[I2C-HID] Intel I2C1 not present\n");
        draw_debug();
        return;
    }
    // Size the BAR to know how much window we need.
    uint32_t orig_low = pci_read32(0x15, 1, 0x10);
    uint32_t orig_high = pci_read32(0x15, 1, 0x14);
    pci_write32(0x15, 1, 0x10, 0xFFFFFFFF);
    pci_write32(0x15, 1, 0x14, 0xFFFFFFFF);
    uint32_t size_low = pci_read32(0x15, 1, 0x10);
    uint32_t size_high = pci_read32(0x15, 1, 0x14);
    pci_write32(0x15, 1, 0x10, orig_low);
    pci_write32(0x15, 1, 0x14, orig_high);
    uint64_t size = 0;
    if((orig_low & 0x6) == 0x4){ // 64-bit BAR
        uint64_t mask = ((uint64_t)size_high << 32) | (size_low & ~0xFU);
        size = (~mask + 1) & ~0xFFFULL;
    } else {
        size = (~(size_low & ~0xFU) + 1) & ~0xFFFULL;
    }
    if(!size) size = 0x1000;

    debug_state.bar_low=orig_low;
    debug_state.bar_high=orig_high;
    uint64_t bar=(uint64_t)(debug_state.bar_low & ~0xFU);
    bar|=(uint64_t)debug_state.bar_high << 32;
    if(!bar){
        uint64_t alloc = alloc_mmio_window(size);
        if(!alloc){
            serial_write_string("[I2C-HID] no MMIO window\n");
            draw_debug();
            return;
        }
        uint32_t cmd = pci_read32(0x15, 1, 0x04);
        pci_write32(0x15, 1, 0x04, cmd & ~0x2U); // disable MEM decode while programming BAR
        pci_write32(0x15, 1, 0x10, (uint32_t)(alloc & 0xFFFFFFFFULL) | 0x4);
        pci_write32(0x15, 1, 0x14, (uint32_t)(alloc >> 32));
        bar = alloc;
        debug_state.bar_low=pci_read32(0x15, 1, 0x10);
        debug_state.bar_high=pci_read32(0x15, 1, 0x14);
    }
    // Enable MEM + Bus Master after BAR is valid.
    uint32_t cmd = pci_read32(0x15, 1, 0x04);
    pci_write32(0x15, 1, 0x04, cmd | 0x6U);
    const uint64_t I2C_VIRT = 0xFFFFFD8000000000ULL;
    if(!map_mmio_4k(bar & ~0xFFFULL, I2C_VIRT)){
        serial_write_string("[I2C-HID] cannot map I2C1 MMIO\n");
        draw_debug();
        return;
    }
    controller=(volatile uint32_t *)(uintptr_t)I2C_VIRT;
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

void i2c_hid_touchpad_redraw(void){ draw_debug(); }

bool i2c_hid_touchpad_is_active(void){ return debug_state.device_ready; }

struct i2c_hid_debug_state i2c_hid_touchpad_get_debug_state(void){
    return *(const struct i2c_hid_debug_state *)&debug_state;
}
