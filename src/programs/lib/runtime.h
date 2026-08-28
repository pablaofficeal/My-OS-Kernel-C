#pragma once

#include <stdbool.h>
#include <stdint.h>

int64_t program_syscall(uint64_t number, uint64_t argument1,
                        uint64_t argument2, uint64_t argument3);
uint32_t program_strlen(const char *text);
int program_strcmp(const char *left, const char *right);
void program_copy(char *destination, const char *source, uint32_t capacity);
void program_write(const char *text);
void program_write_u64(uint64_t value);
void program_write_i64(int64_t value);
void program_read_line(const char *prompt, char *buffer, uint32_t capacity);
void program_exit(int32_t status) __attribute__((noreturn));
