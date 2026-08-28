#include "vfs.h"
#include "fat32.h"
#include "../lib/string.h"

#define VFS_MAX_OPEN_FILES 32

enum vfs_handle_type {
    VFS_HANDLE_NONE,
    VFS_HANDLE_FAT32,
    VFS_HANDLE_KERNEL_FILE
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
        "init=/sbin/init fallback=linked-userspace\n"
    },
    {
        "/kernel/abi",
        "abi",
        "syscall=int80 fd=linux-like vfs=root:fat32,virtual:/kernel\n"
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

    int32_t backend_descriptor=fat32_open(path);
    if(backend_descriptor<0) return backend_descriptor;
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
    return fat32_create_file(path);
}

int32_t vfs_write_file(const char *path, const void *buffer, uint32_t count){
    if(find_kernel_file(path) || path_equals(path,"/kernel")) return FS_ERROR_READ_ONLY;
    return fat32_write_file(path,buffer,count);
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
