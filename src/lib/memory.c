// src/lib/memory.c
#include "memory.h"

#define HEAP_SIZE 65536
static char heap[HEAP_SIZE];
static size_t heap_ptr = 0;

void *malloc(size_t size) {
    if (heap_ptr + size > HEAP_SIZE) {
        return 0; // Out of memory
    }
    void *ptr = &heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

void free(void* ptr) {
    // Простая заглушка, не реализуем полную сборку мусора
    (void)ptr;
}

void* memset(void* dest, int val, size_t len) {
    unsigned char *ptr = (unsigned char*)dest;
    for (size_t i = 0; i < len; i++) {
        ptr[i] = (unsigned char)val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dest;
}