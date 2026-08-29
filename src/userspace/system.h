#pragma once

#include "../kernel/syscall/syscall.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool system_get_cpu_info(struct cpu_monitor_info *info);
bool system_get_memory_info(struct memory_monitor_info *info);
uint64_t system_uptime_ms(void);
uint64_t system_tsc_frequency_hz(void);

#ifdef __cplusplus
}
#endif
