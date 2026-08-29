#pragma once
#include <stdint.h>

#define SYS_WRITE   1
#define SYS_CLEAR   2
#define SYS_SLEEP   3
#define SYS_GETPID  39
#define SYS_EXEC    59
#define SYS_EXIT    60
#define SYS_WAIT    61
#define SYS_DRAW_RECT 100
#define SYS_DRAW_LINE 101
#define SYS_GET_MOUSE 102
#define SYS_FB_INFO 103
#define SYS_DRAW_TEXT 104
#define SYS_DRAW_TEXT_SIZED 105
#define SYS_SCROLL_RECT_UP 106
#define SYS_SET_FONT_FACE 107
#define SYS_GET_FONT_FACE 108
#define SYS_FB_BEGIN_UPDATE 109
#define SYS_FB_END_UPDATE 110
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
#define SYS_FILE_APPEND 223
#define SYS_GETCHAR 224
#define SYS_INSTALL_START 225
#define SYS_INSTALL_STATUS 226
#define SYS_TRY_GETCHAR 227
#define SYS_INSTALL_LOG 228
#define SYS_AUDIO_GET_STATUS 230
#define SYS_AUDIO_GET_VOLUME 231
#define SYS_AUDIO_SET_VOLUME 232
#define SYS_AUDIO_IS_MUTED 233
#define SYS_AUDIO_SET_MUTED 234
#define SYS_AUDIO_ADJUST_VOLUME 235
#define SYS_AUDIO_PLAY_TEST_SOUND 236
#define SYS_AUDIO_UPDATE 237
#define SYS_AUDIO_SELECT_OUTPUT_DEVICE 238
#define SYS_GET_COMMAND_LINE 239
#define SYS_ENV_GET 240
#define SYS_ENV_SET 241
#define SYS_ENV_UNSET 242
#define SYS_ENV_LIST 243
#define SYS_GET_PROCESS_NAME 244
#define SYS_MOUSE_DEBUG_GET 245
#define SYS_MOUSE_DEBUG_SET 246
#define SYS_CONSOLE_CONFIGURE 247
#define SYS_CONSOLE_CLEAR 248
#define SYS_CONSOLE_DISABLE 249
#define SYS_DESKTOP_REDRAW 250

#define PROCESS_ENVIRONMENT_LIMIT 16
#define PROCESS_ENVIRONMENT_NAME_LIMIT 32
#define PROCESS_ENVIRONMENT_VALUE_LIMIT 128

struct process_environment_variable {
    uint8_t used;
    char name[PROCESS_ENVIRONMENT_NAME_LIMIT];
    char value[PROCESS_ENVIRONMENT_VALUE_LIMIT];
};

#define SYS_OPEN  SYS_FILE_OPEN
#define SYS_READ  SYS_FILE_READ
#define SYS_CLOSE SYS_FILE_CLOSE
#define SYS_UNLINK SYS_FILE_DELETE
#define SYS_RENAME SYS_FILE_RENAME
#define SYS_MKDIR SYS_DIR_CREATE

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

#define INSTALL_STAGE_CAPACITY 48
#define INSTALL_LOG_CAPACITY 16

struct install_status {
    uint32_t state;
    uint32_t progress;
    int32_t result;
    char stage[INSTALL_STAGE_CAPACITY];
};

struct install_log_entry {
    uint32_t progress;
    char stage[INSTALL_STAGE_CAPACITY];
};

struct install_log {
    uint32_t count;
    struct install_log_entry entries[INSTALL_LOG_CAPACITY];
};

struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size_bytes;
    uint8_t bpp;
    uint8_t available;
    char protocol_name[16];
};

struct framebuffer_text_request {
    uint32_t x;
    uint32_t y;
    const char *text;
    uint32_t fg;
    uint32_t bg;
    uint32_t size;
};

struct framebuffer_scroll_request {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t amount;
    uint32_t fill_color;
};

struct framebuffer_console_request {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t foreground;
    uint32_t background;
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

#define AUDIO_BACKEND_NONE 0
#define AUDIO_BACKEND_PC_SPEAKER 1
#define AUDIO_BACKEND_HDA 2

struct audio_status {
    uint32_t volume;
    uint32_t muted;
    uint32_t backend;
    uint32_t available_backends;
    uint32_t pcm_ready;
    uint32_t test_active;
    uint32_t output_device_count;
    uint32_t selected_output_device;
    uint32_t hda_codec;
    uint32_t hda_dac_node;
    uint32_t hda_pin_node;
};

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags;
    // rsp, ss only for ring3, не используются в ring0
};

void syscall_init(void);
int64_t syscall_handler(struct syscall_regs *regs);
void install_report_ui_crash(int32_t status);
