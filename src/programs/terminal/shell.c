#include "shell.h"
#include "environment.h"
#include "path.h"
#include "window.h"
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

static bool append_prompt(char *prompt, uint32_t *length,
                          uint32_t capacity, const char *text){
    for(uint32_t index=0;text[index];index++){
        if(*length+1>=capacity) return false;
        prompt[(*length)++]=text[index];
    }
    prompt[*length]='\0';
    return true;
}

static void build_prompt(char *prompt, uint32_t capacity){
    char user[PROCESS_ENVIRONMENT_VALUE_LIMIT];
    char directory[PROCESS_ENVIRONMENT_VALUE_LIMIT];
    if(pc_getenv("USER",user,sizeof(user))<0) pc_copy(user,"purec",sizeof(user));
    if(pc_getenv("PWD",directory,sizeof(directory))<0)
        pc_copy(directory,"/",sizeof(directory));
    uint32_t length=0;
    prompt[0]='\0';
    if(!append_prompt(prompt,&length,capacity,user)
       || !append_prompt(prompt,&length,capacity,"@os:")
       || !append_prompt(prompt,&length,capacity,directory)
       || !append_prompt(prompt,&length,capacity,"$ "))
        pc_copy(prompt,"$ ",capacity);
}

static void show_help(void){
    pc_write("Builtins: help clear cd pwd echo env set unset ping exit\n");
    pc_write("  ping [-c count] <ip|host|url>\n");
    pc_write("System programs resolve through PATH=/bin/program:/bin:\n");
    pc_write("  ls [directory] | cat <file> | touch <file> | mkdir <directory>\n");
    pc_write("  nano <file> | disks | usbscan | dmesg | savelog\n");
    pc_write("  install | setup | update | mkfs.fat32\n");
    pc_write("  uname | about | systeminfo | htop | font | snake | files | gui-demo\n");
    pc_write("  mouse | debug | battery | reboot | poweroff | shutdown | halt\n");
}

static void write_ipv4(uint32_t address){
    pc_write_u64((address>>24)&255);
    pc_write(".");
    pc_write_u64((address>>16)&255);
    pc_write(".");
    pc_write_u64((address>>8)&255);
    pc_write(".");
    pc_write_u64(address&255);
}

static bool parse_count(const char *text, uint32_t length, uint32_t *count){
    if(!length) return false;
    uint32_t value=0;
    for(uint32_t index=0;index<length;index++){
        if(text[index]<'0' || text[index]>'9') return false;
        value=value*10U+(uint32_t)(text[index]-'0');
        if(value>20) return false;
    }
    if(!value) return false;
    *count=value;
    return true;
}

static void ping_error(int32_t status){
    if(status==-2) pc_write("ping: no active network interface\n");
    else if(status==-3) pc_write("ping: network is not configured; waiting for DHCP\n");
    else if(status==-4) pc_write("ping: cannot resolve host name\n");
    else if(status==-5) pc_write("Request timeout\n");
    else if(status==-6) pc_write("ping: another network query is active\n");
    else if(status==-7) pc_write("ping: network transmission failed\n");
    else pc_write("ping: invalid target or arguments\n");
}

