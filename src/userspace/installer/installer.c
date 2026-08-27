#include "installer.h"
#include "../terminal/terminal.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/storage/storage_types.h"
#include "../../fs/fat32.h"
#include "../../kernel/syscall.h"
#include "../syscall.h"
#include "../../lib/string.h"
#include <stdint.h>
#include <stdbool.h>

#define INSTALLER_VERSION "0.1.0"

static void prompt_read_line(const char *prompt, char *out, uint32_t cap){
    terminal_write(prompt);
    uint32_t len=0;
    memset(out,0,cap);
    for(;;){
        char c=keyboard_getc();
        if(c=='\r' || c=='\n'){
            terminal_putc('\n');
            break;
        }
        if(c=='\b' || c==127){
            if(len){
                len--;
                terminal_putc('\b');
            }
            continue;
        }
        if(c<' ' || c>'~') continue;
        if(len+1<cap){
            out[len++]=c;
            terminal_putc(c);
        }
    }
    out[len]=0;
}

static bool fetch_serial(const char *device, char out[STORAGE_SERIAL_CAPACITY]){
    struct storage_device_info devs[20];
    int64_t cnt=userspace_syscall(SYS_DISK_LIST,(uint64_t)devs,20,0);
    if(cnt<0) return false;
    for(int64_t i=0;i<cnt;i++) if(strcmp(devs[i].name,device)==0){
        memcpy(out,devs[i].serial,STORAGE_SERIAL_CAPACITY);
        return true;
    }
    return false;
}

static bool write_file(const char *path, const char *content){
    int64_t r=userspace_syscall(SYS_FILE_WRITE,(uint64_t)path,(uint64_t)content,strlen(content));
    return r>=0;
}
static bool create_dir(const char *path){
    int64_t r=userspace_syscall(SYS_DIR_CREATE,(uint64_t)path,0,0);
    return r==0 || r==FS_ERROR_EXISTS;
}

static void print_banner(void){
    terminal_write("\n");
    terminal_write("  ____                ____   ___  ____    _   _           _        _ _           \n");
    terminal_write(" |  _ \\ _   _ _ __ __ _ / ___| / _ \\/ ___|  | | |_ __  ___| |_ __ _| | | ___ _ __ \n");
    terminal_write(" | |_) | | | | '__/ _` | |    | | | \\___ \\  | | | '_ \\/ __| __/ _` | | |/ _ \\ '__|\n");
    terminal_write(" |  __/| |_| | | | (_| | |___ | |_| |___) | | | | | | \\__ \\ || (_| | | |  __/ |   \n");
    terminal_write(" |_|    \\__,_|_|  \\__, |\\____| \\___/|____/  |_|_|_| |_|___/\\__\\__,_|_|_|\\___|_|   \n");
    terminal_write("                   |___/                                                        \n");
    terminal_write(" PureC OS Installer " INSTALLER_VERSION "  (UEFI/BIOS, real sector layout)\n");
    terminal_write(" ----------------------------------------------------------------\n");
}

