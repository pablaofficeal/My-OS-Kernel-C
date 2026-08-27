#pragma once
#include <stdint.h>

#define SYS_WRITE   1
#define SYS_CLEAR   2
#define SYS_SLEEP   3
#define SYS_GETPID  39
#define SYS_EXIT    60
#define SYS_DRAW_RECT 100
#define SYS_DRAW_LINE 101
#define SYS_GET_MOUSE 102
// FAT32 ABI: open/read/delete/rename/move plus directory, creation and
// whole-file write operations used by the terminal tools.
#define SYS_FILE_OPEN   200
#define SYS_FILE_READ   201
#define SYS_FILE_DELETE 202
#define SYS_FILE_RENAME 203
#define SYS_FILE_MOVE   204
#define SYS_DIR_LIST    205
#define SYS_FILE_CREATE 206
#define SYS_DIR_CREATE  207
#define SYS_DISK_LIST   208
#define SYS_STORAGE_CONTROLLERS 209
#define SYS_FAT32_FORMAT 210
#define SYS_FILE_WRITE   211
#define SYS_USB_RESCAN   212
#define SYS_FAT32_FORMAT_FORCE 213
#define SYS_FAT32_FORMAT_UEFI 214
#define SYS_CPU_INFO 215
#define SYS_MEMORY_INFO 216
#define SYS_DISK_STATS 217
#define SYS_REBOOT 218
#define SYS_SHUTDOWN 219
#define SYS_BATTERY_INFO 220
#define SYS_SCHED_YIELD 221
#define SYS_FILE_CLOSE 222

#define CPU_MONITOR_NAME_CAPACITY 49

struct cpu_monitor_info {
    char name[CPU_MONITOR_NAME_CAPACITY];
    uint32_t logical_processors;
    uint32_t usage_percent;
    uint64_t frequency_hz;
    uint64_t uptime_ms;
};

struct memory_monitor_info {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t available_bytes;
    uint64_t framebuffer_bytes;
};

struct disk_monitor_info {
    uint32_t device_count;
    uint32_t operational_count;
    uint64_t total_bytes;
};

struct usb_scan_status {
    uint32_t xhci_controllers;
    uint32_t xhci_connected_ports;
    uint32_t xhci_addressed_devices;
    uint32_t xhci_disks;
    uint32_t xhci_hid_mice;
    uint32_t xhci_hid_interfaces;
    uint32_t xhci_hubs;
    uint32_t xhci_mouse_transfer_errors;
    uint32_t xhci_failures;
    uint32_t xhci_stage;
    uint32_t xhci_error;
    uint32_t xhci_last_port;
    uint32_t xhci_portsc;
    uint32_t xhci_completion_code;
    uint32_t xhci_max_ports;
    uint32_t xhci_usb_status;
    uint32_t xhci_scratchpad_count;
    uint32_t ehci_connected_ports;
    uint32_t ehci_high_speed_ports;
    uint32_t ehci_disks;
    uint32_t ehci_failures;
    uint32_t ehci_stage;
};

struct battery_info {
    uint32_t present;
    uint32_t percent; // 0-100
    uint32_t charging; // 0 discharging, 1 charging, 2 charged
    uint32_t remaining_minutes;
    uint32_t voltage_mv;
    uint32_t current_ma;
    char name[32];
    char status_text[32]; // "Charging", "Discharging", "AC"
};

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags;
    // rsp, ss only for ring3, не используются в ring0
};

void syscall_init(void);
int64_t syscall_handler(struct syscall_regs *regs);