static void command_ping(const char *arguments){
    const char *cursor=skip_spaces(arguments);
    if(pc_strcmp(cursor,"--help")==0){
        pc_write("usage: ping [-c count] <ip|host|url>\n");
        pc_write("Send 4 ICMP echo requests by default; count range is 1..20.\n");
        return;
    }
    uint32_t count=4;
    if(cursor[0]=='-' && cursor[1]=='c' && space(cursor[2])){
        cursor=skip_spaces(cursor+2);
        const char *end=cursor;
        while(*end && !space(*end)) end++;
        if(!parse_count(cursor,(uint32_t)(end-cursor),&count)){
            pc_write("ping: count must be between 1 and 20\n");
            return;
        }
        cursor=skip_spaces(end);
    }
    if(!*cursor){
        pc_write("ping: target required; use ping --help\n");
        return;
    }
    char target[NETWORK_PING_TARGET_CAPACITY];
    uint32_t length=0;
    while(cursor[length] && !space(cursor[length])){
        if(length+1>=sizeof(target)){
            pc_write("ping: target is too long\n");
            return;
        }
        target[length]=cursor[length];
        length++;
    }
    target[length]='\0';
    if(*skip_spaces(cursor+length)){
        pc_write("ping: unexpected extra argument\n");
        return;
    }
    bool heading=false;
    for(uint32_t sequence=1;sequence<=count;sequence++){
        struct network_ping_result result={0};
        int32_t status=pc_ping(target,(uint16_t)sequence,2000,&result);
        if(status==0){
            if(!heading){
                pc_write("PING ");
                pc_write(target);
                pc_write(" (");
                write_ipv4(result.address);
                pc_write("): 56 data bytes\n");
                heading=true;
            }
            pc_write("64 bytes from ");
            write_ipv4(result.address);
            pc_write(": icmp_seq=");
            pc_write_u64(result.sequence);
            pc_write(" ttl=");
            pc_write_u64(result.ttl);
            pc_write(" time=");
            pc_write_u64(result.round_trip_ms);
            pc_write(" ms\n");
        } else ping_error(status);
        if(sequence<count) pc_sleep(1000);
    }
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

static int32_t start_program(struct terminal_window *terminal,
                             const char *path, const char *arguments){
    int32_t pid=pc_exec_with_args(path,arguments);
    if(pid<0) return -1;
    int32_t status=0;
    if(pc_wait(pid,&status,false)<0){
        (void)terminal_window_restore(terminal);
        pc_write("shell: wait failed\n");
        return -2;
    }
    (void)terminal_window_restore(terminal);
    if(status){
        pc_write("shell: program exited with status ");
        pc_write_i64(status);
        pc_write("\n");
    }
    return pid;
}

static bool build_program_path(const char *directory, uint32_t length,
                               const char *name, char *output,
                               uint32_t capacity){
    uint32_t name_length=pc_strlen(name);
    bool needs_separator=length && directory[length-1]!='/';
    if(length+name_length+(needs_separator ? 1 : 0)+1>capacity) return false;
    uint32_t output_length=0;
    for(uint32_t index=0;index<length;index++)
        output[output_length++]=directory[index];
    if(needs_separator) output[output_length++]='/';
    for(uint32_t index=0;index<name_length;index++)
        output[output_length++]=name[index];
    output[output_length]='\0';
    return true;
}

static void execute_program(struct terminal_window *terminal,
                            const char *name, const char *arguments){
    char expanded[SHELL_LINE_CAPACITY];
    if(!shell_expand_environment(arguments,expanded,sizeof(expanded))){
        pc_write("shell: expanded arguments are too long\n");
        return;
    }
    if(name[0]=='/'){
        if(start_program(terminal,name,expanded)>=0) return;
    } else if(name[0] && !name[1] && name[0]=='.'){
        pc_write("shell: executable name required\n");
        return;
    } else {
        for(const char *cursor=name;*cursor;cursor++){
            if(*cursor=='/'){
                pc_write("shell: relative executable paths are not supported\n");
                return;
            }
        }
        char search_path[PROCESS_ENVIRONMENT_VALUE_LIMIT];
        if(pc_getenv("PATH",search_path,sizeof(search_path))<0){
            pc_copy(search_path,"/bin/program:/bin",sizeof(search_path));
        }
        const char *directory=search_path;
        while(*directory){
            uint32_t length=0;
            while(directory[length] && directory[length]!=':') length++;
            if(length){
                char candidate[SHELL_PATH_CAPACITY];
                if(build_program_path(directory,length,name,candidate,
                                      sizeof(candidate))
                   && start_program(terminal,candidate,expanded)>=0) return;
            } else {
                char candidate[SHELL_PATH_CAPACITY];
                if(build_program_path("/bin/program",12,name,candidate,
                                      sizeof(candidate))
                   && start_program(terminal,candidate,expanded)>=0) return;
            }
            directory+=length;
            if(*directory==':') directory++;
        }
        {
            char candidate[SHELL_PATH_CAPACITY];
            if(build_program_path("/bin/program",12,name,candidate,
                                  sizeof(candidate))
               && start_program(terminal,candidate,expanded)>=0) return;
            if(build_program_path("/bin",4,name,candidate,sizeof(candidate))
               && start_program(terminal,candidate,expanded)>=0) return;
        }
    }
    pc_write(name);
    pc_write(": command not found\n");
}

static bool execute_line(struct terminal_window *terminal, char *line){
    char *command;
    const char *arguments;
    split_line(line,&command,&arguments);
    if(!command[0]) return true;
    if(pc_strcmp(command,"exit")==0) return false;
    if(pc_strcmp(command,"help")==0) show_help();
    else if(pc_strcmp(command,"clear")==0) pc_console_clear();
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
    else if(pc_strcmp(command,"ping")==0) command_ping(arguments);
    else if(pc_strcmp(command,"set")==0) set_variable(arguments);
    else if(pc_strcmp(command,"unset")==0){
        if(pc_unsetenv(arguments)<0) pc_write("unset: variable not found\n");
    } else execute_program(terminal,command,arguments);
    return true;
}

int shell_run(struct terminal_window *terminal, const char *initial_command){
    char line[SHELL_LINE_CAPACITY];
    pc_write("PureC Terminal\n");
    pc_write("Minimal shell: type help. Programs resolve via PATH.\n\n");
    if(initial_command && initial_command[0]){
        pc_copy(line,initial_command,sizeof(line));
        pc_write("purec@os:/$ ");
        pc_write(line);
        pc_write("\n");
        if(!execute_line(terminal,line)) return 0;
    }
    for(;;){
        char prompt[SHELL_LINE_CAPACITY];
        build_prompt(prompt,sizeof(prompt));
        if(!terminal_window_read_line(terminal,prompt,line,sizeof(line)))
            return 0;
        if(!execute_line(terminal,line)) return 0;
    }
}
