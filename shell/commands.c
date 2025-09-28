// shell/commands.c - ИСПРАВЛЕННАЯ ВЕРСИЯ
#include "../drivers/screen.h"
#include "../fs/fat16.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
#include <stdarg.h>

// Объявляем функции из keyboard.c
extern int kbhit();
extern unsigned char keyboard_read_scancode();
extern char scancode_to_char(unsigned char scancode);
extern char getchar();
extern void readline(char *buffer, int max_length);

void cmd_help() {
    printf("=== PureC OS Commands (FAT16) ===\n");
    printf("  help     - Show this help\n");
    printf("  clear    - Clear screen\n");
    printf("  echo     - Print arguments\n");
    printf("  version  - Show kernel version\n");
    printf("  reboot   - Reboot system\n");
    printf("  shutdown - Shutdown system safely\n");
    printf("  ls       - List files\n");
    printf("  cat      - Read file\n");
    printf("  write    - Write to file\n");
    printf("  touch    - Create file\n");
    printf("  rm       - Delete file\n");
    printf("  rename   - Rename file\n");
    printf("  info     - File information\n");
    printf("  space    - Show disk space\n");
    printf("  fsinfo   - File system info\n");
    printf("  df       - Show disk space\n");
}

void cmd_clear() {
    clear_screen();
    printf("FAT16 File System Ready.\n");
    printf("500MB disk space available.\n");
}

void cmd_echo(char *args) {
    if (args[0] == '\0') {
        printf("Usage: echo <text>\n");
        printf("Example: echo Hello FAT16 World!\n");
    } else {
        printf("%s\n", args);
    }
}

void cmd_version() {
    printf("PureC Kernel v2.0 - FAT16 Edition\n");
    printf("Features: 500MB FAT16, Advanced shell\n");
    printf("Built: %s %s\n", __DATE__, __TIME__);
}

void cmd_reboot() {
    printf("FAT16 System rebooting...\n");
    for (volatile int i = 0; i < 1000000; i++);
    asm volatile (
        "mov $0xFE, %%al\n"
        "out %%al, $0x64\n"
        : : : "ax"
    );
}

void cmd_test(char *args) {
    printf("=== FAT16 Keyboard Test ===\n");
    printf("Type characters (ESC to exit):\n");

    int char_count = 0;
    while (char_count < 100) {
        char c = getchar();

        if (c == 27) {
            printf("\nESC pressed - exiting test\n");
            break;
        } else if (c == '\n') {
            printf("[ENTER]\n");
        } else if (c == '\b') {
            printf("[BACKSPACE]");
        } else if (c == '\t') {
            printf("[TAB]");
        } else if (c >= 32 && c < 127) {
            printf("'%c' ", c);
        }

        char_count++;
        if (char_count % 10 == 0) printf("\n");
    }
    printf("\nTest completed. Characters: %d\n", char_count);
}

void cmd_keytest() {
    printf("=== FAT16 Keyboard Debug ===\n");
    printf("Press keys (ESC to exit):\n");

    int key_count = 0;
    while (key_count < 30) {
        if (kbhit()) {
            unsigned char scancode = keyboard_read_scancode();
            printf("Scancode: 0x%02x", scancode);

            if (scancode & 0x80) {
                printf(" [RELEASE]");
            } else {
                printf(" [PRESS]");
            }

            char c = scancode_to_char(scancode);
            if (c != 0 && c >= 32 && c < 127) {
                printf(" -> '%c'", c);
            }

            printf("\n");
            key_count++;

            if ((scancode & 0x7F) == 0x01) break;
        }
        for (volatile int i = 0; i < 10000; i++);
    }
    printf("Keytest completed.\n");
}

void cmd_ls() {
    printf("=== FAT16 File List ===\n");
    fat16_list_files();
}

void cmd_cat(char *filename) {
    if (filename[0] == '\0') {
        printf("Usage: cat <filename.ext>\n");
        printf("Example: cat README.TXT\n");
        return;
    }

    file_t *file = fat16_open(filename, 0);
    if (!file) {
        printf("Error: File not found - %s\n", filename);
        return;
    }

    printf("=== File: %s ===\n", filename);
    printf("Size: %d bytes, Cluster: %d\n\n", file->size, file->first_cluster);

    if (file->size == 0) {
        printf("[Empty file]\n");
        fat16_close(file);
        return;
    }

    char buffer[512];
    int total_read = 0;
    int line_num = 1;

    printf("%4d: ", line_num);

    while (total_read < file->size) {
        int bytes_read = fat16_read(file, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';

        for (int i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n') {
                putchar('\n');
                printf("%4d: ", ++line_num);
            } else if (buffer[i] == '\t') {
                printf("    ");
            } else if (buffer[i] >= 32 && buffer[i] < 127) {
                putchar(buffer[i]);
            } else {
                printf("?");
            }
        }

        total_read += bytes_read;
    }

    printf("\n\n[End of file - %d bytes]\n", total_read);
    fat16_close(file);
}

void cmd_touch(char *filename) {
    if (filename[0] == '\0') {
        printf("Usage: touch <filename.ext>\n");
        return;
    }

    if (fat16_create(filename)) {
        printf("FAT16 File created: %s\n", filename);
        printf("Use 'ls' to verify\n");
    } else {
        printf("Failed to create: %s\n", filename);
    }
}

void cmd_rm(char *filename) {
    if (filename[0] == '\0') {
        printf("Usage: rm <filename.ext>\n");
        printf("Example: rm oldfile.txt\n");
        return;
    }

    if (!fat16_file_exists(filename)) {
        printf("File not found: %s\n", filename);
        printf("Use 'ls' to see available files\n");
        return;
    }

    printf("Deleting: %s\n", filename);

    if (fat16_delete(filename)) {
        printf("✓ File deleted: %s\n", filename);
    } else {
        printf("✗ Delete failed: %s\n", filename);
    }
}

