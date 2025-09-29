// src/lib/memory.h
#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

void* malloc(size_t size);
void free(void* ptr);
void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);

#endif