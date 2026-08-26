#include "ahci.h"

#include "storage_probe.h"
#include "../pci/pci.h"
#include "../../lib/string.h"

#define AHCI_DEVICE_LIMIT       8
#define AHCI_PORT_LIMIT        32
#define AHCI_COMMAND_SLOT_LIMIT 32
#define AHCI_TIMEOUT            10000000U

#define AHCI_GHC_AE             (1U<<31)
#define AHCI_PORT_CMD_ST        (1U<<0)
#define AHCI_PORT_CMD_FRE       (1U<<4)
#define AHCI_PORT_CMD_FR        (1U<<14)
#define AHCI_PORT_CMD_CR        (1U<<15)
#define AHCI_PORT_IS_TFES       (1U<<30)
#define AHCI_TFD_BUSY           0x80
#define AHCI_TFD_DRQ            0x08
#define AHCI_SSTS_DET_PRESENT   0x03
#define AHCI_SSTS_IPM_ACTIVE    0x01
#define AHCI_SIGNATURE_ATA      0x00000101
#define AHCI_FIS_HOST_TO_DEVICE 0x27
#define ATA_COMMAND_IDENTIFY    0xEC

#define HBA_GHC 1
#define HBA_PI  3
#define PORT_CLB   0
#define PORT_CLBU  1
#define PORT_FB    2
#define PORT_FBU   3
#define PORT_IS    4
#define PORT_IE    5
#define PORT_CMD   6
#define PORT_TFD   8
#define PORT_SIG   9
#define PORT_SSTS 10
#define PORT_SERR 12
#define PORT_CI   14

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t transferred_bytes;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
};

struct ahci_prdt_entry {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count;
};

struct ahci_command_table {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
};

_Static_assert(sizeof(struct ahci_command_header)==32,"AHCI command header size");
_Static_assert(sizeof(struct ahci_prdt_entry)==16,"AHCI PRDT entry size");
_Static_assert(sizeof(struct ahci_command_table)==144,"AHCI command table size");

static struct storage_device_info devices[AHCI_DEVICE_LIMIT];
static struct ahci_command_header command_list[AHCI_COMMAND_SLOT_LIMIT]
    __attribute__((aligned(1024)));
static uint8_t received_fis[256] __attribute__((aligned(256)));
static struct ahci_command_table command_table __attribute__((aligned(128)));
static uint16_t identify_words[256] __attribute__((aligned(512)));
static uint64_t hhdm_offset;
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static uint8_t device_count;
static bool mapping_ready;
static bool probe_complete;

void ahci_set_address_mapping(uint64_t direct_map_offset, uint64_t physical_base,
                              uint64_t virtual_base){
    hhdm_offset=direct_map_offset;
    kernel_physical_base=physical_base;
    kernel_virtual_base=virtual_base;
    mapping_ready=true;
}

static uint64_t kernel_pointer_physical(const void *pointer){
    return (uint64_t)(uintptr_t)pointer-kernel_virtual_base+kernel_physical_base;
}

static volatile uint32_t *port_registers(volatile uint32_t *hba, uint8_t port){
    return hba+(0x100+port*0x80)/sizeof(uint32_t);
}

static bool stop_port(volatile uint32_t *port){
    port[PORT_CMD]&=~AHCI_PORT_CMD_ST;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_CMD]&AHCI_PORT_CMD_CR)) break;
        if(wait+1==AHCI_TIMEOUT) return false;
    }
    port[PORT_CMD]&=~AHCI_PORT_CMD_FRE;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_CMD]&AHCI_PORT_CMD_FR)) return true;
    }
    return false;
}

static bool start_port(volatile uint32_t *port){
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_CMD]&AHCI_PORT_CMD_CR)){
            port[PORT_CMD]|=AHCI_PORT_CMD_FRE;
            port[PORT_CMD]|=AHCI_PORT_CMD_ST;
            return true;
        }
    }
    return false;
}

static bool port_has_sata_disk(volatile uint32_t *port){
    uint32_t status=port[PORT_SSTS];
    uint8_t detection=(uint8_t)(status&0x0F);
    uint8_t power=(uint8_t)((status>>8)&0x0F);
    return detection==AHCI_SSTS_DET_PRESENT && power==AHCI_SSTS_IPM_ACTIVE
        && port[PORT_SIG]==AHCI_SIGNATURE_ATA;
}

