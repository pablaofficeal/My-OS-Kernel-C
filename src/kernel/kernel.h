#pragma once
#include "../boot/limine.h"
#include <stdint.h>

// System info exposed to userspace
extern char cpu_brand_string[49]; // CPU brand string (null-terminated)
extern uint64_t total_ram_bytes;  // Total usable RAM in bytes

void kernel_main(struct limine_framebuffer *fb);