#include "system_info.h"

#include <stddef.h>

#define CPU_NAME_CAPACITY 49

static char cpu_name[CPU_NAME_CAPACITY] = "Unknown x86_64 CPU";
static uint64_t usable_ram_bytes;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx){
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

static void copy_register(char *destination, uint32_t value){
    for(uint32_t byte=0;byte<4;byte++){
        destination[byte]=(char)(value>>(byte*8));
    }
}

static void remove_leading_spaces(char *text){
    size_t start=0;
    while(text[start]==' ') start++;
    if(start==0) return;

    size_t destination=0;
    do {
        text[destination++]=text[start++];
    } while(text[start-1]);
}

static void detect_cpu_name(void){
    uint32_t eax,ebx,ecx,edx;
    cpuid(0x80000000,&eax,&ebx,&ecx,&edx);

    if(eax>=0x80000004){
        for(uint32_t part=0;part<3;part++){
            cpuid(0x80000002+part,&eax,&ebx,&ecx,&edx);
            char *destination=&cpu_name[part*16];
            copy_register(destination,eax);
            copy_register(destination+4,ebx);
            copy_register(destination+8,ecx);
            copy_register(destination+12,edx);
        }
        cpu_name[CPU_NAME_CAPACITY-1]='\0';
        remove_leading_spaces(cpu_name);
        return;
    }

    cpuid(0,&eax,&ebx,&ecx,&edx);
    copy_register(cpu_name,ebx);
    copy_register(cpu_name+4,edx);
    copy_register(cpu_name+8,ecx);
    cpu_name[12]='\0';
}

static void detect_usable_ram(const struct limine_memmap_response *memory_map){
    usable_ram_bytes=0;
    if(!memory_map) return;

    for(uint64_t index=0;index<memory_map->entry_count;index++){
        const struct limine_memmap_entry *entry=memory_map->entries[index];
        if(entry && entry->type==LIMINE_MEMMAP_USABLE){
            usable_ram_bytes+=entry->length;
        }
    }
}

void system_info_init(const struct limine_memmap_response *memory_map){
    detect_cpu_name();
    detect_usable_ram(memory_map);
}

const char *system_info_cpu_name(void){ return cpu_name; }

uint64_t system_info_usable_ram_bytes(void){ return usable_ram_bytes; }
