#include "xhci.h"

#include "../pci/pci.h"
#include "../storage/storage_probe.h"
#include "../../arch/x86_64/mmio.h"
#include "../../kernel/klog.h"
#include "../../lib/string.h"

#define XHCI_DEVICE_LIMIT       4
#define XHCI_RING_ENTRIES       64
#define XHCI_EVENT_ENTRIES      128
#define XHCI_TIMEOUT            10000000U
#define XHCI_MMIO_MAP_SIZE      0x100000U
#define XHCI_SCRATCHPAD_LIMIT   32
#define XHCI_NO_DEVICE          0xFF

#define XHCI_CMD_RUN            (1U<<0)
#define XHCI_CMD_RESET          (1U<<1)
#define XHCI_STS_HALTED         (1U<<0)
#define XHCI_STS_NOT_READY      (1U<<11)
#define XHCI_PORT_CONNECTED     (1U<<0)
#define XHCI_PORT_ENABLED       (1U<<1)
#define XHCI_PORT_RESET         (1U<<4)
#define XHCI_PORT_POWER         (1U<<9)
#define XHCI_PORT_SPEED_SHIFT   10
#define XHCI_PORT_CHANGE_BITS   (0x7FU<<17)

#define XHCI_TRB_NORMAL         1
#define XHCI_TRB_SETUP          2
#define XHCI_TRB_DATA           3
#define XHCI_TRB_STATUS         4
#define XHCI_TRB_LINK           6
#define XHCI_TRB_ENABLE_SLOT    9
#define XHCI_TRB_ADDRESS_DEVICE 11
#define XHCI_TRB_CONFIGURE_EP   12
#define XHCI_TRB_EVALUATE_CTX   13
#define XHCI_EVENT_TRANSFER     32
#define XHCI_EVENT_COMMAND      33
#define XHCI_COMPLETION_SUCCESS 1
#define XHCI_COMPLETION_SHORT   13

#define USB_DESCRIPTOR_DEVICE       1
#define USB_DESCRIPTOR_CONFIGURATION 2
#define USB_DESCRIPTOR_INTERFACE    4
#define USB_DESCRIPTOR_ENDPOINT     5
#define USB_CLASS_MASS_STORAGE      8
#define USB_PROTOCOL_BULK_ONLY      0x50

#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_INQUIRY         0x12
#define SCSI_MODE_SENSE6     0x1A
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A
#define SCSI_SYNC_CACHE10    0x35

struct xhci_trb {
    uint32_t parameter_low;
    uint32_t parameter_high;
    uint32_t status;
    uint32_t control;
};

struct xhci_erst_entry {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t size;
    uint32_t reserved;
};

struct xhci_ring_state {
    struct xhci_trb *trbs;
    uint16_t enqueue;
    uint8_t cycle;
};

struct usb_cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t transfer_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t command_length;
    uint8_t command[16];
} __attribute__((packed));

struct usb_csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed));

struct xhci_device {
    struct storage_device_info info;
    uint8_t slot_id;
    uint8_t bulk_in_dci;
    uint8_t bulk_out_dci;
    uint16_t bulk_in_packet;
    uint16_t bulk_out_packet;
};

_Static_assert(sizeof(struct xhci_trb)==16,"xHCI TRB size");
_Static_assert(sizeof(struct xhci_erst_entry)==16,"xHCI ERST entry size");
_Static_assert(sizeof(struct usb_cbw)==31,"USB BOT CBW size");
_Static_assert(sizeof(struct usb_csw)==13,"USB BOT CSW size");

static uint64_t device_context_base[256] __attribute__((aligned(64)));
static struct xhci_trb command_trbs[XHCI_RING_ENTRIES] __attribute__((aligned(64)));
static struct xhci_trb event_trbs[XHCI_EVENT_ENTRIES] __attribute__((aligned(64)));
static struct xhci_erst_entry event_segment __attribute__((aligned(64)));
static uint64_t scratchpad_pointers[XHCI_SCRATCHPAD_LIMIT] __attribute__((aligned(64)));
static uint8_t scratchpad_pages[XHCI_SCRATCHPAD_LIMIT][4096] __attribute__((aligned(4096)));
static uint8_t input_contexts[XHCI_DEVICE_LIMIT][4096] __attribute__((aligned(64)));
static uint8_t output_contexts[XHCI_DEVICE_LIMIT][4096] __attribute__((aligned(64)));
static struct xhci_trb control_trbs[XHCI_DEVICE_LIMIT][XHCI_RING_ENTRIES]
    __attribute__((aligned(64)));
static struct xhci_trb bulk_in_trbs[XHCI_DEVICE_LIMIT][XHCI_RING_ENTRIES]
    __attribute__((aligned(64)));
static struct xhci_trb bulk_out_trbs[XHCI_DEVICE_LIMIT][XHCI_RING_ENTRIES]
    __attribute__((aligned(64)));
static uint8_t descriptor_buffer[512] __attribute__((aligned(64)));
static uint8_t transfer_buffer[512] __attribute__((aligned(64)));
static struct usb_cbw command_block __attribute__((aligned(64)));
static struct usb_csw command_status __attribute__((aligned(64)));

