#include "include/purec.h"
#include "../kernel/syscall.h"

int64_t pc_syscall(uint64_t number, uint64_t argument1,
                   uint64_t argument2, uint64_t argument3){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3)
        : "r10","r8","memory"
    );
    return result;
}

static int64_t syscall5(uint64_t number, uint64_t argument1,
                        uint64_t argument2, uint64_t argument3,
                        uint64_t argument4, uint64_t argument5){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3),
          "S"(argument4),"D"(argument5)
        : "r10","r8","memory"
    );
    return result;
}

uint32_t pc_strlen(const char *text){
    uint32_t length=0;
    while(text[length]) length++;
    return length;
}

int pc_strcmp(const char *left, const char *right){
    while(*left && *left==*right){ left++; right++; }
    return (uint8_t)*left-(uint8_t)*right;
}

void pc_copy(char *destination, const char *source, uint32_t capacity){
    if(!capacity) return;
    uint32_t index=0;
    while(source[index] && index+1<capacity){
        destination[index]=source[index];
        index++;
    }
    destination[index]='\0';
}

void pc_write(const char *text){
    (void)pc_syscall(SYS_WRITE,(uint64_t)(uintptr_t)text,pc_strlen(text),1);
}

void pc_write_u64(uint64_t value){
    char buffer[21];
    uint32_t index=sizeof(buffer);
    buffer[--index]='\0';
    do {
        buffer[--index]=(char)('0'+value%10);
        value/=10;
    } while(value);
    pc_write(&buffer[index]);
}

void pc_write_i64(int64_t value){
    if(value<0){
        pc_write("-");
        pc_write_u64((uint64_t)(-(value+1))+1);
        return;
    }
    pc_write_u64((uint64_t)value);
}

void pc_read_line(const char *prompt, char *buffer, uint32_t capacity){
    pc_write(prompt);
    if(!capacity) return;
    uint32_t length=0;
    for(;;){
        char character=(char)pc_syscall(SYS_GETCHAR,0,0,0);
        if(character=='\r' || character=='\n'){
            pc_write("\n");
            break;
        }
        if((character=='\b' || character==127) && length){
            length--;
            continue;
        }
        if(character<' ' || character>'~') continue;
        if(length+1<capacity) buffer[length++]=character;
    }
    buffer[length]='\0';
}

void pc_sleep(uint32_t milliseconds){
    (void)pc_syscall(SYS_SLEEP,milliseconds,0,0);
}

int32_t pc_getpid(void){ return (int32_t)pc_syscall(SYS_GETPID,0,0,0); }

int32_t pc_exec(const char *path){
    return (int32_t)pc_syscall(SYS_EXEC,(uint64_t)(uintptr_t)path,0,0);
}

int32_t pc_wait(int32_t pid, int32_t *status, bool nohang){
    return (int32_t)pc_syscall(SYS_WAIT,(uint32_t)pid,
        (uint64_t)(uintptr_t)status,nohang ? 1 : 0);
}

int32_t pc_try_getchar(void){
    return (int32_t)pc_syscall(SYS_TRY_GETCHAR,0,0,0);
}

bool pc_file_exists(const char *path){
    int32_t descriptor=(int32_t)pc_syscall(SYS_OPEN,
        (uint64_t)(uintptr_t)path,0,0);
    if(descriptor<0) return false;
    (void)pc_syscall(SYS_CLOSE,(uint32_t)descriptor,0,0);
    return true;
}

bool pc_display_get_info(struct pc_display_info *info){
    if(!info) return false;
    struct framebuffer_info value;
    if(pc_syscall(SYS_FB_INFO,(uint64_t)(uintptr_t)&value,0,0)<0) return false;
    info->width=value.width;
    info->height=value.height;
    info->pitch=value.pitch;
    info->size_bytes=value.size_bytes;
    info->bpp=value.bpp;
    info->available=value.available!=0;
    return true;
}

void pc_display_begin_update(void){
    (void)pc_syscall(SYS_FB_BEGIN_UPDATE,0,0,0);
}

void pc_display_end_update(void){
    (void)pc_syscall(SYS_FB_END_UPDATE,0,0,0);
}

void pc_display_clear(uint32_t color){
    (void)pc_syscall(SYS_CLEAR,color,0,0);
}

void pc_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color){
    (void)syscall5(SYS_DRAW_RECT,x,y,width,height,color);
}

void pc_draw_text(uint32_t x, uint32_t y, const char *text,
                  uint32_t foreground, uint32_t background){
    struct framebuffer_text_request request={
        .x=x,.y=y,.text=text,.fg=foreground,.bg=background,.size=8
    };
    (void)pc_syscall(SYS_DRAW_TEXT,(uint64_t)(uintptr_t)&request,0,0);
}

bool pc_mouse_get(struct mouse_state *state){
    return state && pc_syscall(SYS_GET_MOUSE,(uint64_t)(uintptr_t)state,0,0)>=0;
}

int32_t pc_list_disks(struct storage_device_info *devices, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_DISK_LIST,
        (uint64_t)(uintptr_t)devices,capacity,0);
}

int32_t pc_install_start(const char *device, const char *serial){
    return (int32_t)pc_syscall(SYS_INSTALL_START,
        (uint64_t)(uintptr_t)device,(uint64_t)(uintptr_t)serial,0);
}

bool pc_install_status(struct install_status *status){
    return status && pc_syscall(SYS_INSTALL_STATUS,
        (uint64_t)(uintptr_t)status,0,0)>=0;
}

bool pc_install_log(struct install_log *log){
    return log && pc_syscall(SYS_INSTALL_LOG,
        (uint64_t)(uintptr_t)log,0,0)>=0;
}

void pc_exit(int32_t status){
    (void)pc_syscall(SYS_EXIT,(uint64_t)(int64_t)status,0,0);
    for(;;) __asm__ volatile("pause");
}
