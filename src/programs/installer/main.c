#include "purec.h"
#include "fs_types.h"

#define CONSOLE_BACKGROUND 0x181824u
#define MAX_INSTALL_DISKS 20

static struct pc_disk_info disks[MAX_INSTALL_DISKS];
static int disk_count;
static int selected_disk;

static void clear_console(void) {
    pc_display_begin_update();
    pc_display_clear(CONSOLE_BACKGROUND);
    pc_display_end_update();
}

static void print_separator(void) {
    pc_puts("------------------------------------------------------------\n");
}

static void print_disk(const struct pc_disk_info *disk, int index) {
    pc_printf("  %d) %s  %s  %llu MiB", index + 1, disk->name, disk->model,
              (unsigned long long)(disk->size_bytes / (1024u * 1024u)));
    if (!disk->operational) {
        pc_puts("  [unavailable]");
    } else if (!disk->writable) {
        pc_puts("  [read only]");
    }
    pc_puts("\n");
}

static int parse_disk_number(const char *text) {
    int value = 0;
    int index = 0;

    if (!text || !text[0]) {
        return -1;
    }

    while (text[index]) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10 + (text[index] - '0');
        index++;
    }

    if (value < 1 || value > disk_count) {
        return -1;
    }
    return value - 1;
}

static bool select_disk(void) {
    char input[32];

    for (;;) {
        clear_console();
        pc_puts("Pure OS console installer\n");
        print_separator();
        pc_puts("Select the target disk. All data on it will be erased.\n\n");

        for (int i = 0; i < disk_count; i++) {
            print_disk(&disks[i], i);
        }

        pc_puts("\nDisk number (or q to quit): ");
        if (pc_read_line(input, sizeof(input)) < 0) {
            return false;
        }
        if (pc_streq(input, "q") || pc_streq(input, "Q")) {
            return false;
        }

        int index = parse_disk_number(input);
        if (index < 0) {
            pc_puts("Invalid disk number. Press Enter to try again.\n");
            pc_read_line(input, sizeof(input));
            continue;
        }
        if (!disks[index].operational || !disks[index].writable) {
            pc_puts("This disk cannot be used. Press Enter to try again.\n");
            pc_read_line(input, sizeof(input));
            continue;
        }

        selected_disk = index;
        return true;
    }
}

static bool confirm_erase(void) {
    char input[32];
    const struct pc_disk_info *disk = &disks[selected_disk];

    clear_console();
    pc_puts("Confirm installation\n");
    print_separator();
    pc_printf("Target: %s  %s\n", disk->name, disk->model);
    pc_printf("Size:   %llu MiB\n",
              (unsigned long long)(disk->size_bytes / (1024u * 1024u)));
    pc_puts("\nWARNING: every partition and file on this disk will be erased.\n");
    pc_puts("Type ERASE to start, or anything else to cancel: ");

    if (pc_read_line(input, sizeof(input)) < 0) {
        return false;
    }
    return pc_streq(input, "ERASE");
}

static void print_new_log_entries(uint32_t *next_sequence) {
    struct pc_install_log log;

    if (pc_install_log(*next_sequence, &log) < 0) {
        return;
    }

    for (uint32_t i = 0; i < log.count; i++) {
        const struct pc_install_log_entry *entry = &log.entries[i];
        pc_printf("[%u%%] %s\n", entry->progress, entry->message);
        *next_sequence = entry->sequence + 1;
    }
}

static bool write_install_config(void) {
    static const char config[] =
        "boot=/bin/init\n"
        "installer=console-ring3\n"
        "filesystem=fat32\n";
    int fd = pc_open("/system/install.cfg", FS_OPEN_WRITE | FS_OPEN_CREATE | FS_OPEN_TRUNCATE);

    if (fd < 0) {
        pc_puts("[96%] Cannot create /system/install.cfg\n");
        return false;
    }

    long written = pc_write_fd(fd, config, sizeof(config) - 1);
    pc_close(fd);
    if (written != (long)(sizeof(config) - 1)) {
        pc_puts("[96%] Cannot write /system/install.cfg\n");
        return false;
    }

    pc_puts("[96%] Installation configuration written\n");
    return true;
}

static void wait_for_reboot(void) {
    char input[32];

    pc_puts("\nInstallation completed successfully.\n");
    pc_puts("Remove the installation media, type REBOOT and press Enter.\n");
    for (;;) {
        pc_puts("> ");
        if (pc_read_line(input, sizeof(input)) >= 0 &&
            (pc_streq(input, "REBOOT") || pc_streq(input, "reboot"))) {
            pc_reboot();
        }
    }
}

static bool run_installation(bool start_job) {
    struct pc_install_status status;
    uint32_t next_sequence = 0;

    clear_console();
    pc_puts("Pure OS installation\n");
    print_separator();

    if (start_job) {
        const struct pc_disk_info *disk = &disks[selected_disk];
        int result = pc_install_start(disk->name, disk->serial);
        if (result < 0) {
            pc_printf("Could not start installation (error %d).\n", result);
            return false;
        }
        pc_printf("Installing to %s. Do not power off the computer.\n\n", disk->name);
    } else {
        pc_puts("An installation is already active. Resuming its log.\n\n");
    }

    for (;;) {
        if (pc_install_status(&status) < 0) {
            pc_puts("Cannot read installation status.\n");
            return false;
        }

        print_new_log_entries(&next_sequence);

        if (status.state == PC_INSTALL_FAILED) {
            pc_printf("\nInstallation failed at %u%%: %s (error %d).\n",
                      status.progress, status.stage, status.error);
            return false;
        }

        if (status.state == PC_INSTALL_COMPLETE) {
            if (!write_install_config()) {
                return false;
            }
            pc_puts("[100%] Installation complete\n");
            wait_for_reboot();
            return true;
        }

        pc_yield();
    }
}

static int installer_main(void) {
    struct pc_install_status status;
    char input[16];

    disk_count = pc_disk_list(disks, MAX_INSTALL_DISKS);
    if (disk_count <= 0) {
        clear_console();
        pc_puts("Pure OS console installer\n");
        print_separator();
        pc_puts("No disks were detected.\n");
        return 1;
    }

    if (pc_install_status(&status) >= 0 &&
        status.state != PC_INSTALL_IDLE && status.state != PC_INSTALL_FAILED) {
        if (run_installation(false)) {
            return 0;
        }
    }

    for (;;) {
        if (!select_disk()) {
            clear_console();
            pc_puts("Installation cancelled.\n");
            return 0;
        }
        if (!confirm_erase()) {
            continue;
        }
        if (run_installation(true)) {
            return 0;
        }

        pc_puts("\nPress Enter to return to disk selection.\n");
        pc_read_line(input, sizeof(input));
    }
}

void _start(void) {
    pc_exit(installer_main());
}
