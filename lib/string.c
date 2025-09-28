// lib/string.c
#include "string.h"

int strlen(const char *str) {
    int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

// Memory functions
void *memset(void *ptr, int value, unsigned int size) {
    unsigned char *p = (unsigned char*)ptr;
    for (unsigned int i = 0; i < size; i++) {
        p[i] = (unsigned char)value;
    }
    return ptr;
}

void *memcpy(void *dest, const void *src, unsigned int size) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    for (unsigned int i = 0; i < size; i++) {
        d[i] = s[i];
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, unsigned int n) {
    const unsigned char *p1 = (const unsigned char*)s1;
    const unsigned char *p2 = (const unsigned char*)s2;
    for (unsigned int i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}