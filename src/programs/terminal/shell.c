#include "shell.h"
#include "environment.h"
#include "path.h"
#include "../../libc/include/purec.h"

#define SHELL_LINE_CAPACITY 256
#define SHELL_PATH_CAPACITY 128

static bool space(char character){
    return character==' ' || character=='\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

static void split_line(char *line, char **command, const char **arguments){
    *command=(char*)skip_spaces(line);
    char *cursor=*command;
    while(*cursor && !space(*cursor)) cursor++;
    if(*cursor) *cursor++='\0';
    *arguments=skip_spaces(cursor);
}

static void print_prompt(void){
    char user[PROCESS_ENVIRONMENT_VALUE_LIMIT];
    char directory[PROCESS_ENVIRONMENT_VALUE_LIMIT];
    if(pc_getenv("USER",user,sizeof(user))<0) pc_copy(user,"purec",sizeof(user));
    if(pc_getenv("PWD",directory,sizeof(directory))<0)
        pc_copy(directory,"/",sizeof(directory));
    pc_write(user);
    pc_write("@os:");
    pc_write(directory);
    pc_write("$ ");
}

static void show_help(void){
    pc_write("Builtins: help clear cd pwd echo env set unset exit\n");
    pc_write("Programs require an absolute path, for example:\n");
    pc_write("  /bin/program/nano /notes.txt\n");
}

static void change_directory(const char *argument){
    char current[SHELL_PATH_CAPACITY];
    char normalized[SHELL_PATH_CAPACITY];
    if(!argument[0]) argument="/";
    if(pc_getenv("PWD",current,sizeof(current))<0)
        pc_copy(current,"/",sizeof(current));
    if(!shell_path_normalize(current,argument,normalized,sizeof(normalized))
       || pc_setenv("PWD",normalized)<0){
        pc_write("cd: invalid path\n");
    }
}

static void set_variable(const char *assignment){
    char name[PROCESS_ENVIRONMENT_NAME_LIMIT];
    uint32_t length=0;
    while(assignment[length] && assignment[length]!='='){
        if(length+1>=sizeof(name)){
            pc_write("set: variable name is too long\n");
            return;
        }
        name[length]=assignment[length];
        length++;
    }
    if(!length || assignment[length]!='='){
        pc_write("set: use NAME=value\n");
        return;
    }
    name[length]='\0';
    if(pc_setenv(name,&assignment[length+1])<0)
        pc_write("set: invalid variable or environment is full\n");
}

static void execute_program(const char *path, const char *arguments){
    if(path[0]!='/'){
        pc_write(path);
        pc_write(": explicit program path required\n");
        return;
    }
    char expanded[SHELL_LINE_CAPACITY];
    if(!shell_expand_environment(arguments,expanded,sizeof(expanded))){
        pc_write("shell: expanded arguments are too long\n");
        return;
    }
    int32_t pid=pc_exec_with_args(path,expanded);
    if(pid<0){
        pc_write("shell: cannot execute ");
        pc_write(path);
        pc_write("\n");
        return;
    }
    int32_t status=0;
    if(pc_wait(pid,&status,false)<0){
        pc_write("shell: wait failed\n");
        return;
    }
    if(status){
        pc_write("shell: program exited with status ");
        pc_write_i64(status);
        pc_write("\n");
    }
}

static bool execute_line(char *line){
    char *command;
    const char *arguments;
    split_line(line,&command,&arguments);
    if(!command[0]) return true;
    if(pc_strcmp(command,"exit")==0) return false;
    if(pc_strcmp(command,"help")==0) show_help();
    else if(pc_strcmp(command,"clear")==0) pc_display_clear(0x181825);
    else if(pc_strcmp(command,"pwd")==0){
        char directory[SHELL_PATH_CAPACITY];
        if(pc_getenv("PWD",directory,sizeof(directory))>=0) pc_write(directory);
        pc_write("\n");
    } else if(pc_strcmp(command,"cd")==0) change_directory(arguments);
    else if(pc_strcmp(command,"echo")==0){
        char expanded[SHELL_LINE_CAPACITY];
        if(shell_expand_environment(arguments,expanded,sizeof(expanded)))
            pc_write(expanded);
        else pc_write("echo: expanded text is too long");
        pc_write("\n");
    } else if(pc_strcmp(command,"env")==0) shell_print_environment();
    else if(pc_strcmp(command,"set")==0) set_variable(arguments);
    else if(pc_strcmp(command,"unset")==0){
        if(pc_unsetenv(arguments)<0) pc_write("unset: variable not found\n");
    } else execute_program(command,arguments);
    return true;
}

int shell_run(void){
    char line[SHELL_LINE_CAPACITY];
    pc_display_clear(0x181825);
    pc_write("PureC Terminal\n");
    pc_write("Minimal shell: type help. Programs need an absolute path.\n\n");
    for(;;){
        print_prompt();
        pc_read_line("",line,sizeof(line));
        if(!execute_line(line)) return 0;
    }
}
