#include "string.h"

size_t strlen(const char* s){ size_t n=0; while(s[n]) n++; return n; }
int strcmp(const char* a, const char* b){ while(*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
void* memcpy(void* dst, const void* src, size_t n){ uint8_t *d=dst; const uint8_t *s=src; for(size_t i=0;i<n;i++) d[i]=s[i]; return dst; }
void* memset(void* s, int c, size_t n){ uint8_t *p=s; for(size_t i=0;i<n;i++) p[i]=c; return s; }
int memcmp(const void* a, const void* b, size_t n){ const uint8_t *p=a,*q=b; for(size_t i=0;i<n;i++) if(p[i]!=q[i]) return p[i]-q[i]; return 0; }
