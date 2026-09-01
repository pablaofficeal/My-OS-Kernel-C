#include "editor.h"
#include "../../libc/include/purec.h"

#define HEXEDIT_PATH_CAPACITY 128

void _start(void){
    char arguments[HEXEDIT_PATH_CAPACITY];
    if(pc_get_command_line(arguments,sizeof(arguments))<=0){
        pc_write("usage: /bin/program/hexedit <file>\n");
        pc_exit(2);
    }
    const char *arg=arguments;
    while(*arg==' ' || *arg=='\t') arg++;
    uint32_t len=0;
    while(arg[len] && arg[len]!=' ' && arg[len]!='\t') len++;
    const char *remaining=&arg[len];
    while(*remaining==' ' || *remaining=='\t') remaining++;
    if(!len || *remaining){
        pc_write("hexedit: exactly one file path is required\n");
        pc_exit(2);
    }
    char path[HEXEDIT_PATH_CAPACITY];
    uint32_t path_len=0;
    if(arg[0]!='/'){
        char dir[HEXEDIT_PATH_CAPACITY];
        if(pc_getenv("PWD",dir,sizeof(dir))<0) pc_copy(dir,"/",sizeof(dir));
        path_len=pc_strlen(dir);
        if(path_len+len+2>sizeof(path)){ pc_write("hexedit: path too long\n"); pc_exit(2); }
        pc_copy(path,dir,sizeof(path));
        if(path_len>1) path[path_len++]='/';
    } else if(len+1>sizeof(path)){ pc_write("hexedit: path too long\n"); pc_exit(2); }
    for(uint32_t i=0;i<len;i++) path[path_len++]=arg[i];
    path[path_len]='\0';
    pc_exit(hexedit_run(path));
}
