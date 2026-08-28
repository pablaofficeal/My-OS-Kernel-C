#include "../lib/runtime.h"
#include "../../drivers/storage/storage_types.h"
#include "../../fs/fs_types.h"
#include "../../kernel/syscall.h"

#define INSTALLER_VERSION "0.3.0"

static bool create_directory(const char *path){
    int64_t result=program_syscall(SYS_MKDIR,(uint64_t)(uintptr_t)path,0,0);
    return result==0 || result==FS_ERROR_EXISTS;
}

static bool write_file(const char *path, const char *contents){
    return program_syscall(SYS_FILE_WRITE,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)contents,program_strlen(contents))>=0;
}

static void print_device(const struct storage_device_info *device,
                         uint32_t index){
    program_write("  [");
    program_write_u64(index);
    program_write("] ");
    program_write(device->name);
    program_write("  ");
    program_write_u64((device->sector_count*device->sector_size)/(1024*1024));
    program_write(" MiB  ");
    program_write(device->model);
    program_write(device->writable ? "  rw" : "  ro");
    program_write(device->operational ? "  online\n" : "  offline\n");
}

static int parse_index(const char *text){
    if(!text[0]) return 0;
    int value=0;
    for(uint32_t index=0;text[index];index++){
        if(text[index]<'0' || text[index]>'9') return -1;
        value=value*10+(text[index]-'0');
    }
    return value;
}

static int installer_main(void){
    program_write("\nPureC OS Installer " INSTALLER_VERSION "\n");
    program_write("Running as isolated ring-3 ELF process, syscall ABI only.\n\n");

    struct storage_device_info devices[20];
    int64_t count=program_syscall(SYS_DISK_LIST,
        (uint64_t)(uintptr_t)devices,20,0);
    if(count<=0){
        program_write("No operational block devices found.\n");
        return 1;
    }
    for(int64_t index=0;index<count;index++){
        print_device(&devices[index],(uint32_t)index);
    }

    char answer[32];
    program_read_line("Select target disk [0]: ",answer,sizeof(answer));
    int selected=parse_index(answer);
    if(selected<0 || selected>=count){
        program_write("Invalid disk selection.\n");
        return 2;
    }
    struct storage_device_info *target=&devices[selected];
    if(!target->writable || !target->operational){
        program_write("Selected disk is not writable and operational.\n");
        return 3;
    }

    program_write("Install mode: UEFI GPT with a separate FAT32 ESP.\n");
    program_write("WARNING: all data on ");
    program_write(target->name);
    program_write(" will be erased.\n");
    program_read_line("Type YES to continue: ",answer,sizeof(answer));
    if(program_strcmp(answer,"YES")!=0){
        program_write("Installation cancelled.\n");
        return 0;
    }

    program_write("Formatting target...\n");
    int64_t format=program_syscall(
        SYS_FAT32_FORMAT_UEFI,
        (uint64_t)(uintptr_t)target->name,
        (uint64_t)(uintptr_t)target->serial,0);
    if(format<0){
        program_write("Format failed with status ");
        program_write_i64(format);
        program_write(".\n");
        return 4;
    }

    if(!create_directory("/etc") || !create_directory("/home")
       || !create_directory("/purec")){
        program_write("Cannot create system directories.\n");
        return 5;
    }
    if(!write_file("/etc/hostname","purec-os\n")
       || !write_file("/purec/install.cfg",
                      "version=0.3.0\ninstalled=1\ninstaller=ring3-elf\n")
       || !write_file("/README",
                      "PureC OS installed by /boot/installer.elf\n")){
        program_write("Cannot write system configuration.\n");
        return 6;
    }
    program_write("Installation completed. Remove the ISO and reboot.\n");
    return 0;
}

void _start(void){
    program_exit(installer_main());
}