static struct xhci_device devices[XHCI_DEVICE_LIMIT];
static struct xhci_ring_state command_ring;
static struct xhci_ring_state control_rings[XHCI_DEVICE_LIMIT];
static struct xhci_ring_state bulk_in_rings[XHCI_DEVICE_LIMIT];
static struct xhci_ring_state bulk_out_rings[XHCI_DEVICE_LIMIT];
static volatile uint8_t *mmio_base;
static volatile uint32_t *operational;
static volatile uint32_t *doorbells;
static volatile uint32_t *runtime;
static uint64_t kernel_physical_base;
static uint64_t kernel_virtual_base;
static uint16_t event_index;
static uint8_t event_cycle;
static uint8_t context_size;
static uint8_t controller_number;
static uint8_t device_count;
static uint8_t selected_device=XHCI_NO_DEVICE;
static uint32_t bot_tag=1;
static bool mapping_ready;
static bool probe_complete;
static struct xhci_probe_stats probe_stats;

static inline uint64_t rdtsc(void){
    uint32_t low,high;
    __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32)|low;
}

static uint32_t tsc_mhz(void){
    uint32_t eax,ebx,ecx,edx;
    __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx)
                     :"a"(0),"c"(0));
    if(eax>=0x16){
        __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx)
                         :"a"(0x16),"c"(0));
        if(eax>=100 && eax<=10000) return eax;
    }
    return 5000;
}

static void delay_ms(uint32_t milliseconds){
    uint64_t deadline=rdtsc()+(uint64_t)tsc_mhz()*1000ULL*milliseconds;
    while((int64_t)(rdtsc()-deadline)<0) __asm__ volatile("pause");
}

void xhci_set_address_mapping(uint64_t direct_map_offset, uint64_t physical_base,
                              uint64_t virtual_base){
    (void)direct_map_offset;
    kernel_physical_base=physical_base;
    kernel_virtual_base=virtual_base;
    mapping_ready=true;
}

static uint64_t physical_address(const void *pointer){
    return (uint64_t)(uintptr_t)pointer-kernel_virtual_base+kernel_physical_base;
}

static uint32_t read_be32(const uint8_t *data){
    return ((uint32_t)data[0]<<24)|((uint32_t)data[1]<<16)
        |((uint32_t)data[2]<<8)|data[3];
}

static void write_be32(uint8_t *data, uint32_t value){
    data[0]=(uint8_t)(value>>24);
    data[1]=(uint8_t)(value>>16);
    data[2]=(uint8_t)(value>>8);
    data[3]=(uint8_t)value;
}

static uint32_t *context_at(uint8_t *base, uint8_t index){
    return (uint32_t*)(void*)(base+(uint32_t)index*context_size);
}

static void initialize_ring(struct xhci_ring_state *ring,
                            struct xhci_trb *storage){
    memset(storage,0,sizeof(struct xhci_trb)*XHCI_RING_ENTRIES);
    ring->trbs=storage;
    ring->enqueue=0;
    ring->cycle=1;
}

static struct xhci_trb *ring_next(struct xhci_ring_state *ring){
    if(ring->enqueue==XHCI_RING_ENTRIES-1){
        uint64_t base=physical_address(ring->trbs);
        struct xhci_trb *link=&ring->trbs[ring->enqueue];
        memset(link,0,sizeof(*link));
        link->parameter_low=(uint32_t)base;
        link->parameter_high=(uint32_t)(base>>32);
        link->control=(XHCI_TRB_LINK<<10)|(1U<<1)|ring->cycle;
        __sync_synchronize();
        ring->enqueue=0;
        ring->cycle^=1;
    }
    struct xhci_trb *trb=&ring->trbs[ring->enqueue++];
    memset(trb,0,sizeof(*trb));
    return trb;
}

