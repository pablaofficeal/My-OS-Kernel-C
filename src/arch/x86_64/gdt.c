#include "gdt.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_high_flags;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr gp;

extern void gdt_flush(uint64_t);

void gdt_init(void) {
    // null
    gdt[0] = (struct gdt_entry){0,0,0,0,0,0};
    // code 64-bit: base=0 limit=0 access=0x9A flags=0xA0 (L=1)
    gdt[1] = (struct gdt_entry){0,0,0,0x9A,0xA0,0};
    // data: access=0x92 flags=0xA0
    gdt[2] = (struct gdt_entry){0,0,0,0x92,0x00,0};

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint64_t)&gdt;

    gdt_flush((uint64_t)&gp);
}
