// src/lib/string.h
#ifndef STRING_H
#define STRING_H

#include "stddef.h"

// Use custom names to avoid conflicts with standard library
int my_strlen(const char *str);
int my_strcmp(const char *s1, const char *s2);
int my_strncmp(const char *s1, const char *s2, int n);
void my_strcpy(char *dest, const char *src);
char* my_strchr(const char *str, int c);
char* my_strstr(const char *haystack, const char *needle);

// Memory functions
void *my_memset(void *ptr, int value, unsigned int size);
void *my_memcpy(void *dest, const void *src, unsigned int size);
int my_memcmp(const void *s1, const void *s2, unsigned int n);

// Compatibility macros for code that uses standard names
#define strlen my_strlen
#define strcmp my_strcmp
#define strncmp my_strncmp
#define strcpy my_strcpy
#define strchr my_strchr
#define strstr my_strstr
#define memset my_memset
#define memcpy my_memcpy
#define memcmp my_memcmp

#endif