static bool wait_register(volatile uint32_t *reg, uint32_t mask, bool set){
    for(uint32_t wait=0;wait<XHCI_TIMEOUT;wait++){
        if(((*reg&mask)!=0)==set) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool next_event(uint8_t wanted_type, uint8_t slot, uint8_t endpoint,
                       struct xhci_trb *result){
    volatile uint32_t *interrupter=runtime+0x20/4;
    for(uint32_t wait=0;wait<XHCI_TIMEOUT;wait++){
        volatile struct xhci_trb *event=&event_trbs[event_index];
        if((event->control&1)==event_cycle){
            __sync_synchronize();
            struct xhci_trb copy=*event;
            event_index++;
            if(event_index==XHCI_EVENT_ENTRIES){
                event_index=0;
                event_cycle^=1;
            }
            uint64_t dequeue=physical_address(&event_trbs[event_index]);
            interrupter[6]=(uint32_t)dequeue|8;
            interrupter[7]=(uint32_t)(dequeue>>32);
            uint8_t type=(uint8_t)((copy.control>>10)&0x3F);
            uint8_t event_slot=(uint8_t)(copy.control>>24);
            uint8_t event_endpoint=(uint8_t)((copy.control>>16)&0x1F);
            if(type==wanted_type && (!slot || event_slot==slot)
               && (!endpoint || event_endpoint==endpoint)){
                if(result) *result=copy;
                uint8_t code=(uint8_t)(copy.status>>24);
                probe_stats.last_completion_code=code;
                bool success=code==XHCI_COMPLETION_SUCCESS
                    || (wanted_type==XHCI_EVENT_TRANSFER
                        && code==XHCI_COMPLETION_SHORT);
                if(!success) probe_stats.last_error=XHCI_PROBE_COMPLETION;
                return success;
            }
        } else {
            __asm__ volatile("pause");
        }
    }
    probe_stats.last_error=XHCI_PROBE_EVENT_TIMEOUT;
    return false;
}

static bool submit_command(uint64_t parameter, uint32_t type, uint8_t slot,
                           uint8_t *result_slot){
    struct xhci_trb *trb=ring_next(&command_ring);
    trb->parameter_low=(uint32_t)parameter;
    trb->parameter_high=(uint32_t)(parameter>>32);
    trb->control=(type<<10)|((uint32_t)slot<<24)|command_ring.cycle;
    __sync_synchronize();
    doorbells[0]=0;
    struct xhci_trb event;
    if(!next_event(XHCI_EVENT_COMMAND,0,0,&event)) return false;
    if(result_slot) *result_slot=(uint8_t)(event.control>>24);
    return true;
}

static bool transfer(uint8_t device_index, uint8_t endpoint,
                     struct xhci_ring_state *ring, void *buffer,
                     uint32_t length){
    struct xhci_trb *trb=ring_next(ring);
    uint64_t address=physical_address(buffer);
    trb->parameter_low=(uint32_t)address;
    trb->parameter_high=(uint32_t)(address>>32);
    trb->status=length;
    trb->control=(XHCI_TRB_NORMAL<<10)|(1U<<5)|ring->cycle;
    __sync_synchronize();
    uint8_t slot=devices[device_index].slot_id;
    doorbells[slot]=endpoint;
    return next_event(XHCI_EVENT_TRANSFER,slot,endpoint,0);
}

static bool control_transfer(uint8_t device_index, uint8_t request_type,
                             uint8_t request, uint16_t value, uint16_t index,
                             void *buffer, uint16_t length, bool data_in){
    struct xhci_device *device=&devices[device_index];
    struct xhci_ring_state *ring=&control_rings[device_index];
    uint64_t setup=(uint64_t)request_type|((uint64_t)request<<8)
        |((uint64_t)value<<16)|((uint64_t)index<<32)|((uint64_t)length<<48);
    struct xhci_trb *trb=ring_next(ring);
    trb->parameter_low=(uint32_t)setup;
    trb->parameter_high=(uint32_t)(setup>>32);
    trb->status=8;
    uint32_t transfer_type=length ? (data_in ? 3U : 2U) : 0U;
    trb->control=(XHCI_TRB_SETUP<<10)|(1U<<6)|(transfer_type<<16)|ring->cycle;
    if(length){
        trb=ring_next(ring);
        uint64_t address=physical_address(buffer);
        trb->parameter_low=(uint32_t)address;
        trb->parameter_high=(uint32_t)(address>>32);
        trb->status=length;
        trb->control=(XHCI_TRB_DATA<<10)|(data_in ? 1U<<16 : 0)|ring->cycle;
    }
    trb=ring_next(ring);
    bool status_in=length ? !data_in : true;
    trb->control=(XHCI_TRB_STATUS<<10)|(status_in ? 1U<<16 : 0)
        |(1U<<5)|ring->cycle;
    __sync_synchronize();
    doorbells[device->slot_id]=1;
    return next_event(XHCI_EVENT_TRANSFER,device->slot_id,1,0);
}

static uint16_t initial_packet_size(uint8_t speed){
    if(speed>=4) return 512;
    if(speed==3) return 64;
    return 8;
}

static bool reset_port(uint8_t port_number, uint8_t *speed){
    volatile uint8_t *port_base=(volatile uint8_t*)(void*)operational+0x400;
    volatile uint32_t *port=(volatile uint32_t*)(void*)(port_base
                                                +(port_number-1)*0x10);
    uint32_t status=port[0];
    if(!(status&XHCI_PORT_CONNECTED)) return false;
    probe_stats.connected_ports++;
    probe_stats.last_port=port_number;
    probe_stats.last_portsc=status;
    if(status&XHCI_PORT_ENABLED){
        *speed=(uint8_t)((status>>XHCI_PORT_SPEED_SHIFT)&0x0F);
        port[0]=(status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                          |XHCI_PORT_RESET))|(status&XHCI_PORT_CHANGE_BITS);
        if(!*speed) probe_stats.last_error=XHCI_PROBE_PORT_RESET;
        return *speed!=0;
    }
    if(!(status&XHCI_PORT_POWER)){
        port[0]=(status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                          |XHCI_PORT_RESET))|XHCI_PORT_POWER;
        delay_ms(20);
        status=port[0];
        if(!(status&XHCI_PORT_CONNECTED)){
            probe_stats.last_error=XHCI_PROBE_PORT_RESET;
            probe_stats.last_portsc=status;
            return false;
        }
    }
    uint32_t writable=status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                               |XHCI_PORT_RESET);
    port[0]=writable|XHCI_PORT_POWER|XHCI_PORT_RESET;
    if(!wait_register(&port[0],XHCI_PORT_RESET,false)){
        probe_stats.last_error=XHCI_PROBE_PORT_RESET;
        probe_stats.last_portsc=port[0];
        return false;
    }
    delay_ms(20);
    status=port[0];
    probe_stats.last_portsc=status;
    if(!(status&XHCI_PORT_CONNECTED) || !(status&XHCI_PORT_ENABLED)){
        probe_stats.last_error=XHCI_PROBE_PORT_RESET;
        return false;
    }
    *speed=(uint8_t)((status>>XHCI_PORT_SPEED_SHIFT)&0x0F);
    port[0]=(status&~(XHCI_PORT_CHANGE_BITS|XHCI_PORT_ENABLED
                      |XHCI_PORT_RESET))|(status&XHCI_PORT_CHANGE_BITS);
    return *speed!=0;
}

