// src/lib/string.h
#ifndef STRING_H
#define STRING_H

#include "stddef.h"

int strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, int n);
void strcpy(char *dest, const char *src);
char* strchr(const char *str, int c);
char* strstr(const char *haystack, const char *needle);

// Memory functions
void *memset(void *ptr, int value, unsigned int size);
void *memcpy(void *dest, const void *src, unsigned int size);
int memcmp(const void *s1, const void *s2, unsigned int n);

#endif