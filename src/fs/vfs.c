#include "vfs.h"
#include "fat32.h"
#include "../lib/string.h"
#include "../kernel/diagnostics/klog.h"
#include "../drivers/storage/block_device.h"

#define VFS_MAX_OPEN_FILES 32

enum vfs_handle_type {
    VFS_HANDLE_NONE,
    VFS_HANDLE_FAT32,
    VFS_HANDLE_KERNEL_FILE,
    VFS_HANDLE_KLOG
};

struct kernel_file {
    const char *path;
    const char *name;
    const char *content;
};

struct vfs_handle {
    enum vfs_handle_type type;
    int32_t backend_descriptor;
    const char *data;
    uint32_t size;
    uint32_t position;
    uint64_t klog_cursor;
    bool klog_data_lost;
};

static const struct kernel_file kernel_files[]={
    {
        "/kernel/version",
        "version",
        "PureC OS kernel 0.1.0\n"
    },
    {
        "/kernel/init",
        "init",
        "pid1=/bin/init shell=/bin/program/terminal editor=/bin/program/nano\n"
    },
    {
        "/kernel/abi",
        "abi",
        "syscall=int80 process=exec,args,env,wait,exit fd=per-process vfs=fat32\n"
    }
};

static struct vfs_handle handles[VFS_MAX_OPEN_FILES];

static bool path_equals(const char *left, const char *right){
    return left && right && strcmp(left,right)==0;
}

static void copy_name(char destination[FS_DIRECTORY_NAME_CAPACITY],
                      const char *source){
    uint32_t index=0;
    while(source[index] && index+1<FS_DIRECTORY_NAME_CAPACITY){
        destination[index]=source[index];
        index++;
    }
    destination[index]='\0';
}

static const struct kernel_file *find_kernel_file(const char *path){
    for(uint32_t index=0;index<sizeof(kernel_files)/sizeof(kernel_files[0]);
        index++){
        if(path_equals(path,kernel_files[index].path)) return &kernel_files[index];
    }
    return 0;
}

static int32_t allocate_handle(void){
    for(uint32_t index=0;index<VFS_MAX_OPEN_FILES;index++){
        if(handles[index].type==VFS_HANDLE_NONE) return (int32_t)index;
    }
    return FS_ERROR_NO_SPACE;
}

static struct vfs_handle *get_handle(int32_t descriptor){
    int32_t index=descriptor-VFS_FD_BASE;
    if(index<0 || index>=VFS_MAX_OPEN_FILES) return 0;
    if(handles[index].type==VFS_HANDLE_NONE) return 0;
    return &handles[index];
}

bool vfs_mount_root(void){
    return fat32_init();
}

bool vfs_is_root_mounted(void){
    return fat32_is_mounted();
}

const char *vfs_root_device_name(void){
    return fat32_device_name();
}

static bool is_klog_path(const char *path){
    return path && (strcmp(path,"/kernel.log")==0 || strcmp(path,"/dmesg.txt")==0);
}
int32_t vfs_open(const char *path){
    if(!path || !path[0]) return FS_ERROR_INVALID;
    const struct kernel_file *kernel_file=find_kernel_file(path);
    int32_t handle_index=allocate_handle();
    if(handle_index<0) return handle_index;

    struct vfs_handle *handle=&handles[handle_index];
    if(kernel_file){
        handle->type=VFS_HANDLE_KERNEL_FILE;
        handle->backend_descriptor=-1;
        handle->data=kernel_file->content;
        handle->size=(uint32_t)strlen(kernel_file->content);
        handle->position=0;
        return VFS_FD_BASE+handle_index;
    }
    if(is_klog_path(path)){
        handle->type=VFS_HANDLE_KLOG;
        handle->backend_descriptor=-1;
        handle->data=0;
        handle->size=0;
        handle->position=0;
        handle->klog_cursor=0;
        uint64_t total=klog_total_bytes();
        handle->klog_cursor = total > (8*1024*1024) ? total - 8*1024*1024 : 0;
        handle->klog_data_lost=false;
        return VFS_FD_BASE+handle_index;
    }

    int32_t backend_descriptor=fat32_open(path);
    if(backend_descriptor<0){
        if(is_klog_path(path)){
            handle->type=VFS_HANDLE_KLOG;
            handle->backend_descriptor=-1;
            handle->klog_cursor=0;
            return VFS_FD_BASE+handle_index;
        }
        memset(handle,0,sizeof(*handle));
        return backend_descriptor;
    }
    handle->type=VFS_HANDLE_FAT32;
    handle->backend_descriptor=backend_descriptor;
    handle->data=0;
    handle->size=0;
    handle->position=0;
    return VFS_FD_BASE+handle_index;
}

