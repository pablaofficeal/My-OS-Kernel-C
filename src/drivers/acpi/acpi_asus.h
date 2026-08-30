#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ASUS_WMI_DEVID_WLAN      0x00010011U
#define ASUS_WMI_METHODID_DEVS   0x53564544U /* 'DEVS' */

bool acpi_asus_init(void);
bool acpi_asus_rfkill_clear_wifi(void);
void acpi_asus_log_ec_info(void);
