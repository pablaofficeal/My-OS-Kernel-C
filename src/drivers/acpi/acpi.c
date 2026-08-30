#include "acpi.h"
#include "../../boot/limine.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include <stddef.h>

extern struct limine_rsdp_response *rsdp_response_ptr;
extern uint64_t hhdm_offset_global;

static bool acpi_ready;
static const struct acpi_rsdp *acpi_rsdp;
static const struct acpi_table_header *acpi_root;
static bool acpi_root_xsdt;
static const struct acpi_fadt *acpi_fadt;
static const struct acpi_table_header *acpi_dsdt;
static uint32_t acpi_lapic_count;

static void *acpi_map_phys_impl(uint64_t physical_address){
    if(physical_address>=hhdm_offset_global)
        return (void *)(uintptr_t)physical_address;
    return (void *)(uintptr_t)(physical_address+hhdm_offset_global);
}

void *acpi_map_phys(uint64_t physical_address){
    return acpi_map_phys_impl(physical_address);
}

static uint8_t acpi_checksum_bytes(const void *data, uint32_t length){
    uint8_t sum=0;
    const uint8_t *bytes=(const uint8_t *)data;
    for(uint32_t i=0;i<length;i++)
        sum+=bytes[i];
    return sum;
}

bool acpi_table_valid(const void *table, uint32_t length){
    if(!table || length<sizeof(struct acpi_table_header)
       || length>ACPI_TABLE_MAX_LENGTH)
        return false;
    return acpi_checksum_bytes(table, length)==0;
}

static bool acpi_header_sane(const struct acpi_table_header *table){
    if(!table)
        return false;
    uint32_t length=table->length;
    return length>=sizeof(struct acpi_table_header)
        && length<=ACPI_TABLE_MAX_LENGTH;
}

static const struct acpi_table_header *acpi_map_table(uint64_t address){
    if(!address)
        return NULL;
    const struct acpi_table_header *table=
        (const struct acpi_table_header *)acpi_map_phys_impl(address);
    if(!acpi_header_sane(table))
        return NULL;
    if(!acpi_table_valid(table, table->length))
        return NULL;
    return table;
}

static bool acpi_rsdp_valid(const struct acpi_rsdp *rsdp){
    if(!rsdp || memcmp(rsdp->signature, "RSD PTR ", 8)!=0)
        return false;
    if(acpi_checksum_bytes(rsdp, 20)!=0)
        return false;
    if(rsdp->revision<2)
        return true;
    if(rsdp->length<sizeof(struct acpi_rsdp))
        return false;
    return acpi_checksum_bytes(rsdp, rsdp->length)==0;
}

static const struct acpi_table_header *acpi_resolve_root(const struct acpi_rsdp *rsdp){
    const struct acpi_table_header *xsdt=NULL;
    const struct acpi_table_header *rsdt=NULL;

    if(rsdp->revision>=2 && rsdp->xsdt_address)
        xsdt=(const struct acpi_table_header *)acpi_map_phys_impl(rsdp->xsdt_address);
    if(rsdp->rsdt_address)
        rsdt=(const struct acpi_table_header *)acpi_map_phys_impl(rsdp->rsdt_address);

    const struct acpi_table_header *candidates[2]={xsdt, rsdt};
    for(int attempt=0;attempt<2;attempt++){
        for(int index=0;index<2;index++){
            const struct acpi_table_header *root=candidates[index];
            if(!root)
                continue;
            if(attempt==1){
                if((uint64_t)(uintptr_t)root>=0x100000000ULL)
                    continue;
                root=(const struct acpi_table_header *)((uint64_t)(uintptr_t)root
                                                        +hhdm_offset_global);
            }
            if(!acpi_header_sane(root))
                continue;
            if(!acpi_table_valid(root, root->length))
                continue;
            acpi_root_xsdt=(root==xsdt || (xsdt && root==(const struct acpi_table_header *)
                                           ((uint64_t)(uintptr_t)xsdt+hhdm_offset_global)));
            return root;
        }
    }
    return NULL;
}

static uint32_t acpi_root_entry_count(const struct acpi_table_header *root){
    if(!root)
        return 0;
    uint32_t entry_size=acpi_root_xsdt ? 8 : 4;
    if(root->length<=sizeof(struct acpi_table_header))
        return 0;
    return (root->length-sizeof(struct acpi_table_header))/entry_size;
}

static uint64_t acpi_root_entry_address(const struct acpi_table_header *root, uint32_t index){
    const uint8_t *entries=(const uint8_t *)root+sizeof(struct acpi_table_header);
    if(acpi_root_xsdt){
        const uint64_t *array=(const uint64_t *)entries;
        return array[index];
    }
    const uint32_t *array=(const uint32_t *)entries;
    return array[index];
}

const struct acpi_table_header *acpi_find_table(const char signature[ACPI_SIG_LEN]){
    if(!acpi_ready || !acpi_root || !signature)
        return NULL;

    uint32_t count=acpi_root_entry_count(acpi_root);
    for(uint32_t i=0;i<count;i++){
        uint64_t address=acpi_root_entry_address(acpi_root, i);
        const struct acpi_table_header *table=acpi_map_table(address);
        if(!table)
            continue;
        if(memcmp(table->signature, signature, ACPI_SIG_LEN)==0)
            return table;
    }
    return NULL;
}