int32_t vfs_read(int32_t descriptor, void *buffer, uint32_t count){
    if(!buffer && count) return FS_ERROR_INVALID;
    struct vfs_handle *handle=get_handle(descriptor);
    if(!handle) return FS_ERROR_INVALID;
    if(handle->type==VFS_HANDLE_FAT32){
        return fat32_read(handle->backend_descriptor,buffer,count);
    }
    if(handle->type==VFS_HANDLE_KLOG){
        uint32_t amount=klog_read_since(&handle->klog_cursor, (char*)buffer, count, &handle->klog_data_lost);
        return (int32_t)amount;
    }
    if(handle->type!=VFS_HANDLE_KERNEL_FILE) return FS_ERROR_INVALID;
    if(handle->position>=handle->size) return 0;
    uint32_t remaining=handle->size-handle->position;
    uint32_t amount=count<remaining ? count : remaining;
    if(amount) memcpy(buffer,handle->data+handle->position,amount);
    handle->position+=amount;
    return (int32_t)amount;
}

int32_t vfs_close(int32_t descriptor){
    struct vfs_handle *handle=get_handle(descriptor);
    if(!handle) return FS_ERROR_INVALID;
    int32_t result=0;
    if(handle->type==VFS_HANDLE_FAT32)
        result=fat32_close(handle->backend_descriptor);
    memset(handle,0,sizeof(*handle));
    return result;
}

int32_t vfs_delete(const char *path){
    if(find_kernel_file(path) || path_equals(path,"/kernel")) return FS_ERROR_READ_ONLY;
    return fat32_delete(path);
}

int32_t vfs_rename(const char *path, const char *new_name){
    if(find_kernel_file(path) || path_equals(path,"/kernel")) return FS_ERROR_READ_ONLY;
    return fat32_rename(path,new_name);
}

int32_t vfs_move(const char *path, const char *destination_directory){
    if(find_kernel_file(path) || path_equals(path,"/kernel")) return FS_ERROR_READ_ONLY;
    return fat32_move(path,destination_directory);
}

int32_t vfs_list(const char *path, struct fs_directory_entry *entries,
                 uint32_t capacity){
    if(!path || !entries || capacity==0) return FS_ERROR_INVALID;
    if(path_equals(path,"/")){
        int32_t fat_count=fat32_list(path,entries,capacity);
        uint32_t count=fat_count>0 ? (uint32_t)fat_count : 0;
        if(count<capacity){
            copy_name(entries[count].name,"kernel");
            entries[count].size=0;
            entries[count].attributes=FS_ATTRIBUTE_DIRECTORY;
            count++;
        }
        if(count<capacity){
            bool has=false;
            for(uint32_t i=0;i<count;i++) if(strcmp(entries[i].name,"dmesg.txt")==0) has=true;
            if(!has){
                copy_name(entries[count].name,"dmesg.txt");
                entries[count].size=(uint32_t)klog_total_bytes();
                if(entries[count].size>8*1024*1024) entries[count].size=8*1024*1024;
                entries[count].attributes=0;
                count++;
            }
        }
        if(count<capacity){
            bool has=false;
            for(uint32_t i=0;i<count;i++) if(strcmp(entries[i].name,"kernel.log")==0) has=true;
            if(!has){
                copy_name(entries[count].name,"kernel.log");
                entries[count].size=(uint32_t)klog_total_bytes();
                if(entries[count].size>8*1024*1024) entries[count].size=8*1024*1024;
                entries[count].attributes=0;
                count++;
            }
        }
        return (int32_t)count;
    }
    if(path_equals(path,"/kernel")){
        uint32_t count=sizeof(kernel_files)/sizeof(kernel_files[0]);
        if(count>capacity) count=capacity;
        for(uint32_t index=0;index<count;index++){
            copy_name(entries[index].name,kernel_files[index].name);
            entries[index].size=(uint32_t)strlen(kernel_files[index].content);
            entries[index].attributes=0;
        }
        return (int32_t)count;
    }
    return fat32_list(path,entries,capacity);
}

int32_t vfs_create_file(const char *path){
    if(!path || path_equals(path,"/kernel")) return FS_ERROR_INVALID;
    if(find_kernel_file(path)) return FS_ERROR_READ_ONLY;
    if(is_klog_path(path)) return 0;
    return fat32_create_file(path);
}
static uint32_t raw_log_base_lba;
static uint32_t raw_log_next_sector;
static bool raw_log_inited;
static int32_t raw_log_write(const void *buffer, uint32_t count);
static int32_t raw_log_truncate(void);
int32_t vfs_write_file(const char *path, const void *buffer, uint32_t count){
    if(find_kernel_file(path) || path_equals(path,"/kernel")) return FS_ERROR_READ_ONLY;
    if(is_klog_path(path)){
        if(count==0) raw_log_truncate();
        int32_t r = fat32_write_file(path,buffer,count);
        if(r>=0) {
            (void)raw_log_write(buffer,count);
            return r;
        }
        int32_t raw = raw_log_write(buffer,count);
        if(raw>=0) return (int32_t)count;
        return (int32_t)count;
    }
    return fat32_write_file(path,buffer,count);
}

