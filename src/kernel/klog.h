#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

enum klog_level {
    KLOG_INFO,   // [ INFO ]
    KLOG_OK,     // [  OK  ] green
    KLOG_WARN,   // [ WARN ] yellow
    KLOG_ERROR,  // [ FAIL ] red
    KLOG_DEBUG   // [ DEBUG] grey - скрывается если !verbose
};

void klog_init(void);
void klog_raw(const char *s);
void klog(enum klog_level lvl, const char *msg);
void klogf(enum klog_level lvl, const char *fmt, ...);
void klog_vf(enum klog_level lvl, const char *fmt, va_list ap);

// printf без уровня (INFO по дефолту)
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

// сервисные
void klog_set_verbose(bool v);
bool klog_is_verbose(void);
void klog_clear(void);
void klog_dump(void); // dmesg в serial

// цвета для GOP/VGA
#define KLOG_BG        0x1E1E2E
#define KLOG_FG        0xCDD6F4
#define KLOG_OK_FG     0xA6E3A1
#define KLOG_INFO_FG   0x89B4FA
#define KLOG_WARN_FG   0xF9E2AF
#define KLOG_ERROR_FG  0xF38BA8
#define KLOG_DEBUG_FG  0x6C7086
#define KLOG_TIME_FG   0x9399B2
