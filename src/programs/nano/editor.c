#include "editor.h"
#include "../../libc/include/purec.h"

#define NANO_BUFFER_CAPACITY 4096
#define NANO_READ_CHUNK 256

static char editor_buffer[NANO_BUFFER_CAPACITY];
static uint32_t editor_length;
static bool editor_dirty;
static const char *editor_path;

static void write_buffer(void){
    char character[2]={'\0','\0'};
    for(uint32_t index=0;index<editor_length;index++){
        character[0]=editor_buffer[index];
        pc_write(character);
    }
}

static void redraw(const char *status){
    pc_display_clear(0x181825);
    pc_write("PureC nano - ");
    pc_write(editor_path);
    if(editor_dirty) pc_write(" [modified]");
    pc_write("\nCtrl+S save | Ctrl+X exit\n");
    if(status && status[0]){
        pc_write(status);
        pc_write("\n");
    }
    pc_write("------------------------------------------------------------\n");
    write_buffer();
}

static int load_file(void){
    int32_t descriptor=pc_file_open(editor_path);
    if(descriptor<0) return 0;
    for(;;){
        char chunk[NANO_READ_CHUNK];
        int32_t count=pc_file_read(descriptor,chunk,sizeof(chunk));
        if(count<0){
            (void)pc_file_close(descriptor);
            return -1;
        }
        if(!count) break;
        if(editor_length+(uint32_t)count>NANO_BUFFER_CAPACITY){
            (void)pc_file_close(descriptor);
            return -2;
        }
        for(int32_t index=0;index<count;index++)
            editor_buffer[editor_length++]=chunk[index];
    }
    (void)pc_file_close(descriptor);
    return 1;
}

static bool save_file(void){
    if(pc_file_write(editor_path,editor_buffer,editor_length)<0) return false;
    editor_dirty=false;
    return true;
}

int nano_run(const char *path){
    editor_path=path;
    editor_length=0;
    editor_dirty=false;
    int loaded=load_file();
    if(loaded<0){
        pc_write(loaded==-2 ? "nano: file exceeds 4096 bytes\n"
                            : "nano: cannot read file\n");
        return 1;
    }
    redraw(loaded ? "" : "New file.");
    bool exit_armed=false;
    for(;;){
        char character=(char)pc_syscall(SYS_GETCHAR,0,0,0);
        if(character==19){
            exit_armed=false;
            redraw(save_file() ? "Saved." : "Save failed.");
            continue;
        }
        if(character==24 || character==27){
            if(editor_dirty && !exit_armed){
                exit_armed=true;
                redraw("Unsaved changes: press Ctrl+X again to discard.");
                continue;
            }
            return 0;
        }
        if(character=='\b' || character==127){
            if(editor_length){
                editor_length--;
                editor_dirty=true;
                exit_armed=false;
                redraw("");
            }
            continue;
        }
        if(character=='\r') character='\n';
        if(character!='\n' && character!='\t'
           && (character<' ' || character>'~')) continue;
        if(editor_length>=NANO_BUFFER_CAPACITY){
            redraw("Buffer is full.");
            continue;
        }
        editor_buffer[editor_length++]=character;
        editor_dirty=true;
        exit_armed=false;
        char echo[2]={character,'\0'};
        pc_write(echo);
    }
}
