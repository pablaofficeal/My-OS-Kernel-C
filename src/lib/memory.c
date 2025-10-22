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

void free(void *ptr) {
    // Simple implementation - no freeing for now
    // This is sufficient for our 2048 game
}

// memset and memcpy are now in string.c