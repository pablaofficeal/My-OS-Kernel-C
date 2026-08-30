#pragma once

#include <stdbool.h>
#include <stdint.h>

struct acpi_aml_method {
    const uint8_t *bytecode;
    uint32_t bytecode_length;
    uint8_t arg_count;
};

bool acpi_aml_find_method(const char *parent_name, const char *method_name,
                          struct acpi_aml_method *out);
bool acpi_aml_evaluate_method(const struct acpi_aml_method *method,
                              const uint64_t args[7], uint8_t arg_count,
                              uint64_t *retval);
