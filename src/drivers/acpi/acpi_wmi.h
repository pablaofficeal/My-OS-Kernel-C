#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ACPI_WMI_EXPENSIVE 0x02U

struct acpi_wmi_guid_block {
    uint8_t guid[16];
    uint8_t object_id[2];
    uint8_t instance_count;
    uint8_t flags;
} __attribute__((packed));

bool acpi_wmi_init(void);
bool acpi_wmi_has_guid(const uint8_t guid[16]);
bool acpi_wmi_evaluate_method(const uint8_t guid[16], uint8_t instance,
                              uint32_t method_id, const void *input,
                              uint32_t input_size, uint32_t *retval);
