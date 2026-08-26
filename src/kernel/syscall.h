#pragma once
#include <stdint.h>

#define SYS_WRITE   1
#define SYS_CLEAR   2
#define SYS_SLEEP   3
#define SYS_GETPID  39
#define SYS_EXIT    60
#define SYS_DRAW_RECT 100
#define SYS_DRAW_LINE 101

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags;
    // rsp, ss only for ring3, не используются в ring0
};

void syscall_init(void);
int64_t syscall_handler(struct syscall_regs *regs);
