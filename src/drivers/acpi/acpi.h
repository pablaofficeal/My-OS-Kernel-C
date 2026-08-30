#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ACPI_SIG_LEN 4

struct acpi_table_header {
    char signature[ACPI_SIG_LEN];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_generic_address {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct acpi_fadt {
    struct acpi_table_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved0;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1a_evt_len;
    uint8_t pm1b_evt_len;
    uint8_t pm1a_cnt_len;
    uint8_t pm1b_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint8_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved1;
    uint32_t flags;
    struct acpi_generic_address reset_reg;
    uint8_t reset_value;
    uint16_t arm_boot_arch;
    uint8_t fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_lapic {
    struct acpi_madt_entry_header header;
    uint8_t acpi_processor_uid;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

typedef void (*acpi_table_visitor)(const struct acpi_table_header *table, void *context);

bool acpi_init(void);
bool acpi_is_ready(void);

const struct acpi_rsdp *acpi_get_rsdp(void);
const struct acpi_table_header *acpi_get_root_table(void);
bool acpi_root_is_xsdt(void);

void *acpi_map_phys(uint64_t physical_address);
bool acpi_table_valid(const void *table, uint32_t length);

const struct acpi_table_header *acpi_find_table(const char signature[ACPI_SIG_LEN]);
void acpi_foreach_table(acpi_table_visitor visitor, void *context);

const struct acpi_fadt *acpi_get_fadt(void);
uint32_t acpi_fadt_pm1a_cnt(void);
bool acpi_fadt_get_reset(uint8_t *value, const struct acpi_generic_address **reg);

uint32_t acpi_madt_lapic_count(void);

const struct acpi_table_header *acpi_get_dsdt(void);