static bool update_control_packet_size(uint8_t index, uint16_t packet_size){
    uint8_t slot=devices[index].slot_id;
    uint8_t *input=input_contexts[index];
    uint8_t *output=output_contexts[index];
    memset(input,0,4096);
    context_at(input,0)[1]=(1U<<1);
    memcpy(context_at(input,2),context_at(output,1),context_size);
    uint32_t *ep0=context_at(input,2);
    ep0[1]=(ep0[1]&0x0000FFFFU)|((uint32_t)packet_size<<16);
    return submit_command(physical_address(input),XHCI_TRB_EVALUATE_CTX,slot,0);
}

static bool address_port(uint8_t device_index, uint8_t port, uint8_t speed){
    uint8_t slot;
    if(!submit_command(0,XHCI_TRB_ENABLE_SLOT,0,&slot) || !slot){
        probe_stats.last_error=XHCI_PROBE_ENABLE_SLOT;
        return false;
    }
    devices[device_index].slot_id=slot;
    memset(input_contexts[device_index],0,4096);
    memset(output_contexts[device_index],0,4096);
    initialize_ring(&control_rings[device_index],control_trbs[device_index]);
    device_context_base[slot]=physical_address(output_contexts[device_index]);

    uint8_t *input=input_contexts[device_index];
    context_at(input,0)[1]=(1U<<0)|(1U<<1);
    uint32_t *slot_context=context_at(input,1);
    slot_context[0]=((uint32_t)speed<<20)|(1U<<27);
    slot_context[1]=(uint32_t)port<<16;
    uint32_t *ep0=context_at(input,2);
    ep0[1]=(3U<<1)|(4U<<3)|((uint32_t)initial_packet_size(speed)<<16);
    uint64_t ring_address=physical_address(control_trbs[device_index])|1;
    ep0[2]=(uint32_t)ring_address;
    ep0[3]=(uint32_t)(ring_address>>32);
    ep0[4]=8;
    __sync_synchronize();
    if(!submit_command(physical_address(input),XHCI_TRB_ADDRESS_DEVICE,slot,0)){
        probe_stats.last_error=XHCI_PROBE_ADDRESS_DEVICE;
        return false;
    }
    probe_stats.addressed_devices++;
    return true;
}

static bool get_descriptor(uint8_t index, uint8_t type, uint8_t descriptor_index,
                           uint16_t length){
    memset(descriptor_buffer,0,sizeof(descriptor_buffer));
    return control_transfer(index,0x80,6,
                            (uint16_t)((uint16_t)type<<8)|descriptor_index,
                            0,descriptor_buffer,length,true);
}

static bool find_mass_storage_interface(uint16_t total_length,
                                        uint8_t *configuration,
                                        uint8_t *interface_number,
                                        uint8_t *bulk_in, uint16_t *bulk_in_packet,
                                        uint8_t *bulk_out, uint16_t *bulk_out_packet){
    bool mass_interface=false;
    for(uint16_t offset=0;offset+2<=total_length;){
        uint8_t length=descriptor_buffer[offset];
        uint8_t type=descriptor_buffer[offset+1];
        if(length<2 || offset+length>total_length) return false;
        if(type==USB_DESCRIPTOR_CONFIGURATION && length>=9){
            *configuration=descriptor_buffer[offset+5];
        } else if(type==USB_DESCRIPTOR_INTERFACE && length>=9){
            mass_interface=descriptor_buffer[offset+5]==USB_CLASS_MASS_STORAGE
                && descriptor_buffer[offset+7]==USB_PROTOCOL_BULK_ONLY;
            if(mass_interface) *interface_number=descriptor_buffer[offset+2];
        } else if(type==USB_DESCRIPTOR_ENDPOINT && length>=7 && mass_interface
                  && (descriptor_buffer[offset+3]&3)==2){
            uint8_t address=descriptor_buffer[offset+2];
            uint16_t packet=(uint16_t)descriptor_buffer[offset+4]
                |((uint16_t)descriptor_buffer[offset+5]<<8);
            if(address&0x80){ *bulk_in=address; *bulk_in_packet=packet; }
            else { *bulk_out=address; *bulk_out_packet=packet; }
        }
        offset+=length;
    }
    return *configuration && *bulk_in && *bulk_out;
}

static void configure_endpoint_context(uint32_t *endpoint, uint8_t type,
                                       uint16_t max_packet,
                                       struct xhci_ring_state *ring){
    endpoint[1]=(3U<<1)|((uint32_t)type<<3)|((uint32_t)max_packet<<16);
    uint64_t dequeue=physical_address(ring->trbs)|1;
    endpoint[2]=(uint32_t)dequeue;
    endpoint[3]=(uint32_t)(dequeue>>32);
    endpoint[4]=max_packet;
}

