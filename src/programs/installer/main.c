#include "purec.h"

#define CONSOLE_BACKGROUND 0x181824u
#define MAX_INSTALL_DISKS 20

#define INSTALL_IDLE 0
#define INSTALL_RUNNING 1
#define INSTALL_COMPLETE 2
#define INSTALL_FAILED 3

static struct storage_device_info disks[MAX_INSTALL_DISKS];
static int32_t disk_count;
static int32_t selected_disk;

static void clear_console(void) {
    pc_display_begin_update();
    pc_display_clear(CONSOLE_BACKGROUND);
    pc_display_end_update();
}

static void print_separator(void) {
    pc_write("------------------------------------------------------------\n");
}

static uint64_t disk_size_mib(const struct storage_device_info *disk) {
    return disk->sector_count * disk->sector_size / (1024u * 1024u);
}

static void print_disk(const struct storage_device_info *disk, int32_t index) {
    pc_write("  ");
    pc_write_i64(index + 1);
    pc_write(") ");
    pc_write(disk->name);
    pc_write("  ");
    pc_write(disk->model);
    pc_write("  ");
    pc_write_u64(disk_size_mib(disk));
    pc_write(" MiB");
    if (!disk->operational) {
        pc_write("  [unavailable]");
    } else if (!disk->writable) {
        pc_write("  [read only]");
    }
    pc_write("\n");
}

static int32_t parse_disk_number(const char *text) {
    int32_t value = 0;
    int32_t index = 0;

    if (!text || !text[0]) {
        return -1;
    }
    while (text[index]) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10 + text[index] - '0';
        index++;
    }
    if (value < 1 || value > disk_count) {
        return -1;
    }
    return value - 1;
}

static void wait_for_enter(void) {
    char input[2];
    pc_read_line("Press Enter to continue.\n", input, sizeof(input));
}

static bool select_disk(void) {
    char input[32];

    for (;;) {
        clear_console();
        pc_write("Pure OS console installer\n");
        print_separator();
        pc_write("Select the target disk. All data on it will be erased.\n\n");
        for (int32_t i = 0; i < disk_count; i++) {
            print_disk(&disks[i], i);
        }

        pc_read_line("\nDisk number (or q to quit): ", input, sizeof(input));
        if (pc_strcmp(input, "q") == 0 || pc_strcmp(input, "Q") == 0) {
            return false;
        }

        int32_t index = parse_disk_number(input);
        if (index < 0) {
            pc_write("Invalid disk number.\n");
            wait_for_enter();
            continue;
        }
        if (!disks[index].operational || !disks[index].writable) {
            pc_write("This disk cannot be used.\n");
            wait_for_enter();
            continue;
        }

        selected_disk = index;
        return true;
    }
}

static bool confirm_erase(void) {
    char input[32];
    const struct storage_device_info *disk = &disks[selected_disk];

    clear_console();
    pc_write("Confirm installation\n");
    print_separator();
    pc_write("Target: ");
    pc_write(disk->name);
    pc_write("  ");
    pc_write(disk->model);
    pc_write("\nSize:   ");
    pc_write_u64(disk_size_mib(disk));
    pc_write(" MiB\n\n");
    pc_write("WARNING: every partition and file on this disk will be erased.\n");
    pc_read_line("Type ERASE to start, or anything else to cancel: ",
                 input, sizeof(input));
    return pc_strcmp(input, "ERASE") == 0;
}

static void print_progress(uint32_t progress, const char *stage) {
    pc_write("[");
    pc_write_u64(progress);
    pc_write("%] ");
    pc_write(stage);
    pc_write("\n");
}

static void print_new_log_entries(uint32_t *shown_count) {
    struct install_log log;

    if (!pc_install_log(&log)) {
        return;
    }
    if (log.count < *shown_count) {
        *shown_count = 0;
    }
    for (uint32_t i = *shown_count; i < log.count; i++) {
        print_progress(log.entries[i].progress, log.entries[i].stage);
    }
    *shown_count = log.count;
}

static bool write_install_config(void) {
    static const char directory[] = "/purec";
    static const char path[] = "/purec/install.cfg";
    static const char config[] =
        "boot=/bin/init\n"
        "installer=console-ring3\n"
        "filesystem=fat32\n";

    (void)pc_syscall(SYS_DIR_CREATE, (uint64_t)(uintptr_t)directory, 0, 0);
    (void)pc_syscall(SYS_FILE_CREATE, (uint64_t)(uintptr_t)path, 0, 0);
    int64_t result = pc_syscall(SYS_FILE_WRITE,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)config,
        sizeof(config) - 1);
    if (result < 0) {
        pc_write("[96%] Cannot write /purec/install.cfg\n");
        return false;
    }
    pc_write("[96%] Installation configuration written\n");
    return true;
}

static void wait_for_reboot(void) {
    char input[32];

    pc_write("\nInstallation completed successfully.\n");
    pc_write("Remove the installation media, type REBOOT and press Enter.\n");
    for (;;) {
        pc_read_line("> ", input, sizeof(input));
        if (pc_strcmp(input, "REBOOT") == 0 ||
            pc_strcmp(input, "reboot") == 0) {
            (void)pc_syscall(SYS_REBOOT, 0, 0, 0);
        }
    }
}

static bool run_installation(bool start_job) {
    struct install_status status;
    uint32_t shown_log_entries = 0;

    clear_console();
    pc_write("Pure OS installation\n");
    print_separator();

    if (start_job) {
        const struct storage_device_info *disk = &disks[selected_disk];
        int32_t result = pc_install_start(disk->name, disk->serial);
        if (result < 0) {
            pc_write("Could not start installation. Error: ");
            pc_write_i64(result);
            pc_write("\n");
            return false;
        }
        pc_write("Installing to ");
        pc_write(disk->name);
        pc_write(". Do not power off the computer.\n\n");
    } else {
        pc_write("An installation is already active. Resuming progress.\n\n");
    }

    for (;;) {
        if (!pc_install_status(&status)) {
            pc_write("Cannot read installation status.\n");
            return false;
        }
        print_new_log_entries(&shown_log_entries);
        if (status.state == INSTALL_FAILED) {
            pc_write("\nInstallation failed. Error: ");
            pc_write_i64(status.result);
            pc_write("\n");
            return false;
        }
        if (status.state == INSTALL_COMPLETE) {
            if (!write_install_config()) {
                return false;
            }
            pc_write("[100%] Installation complete\n");
            wait_for_reboot();
        }
        pc_sleep(20);
    }
}

static int installer_main(void) {
    struct install_status status;

    disk_count = pc_list_disks(disks, MAX_INSTALL_DISKS);
    if (disk_count <= 0) {
        clear_console();
        pc_write("Pure OS console installer\n");
        print_separator();
        pc_write("No disks were detected.\n");
        return 1;
    }

    if (pc_install_status(&status) &&
        status.state != INSTALL_IDLE && status.state != INSTALL_FAILED) {
        (void)run_installation(false);
    }

    for (;;) {
        if (!select_disk()) {
            clear_console();
            pc_write("Installation cancelled.\n");
            return 0;
        }
        if (!confirm_erase()) {
            continue;
        }
        if (run_installation(true)) {
            return 0;
        }
        pc_write("\n");
        wait_for_enter();
    }
}

void _start(void) {
    pc_exit(installer_main());
}
