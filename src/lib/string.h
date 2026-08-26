#pragma once
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
