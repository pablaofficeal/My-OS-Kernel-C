#pragma once
#include <stdint.h>

#define SYS_WRITE   1
#define SYS_CLEAR   2
#define SYS_SLEEP   3
#define SYS_GETPID  39
#define SYS_EXIT    60
#define SYS_DRAW_RECT 100
#define SYS_DRAW_LINE 101
#define SYS_GET_MOUSE 102
// FAT32 ABI: open(path), read(fd, buffer, count), delete(path),
// rename(path, new_8_3_name), move(path, destination_directory).
#define SYS_FILE_OPEN   200
#define SYS_FILE_READ   201
#define SYS_FILE_DELETE 202
#define SYS_FILE_RENAME 203
#define SYS_FILE_MOVE   204
#define SYS_DIR_LIST    205
#define SYS_FILE_CREATE 206
#define SYS_DIR_CREATE  207
#define SYS_DISK_LIST   208

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags;
    // rsp, ss only for ring3, не используются в ring0
};

void syscall_init(void);
int64_t syscall_handler(struct syscall_regs *regs);
