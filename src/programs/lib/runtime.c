#include "runtime.h"
#include "../../kernel/syscall.h"

int64_t program_syscall(uint64_t number, uint64_t argument1,
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

uint32_t program_strlen(const char *text){
    uint32_t length=0;
    while(text[length]) length++;
    return length;
}

int program_strcmp(const char *left, const char *right){
    while(*left && *left==*right){ left++; right++; }
    return (uint8_t)*left-(uint8_t)*right;
}

void program_copy(char *destination, const char *source, uint32_t capacity){
    if(!capacity) return;
    uint32_t index=0;
    while(source[index] && index+1<capacity){
        destination[index]=source[index];
        index++;
    }
    destination[index]='\0';
}

void program_write(const char *text){
    (void)program_syscall(SYS_WRITE,(uint64_t)(uintptr_t)text,
                          program_strlen(text),1);
}

void program_write_u64(uint64_t value){
    char buffer[21];
    uint32_t index=sizeof(buffer);
    buffer[--index]='\0';
    do {
        buffer[--index]=(char)('0'+value%10);
        value/=10;
    } while(value);
    program_write(&buffer[index]);
}

void program_write_i64(int64_t value){
    if(value<0){
        program_write("-");
        program_write_u64((uint64_t)(-(value+1))+1);
        return;
    }
    program_write_u64((uint64_t)value);
}

void program_read_line(const char *prompt, char *buffer, uint32_t capacity){
    program_write(prompt);
    if(!capacity) return;
    uint32_t length=0;
    for(;;){
        char character=(char)program_syscall(SYS_GETCHAR,0,0,0);
        if(character=='\r' || character=='\n'){
            program_write("\n");
            break;
        }
        if(character=='\b' || character==127){
            if(length){
                length--;
                program_write("\b");
            }
            continue;
        }
        if(character<' ' || character>'~') continue;
        if(length+1<capacity){
            buffer[length++]=character;
            char output[2]={character,'\0'};
            program_write(output);
        }
    }
    buffer[length]='\0';
}

void program_exit(int32_t status){
    (void)program_syscall(SYS_EXIT,(uint64_t)(int64_t)status,0,0);
    for(;;) __asm__ volatile("pause");
}
