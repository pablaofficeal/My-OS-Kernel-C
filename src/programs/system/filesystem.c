#include "filesystem.h"
#include "../terminal/path.h"
#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"

#define SYSTEM_PATH_CAPACITY 128

static bool resolve_path(const char *input, const char *fallback,
                         char output[SYSTEM_PATH_CAPACITY]){
    char current[SYSTEM_PATH_CAPACITY];
    if(!input[0]) input=fallback;
    if(pc_getenv("PWD",current,sizeof(current))<0)
        pc_copy(current,"/",sizeof(current));
    return shell_path_normalize(current,input,output,SYSTEM_PATH_CAPACITY);
}

static int command_ls(const char *arguments){
    char path[SYSTEM_PATH_CAPACITY];
    if(!resolve_path(arguments,".",path)){
        pc_write("ls: invalid path\n");
        return 1;
    }
    struct pf_entry entries[32];
    int32_t count=pf_list(path,entries,32);
    if(count<0){
        pc_write("ls: cannot list directory, error ");
        pc_write_i64(count);
        pc_write("\n");
        return 1;
    }
    if(!count){
        pc_write("(empty)\n");
        return 0;
    }
    for(int32_t index=0;index<count;index++){
        pc_write(pf_is_dir(&entries[index])
            ? "[DIR]  " : "[FILE] ");
        pc_write(entries[index].name);
        if(!pf_is_dir(&entries[index])){
            pc_write("  ");
            pc_write_u64(entries[index].size);
            pc_write(" bytes");
        }
        pc_write("\n");
    }
    return 0;
}

static int print_file(const char *path, bool add_final_newline){
    int32_t descriptor=pf_open(path);
    if(descriptor<0){
        pc_write("cat: cannot open file, error ");
        pc_write_i64(descriptor);
        pc_write("\n");
        return 1;
    }
    bool wrote=false;
    char last='\0';
    for(;;){
        char buffer[256];
        int32_t count=pf_read(descriptor,buffer,sizeof(buffer));
        if(count<0){
            (void)pf_close(descriptor);
            pc_write("cat: read failed\n");
            return 1;
        }
        if(!count) break;
        for(int32_t index=0;index<count;index++){
            char text[2]={buffer[index],'\0'};
            pc_write(text);
            last=buffer[index];
        }
        wrote=true;
    }
    (void)pf_close(descriptor);
    if(add_final_newline && wrote && last!='\n') pc_write("\n");
    return 0;
}

static int command_cat(const char *arguments){
    char path[SYSTEM_PATH_CAPACITY];
    if(!arguments[0] || !resolve_path(arguments,"",path)){
        pc_write("cat: file path required\n");
        return 1;
    }
    return print_file(path,true);
}

static int command_create(const char *name, const char *arguments,
                          bool is_dir){
    char path[SYSTEM_PATH_CAPACITY];
    if(!arguments[0] || !resolve_path(arguments,"",path)){
        pc_write(name);
        pc_write(": path required\n");
        return 1;
    }
    int64_t status=is_dir ? pf_create_dir(path) : pf_create_file(path);
    if(status<0){
        pc_write(name);
        pc_write(": cannot create path, error ");
        pc_write_i64(status);
        pc_write("\n");
        return 1;
    }
    return 0;
}

static int command_dmesg(void){
    pc_write("--- kernel log ---\n");
    int status=print_file("/kernel.log",false);
    pc_write("\n--- end kernel log ---\n");
    return status;
}

static int command_savelog(void){
    int32_t source=pf_open("/kernel.log");
    if(source<0){
        pc_write("savelog: /kernel.log is unavailable\n");
        return 1;
    }
    if(pf_write_file("/dmesg.txt",0,0)<0){
        (void)pf_close(source);
        pc_write("savelog: cannot create /dmesg.txt\n");
        return 1;
    }
    uint64_t total=0;
    for(;;){
        char buffer[512];
        int32_t count=pf_read(source,buffer,sizeof(buffer));
        if(count<0){
            (void)pf_close(source);
            return 1;
        }
        if(!count) break;
        int64_t written=pf_append_file("/dmesg.txt",buffer,(uint32_t)count);
        if(written!=count){
            (void)pf_close(source);
            pc_write("savelog: write failed\n");
            return 1;
        }
        total+=(uint32_t)written;
    }
    (void)pf_close(source);
    pc_write("Saved ");
    pc_write_u64(total);
    pc_write(" bytes to /dmesg.txt\n");
    return 0;
}

int system_filesystem_command(const char *name, const char *arguments){
    if(pc_strcmp(name,"ls")==0) return command_ls(arguments);
    if(pc_strcmp(name,"cat")==0) return command_cat(arguments);
    if(pc_strcmp(name,"touch")==0)
        return command_create(name,arguments,false);
    if(pc_strcmp(name,"mkdir")==0)
        return command_create(name,arguments,true);
    if(pc_strcmp(name,"dmesg")==0) return command_dmesg();
    if(pc_strcmp(name,"savelog")==0) return command_savelog();
    return -1;
}