#include "storage_probe.h"

#include "../pci/pci.h"
#include "../../lib/string.h"

#define STORAGE_CONTROLLER_LIMIT 8
#define PCI_CLASS_MASS_STORAGE   0x01
#define PCI_SUBCLASS_SATA        0x06
#define PCI_SUBCLASS_NVM         0x08
#define PCI_INTERFACE_AHCI       0x01
#define PCI_INTERFACE_NVME_IO    0x02
#define PCI_INTERFACE_NVME_ADMIN 0x03
#define PCI_CLASS_SERIAL_BUS     0x0C
#define PCI_SUBCLASS_USB         0x03
#define PCI_INTERFACE_XHCI       0x30
#define PCI_INTERFACE_EHCI       0x20

static struct storage_controller_info controllers[STORAGE_CONTROLLER_LIMIT];
static uint8_t controller_count;
static uint8_t ahci_count;
static uint8_t nvme_count;
static uint8_t xhci_count;
static uint8_t ehci_count;
static bool probe_complete;

static void set_controller_name(struct storage_controller_info *controller,
                                const char *prefix, uint8_t index){
    memset(controller->name,0,sizeof(controller->name));
    uint8_t position=0;
    while(prefix[position] && position+2<sizeof(controller->name)){
        controller->name[position]=prefix[position];
        position++;
    }
    controller->name[position++]=(char)('0'+index);
    controller->name[position]='\0';
}

static void inspect_pci_device(const struct pci_device_info *device, void *context){
    (void)context;
    if(controller_count>=STORAGE_CONTROLLER_LIMIT){
        return;
    }

    uint8_t type=0;
    uint8_t bar_index=0;
    if(device->class_code==PCI_CLASS_MASS_STORAGE
       && device->subclass==PCI_SUBCLASS_SATA
       && device->programming_interface==PCI_INTERFACE_AHCI){
        type=STORAGE_CONTROLLER_AHCI;
        bar_index=5;
    } else if(device->class_code==PCI_CLASS_MASS_STORAGE
              && device->subclass==PCI_SUBCLASS_NVM
              && (device->programming_interface==PCI_INTERFACE_NVME_IO
                  || device->programming_interface==PCI_INTERFACE_NVME_ADMIN)){
        type=STORAGE_CONTROLLER_NVME;
    } else if(device->class_code==PCI_CLASS_SERIAL_BUS
              && device->subclass==PCI_SUBCLASS_USB
              && (device->programming_interface==PCI_INTERFACE_XHCI
                  || device->programming_interface==PCI_INTERFACE_EHCI)){
        type=device->programming_interface==PCI_INTERFACE_XHCI
            ? STORAGE_CONTROLLER_XHCI : STORAGE_CONTROLLER_EHCI;
    } else {
        return;
    }

    struct storage_controller_info *controller=&controllers[controller_count];
    memset(controller,0,sizeof(*controller));
    controller->register_base=pci_read_bar(device->bus,device->slot,
                                           device->function,bar_index);
    controller->vendor_id=device->vendor_id;
    controller->device_id=device->device_id;
    controller->bus=device->bus;
    controller->slot=device->slot;
    controller->function=device->function;
    controller->type=type;
    controller->programming_interface=device->programming_interface;
    if(type==STORAGE_CONTROLLER_AHCI){
        set_controller_name(controller,"ahci",ahci_count++);
    } else if(type==STORAGE_CONTROLLER_NVME){
        set_controller_name(controller,"nvme",nvme_count++);
    } else if(type==STORAGE_CONTROLLER_XHCI){
        set_controller_name(controller,"xhci",xhci_count++);
    } else {
        set_controller_name(controller,"ehci",ehci_count++);
    }
    controller_count++;
}

void storage_probe_init(void){
    if(probe_complete) return;
    probe_complete=true;
    pci_enumerate(inspect_pci_device,0);
}

uint32_t storage_controller_count(void){
    storage_probe_init();
    return controller_count;
}

int32_t storage_controller_list(struct storage_controller_info *output,
                                uint32_t capacity){
    if(!output || capacity==0 || capacity>0x7FFFFFFF) return -1;
    storage_probe_init();
    uint32_t count=controller_count;
    if(count>capacity) count=capacity;
    for(uint32_t index=0;index<count;index++) output[index]=controllers[index];
    return (int32_t)count;
}