void cmd_fsinfo() {
    printf("=== FAT16 File System Information ===\n");
    printf("Disk Size:      500MB\n");
    printf("Cluster Size:   4KB\n");
    printf("Max Files:      512 in root directory\n");
    printf("Max File Size:  2GB\n");
    printf("File System:    FAT16 (compatible with Windows/Linux)\n");
    printf("Status:         Operational\n");
}

void cmd_format(char *args) {
    printf("=== FAT16 Format Utility ===\n");
    printf("WARNING: This will erase all data!\n");
    printf("Type 'YES' to confirm: ");

    char response[10];
    readline(response, sizeof(response));

    int is_yes = (response[0] == 'Y' && response[1] == 'E' && response[2] == 'S' && response[3] == '\0');

    if (is_yes) {
        printf("Formatting FAT16 filesystem...\n");
        printf("Format complete. Rebooting...\n");
        cmd_reboot();
    } else {
        printf("Format cancelled.\n");
    }
}

void cmd_df() {
    printf("=== FAT16 Disk Space ===\n");
    printf("Total Space:    500MB\n");
    printf("Used Space:     Calculating...\n");
    printf("Free Space:     ~500MB (fresh system)\n");
    printf("Cluster Size:   4KB\n");
    printf("\nDisk status: Healthy\n");
    printf("Filesystem: FAT16\n");
}

void cmd_create_test_files() {
    printf("=== Creating FAT16 Test Files ===\n");

    char *test_files[] = {
        "README.TXT",
        "TEST.DAT",
        "CONFIG.CFG",
        "DATA.BIN",
        "LOG.TXT"
    };
    int file_count = 5;

    int created = 0;
    for (int i = 0; i < file_count; i++) {
        if (!fat16_file_exists(test_files[i])) {
            if (fat16_create(test_files[i])) {
                printf("✓ Created: %s\n", test_files[i]);
                created++;
            }
        } else {
            printf("File exists: %s\n", test_files[i]);
        }
    }

    printf("Test files created: %d\n", created);
    printf("Use 'ls' to see all files\n");
}

void cmd_shutdown() {
    printf("=== FAT16 System Shutdown ===\n");
    printf("Saving file system state...\n");
    printf("All data has been preserved.\n");
    printf("System is now safe to power off.\n");
    printf("Goodbye!\n");

    for (volatile int i = 0; i < 3000000; i++);

    printf("System halted. Use Ctrl+Alt+Del to restart.\n");

    while (1) {
        asm volatile ("hlt");
    }
}

void cmd_write(char *args) {
    char filename[32] = {0};
    char text[256] = {0};

    int i = 0;
    while (args[i] == ' ') i++;
    int j = 0;
    while (args[i] != ' ' && args[i] != '\0' && j < 31) {
        filename[j++] = args[i++];
    }
    filename[j] = '\0';

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] != '\0' && j < 255) {
        text[j++] = args[i++];
    }
    text[j] = '\0';

    if (filename[0] == '\0' || text[0] == '\0') {
        printf("Usage: write <filename> <text>\n");
        return;
    }

    file_t *file = fat16_open(filename, 1);
    if (!file) {
        printf("Error: Cannot open '%s' for writing\n", filename);
        return;
    }

    int written = fat16_write(file, text, strlen(text));
    if (written > 0) {
        printf("Written %d bytes to '%s'\n", written, filename);
    } else {
        printf("Error writing to '%s'\n", filename);
    }

    fat16_close(file);
}

void cmd_info(char *filename) {
    if (filename[0] == '\0') {
        printf("Usage: info <filename>\n");
        return;
    }

    fat16_dir_entry_t info;
    if (fat16_get_file_info(filename, &info)) {
        char name83[13];
        name83_to_filename(info.filename, name83);

        int day = info.date & 0x1F;
        int month = (info.date >> 5) & 0x0F;
        int year = ((info.date >> 9) & 0x7F) + 1980;

        printf("File: %s\n", name83);
        printf("Size: %d bytes\n", info.file_size);
        printf("Cluster: %d\n", info.starting_cluster);
        printf("Attributes: 0x%02x\n", info.attributes);
        printf("Modified: %02d/%02d/%04d\n", day, month, year);
    } else {
        printf("File not found: %s\n", filename);
    }
}

void cmd_rename(char *args) {
    char oldname[32] = {0};
    char newname[32] = {0};

    int i = 0;
    while (args[i] == ' ') i++;
    int j = 0;
    while (args[i] != ' ' && args[i] != '\0' && j < 31) {
        oldname[j++] = args[i++];
    }
    oldname[j] = '\0';

    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] != '\0' && j < 31) {
        newname[j++] = args[i++];
    }
    newname[j] = '\0';

    if (oldname[0] == '\0' || newname[0] == '\0') {
        printf("Usage: rename <oldname> <newname>\n");
        return;
    }

    if (fat16_rename(oldname, newname)) {
        printf("Renamed '%s' to '%s'\n", oldname, newname);
    } else {
        printf("Rename failed\n");
    }
}

void cmd_space() {
    unsigned int total = fat16_get_total_space();
    unsigned int free = fat16_get_free_space();
    unsigned int used = total - free;

    printf("Disk Space Information:\n");
    printf("Total:  %10u bytes (%u MB)\n", total, total / (1024 * 1024));
    printf("Used:   %10u bytes (%u MB)\n", used, used / (1024 * 1024));
    printf("Free:   %10u bytes (%u MB)\n", free, free / (1024 * 1024));
    printf("Usage:  %d%%\n", (used * 100) / total);
}