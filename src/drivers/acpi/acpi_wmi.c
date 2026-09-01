#include "acpi_wmi.h"
#include "acpi.h"
#include "acpi_aml.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

#define ACPI_WMI_BLOCK_SIZE 20U

static bool wmi_ready;
static struct acpi_wmi_guid_block wmi_blocks[32];
static uint32_t wmi_block_count;

static const uint8_t asus_wmi_guid[16]={
    0xD0, 0x5E, 0x84, 0x97, 0x6D, 0x4E, 0xDE, 0x11,
    0x8A, 0x39, 0x08, 0x00, 0x20, 0x0C, 0x9A, 0x66
};

static bool guid_equal(const uint8_t *a, const uint8_t *b){
    return memcmp(a, b, 16)==0;
}

static uint32_t wmi_method_id_from_block(const struct acpi_wmi_guid_block *block){
    return ((uint32_t)block->object_id[0])
        | ((uint32_t)block->object_id[1] << 8)
        | ((uint32_t)block->object_id[0] << 16)
        | ((uint32_t)block->object_id[1] << 24);
}

static void wmi_collect_blocks_from_buffer(const uint8_t *data, uint32_t length){
    if(!data || length<ACPI_WMI_BLOCK_SIZE)
        return;
    for(uint32_t offset=0; offset+ACPI_WMI_BLOCK_SIZE<=length; offset+=ACPI_WMI_BLOCK_SIZE){
        const struct acpi_wmi_guid_block *block=
            (const struct acpi_wmi_guid_block *)(data+offset);
        if(wmi_block_count>=32U)
            return;
        wmi_blocks[wmi_block_count++]=*block;
    }
}

static void wmi_scan_dsdt_for_guid_blocks(void){
    const struct acpi_table_header *dsdt=acpi_get_dsdt();
    if(!dsdt || dsdt->length<=sizeof(struct acpi_table_header))
        return;
    const uint8_t *body=(const uint8_t *)dsdt+sizeof(struct acpi_table_header);
    uint32_t body_len=dsdt->length-(uint32_t)sizeof(struct acpi_table_header);
    for(uint32_t i=0;i+ACPI_WMI_BLOCK_SIZE<=body_len;i++){
        if(!guid_equal(body+i, asus_wmi_guid))
            continue;
        const uint8_t *start=body+i-(i % ACPI_WMI_BLOCK_SIZE);
        uint32_t span=body_len-(uint32_t)(start-body);
        if(span>ACPI_WMI_BLOCK_SIZE*8U)
            span=ACPI_WMI_BLOCK_SIZE*8U;
        wmi_collect_blocks_from_buffer(start, span);
        klogf(KLOG_DEBUG, "acpi-wmi: ASUS GUID block cluster at DSDT+0x%x",
              (unsigned)(start-body));
    }
}

bool acpi_wmi_init(void){
    wmi_ready=false;
    wmi_block_count=0;
    if(!acpi_is_ready())
        return false;
    wmi_scan_dsdt_for_guid_blocks();
    wmi_ready=wmi_block_count>0;
    if(wmi_ready)
        klogf(KLOG_OK, "acpi-wmi: discovered %u WMI blocks", wmi_block_count);
    else
        klog(KLOG_INFO, "acpi-wmi: no WMI blocks found in DSDT");
    return wmi_ready;
}

bool acpi_wmi_has_guid(const uint8_t guid[16]){
    if(!guid)
        return false;
    for(uint32_t i=0;i<wmi_block_count;i++){
        if(guid_equal(wmi_blocks[i].guid, guid))
            return true;
    }
    return false;
}

static bool acpi_wmi_call_wm_method(const struct acpi_wmi_guid_block *block,
                                    const void *input, uint32_t input_size,
                                    uint32_t *retval){
    char method_name[5]="WM??";
    method_name[2]=(char)block->object_id[0];
    method_name[3]=(char)block->object_id[1];

    struct acpi_aml_method method;
    if(!acpi_aml_find_method("ATKD", method_name, &method)){
        if(!acpi_aml_find_method("ASUS", method_name, &method))
            return false;
    }

    uint64_t args[7]={0,0,0,0,0,0,0};
    if(input && input_size>=4U){
        const uint8_t *bytes=(const uint8_t *)input;
        args[0]=(uint64_t)bytes[0]
            |((uint64_t)bytes[1]<<8)
            |((uint64_t)bytes[2]<<16)
            |((uint64_t)bytes[3]<<24);
    }
    if(input && input_size>=8U){
        const uint8_t *bytes=(const uint8_t *)input;
        args[1]=(uint64_t)bytes[4]
            |((uint64_t)bytes[5]<<8)
            |((uint64_t)bytes[6]<<16)
            |((uint64_t)bytes[7]<<24);
    }

    uint64_t result=0;
    if(!acpi_aml_evaluate_method(&method, args, 2, &result))
        return false;
    if(retval)
        *retval=(uint32_t)result;
    klogf(KLOG_INFO, "acpi-wmi: %s returned 0x%x", method_name, (unsigned)result);
    return true;
}

bool acpi_wmi_evaluate_method(const uint8_t guid[16], uint8_t instance,
                              uint32_t method_id, const void *input,
                              uint32_t input_size, uint32_t *retval){
    if(!guid)
        return false;
    if(!wmi_ready && !acpi_wmi_init())
        return false;

    for(uint32_t i=0;i<wmi_block_count;i++){
        const struct acpi_wmi_guid_block *block=&wmi_blocks[i];
        if(!guid_equal(block->guid, guid))
            continue;
        if(instance!=0xFFU && block->instance_count!=instance && instance!=0U)
            continue;
        if(method_id!=0U && method_id!=0x53564544U
           && wmi_method_id_from_block(block)!=method_id)
            continue;
        if(acpi_wmi_call_wm_method(block, input, input_size, retval))
            return true;
    }
    return false;
}