static bool issue_identify(volatile uint32_t *port){
    if(!stop_port(port)) return false;

    memset(command_list,0,sizeof(command_list));
    memset(received_fis,0,sizeof(received_fis));
    memset(&command_table,0,sizeof(command_table));
    memset(identify_words,0,sizeof(identify_words));

    uint64_t command_list_physical=kernel_pointer_physical(command_list);
    uint64_t received_fis_physical=kernel_pointer_physical(received_fis);
    uint64_t command_table_physical=kernel_pointer_physical(&command_table);
    uint64_t identify_physical=kernel_pointer_physical(identify_words);

    port[PORT_CLB]=(uint32_t)command_list_physical;
    port[PORT_CLBU]=(uint32_t)(command_list_physical>>32);
    port[PORT_FB]=(uint32_t)received_fis_physical;
    port[PORT_FBU]=(uint32_t)(received_fis_physical>>32);
    port[PORT_IE]=0;
    port[PORT_IS]=0xFFFFFFFF;
    port[PORT_SERR]=0xFFFFFFFF;

    command_list[0].flags=5;
    command_list[0].prdt_length=1;
    command_list[0].command_table_base=(uint32_t)command_table_physical;
    command_list[0].command_table_base_upper=(uint32_t)(command_table_physical>>32);
    command_table.command_fis[0]=AHCI_FIS_HOST_TO_DEVICE;
    command_table.command_fis[1]=0x80;
    command_table.command_fis[2]=ATA_COMMAND_IDENTIFY;
    command_table.prdt[0].data_base=(uint32_t)identify_physical;
    command_table.prdt[0].data_base_upper=(uint32_t)(identify_physical>>32);
    command_table.prdt[0].byte_count=(1U<<31)|511;

    __sync_synchronize();
    if(!start_port(port)) return false;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(!(port[PORT_TFD]&(AHCI_TFD_BUSY|AHCI_TFD_DRQ))) break;
        if(wait+1==AHCI_TIMEOUT){ (void)stop_port(port); return false; }
    }

    port[PORT_CI]=1;
    for(uint32_t wait=0;wait<AHCI_TIMEOUT;wait++){
        if(port[PORT_IS]&AHCI_PORT_IS_TFES){ (void)stop_port(port); return false; }
        if(!(port[PORT_CI]&1)){
            __sync_synchronize();
            (void)stop_port(port);
            return true;
        }
    }
    (void)stop_port(port);
    return false;
}

static void copy_identify_text(char *output, uint32_t capacity,
                               uint16_t first_word, uint16_t word_count){
    uint32_t length=(uint32_t)word_count*2;
    if(length>=capacity) length=capacity-1;
    for(uint32_t index=0;index<length;index++){
        uint16_t word=identify_words[first_word+index/2];
        output[index]=(char)(index&1 ? word&0xFF : word>>8);
    }
    while(length>0 && output[length-1]==' ') length--;
    output[length]='\0';
}

static void fill_device_info(struct storage_device_info *info, uint8_t name_index,
                             uint8_t controller, uint8_t port){
    memset(info,0,sizeof(*info));
    info->name[0]='/';
    info->name[1]='d';
    info->name[2]='e';
    info->name[3]='v';
    info->name[4]='/';
    info->name[5]='s';
    info->name[6]='d';
    info->name[7]=(char)('a'+name_index);
    info->sector_count=(uint32_t)identify_words[60]
        |((uint32_t)identify_words[61]<<16);
    if(identify_words[83]&(1<<10)){
        uint64_t lba48=(uint64_t)identify_words[100]
            |((uint64_t)identify_words[101]<<16)
            |((uint64_t)identify_words[102]<<32)
            |((uint64_t)identify_words[103]<<48);
        if(lba48) info->sector_count=lba48;
    }
    info->sector_size=512;
    info->transport=STORAGE_TRANSPORT_AHCI;
    info->controller=controller;
    info->port=port;
    info->writable=0;
    info->operational=0;
    copy_identify_text(info->serial,STORAGE_SERIAL_CAPACITY,10,10);
    copy_identify_text(info->model,STORAGE_MODEL_CAPACITY,27,20);
}

bool ahci_init(uint32_t linux_name_base){
    if(probe_complete) return device_count>0;
    probe_complete=true;
    if(!mapping_ready) return false;

    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0) return false;
    uint8_t ahci_index=0;
    for(int32_t index=0;index<count && device_count<AHCI_DEVICE_LIMIT;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_AHCI) continue;
        uint64_t abar=controllers[index].register_base;
        if(abar==0 || abar>0xFFFFFFFFULL){ ahci_index++; continue; }

        uint32_t pci_command=pci_read_config32(controllers[index].bus,
                                               controllers[index].slot,
                                               controllers[index].function,0x04);
        pci_write_config32(controllers[index].bus,controllers[index].slot,
                           controllers[index].function,0x04,pci_command|0x06);
        volatile uint32_t *hba=(volatile uint32_t*)(uintptr_t)(hhdm_offset+abar);
        hba[HBA_GHC]|=AHCI_GHC_AE;
        uint32_t implemented_ports=hba[HBA_PI];
        for(uint8_t port_index=0;port_index<AHCI_PORT_LIMIT
            && device_count<AHCI_DEVICE_LIMIT;port_index++){
            if(!(implemented_ports&(1U<<port_index))) continue;
            volatile uint32_t *port=port_registers(hba,port_index);
            if(!port_has_sata_disk(port) || !issue_identify(port)) continue;
            fill_device_info(&devices[device_count],
                             (uint8_t)(linux_name_base+device_count),
                             ahci_index,port_index);
            if(devices[device_count].sector_count) device_count++;
        }
        ahci_index++;
    }
    return device_count>0;
}

uint32_t ahci_device_count(void){ return device_count; }

bool ahci_get_device_info(uint32_t index, struct storage_device_info *info){
    if(!info || index>=device_count) return false;
    *info=devices[index];
    return true;
}