static bool configure_mass_storage(uint8_t index, uint8_t configuration,
                                   uint8_t bulk_in, uint16_t bulk_in_packet,
                                   uint8_t bulk_out, uint16_t bulk_out_packet){
    uint8_t in_dci=(uint8_t)((bulk_in&0x0F)*2+1);
    uint8_t out_dci=(uint8_t)((bulk_out&0x0F)*2);
    uint8_t highest=in_dci>out_dci ? in_dci : out_dci;
    if(!in_dci || !out_dci || highest>=32) return false;

    initialize_ring(&bulk_in_rings[index],bulk_in_trbs[index]);
    initialize_ring(&bulk_out_rings[index],bulk_out_trbs[index]);
    uint8_t *input=input_contexts[index];
    uint8_t *output=output_contexts[index];
    memset(input,0,4096);
    context_at(input,0)[1]=(1U<<0)|(1U<<in_dci)|(1U<<out_dci);
    memcpy(context_at(input,1),context_at(output,0),context_size);
    uint32_t *slot=context_at(input,1);
    slot[0]=(slot[0]&~(0x1FU<<27))|((uint32_t)highest<<27);
    configure_endpoint_context(context_at(input,(uint8_t)(in_dci+1)),6,
                               bulk_in_packet,&bulk_in_rings[index]);
    configure_endpoint_context(context_at(input,(uint8_t)(out_dci+1)),2,
                               bulk_out_packet,&bulk_out_rings[index]);
    if(!submit_command(physical_address(input),XHCI_TRB_CONFIGURE_EP,
                       devices[index].slot_id,0)){
        return false;
    }
    if(!control_transfer(index,0,9,configuration,0,0,0,false)) return false;
    devices[index].bulk_in_dci=in_dci;
    devices[index].bulk_out_dci=out_dci;
    devices[index].bulk_in_packet=bulk_in_packet;
    devices[index].bulk_out_packet=bulk_out_packet;
    return true;
}

static bool bulk_only_command(uint8_t index, const uint8_t *command,
                              uint8_t command_length, void *data,
                              uint32_t data_length, bool data_in){
    struct xhci_device *device=&devices[index];
    memset(&command_block,0,sizeof(command_block));
    uint32_t tag=bot_tag++;
    command_block.signature=0x43425355;
    command_block.tag=tag;
    command_block.transfer_length=data_length;
    command_block.flags=data_in ? 0x80 : 0;
    command_block.command_length=command_length;
    memcpy(command_block.command,command,command_length);
    if(!transfer(index,device->bulk_out_dci,&bulk_out_rings[index],
                 &command_block,sizeof(command_block))){
        return false;
    }
    if(data_length){
        struct xhci_ring_state *ring=data_in
            ? &bulk_in_rings[index] : &bulk_out_rings[index];
        uint8_t endpoint=data_in ? device->bulk_in_dci : device->bulk_out_dci;
        if(!transfer(index,endpoint,ring,data,data_length)) return false;
    }
    memset(&command_status,0,sizeof(command_status));
    if(!transfer(index,device->bulk_in_dci,&bulk_in_rings[index],
                 &command_status,sizeof(command_status))){
        return false;
    }
    return command_status.signature==0x53425355
        && command_status.tag==tag && command_status.status==0;
}

static void build_device_name(char output[STORAGE_DEVICE_NAME_CAPACITY],
                              uint32_t name_index){
    memset(output,0,STORAGE_DEVICE_NAME_CAPACITY);
    output[0]='/'; output[1]='d'; output[2]='e'; output[3]='v';
    output[4]='/'; output[5]='s'; output[6]='d';
    output[7]=(char)('a'+name_index);
}

static void build_usb_serial(char output[STORAGE_SERIAL_CAPACITY], uint16_t vendor,
                             uint16_t product, uint8_t port){
    const char *hex="0123456789ABCDEF";
    memcpy(output,"USB-",4);
    for(uint8_t index=0;index<4;index++) output[4+index]=hex[(vendor>>(12-index*4))&0xF];
    output[8]='-';
    for(uint8_t index=0;index<4;index++) output[9+index]=hex[(product>>(12-index*4))&0xF];
    output[13]='-'; output[14]='P';
    output[15]=hex[(port>>4)&0xF]; output[16]=hex[port&0xF]; output[17]='\0';
}

static void copy_inquiry_model(char output[STORAGE_MODEL_CAPACITY],
                               const uint8_t *inquiry){
    uint8_t length=0;
    for(uint8_t index=8;index<36 && length+1<STORAGE_MODEL_CAPACITY;index++){
        char character=(char)inquiry[index];
        if(character<' ' || character>'~') character=' ';
        output[length++]=character;
    }
    while(length && output[length-1]==' ') length--;
    output[length]='\0';
    if(!length) memcpy(output,"USB Mass Storage",17);
}

