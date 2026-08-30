#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ACPI_EC_STATUS_IBF 0x02U
#define ACPI_EC_STATUS_OBF 0x01U

bool acpi_ec_init(void);
bool acpi_ec_is_ready(void);
bool acpi_ec_read(uint8_t offset, uint8_t *value);
bool acpi_ec_write(uint8_t offset, uint8_t value);
bool acpi_ec_query(uint8_t *value);
