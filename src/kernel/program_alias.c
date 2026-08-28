#include "program_alias.h"
#include "../lib/string.h"

static const char *system_program_aliases[]={
    "/bin/program/ls",
    "/bin/program/cat",
    "/bin/program/touch",
    "/bin/program/mkdir",
    "/bin/program/disks",
    "/bin/program/usbscan",
    "/bin/program/mkfs.fat32",
    "/bin/program/install",
    "/bin/program/setup",
    "/bin/program/update",
    "/bin/program/dmesg",
    "/bin/program/savelog",
    "/bin/program/uname",
    "/bin/program/about",
    "/bin/program/systeminfo",
    "/bin/program/htop",
    "/bin/program/font",
    "/bin/program/snake",
    "/bin/program/mouse",
    "/bin/program/debug",
    "/bin/program/reboot",
    "/bin/program/poweroff",
    "/bin/program/shutdown",
    "/bin/program/battery",
    "/bin/program/halt"
};

bool program_alias_resolve(const char *requested_path, const char **module_path){
    if(!requested_path || !module_path) return false;
    for(uint32_t index=0;
        index<sizeof(system_program_aliases)/sizeof(system_program_aliases[0]);
        index++){
        if(strcmp(requested_path,system_program_aliases[index])==0){
            *module_path="/bin/program/system";
            return true;
        }
    }
    return false;
}