int32_t vfs_append_file(const char *path, const void *buffer, uint32_t count){
    if(find_kernel_file(path) || path_equals(path,"/kernel")) return FS_ERROR_READ_ONLY;
    if(is_klog_path(path)){
        int32_t r = fat32_append_file(path,buffer,count);
        if(r<0 && !vfs_is_root_mounted()){
            r = fat32_write_file(path,buffer,count);
        }
        if(r>=0) {
            (void)raw_log_write(buffer,count);
            return r;
        }
        int32_t raw = raw_log_write(buffer,count);
        if(raw>=0) return (int32_t)count;
        return (int32_t)count;
    }
    return fat32_append_file(path,buffer,count);
}

int32_t vfs_create_directory(const char *path){
    if(!path || path_equals(path,"/kernel")) return FS_ERROR_INVALID;
    if(find_kernel_file(path)) return FS_ERROR_READ_ONLY;
    return fat32_create_directory(path);
}

int32_t vfs_format_device(const char *device_name,
                          const char *serial_confirmation,
                          const char *erase_confirmation){
    return fat32_format_device(device_name,serial_confirmation,erase_confirmation);
}

int32_t vfs_format_device_force(const char *device_name,
                                const char *serial_confirmation){
    return fat32_format_device_force(device_name,serial_confirmation);
}

int32_t vfs_format_uefi_device(const char *device_name,
                               const char *serial_confirmation){
    return fat32_format_uefi_device(device_name,serial_confirmation);
}

int32_t vfs_format_uefi_device_progress(
    const char *device_name, const char *serial_confirmation,
    fat32_progress_callback callback){
    return fat32_format_uefi_device_progress(device_name,serial_confirmation,
                                              callback);
}
static bool raw_log_init(void){
    if(raw_log_inited) return true;
    uint32_t n = block_device_count();
    if(n==0) return false;
    struct storage_device_info info;
    bool found=false;
    for(uint32_t i=0;i<n;i++){
        if(block_device_get_info(i,&info) && info.selected){ found=true; break; }
    }
    if(!found){
        if(!block_device_get_info(0,&info)) return false;
    }
    if(info.sector_count < 4096) return false;
    raw_log_base_lba = 1024;
    if(raw_log_base_lba + 2048 > info.sector_count) raw_log_base_lba = info.sector_count - 2048;
    raw_log_next_sector = 1;
    raw_log_inited = true;
    uint8_t hdr[512];
    hdr[0]='K'; hdr[1]='L'; hdr[2]='O'; hdr[3]='G';
    hdr[4]=0; hdr[5]=0; hdr[6]=0; hdr[7]=0;
    for(uint32_t i=8;i<512;i++) hdr[i]=0;
    (void)block_device_write(raw_log_base_lba, hdr);
    return true;
}
static int32_t raw_log_truncate(void){
    if(!raw_log_init()) return FS_ERROR_INVALID;
    raw_log_next_sector = 1;
    uint8_t hdr[512];
    hdr[0]='K'; hdr[1]='L'; hdr[2]='O'; hdr[3]='G';
    for(uint32_t i=4;i<512;i++) hdr[i]=0;
    if(!block_device_write(raw_log_base_lba, hdr)) return FS_ERROR_IO;
    return 0;
}
static int32_t raw_log_write(const void *buffer, uint32_t count){
    if(!buffer && count) return FS_ERROR_INVALID;
    if(count==0) return 0;
    if(!raw_log_init()) return FS_ERROR_INVALID;
    const uint8_t *src=(const uint8_t*)buffer;
    uint32_t written=0;
    uint8_t sector[512];
    while(count>0){
        if(raw_log_next_sector >= 2048) break;
        uint32_t to_copy = count > 512 ? 512 : count;
        memset(sector,0,512);
        memcpy(sector,src,to_copy);
        if(!block_device_write(raw_log_base_lba + raw_log_next_sector, sector)) break;
        raw_log_next_sector++;
        src+=to_copy;
        count-=to_copy;
        written+=to_copy;
        if(count==0){
            uint8_t hdr[512];
            if(block_device_read(raw_log_base_lba, hdr)){
                hdr[4]=(uint8_t)(raw_log_next_sector & 0xFF);
                hdr[5]=(uint8_t)((raw_log_next_sector>>8)&0xFF);
                hdr[6]=(uint8_t)((raw_log_next_sector>>16)&0xFF);
                hdr[7]=(uint8_t)((raw_log_next_sector>>24)&0xFF);
                (void)block_device_write(raw_log_base_lba, hdr);
            }
        }
    }
    return written ? (int32_t)written : FS_ERROR_IO;
}