static bool identify_mass_storage(uint8_t index, uint32_t name_index,
                                  uint8_t port, uint16_t vendor, uint16_t product){
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=SCSI_TEST_UNIT_READY;
    bool ready=false;
    for(uint8_t attempt=0;attempt<3;attempt++){
        if(bulk_only_command(index,command,6,0,0,true)){ ready=true; break; }
    }
    if(!ready) return false;

    memset(command,0,sizeof(command));
    command[0]=SCSI_INQUIRY;
    command[4]=36;
    memset(transfer_buffer,0,sizeof(transfer_buffer));
    if(!bulk_only_command(index,command,6,transfer_buffer,36,true)) return false;

    struct storage_device_info *info=&devices[index].info;
    memset(info,0,sizeof(*info));
    build_device_name(info->name,name_index);
    copy_inquiry_model(info->model,transfer_buffer);
    build_usb_serial(info->serial,vendor,product,port);
    memset(command,0,sizeof(command));
    command[0]=SCSI_READ_CAPACITY10;
    if(!bulk_only_command(index,command,10,transfer_buffer,8,true)) return false;
    uint32_t last_lba=read_be32(transfer_buffer);
    uint32_t block_size=read_be32(&transfer_buffer[4]);
    if(last_lba==0xFFFFFFFF || block_size!=512) return false;
    info->sector_count=(uint64_t)last_lba+1;
    info->sector_size=block_size;
    info->transport=STORAGE_TRANSPORT_USB_MSC;
    info->controller=controller_number;
    info->port=port;
    memset(command,0,sizeof(command));
    command[0]=SCSI_MODE_SENSE6;
    command[2]=0x3F;
    command[4]=4;
    info->writable=1;
    if(bulk_only_command(index,command,6,transfer_buffer,4,true)){
        info->writable=(transfer_buffer[2]&0x80)==0;
    }
    info->operational=1;
    return info->sector_count!=0;
}

static bool enumerate_port(uint8_t port, uint8_t speed, uint32_t name_index){
    uint8_t index=device_count;
    memset(&devices[index],0,sizeof(devices[index]));
    probe_stats.last_stage=3;
    if(!address_port(index,port,speed)){
        if(probe_stats.last_error==XHCI_PROBE_OK
           || probe_stats.last_error==XHCI_PROBE_EVENT_TIMEOUT
           || probe_stats.last_error==XHCI_PROBE_COMPLETION){
            probe_stats.last_error=XHCI_PROBE_ADDRESS_DEVICE;
        }
        return false;
    }
    probe_stats.last_stage=4;
    if(!get_descriptor(index,USB_DESCRIPTOR_DEVICE,0,18)){
        probe_stats.last_error=XHCI_PROBE_DEVICE_DESCRIPTOR;
        return false;
    }
    uint16_t vendor=(uint16_t)descriptor_buffer[8]
        |((uint16_t)descriptor_buffer[9]<<8);
    uint16_t product=(uint16_t)descriptor_buffer[10]
        |((uint16_t)descriptor_buffer[11]<<8);
    uint16_t packet=descriptor_buffer[7];
    if(speed>=4) packet=(uint16_t)(1U<<packet);
    if(packet && packet!=initial_packet_size(speed)
       && !update_control_packet_size(index,packet)){
        return false;
    }
    if(!get_descriptor(index,USB_DESCRIPTOR_CONFIGURATION,0,9)){
        probe_stats.last_error=XHCI_PROBE_CONFIG_DESCRIPTOR;
        return false;
    }
    uint16_t total=(uint16_t)descriptor_buffer[2]
        |((uint16_t)descriptor_buffer[3]<<8);
    if(total<9 || total>sizeof(descriptor_buffer)
       || !get_descriptor(index,USB_DESCRIPTOR_CONFIGURATION,0,total)){
        probe_stats.last_error=XHCI_PROBE_CONFIG_DESCRIPTOR;
        return false;
    }
    uint8_t configuration=0,interface_number=0,bulk_in=0,bulk_out=0;
    uint16_t bulk_in_packet=0,bulk_out_packet=0;
    if(!find_mass_storage_interface(total,&configuration,&interface_number,
                                    &bulk_in,&bulk_in_packet,
                                    &bulk_out,&bulk_out_packet)){
        probe_stats.last_error=XHCI_PROBE_MASS_STORAGE_INTERFACE;
        return false;
    }
    probe_stats.last_stage=5;
    (void)interface_number;
    if(!configure_mass_storage(index,configuration,bulk_in,bulk_in_packet,
                               bulk_out,bulk_out_packet)){
        probe_stats.last_error=XHCI_PROBE_CONFIGURE_ENDPOINT;
        return false;
    }
    if(!identify_mass_storage(index,name_index,port,vendor,product)){
        probe_stats.last_error=XHCI_PROBE_SCSI;
        return false;
    }
    device_count++;
    probe_stats.mass_storage_devices++;
    probe_stats.last_stage=7;
    probe_stats.last_error=XHCI_PROBE_OK;
    return true;
}

static bool take_ownership(volatile uint32_t *capability, uint32_t hccparams1){
    uint16_t offset=(uint16_t)(hccparams1>>16)*4;
    for(uint8_t visited=0;offset && visited<64;visited++){
        volatile uint32_t *extended=(volatile uint32_t*)((volatile uint8_t*)capability+offset);
        uint32_t header=extended[0];
        if((header&0xFF)==1){
            extended[0]=header|(1U<<24);
            uint64_t deadline=rdtsc()+(uint64_t)tsc_mhz()*1000ULL*1000ULL;
            while((extended[0]&(1U<<16))
                  && (int64_t)(rdtsc()-deadline)<0){
                __asm__ volatile("pause");
            }
            if(extended[0]&(1U<<16)){
                probe_stats.last_error=XHCI_PROBE_BIOS_HANDOFF;
                klog(KLOG_WARN,"xhci: BIOS ownership timeout; forcing OS handoff");
            }
            // Disable legacy SMI sources even when broken firmware keeps the
            // BIOS-owned semaphore asserted, matching the non-fatal Linux path.
            extended[1]=0;
            return true;
        }
        uint8_t next=(uint8_t)((header>>8)&0xFF);
        if(!next) break;
        offset=(uint16_t)(offset+next*4);
    }
    return true;
}

