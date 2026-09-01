#include "module.h"
#include "../boot/install_source.h"
#include "diagnostics/klog.h"
#include "diagnostics/panic.h"
#include "../mm/pmm.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../drivers/pci/pci.h"
#include "../arch/x86_64/mmio.h"
#include "../net/core/net_device.h"
#include "../net/wifi/wifi.h"
#include "../net/network/ipv4.h"
#include "../drivers/interrupts/timer.h"
#define ET_REL 1
#define ET_EXEC 2
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_WRITE 0x1
#define STB_GLOBAL 1
#define STT_FUNC 2
#define STT_OBJECT 1
#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_16 12
#define R_X86_64_PC16 13
#define R_X86_64_8 14
#define R_X86_64_PC8 15
struct elf64_hdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));
struct elf64_shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed));
struct elf64_sym {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed));
struct elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} __attribute__((packed));
struct kernel_symbol {
    const char *name;
    void *addr;
};
static void *ksym_resolve(const char *name);
static struct kernel_symbol g_ksyms[] = {
    {"pci_enumerate",(void*)pci_enumerate},
    {"pci_read_config32",(void*)pci_read_config32},
    {"pci_write_config32",(void*)pci_write_config32},
    {"pci_read_bar",(void*)pci_read_bar},
    {"pci_update_command",(void*)pci_update_command},
    {"mmio_map",(void*)mmio_map},
    {"klog",(void*)klog},
    {"klogf",(void*)klogf},
    {"pmm_allocate_page",(void*)pmm_allocate_page},
    {"pmm_free_page",(void*)pmm_free_page},
    {"pmm_allocate_contiguous",(void*)pmm_allocate_contiguous},
    {"pmm_physical_to_virtual",(void*)pmm_physical_to_virtual},
    {"timer_ticks",(void*)timer_ticks},
    {"timer_sleep",(void*)timer_sleep},
    {"memcpy",(void*)memcpy},
    {"memset",(void*)memset},
    {"memcmp",(void*)memcmp},
    {"strlen",(void*)strlen},
    {"strcmp",(void*)strcmp},
    {"strncmp",(void*)strncmp},
    {"strcpy",(void*)strcpy},
    {"strncpy",(void*)strncpy},
    {"net_device_register",(void*)net_device_register},
    {"wifi_device_register",(void*)wifi_device_register},
    {"wifi_trigger_scan",(void*)wifi_trigger_scan},
    {"wifi_get_scan_results",(void*)wifi_get_scan_results},
    {"wifi_report_scan_result",(void*)wifi_report_scan_result},
    {"wifi_notify_scan_done",(void*)wifi_notify_scan_done},
    {"wifi_notify_connected",(void*)wifi_notify_connected},
    {"wifi_notify_connect_failed",(void*)wifi_notify_connect_failed},
    {"wifi_get_status",(void*)wifi_get_status},
    {"ipv4_configure",(void*)ipv4_configure},
    {0,0}
};
static void *ksym_resolve(const char *name){
    for(size_t i=0;g_ksyms[i].name;i++){
        if(strcmp(g_ksyms[i].name,name)==0) return g_ksyms[i].addr;
    }
    return 0;
}
static uint64_t align_up(uint64_t v, uint64_t a){
    if(a==0) return v;
    uint64_t mask=a-1;
    return (v+mask)&~mask;
}
bool module_load_from_memory(const void *data, uint64_t size){
    if(!data||size<sizeof(struct elf64_hdr)) return false;
    const struct elf64_hdr *hdr=data;
    if(hdr->e_ident[0]!=0x7F||hdr->e_ident[1]!='E'||hdr->e_ident[2]!='L'||hdr->e_ident[3]!='F') return false;
    if(hdr->e_ident[4]!=2||hdr->e_ident[5]!=1) return false;
    if(hdr->e_type!=ET_REL) return false;
    if(hdr->e_machine!=62) return false;
    if(hdr->e_shoff==0||hdr->e_shnum==0||hdr->e_shentsize!=sizeof(struct elf64_shdr)) return false;
    if(hdr->e_shoff + (uint64_t)hdr->e_shnum*sizeof(struct elf64_shdr) > size) return false;
    const struct elf64_shdr *shdrs=(const struct elf64_shdr*)((const uint8_t*)data+hdr->e_shoff);
    const struct elf64_shdr *shstr_shdr=&shdrs[hdr->e_shstrndx];
    if(shstr_shdr->sh_offset+shstr_shdr->sh_size>size) return false;
    const char *shstr=(const char*)data+shstr_shdr->sh_offset;
    uint64_t alloc_total=0;
    for(uint16_t i=0;i<hdr->e_shnum;i++){
        const struct elf64_shdr *sh=&shdrs[i];
        if(sh->sh_flags & SHF_ALLOC){
            alloc_total=align_up(alloc_total,sh->sh_addralign?sh->sh_addralign:1);
            alloc_total+=sh->sh_size;
        }
    }
    if(alloc_total==0) return false;
    uint64_t pages=(alloc_total+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE;
    uint64_t phys=pmm_allocate_contiguous(pages);
    if(!phys) return false;
    void *load_base=pmm_physical_to_virtual(phys);
    if(!load_base) return false;
    memset(load_base,0,pages*PMM_PAGE_SIZE);
    uint64_t *sec_addr=(uint64_t*)pmm_allocate_contiguous((hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE);
    if(!sec_addr){pmm_free_contiguous(phys,pages); return false;}
    void *sec_addr_virt=pmm_physical_to_virtual((uint64_t)sec_addr);
    memset(sec_addr_virt,0,hdr->e_shnum*sizeof(uint64_t));
    uint64_t *sec_v=(uint64_t*)sec_addr_virt;
    uint64_t off=0;
    for(uint16_t i=0;i<hdr->e_shnum;i++){
        const struct elf64_shdr *sh=&shdrs[i];
        if(sh->sh_flags & SHF_ALLOC){
            off=align_up(off,sh->sh_addralign?sh->sh_addralign:1);
            sec_v[i]=(uint64_t)load_base+off;
            if(sh->sh_type!=SHT_NOBITS){
                if(sh->sh_offset+sh->sh_size>size){pmm_free_contiguous(phys,pages); pmm_free_contiguous((uint64_t)sec_addr,(hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE); return false;}
                memcpy((void*)sec_v[i],(const uint8_t*)data+sh->sh_offset,sh->sh_size);
            }
            off+=sh->sh_size;
        }
    }
    const struct elf64_shdr *symtab_shdr=0;
    const struct elf64_shdr *strtab_shdr=0;
    for(uint16_t i=0;i<hdr->e_shnum;i++){
        const struct elf64_shdr *sh=&shdrs[i];
        if(sh->sh_type==SHT_SYMTAB){symtab_shdr=sh; if(sh->sh_link < hdr->e_shnum) strtab_shdr=&shdrs[sh->sh_link]; break;}
    }
    if(!symtab_shdr||!strtab_shdr){pmm_free_contiguous(phys,pages); pmm_free_contiguous((uint64_t)sec_addr,(hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE); return false;}
    if(symtab_shdr->sh_offset+symtab_shdr->sh_size>size||strtab_shdr->sh_offset+strtab_shdr->sh_size>size){pmm_free_contiguous(phys,pages); pmm_free_contiguous((uint64_t)sec_addr,(hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE); return false;}
    const struct elf64_sym *syms=(const struct elf64_sym*)((const uint8_t*)data+symtab_shdr->sh_offset);
    size_t sym_count=symtab_shdr->sh_size / sizeof(struct elf64_sym);
    const char *strtab=(const char*)data+strtab_shdr->sh_offset;
    uint64_t *sym_resolved=(uint64_t*)pmm_allocate_contiguous((sym_count*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE);
    if(!sym_resolved){pmm_free_contiguous(phys,pages); pmm_free_contiguous((uint64_t)sec_addr,(hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE); return false;}
    void *sym_resolved_virt=pmm_physical_to_virtual((uint64_t)sym_resolved);
    memset(sym_resolved_virt,0,sym_count*sizeof(uint64_t));
    uint64_t *sym_v=(uint64_t*)sym_resolved_virt;
    for(size_t i=0;i<sym_count;i++){
        const struct elf64_sym *sym=&syms[i];
        uint8_t bind=sym->st_info>>4;
        uint16_t shndx=sym->st_shndx;
        if(sym->st_name==0){sym_v[i]=0; continue;}
        if(shndx==0){
            const char *name=strtab+sym->st_name;
            void *addr=ksym_resolve(name);
            if(!addr){
                if(bind==STB_GLOBAL){
                    klogf(KLOG_WARN,"module: unresolved symbol %s",name);
                    pmm_free_contiguous(phys,pages);
                    pmm_free_contiguous((uint64_t)sec_addr,(hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE);
                    pmm_free_contiguous((uint64_t)sym_resolved,(sym_count*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE);
                    return false;
                } else sym_v[i]=0;
            } else sym_v[i]=(uint64_t)addr;
        } else if(shndx < hdr->e_shnum){
            sym_v[i]=sec_v[shndx]+sym->st_value;
        } else sym_v[i]=0;
    }
    for(uint16_t i=0;i<hdr->e_shnum;i++){
        const struct elf64_shdr *sh=&shdrs[i];
        if(sh->sh_type!=SHT_RELA) continue;
        const struct elf64_rela *relas=(const struct elf64_rela*)((const uint8_t*)data+sh->sh_offset);
        size_t rela_count=sh->sh_size / sizeof(struct elf64_rela);
        uint32_t target_idx=sh->sh_info;
        if(target_idx>=hdr->e_shnum) continue;
        uint64_t target_base=sec_v[target_idx];
        if(!target_base) continue;
        for(size_t r=0;r<rela_count;r++){
            const struct elf64_rela *rela=&relas[r];
            uint64_t offset=rela->r_offset;
            uint64_t info=rela->r_info;
            uint32_t sym_idx=info>>32;
            uint32_t type=info & 0xFFFFFFFFU;
            int64_t addend=rela->r_addend;
            uint64_t sym_val=0;
            if(sym_idx < sym_count) sym_val=sym_v[sym_idx];
            uint64_t place=target_base+offset;
            uint64_t val=sym_val+addend;
            switch(type){
                case R_X86_64_NONE: break;
                case R_X86_64_64: *(uint64_t*)place=val; break;
                case R_X86_64_PC32:
                case R_X86_64_PLT32: *(uint32_t*)place=(uint32_t)(val - place); break;
                case R_X86_64_32: *(uint32_t*)place=(uint32_t)val; break;
                case R_X86_64_32S: *(int32_t*)place=(int32_t)val; break;
                default: klogf(KLOG_WARN,"module: unhandled rela type %u",type); break;
            }
        }
    }
    const char *target_sym="ar928x_module_init";
    uint64_t entry=0;
    for(size_t i=0;i<sym_count;i++){
        const struct elf64_sym *sym=&syms[i];
        if(sym->st_name==0) continue;
        const char *name=strtab+sym->st_name;
        if(strcmp(name,target_sym)==0){entry=sym_v[i]; break;}
    }
    if(!entry){
        for(size_t i=0;i<sym_count;i++){
            const struct elf64_sym *sym=&syms[i];
            if(sym->st_name==0) continue;
            const char *name=strtab+sym->st_name;
            if(strcmp(name,"module_init")==0){entry=sym_v[i]; break;}
        }
    }
    pmm_free_contiguous((uint64_t)sec_addr,(hdr->e_shnum*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE);
    pmm_free_contiguous((uint64_t)sym_resolved,(sym_count*sizeof(uint64_t)+PMM_PAGE_SIZE-1)/PMM_PAGE_SIZE);
    if(!entry){
        klog(KLOG_ERROR,"module: entry ar928x_module_init not found");
        pmm_free_contiguous(phys,pages);
        return false;
    }
    klogf(KLOG_INFO,"module: calling entry 0x%llx base %p pages %llu",(unsigned long long)entry,load_base,(unsigned long long)pages);
    bool (*init_fn)(void) = (bool(*)(void))(uintptr_t)entry;
    bool ok=init_fn();
    if(!ok) klog(KLOG_WARN,"module: init returned false");
    else klog(KLOG_OK,"module: init success");
    return ok;
}
bool module_load_from_limine(void){
    uint64_t count=boot_get_module_count();
    if(!count) return false;
    bool loaded=false;
    for(uint64_t i=0;i<count;i++){
        const void *addr=0;
        uint64_t sz=0;
        const char *mod_path=0;
        if(!boot_get_module_by_index(i,&addr,&sz,&mod_path)) continue;
        if(!mod_path) continue;
        size_t len=strlen(mod_path);
        if(len<3) continue;
        if(!(mod_path[len-3]=='.' && mod_path[len-2]=='k' && mod_path[len-1]=='o')) continue;
        bool has=false;
        for(size_t k=0;k+5<len;k++){
            if(mod_path[k]=='a'&&mod_path[k+1]=='r'&&mod_path[k+2]=='9'&&mod_path[k+3]=='2'&&mod_path[k+4]=='8'&&mod_path[k+5]=='x'){has=true; break;}
        }
        if(!has) continue;
        klogf(KLOG_INFO,"module: found %s size %llu",mod_path,(unsigned long long)sz);
        if(module_load_from_memory(addr,sz)) loaded=true;
    }
    return loaded;
}
__attribute__((weak)) bool ar928x_module_init(void);
void modules_init(void){
    klog(KLOG_INFO,"modules: scanning limine modules for .ko");
    bool ok=module_load_from_limine();
    if(!ok) klog(KLOG_INFO,"modules: no .ko loaded via limine, trying fallback built-in");
    if(!ok){
        if(ar928x_module_init){
            if(ar928x_module_init()) klog(KLOG_OK,"modules: ar928x built-in init ok");
            else klog(KLOG_WARN,"modules: ar928x built-in not present or failed");
        } else klog(KLOG_WARN,"modules: ar928x built-in symbol not found");
    }
}
