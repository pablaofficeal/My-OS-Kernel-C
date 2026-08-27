#include "shell_path.h"
#include "../../lib/string.h"

static char current_directory[SHELL_PATH_CAPACITY]="/";

static bool append_component(char output[SHELL_PATH_CAPACITY], uint32_t *length,
                             const char *component, uint32_t component_length){
    uint32_t required=*length+component_length+(*length>1 ? 1 : 0);
    if(required>=SHELL_PATH_CAPACITY) return false;
    if(*length>1) output[(*length)++]='/';
    memcpy(&output[*length],component,component_length);
    *length+=component_length;
    output[*length]='\0';
    return true;
}

static void remove_component(char output[SHELL_PATH_CAPACITY], uint32_t *length){
    if(*length<=1) return;
    while(*length>1 && output[*length-1]!='/') (*length)--;
    if(*length>1) (*length)--;
    output[*length]='\0';
}

const char *shell_path_current(void){ return current_directory; }

bool shell_path_resolve(const char *path, char output[SHELL_PATH_CAPACITY]){
    if(!path || !output) return false;
    while(*path==' ' || *path=='\t') path++;

    uint32_t length;
    if(*path=='/'){
        output[0]='/';
        output[1]='\0';
        length=1;
    } else {
        length=(uint32_t)strlen(current_directory);
        if(length>=SHELL_PATH_CAPACITY) return false;
        memcpy(output,current_directory,length+1);
    }

    while(*path){
        while(*path=='/') path++;
        if(!*path || *path==' ' || *path=='\t') break;
        const char *component=path;
        uint32_t component_length=0;
        while(path[component_length] && path[component_length]!='/'
              && path[component_length]!=' ' && path[component_length]!='\t'){
            component_length++;
        }
        path+=component_length;
        if(component_length==1 && component[0]=='.') continue;
        if(component_length==2 && component[0]=='.' && component[1]=='.'){
            remove_component(output,&length);
            continue;
        }
        if(!append_component(output,&length,component,component_length)) return false;
    }

    while(*path==' ' || *path=='\t') path++;
    return *path=='\0';
}

bool shell_path_change(const char *path){
    char resolved[SHELL_PATH_CAPACITY];
    if(!path || !path[0]) path="/";
    if(!shell_path_resolve(path,resolved)) return false;
    memcpy(current_directory,resolved,strlen(resolved)+1);
    return true;
}