void acpi_foreach_table(acpi_table_visitor visitor, void *context){
    if(!acpi_ready || !acpi_root || !visitor)
        return;

    uint32_t count=acpi_root_entry_count(acpi_root);
    for(uint32_t i=0;i<count;i++){
        uint64_t address=acpi_root_entry_address(acpi_root, i);
        const struct acpi_table_header *table=acpi_map_table(address);
        if(!table)
            continue;
        visitor(table, context);
    }
}

static void acpi_log_table(const struct acpi_table_header *table, void *context){
    (void)context;
    char signature[ACPI_SIG_LEN+1];
    memcpy(signature, table->signature, ACPI_SIG_LEN);
    signature[ACPI_SIG_LEN]='\0';
    klogf(KLOG_DEBUG, "acpi: table %s rev=%u len=%u", signature,
          table->revision, table->length);
}

static void acpi_probe_madt(void){
    const struct acpi_table_header *madt=acpi_find_table("APIC");
    if(!madt)
        return;

    const uint8_t *cursor=(const uint8_t *)madt+sizeof(struct acpi_table_header)+8;
    const uint8_t *end=(const uint8_t *)madt+madt->length;
    while(cursor+sizeof(struct acpi_madt_entry_header)<=end){
        const struct acpi_madt_entry_header *entry=
            (const struct acpi_madt_entry_header *)cursor;
        if(entry->length<sizeof(struct acpi_madt_entry_header) || cursor+entry->length>end)
            break;
        if(entry->type==0)
            acpi_lapic_count++;
        cursor+=entry->length;
    }
    klogf(KLOG_INFO, "acpi: MADT reports %u local APIC entries", acpi_lapic_count);
}

bool acpi_init(void){
    acpi_ready=false;
    acpi_rsdp=NULL;
    acpi_root=NULL;
    acpi_root_xsdt=false;
    acpi_fadt=NULL;
    acpi_dsdt=NULL;
    acpi_lapic_count=0;

    if(!rsdp_response_ptr || !rsdp_response_ptr->address){
        klog(KLOG_WARN, "acpi: RSDP response missing from bootloader");
        return false;
    }

    const struct acpi_rsdp *rsdp=(const struct acpi_rsdp *)rsdp_response_ptr->address;
    if(!acpi_rsdp_valid(rsdp)){
        klog(KLOG_ERROR, "acpi: invalid RSDP checksum or signature");
        return false;
    }

    const struct acpi_table_header *root=acpi_resolve_root(rsdp);
    if(!root){
        klog(KLOG_ERROR, "acpi: XSDT/RSDT not found or checksum failed");
        return false;
    }

    acpi_rsdp=rsdp;
    acpi_root=root;
    acpi_ready=true;

    klogf(KLOG_OK, "acpi: RSDP rev=%u OEM=%.6s root=%s",
          rsdp->revision, rsdp->oem_id, acpi_root_xsdt ? "XSDT" : "RSDT");
    acpi_foreach_table(acpi_log_table, NULL);

    acpi_fadt=(const struct acpi_fadt *)acpi_find_table("FACP");
    if(acpi_fadt){
        klogf(KLOG_INFO, "acpi: FADT rev=%u PM1a_CNT=0x%x",
              acpi_fadt->header.revision, acpi_fadt_pm1a_cnt());
    } else {
        klog(KLOG_WARN, "acpi: FADT (FACP) not found");
    }

    acpi_dsdt=acpi_find_table("DSDT");
    if(acpi_dsdt)
        klogf(KLOG_INFO, "acpi: DSDT mapped at %p len=%u", acpi_dsdt, acpi_dsdt->length);
    else
        klog(KLOG_WARN, "acpi: DSDT not found in XSDT/RSDT");

    acpi_probe_madt();
    return true;
}

bool acpi_is_ready(void){
    return acpi_ready;
}

const struct acpi_rsdp *acpi_get_rsdp(void){
    return acpi_rsdp;
}

const struct acpi_table_header *acpi_get_root_table(void){
    return acpi_root;
}

bool acpi_root_is_xsdt(void){
    return acpi_root_xsdt;
}

const struct acpi_fadt *acpi_get_fadt(void){
    return acpi_fadt;
}

uint32_t acpi_fadt_pm1a_cnt(void){
    if(!acpi_fadt)
        return 0;
    return acpi_fadt->pm1a_cnt_blk;
}

bool acpi_fadt_get_reset(uint8_t *value, const struct acpi_generic_address **reg){
    if(!acpi_fadt || acpi_fadt->header.length<offsetof(struct acpi_fadt, reset_value)+1)
        return false;
    if(value)
        *value=acpi_fadt->reset_value;
    if(reg)
        *reg=&acpi_fadt->reset_reg;
    return acpi_fadt->reset_reg.address!=0;
}

uint32_t acpi_madt_lapic_count(void){
    return acpi_lapic_count;
}

const struct acpi_table_header *acpi_get_dsdt(void){
    return acpi_dsdt;
}
