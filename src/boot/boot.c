#include "limine.h"
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/gop.h"
#include "../drivers/vga.h"
#include "../drivers/fb.h"
#include "../drivers/serial.h"
#include "../drivers/pic.h"
#include "../drivers/timer.h"
#include "../drivers/mouse/ps2_mouse.h"
#include "../drivers/storage/ahci.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/usb/ehci.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/mmio.h"
#include "../kernel/kernel.h"
#include "../kernel/klog.h"
#include "../kernel/boot_diag.h"
#include "../kernel/panic.h"
#include "install_source.h"

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(0);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 1
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_firmware_type_request firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

// for fb/serial access from other units
struct limine_framebuffer *fb_ptr = 0;
// export memmap request response for kernel use
struct limine_memmap_response *memmap_response_ptr = 0;
struct limine_rsdp_response *rsdp_response_ptr = 0;
struct limine_smp_response *smp_response_ptr = 0;

bool boot_get_kernel_image(const void **address, uint32_t *size){
    if(!address || !size || !kernel_file_request.response
       || !kernel_file_request.response->kernel_file
       || !kernel_file_request.response->kernel_file->address
       || kernel_file_request.response->kernel_file->size==0
       || kernel_file_request.response->kernel_file->size>UINT32_MAX){
        return false;
    }
    *address=kernel_file_request.response->kernel_file->address;
    *size=(uint32_t)kernel_file_request.response->kernel_file->size;
    return true;
}

bool boot_get_efi_loader(const void **address, uint32_t *size){
    if(!address || !size || !module_request.response
       || module_request.response->module_count==0
       || !module_request.response->modules
       || !module_request.response->modules[0]
       || !module_request.response->modules[0]->address
       || module_request.response->modules[0]->size<2
       || module_request.response->modules[0]->size>UINT32_MAX){
        return false;
    }
    *address=module_request.response->modules[0]->address;
    *size=(uint32_t)module_request.response->modules[0]->size;
    return true;
}

void _start(void) {
    // Limine already in 64-bit long mode, paging enabled
    serial_init();
    serial_write_string("[EARLY 01] entered _start\n");
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        serial_write_string("[EARLY PANIC] unsupported Limine base revision\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    serial_write_string("[EARLY 02] Limine base revision supported\n");
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1
     || framebuffer_request.response->framebuffers[0] == NULL) {
        serial_write_string("[EARLY PANIC] Limine supplied no framebuffer\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    serial_write_string("[EARLY 03] framebuffer response present\n");

    fb_ptr = framebuffer_request.response->framebuffers[0];
    if(!fb_ptr->address || !fb_ptr->width || !fb_ptr->height || !fb_ptr->pitch){
        serial_write_string("[EARLY PANIC] invalid framebuffer geometry or address\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    if(fb_ptr->bpp != 32){
        serial_write_string("[EARLY WARN] framebuffer bpp !=32, trying to continue\n");
    }
    memmap_response_ptr = memmap_request.response;
    rsdp_response_ptr = rsdp_request.response;
    smp_response_ptr = smp_request.response;

    // HHDM для VGA 0xB8000 в higher half (иначе #PF и ребут)
    if(hhdm_request.response) vga_set_hhdm(hhdm_request.response->offset);
    else vga_set_hhdm(0);
    if(hhdm_request.response && kernel_address_request.response){
        mmio_configure(hhdm_request.response->offset,
                       kernel_address_request.response->physical_base,
                       kernel_address_request.response->virtual_base);
        ahci_set_address_mapping(hhdm_request.response->offset,
                                 kernel_address_request.response->physical_base,
                                 kernel_address_request.response->virtual_base);
        xhci_set_address_mapping(hhdm_request.response->offset,
                                 kernel_address_request.response->physical_base,
                                 kernel_address_request.response->virtual_base);
        ehci_set_address_mapping(hhdm_request.response->offset,
                                 kernel_address_request.response->physical_base,
                                 kernel_address_request.response->virtual_base);
    }

    // GOP сначала, VGA только если GOP нет
    uint64_t firmware_type=firmware_type_request.response
        ? firmware_type_request.response->firmware_type : UINT64_MAX;
    gop_init_from_limine(fb_ptr,firmware_type);

    // первичный экран как в Linux: показываем всё по дефолту
    klog_init();
    boot_diag_checkpoint(BOOT_STAGE_ENTRY, "Limine _start reached");
    boot_diag_checkpoint(BOOT_STAGE_BOOT_PROTOCOL, "boot responses captured");
    if(!gop_is_available()) kernel_panic("framebuffer driver initialization failed");
    if(!hhdm_request.response) kernel_panic("Limine HHDM response is missing");
    if(!kernel_address_request.response) kernel_panic("Limine kernel address response is missing");
    if(!memmap_response_ptr) kernel_panic("Limine memory map response is missing");
    if(fb_ptr->bpp==32) boot_diag_checkpoint(BOOT_STAGE_FRAMEBUFFER, "32-bpp framebuffer validated");
    else boot_diag_checkpoint(BOOT_STAGE_FRAMEBUFFER, "framebuffer validated (non-32 bpp)");
    klog(KLOG_OK, "Limine boot: 64-bit long mode, paging enabled");
    klogf(KLOG_INFO, "HHDM offset: 0x%llx", hhdm_request.response ? hhdm_request.response->offset : 0);
    if(gop_is_available()){
        klogf(KLOG_OK, "%s initialized: %dx%d bpp=%d pitch=%d",
              gop_get_protocol_name(),fb_ptr->width,fb_ptr->height,fb_ptr->bpp,fb_ptr->pitch);
        if(fb_ptr->bpp!=32) klogf(KLOG_WARN, "GOP bpp %d !=32: rendering assumes 32 bpp – colors may be off but boot continues", fb_ptr->bpp);
    } else {
        klog(KLOG_WARN, "GOP unavailable, fallback to VGA text 80x25");
    }

    klog(KLOG_INFO, "Initializing PIC...");
    pic_remap(0x20,0x28); pic_mask_all();
    klog(KLOG_OK, "PIC remapped: master 0x20 slave 0x28, all masked");

    klog(KLOG_INFO, "Loading GDT...");
    gdt_init();
    klog(KLOG_OK, "GDT loaded (code/data, TSS, emergency IST stacks)");

    klog(KLOG_INFO, "Loading IDT...");
    idt_init();
    klog(KLOG_OK, "IDT loaded (256 vectors, DF/NMI/MC IST, DPL3 for 0x80)");
    boot_diag_checkpoint(BOOT_STAGE_DESCRIPTOR_TABLES, "GDT and all 256 IDT vectors loaded");

    boot_diag_checkpoint(BOOT_STAGE_INTERRUPTS, "initializing PS/2 mouse");
    klog(KLOG_INFO, "Initializing PS/2 mouse...");
    ps2_mouse_init();
    klog(KLOG_OK, "PS/2 mouse ready (IRQ12)");

    timer_init(1000);

    klog(KLOG_INFO, "Enabling interrupts...");
    __asm__ volatile("cli");
    klog(KLOG_DEBUG, "CLI executed, preparing STI");
    __asm__ volatile("sti");
    klog(KLOG_OK, "Interrupts enabled (STI)");
    boot_diag_checkpoint(BOOT_STAGE_INTERRUPTS, "interrupts enabled");

    boot_diag_checkpoint(BOOT_STAGE_KERNEL_MAIN, "calling kernel_main");
    kernel_main(fb_ptr);

    kernel_panic("kernel_main returned unexpectedly");
}