static bool initialize_controller(const struct storage_controller_info *controller,
                                  uint32_t linux_name_base){
    if(!controller->register_base) return false;
    uint32_t pci_command=pci_read_config32(controller->bus,controller->slot,
                                            controller->function,0x04);
    pci_write_config32(controller->bus,controller->slot,controller->function,
                       0x04,pci_command|0x06);
    mmio_base=(volatile uint8_t*)mmio_map(controller->register_base,
                                          XHCI_MMIO_MAP_SIZE);
    if(!mmio_base){
        probe_stats.last_error=XHCI_PROBE_MMIO;
        klogf(KLOG_ERROR,"xhci%u: cannot map BAR 0x%llx",controller_number,
              controller->register_base);
        return false;
    }
    klogf(KLOG_INFO,"xhci%u: BAR phys=0x%llx mapped=%p",controller_number,
          controller->register_base,(void*)mmio_base);
    volatile uint32_t *capability=(volatile uint32_t*)(void*)mmio_base;
    uint8_t capability_length=mmio_base[0];
    uint32_t hcsparams1=capability[1];
    uint32_t hcsparams2=capability[2];
    uint32_t hccparams1=capability[4];
    if(capability_length<0x20){
        probe_stats.last_error=XHCI_PROBE_CAPABILITY;
        return false;
    }
    if(!take_ownership(capability,hccparams1)) return false;
    context_size=(hccparams1&(1U<<2)) ? 64 : 32;
    uint8_t max_slots=(uint8_t)hcsparams1;
    uint8_t max_ports=(uint8_t)(hcsparams1>>24);
    probe_stats.max_ports=max_ports;
    if(!max_slots || !max_ports){
        probe_stats.last_error=XHCI_PROBE_CAPABILITY;
        return false;
    }
    if(max_slots>32) max_slots=32;

    uint32_t doorbell_offset=capability[5]&~3U;
    uint32_t runtime_offset=capability[6]&~0x1FU;
    if((uint32_t)capability_length+0x40U>XHCI_MMIO_MAP_SIZE
       || doorbell_offset>XHCI_MMIO_MAP_SIZE-4U
       || runtime_offset>XHCI_MMIO_MAP_SIZE-0x40U){
        probe_stats.last_error=XHCI_PROBE_CAPABILITY;
        return false;
    }
    operational=(volatile uint32_t*)(void*)(mmio_base+capability_length);
    doorbells=(volatile uint32_t*)(void*)(mmio_base+doorbell_offset);
    runtime=(volatile uint32_t*)(void*)(mmio_base+runtime_offset);
    operational[0]&=~XHCI_CMD_RUN;
    if(!wait_register(&operational[1],XHCI_STS_HALTED,true)){
        probe_stats.last_error=XHCI_PROBE_HALT_TIMEOUT;
        probe_stats.usb_status=operational[1];
        return false;
    }
    operational[0]|=XHCI_CMD_RESET;
    if(!wait_register(&operational[0],XHCI_CMD_RESET,false)){
        probe_stats.last_error=XHCI_PROBE_RESET_TIMEOUT;
        probe_stats.usb_status=operational[1];
        return false;
    }
    if(!wait_register(&operational[1],XHCI_STS_NOT_READY,false)){
        probe_stats.last_error=XHCI_PROBE_NOT_READY_TIMEOUT;
        probe_stats.usb_status=operational[1];
        return false;
    }
    if(!(operational[2]&1)){
        probe_stats.last_error=XHCI_PROBE_PAGE_SIZE;
        return false;
    }

    uint64_t dma_high=physical_address(device_context_base)
        |physical_address(command_trbs)|physical_address(event_trbs)
        |physical_address(input_contexts)|physical_address(output_contexts)
        |physical_address(control_trbs)|physical_address(bulk_in_trbs)
        |physical_address(bulk_out_trbs)|physical_address(descriptor_buffer)
        |physical_address(transfer_buffer)|physical_address(&command_block)
        |physical_address(&command_status)|physical_address(&event_segment)
        |physical_address(scratchpad_pointers)|physical_address(scratchpad_pages);
    if((dma_high>>32)!=0 && !(hccparams1&1)){
        probe_stats.last_error=XHCI_PROBE_DMA_ADDRESS;
        return false;
    }

    memset(device_context_base,0,sizeof(device_context_base));
    uint16_t scratchpads=(uint16_t)((hcsparams2>>27)&0x1F)
        |(uint16_t)((hcsparams2>>16)&0x3E0);
    if(scratchpads>XHCI_SCRATCHPAD_LIMIT){
        probe_stats.last_error=XHCI_PROBE_SCRATCHPADS;
        return false;
    }
    if(scratchpads){
        for(uint16_t index=0;index<scratchpads;index++){
            scratchpad_pointers[index]=physical_address(scratchpad_pages[index]);
        }
        device_context_base[0]=physical_address(scratchpad_pointers);
    }
    initialize_ring(&command_ring,command_trbs);
    uint64_t command_address=physical_address(command_trbs)|1;
    operational[6]=(uint32_t)command_address;
    operational[7]=(uint32_t)(command_address>>32);
    uint64_t dcbaa_address=physical_address(device_context_base);
    operational[12]=(uint32_t)dcbaa_address;
    operational[13]=(uint32_t)(dcbaa_address>>32);
    operational[14]=max_slots;

    memset(event_trbs,0,sizeof(event_trbs));
    event_index=0;
    event_cycle=1;
    uint64_t event_address=physical_address(event_trbs);
    event_segment.base_low=(uint32_t)event_address;
    event_segment.base_high=(uint32_t)(event_address>>32);
    event_segment.size=XHCI_EVENT_ENTRIES;
    event_segment.reserved=0;
    volatile uint32_t *interrupter=runtime+0x20/4;
    interrupter[2]=1;
    uint64_t erst_address=physical_address(&event_segment);
    interrupter[4]=(uint32_t)erst_address;
    interrupter[5]=(uint32_t)(erst_address>>32);
    interrupter[6]=(uint32_t)event_address;
    interrupter[7]=(uint32_t)(event_address>>32);
    __sync_synchronize();
    operational[0]|=XHCI_CMD_RUN;
    if(!wait_register(&operational[1],XHCI_STS_HALTED,false)){
        probe_stats.last_error=XHCI_PROBE_RUN_TIMEOUT;
        probe_stats.usb_status=operational[1];
        return false;
    }
    probe_stats.usb_status=operational[1];
    probe_stats.last_stage=2;

    for(uint8_t port=1;port<=max_ports && device_count<XHCI_DEVICE_LIMIT;port++){
        uint8_t speed;
        if(!reset_port(port,&speed)) continue;
        if(!enumerate_port(port,speed,linux_name_base+device_count)){
            probe_stats.failures++;
        }
    }
    if(!probe_stats.connected_ports && !device_count)
        probe_stats.last_error=XHCI_PROBE_NO_CONNECTED_PORT;
    return true;
}

