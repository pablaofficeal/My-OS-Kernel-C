// src/lib/memory.c
#include "memory.h"

// Простая реализация кучи для ОС
#define HEAP_SIZE 0x10000  // 64KB
static char heap[HEAP_SIZE];
static size_t heap_ptr = 0;

void* malloc(size_t size) {
    if (heap_ptr + size > HEAP_SIZE) {
        return 0;
    }
    
    void* ptr = &heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

void free(void* ptr) {
    // В этой простой реализации мы не освобождаем память
    // Для игры 2048 этого достаточно
    (void)ptr;
}

void* memset(void* dest, int val, size_t len) {
    unsigned char* ptr = dest;
    while (len-- > 0) {
        *ptr++ = val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    char* d = dest;
    const char* s = src;
    while (len--) {
        *d++ = *s++;
    }
    return dest;
}