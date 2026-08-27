#include "nano.h"
#include "../syscall.h"
#include "../terminal/terminal.h"
#include "../../fs/fat32.h"
#include "../../kernel/syscall.h"
#include "../../lib/string.h"
#include <stdint.h>

#define NANO_BUFFER_CAPACITY 4096
#define NANO_PATH_CAPACITY    128

static char editor_buffer[NANO_BUFFER_CAPACITY];
static char editor_path[NANO_PATH_CAPACITY];
static uint32_t editor_length;
static bool editor_active;
static bool editor_dirty;
static bool exit_armed;

static void nano_redraw(const char *status){
    terminal_clear();
    terminal_printf("PureC nano - %s%s\n",editor_path,
                    editor_dirty ? " [modified]" : "");
    terminal_write("Ctrl+S save | Ctrl+X exit | editor supports append/backspace\n");
    if(status && status[0]) terminal_printf("%s\n",status);
    terminal_write("------------------------------------------------------------\n");
    for(uint32_t index=0;index<editor_length;index++){
        terminal_putc(editor_buffer[index]);
    }
}

static void nano_leave(void){
    editor_active=false;
    editor_dirty=false;
    exit_armed=false;
    terminal_clear();
    terminal_prompt();
}

static void nano_save(void){
    int64_t status=userspace_syscall(SYS_FILE_WRITE,(uint64_t)editor_path,
                                     (uint64_t)editor_buffer,editor_length);
    if(status<0){
        nano_redraw("Save failed; the original file is unchanged.");
        return;
    }
    editor_dirty=false;
    exit_armed=false;
    nano_redraw("Saved.");
}

void nano_open(const char *path){
    if(!path || !path[0] || strlen(path)>=NANO_PATH_CAPACITY){
        terminal_write("nano: invalid or too long file path\n");
        return;
    }

    memcpy(editor_path,path,strlen(path)+1);
    editor_length=0;
    editor_dirty=false;
    exit_armed=false;
    bool file_too_large=false;

    int64_t descriptor=userspace_syscall(SYS_FILE_OPEN,(uint64_t)path,0,0);
    if(descriptor>=0){
        char overflow_buffer[256];
        for(;;){
            void *target=overflow_buffer;
            uint32_t capacity=sizeof(overflow_buffer);
            if(editor_length<NANO_BUFFER_CAPACITY){
                target=&editor_buffer[editor_length];
                capacity=NANO_BUFFER_CAPACITY-editor_length;
                if(capacity>sizeof(overflow_buffer)) capacity=sizeof(overflow_buffer);
            }
            int64_t count=userspace_syscall(SYS_FILE_READ,(uint64_t)descriptor,
                                             (uint64_t)target,capacity);
            if(count<0){
                terminal_printf("nano: read failed (error %d)\n",(int)count);
                return;
            }
            if(count==0) break;
            if(editor_length<NANO_BUFFER_CAPACITY){
                editor_length+=(uint32_t)count;
            } else {
                file_too_large=true;
            }
        }
    } else if(descriptor!=FS_ERROR_NOT_FOUND){
        terminal_printf("nano: cannot open file (error %d)\n",(int)descriptor);
        return;
    }
    if(file_too_large){
        terminal_write("nano: file is larger than the 4096-byte editor limit\n");
        return;
    }

    editor_active=true;
    nano_redraw(descriptor==FS_ERROR_NOT_FOUND ? "New file." : "");
}

bool nano_is_active(void){ return editor_active; }

void nano_handle_key(char character){
    if(!editor_active) return;
    if(character==19){
        nano_save();
        return;
    }
    if(character==24 || character==27){
        if(editor_dirty && !exit_armed){
            exit_armed=true;
            nano_redraw("Unsaved changes: press Ctrl+X again to discard them.");
            return;
        }
        nano_leave();
        return;
    }
    if(character=='\b' || character==127){
        if(editor_length){
            editor_length--;
            editor_dirty=true;
            exit_armed=false;
            nano_redraw("");
        }
        return;
    }
    if(character=='\r') character='\n';
    if(character!='\n' && character!='\t'
       && (character<' ' || character>'~')){
        return;
    }
    if(editor_length>=NANO_BUFFER_CAPACITY){
        nano_redraw("Buffer is full (4096 bytes). Save or remove text.");
        return;
    }
    editor_buffer[editor_length++]=character;
    editor_dirty=true;
    exit_armed=false;
    terminal_putc(character);
}