bool xhci_init(uint32_t linux_name_base){
    if(probe_complete) return device_count>0;
    probe_complete=true;
    if(!mapping_ready) return false;
    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0) return false;
    uint8_t xhci_index=0;
    for(int32_t index=0;index<count;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_XHCI) continue;
        probe_stats.controllers++;
        probe_stats.last_stage=1;
        controller_number=xhci_index++;
        if(!initialize_controller(&controllers[index],linux_name_base)){
            probe_stats.failures++;
            continue;
        }
        if(device_count) break;
    }
    return device_count>0;
}

bool xhci_rescan(uint32_t linux_name_base){
    if(device_count) return true;
    if(!mapping_ready) return false;
    selected_device=XHCI_NO_DEVICE;
    probe_stats.connected_ports=0;
    probe_stats.addressed_devices=0;
    probe_stats.mass_storage_devices=0;
    probe_stats.failures=0;
    probe_stats.last_stage=0;
    probe_stats.last_error=XHCI_PROBE_OK;
    probe_stats.last_port=0;
    probe_stats.last_portsc=0;
    probe_stats.last_completion_code=0;
    probe_stats.max_ports=0;
    probe_stats.usb_status=0;

    struct storage_controller_info controllers[8];
    int32_t count=storage_controller_list(controllers,8);
    if(count<0) return false;
    uint8_t xhci_index=0;
    for(int32_t index=0;index<count;index++){
        if(controllers[index].type!=STORAGE_CONTROLLER_XHCI) continue;
        probe_stats.last_stage=1;
        controller_number=xhci_index++;
        if(!initialize_controller(&controllers[index],linux_name_base)){
            probe_stats.failures++;
            continue;
        }
        if(device_count) break;
    }
    return device_count>0;
}

uint32_t xhci_device_count(void){ return device_count; }

bool xhci_get_device_info(uint32_t index, struct storage_device_info *info){
    if(!info || index>=device_count) return false;
    *info=devices[index].info;
    return true;
}

bool xhci_select_device(uint32_t index){
    if(index>=device_count) return false;
    selected_device=(uint8_t)index;
    return true;
}

static bool scsi_sector_command(uint8_t operation, uint32_t lba, void *buffer,
                                bool data_in){
    if(selected_device>=device_count || !buffer) return false;
    struct xhci_device *device=&devices[selected_device];
    if(lba>=device->info.sector_count) return false;
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=operation;
    write_be32(&command[2],lba);
    command[8]=1;
    return bulk_only_command(selected_device,command,10,buffer,512,data_in);
}

bool xhci_read_sector(uint32_t lba, void *buffer){
    return scsi_sector_command(SCSI_READ10,lba,buffer,true);
}

bool xhci_write_sector(uint32_t lba, const void *buffer){
    if(selected_device>=device_count || !devices[selected_device].info.writable){
        return false;
    }
    if(!scsi_sector_command(SCSI_WRITE10,lba,(void*)buffer,false)) return false;
    uint8_t command[16];
    memset(command,0,sizeof(command));
    command[0]=SCSI_SYNC_CACHE10;
    return bulk_only_command(selected_device,command,10,0,0,false);
}

const char *xhci_device_name(void){
    return selected_device<device_count ? devices[selected_device].info.name : "none";
}

void xhci_get_probe_stats(struct xhci_probe_stats *stats){
    if(stats) *stats=probe_stats;
}