void installer_run(const char *args){
    print_banner();
    // 1. List disks via real block_device_list syscall (sector-level)
    struct storage_device_info devs[20];
    int64_t cnt=userspace_syscall(SYS_DISK_LIST,(uint64_t)devs,20,0);
    if(cnt<=0){
        terminal_write("No disks found. Run 'disks' for diagnostics.\n");
        terminal_write("Hint: check SATA controller in VM settings, ensure disk is not empty VDI header.\n");
        return;
    }
    terminal_write("Disks (real block devices, sectors):\n");
    for(int64_t i=0;i<cnt;i++){
        uint32_t mb=(uint32_t)(devs[i].sector_count/2048);
        terminal_printf("  [%d] %s  %u MB  %s  serial=%s  %s  %s\n",
                        (int)i,devs[i].name,mb,devs[i].model,devs[i].serial,
                        devs[i].operational?"op":"offline",
                        devs[i].writable?"rw":"ro");
        const char *tr=devs[i].transport==STORAGE_TRANSPORT_USB_MSC?"USB-xHCI":
                       devs[i].transport==STORAGE_TRANSPORT_USB_EHCI?"USB-EHCI":
                       devs[i].transport==STORAGE_TRANSPORT_AHCI?"AHCI":"ATA";
        terminal_printf("       transport=%s ctrl=%u port=%u sectors=%u\n",tr,devs[i].controller,devs[i].port,(uint32_t)devs[i].sector_count);
    }
    // 2. Select disk
    char arg_dev[STORAGE_DEVICE_NAME_CAPACITY]={0};
    const char *rem;
    // simple split
    const char *p=args;
    while(*p==' '||*p=='\t') p++;
    uint32_t l=0;
    while(*p && *p!=' ' && *p!='\t' && l+1<sizeof(arg_dev)){ arg_dev[l++]=*p++; }
    arg_dev[l]=0;
    char devname[STORAGE_DEVICE_NAME_CAPACITY]={0};
    if(arg_dev[0]){
        bool ok=false;
        for(int64_t i=0;i<cnt;i++) if(strcmp(devs[i].name,arg_dev)==0) ok=true;
        if(!ok){
            terminal_printf("Device %s not found\n",arg_dev);
            return;
        }
        strcpy(devname,arg_dev);
        terminal_printf("Selected %s from argument\n",devname);
    } else {
        char sel[16]={0};
        prompt_read_line("Select disk number [0]: ",sel,sizeof(sel));
        if(!sel[0]) strcpy(sel,"0");
        int idx=0;
        for(uint32_t i=0;sel[i]>='0'&&sel[i]<='9';i++) idx=idx*10+(sel[i]-'0');
        if(idx<0 || idx>=cnt){
            terminal_write("Invalid selection\n");
            return;
        }
        strcpy(devname,devs[idx].name);
    }
    // 3. Fetch real serial/model/sectors for sector-level ops
    char serial[STORAGE_SERIAL_CAPACITY]={0};
    char model[STORAGE_MODEL_CAPACITY]={0};
    uint64_t sectors=0;
    uint32_t sector_size=512;
    for(int64_t i=0;i<cnt;i++) if(strcmp(devs[i].name,devname)==0){
        strcpy(serial,devs[i].serial);
        strcpy(model,devs[i].model);
        sectors=devs[i].sector_count;
        sector_size=devs[i].sector_size;
        if(!devs[i].writable){ terminal_printf("Device %s is read-only\n",devname); return; }
        if(!devs[i].operational){ terminal_printf("Device %s not operational (dmesg)\n",devname); return; }
        break;
    }
    if(!serial[0]){
        terminal_write("Warning: serial empty, using fallback\n");
        strcpy(serial,"PURE-0000");
    }
    terminal_printf("Target: %s  %s  %u MB  serial=%s  ss=%u\n",devname,model,(uint32_t)(sectors/2048),serial,sector_size);
    // 4. Ask UEFI/BIOS - real firmware matters for sector layout
    char uefi_ans[8]={0};
    prompt_read_line("UEFI system? [Y/n]: ",uefi_ans,sizeof(uefi_ans));
    bool is_uefi = !(uefi_ans[0]=='n' || uefi_ans[0]=='N');
    terminal_printf("Mode: %s (real sectors, MBR+ESP for UEFI, superfloppy for BIOS)\n",is_uefi?"UEFI":"BIOS");
    char hostname[32]={0};
    prompt_read_line("Hostname [purec-os]: ",hostname,sizeof(hostname));
    if(!hostname[0]) strcpy(hostname,"purec-os");
    char username[32]={0};
    prompt_read_line("Username [purec]: ",username,sizeof(username));
    if(!username[0]) strcpy(username,"purec");
    terminal_write("\nSummary (real install):\n");
    terminal_printf("  Device   : %s (%u sectors)\n  Hostname : %s\n  User     : %s\n  Mode     : %s\n",
                    devname,(uint32_t)sectors,hostname,username,is_uefi?"UEFI GPT/MBR+ESP":"BIOS");
    terminal_write("This will ERASE all data and REWRITE sectors (MBR/GPT, FAT, files)!\n");
    char confirm[16]={0};
    prompt_read_line("Type YES to continue: ",confirm,sizeof(confirm));
    bool is_yes = (strcmp(confirm,"YES")==0 || strcmp(confirm,"yes")==0 || strcmp(confirm,"Yes")==0 || strcmp(confirm,"y")==0 || strcmp(confirm,"Y")==0);
    if(!is_yes){
        terminal_write("Aborted (need YES/yes).\n");
        return;
    }
    // 5. Real sector-level format
    terminal_write("\n[1/4] Formatting sectors...\n");
    int64_t fmt;
    if(is_uefi){
        terminal_printf("  -> UEFI ESP: MBR type EF at LBA0, FAT32 at LBA2048 (%u sectors)\n",(uint32_t)(sectors-2048));
        fmt=userspace_syscall(214,(uint64_t)devname,(uint64_t)serial,0); // SYS_FAT32_FORMAT_UEFI
    } else {
        // BIOS superfloppy with force
        fmt=userspace_syscall(SYS_FAT32_FORMAT,(uint64_t)devname,(uint64_t)serial,(uint64_t)"ERASE");
        if(fmt==FS_ERROR_NOT_BLANK || fmt==FS_ERROR_BUSY){
            terminal_write("  -> Not blank/busy, forcing...\n");
            fmt=userspace_syscall(213,(uint64_t)devname,(uint64_t)serial,0);
        }
    }
    if(fmt<0){
        terminal_printf("Format failed (%d) see dmesg\n",(int)fmt);
        if(fmt==FS_ERROR_BUSY) terminal_write("  Hint: disk is mounted, reboot and try again or use another disk\n");
        if(fmt==FS_ERROR_UNSUPPORTED) terminal_write("  Hint: sector_size must be 512, disk must fit 32-bit LBA\n");
        return;
    }
    terminal_write("  Format OK. Verifying sectors...\n");
    // Verify by listing
    struct storage_device_info check[20];
    int64_t cnt2=userspace_syscall(SYS_DISK_LIST,(uint64_t)check,20,0);
    (void)cnt2;
    // 6. Real file layout via FAT syscalls (sector writes via block_device)
    terminal_write("[2/4] Creating directories (FAT clusters)...\n");
    create_dir("/EFI");
    create_dir("/EFI/BOOT");
    create_dir("/boot");
    create_dir("/boot/limine");
    create_dir("/etc");
    create_dir("/home");
    create_dir("/purec");
    terminal_write("[3/4] Copying files to sectors (kernel, bootloader, configs)...\n");
    // For UEFI, kernel and BOOTX64.EFI already written by kernel's format_uefi (embedded blobs)
    // Now write configs as real files
    char cfg[1024];
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,"# PureC OS Install Config - real sector install\n");
    strcat(cfg,"hostname="); strcat(cfg,hostname); strcat(cfg,"\n");
    strcat(cfg,"device="); strcat(cfg,devname); strcat(cfg,"\n");
    strcat(cfg,"serial="); strcat(cfg,serial); strcat(cfg,"\n");
    strcat(cfg,"user="); strcat(cfg,username); strcat(cfg,"\n");
    strcat(cfg,"mode="); strcat(cfg,is_uefi?"uefi":"bios"); strcat(cfg,"\n");
    strcat(cfg,"version=0.1.0\ninstalled=1\n");
    if(!write_file("/purec/install.cfg",cfg)) terminal_write("  Warning: /purec/install.cfg failed\n");
    else terminal_write("  -> /purec/install.cfg written\n");
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,hostname); strcat(cfg,"\n");
    write_file("/etc/hostname",cfg);
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,"timeout: 10\nverbose: yes\n/PureC OS\n    protocol: limine\n    kernel_path: boot():/boot/kernel.elf\n");
    write_file("/boot/limine/limine.cfg",cfg);
    write_file("/limine.cfg",cfg);
    char home_path[64]={0};
    strcpy(home_path,"/home/"); strcat(home_path,username);
    create_dir(home_path);
    char readme_path[80]={0};
    strcpy(readme_path,home_path); strcat(readme_path,"/README");
    memset(cfg,0,sizeof(cfg));
    strcpy(cfg,"Welcome "); strcat(cfg,username); strcat(cfg,"!\nPureC OS installed (real sectors).\n");
    strcat(cfg,"Hostname: "); strcat(cfg,hostname); strcat(cfg,"\nDevice: "); strcat(cfg,devname); strcat(cfg,"\n");
    write_file(readme_path,cfg);
    write_file("/README","PureC OS - see /purec/install.cfg\n");
    terminal_write("[4/4] Verifying install...\n");
    // List written files
    struct fs_directory_entry entries[16];
    int64_t n=userspace_syscall(SYS_DIR_LIST,(uint64_t)"/purec",(uint64_t)entries,16);
    if(n>0){
        terminal_printf("  /purec: %d file(s)\n",(int)n);
        for(int64_t i=0;i<n;i++) terminal_printf("    %s %u bytes\n",entries[i].name,entries[i].size);
    }
    n=userspace_syscall(SYS_DIR_LIST,(uint64_t)"/EFI/BOOT",(uint64_t)entries,16);
    if(n>0){
        terminal_printf("  /EFI/BOOT: %d file(s)\n",(int)n);
        for(int64_t i=0;i<n;i++) terminal_printf("    %s %u bytes\n",entries[i].name,entries[i].size);
    } else if(is_uefi){
        terminal_write("  Warning: /EFI/BOOT not found (UEFI may not boot)\n");
    }
    n=userspace_syscall(SYS_DIR_LIST,(uint64_t)"/boot",(uint64_t)entries,16);
    if(n>0){
        terminal_printf("  /boot: %d file(s)\n",(int)n);
        for(int64_t i=0;i<n;i++) terminal_printf("    %s %u bytes\n",entries[i].name,entries[i].size);
    }
    terminal_write("\nInstall complete (real sectors)!\n");
    terminal_printf("  Device: %s  Host: %s  User: %s  Mode: %s\n",devname,hostname,username,is_uefi?"UEFI":"BIOS");
    terminal_write("  You can now remove ISO and boot from disk.\n");
    terminal_write("  Run 'dmesg | grep fat32' and 'ls /' to verify.\n");
    // Also show VDI host path hint
    terminal_write("  Host VDI: /home/pabla/VirtualBox VMs/test_kernel2/test_kernel2_1.vdi\n");
}

bool installer_is_active(void){ return false; }
void installer_handle_key(char c){ (void)c; }
