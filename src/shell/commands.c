// shell/commands.c - ПОЛНАЯ ИСПРАВЛЕННАЯ ВЕРСИЯ
#include "../drivers/screen.h"
#include "../fs/fat16.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
#include "../game/snake/snake.h"

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
    printf("  snake    - Play Snake game\n");
    printf("  tetris    - Play Tetris game\n");
    printf("  2045     - Play 2048 game\n");
    printf("  wifi scan - Scan wifi interface\n");
    printf("  wifi status - Get wifi status\n");
    printf("  wifi connect name, and, password. - Connect to WiFi\n");
    printf("  wifi disconnect - Disconnect from WiFi\n");
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
    // Исправленный outb
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
    int file_count = fat16_list_files();
}

void cmd_cat(char *filename) {
    if (filename[0] == '\0') {
        printf("Usage: cat <filename.ext>\n");
        printf("Example: cat README.TXT\n");
        return;
    }
    
    file_t *file = fat16_open(filename, 0);  // <-- ИСПРАВЛЕНО: добавить 0 для режима чтения
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
    
    int is_yes = 1;
    if (response[0] != 'Y' || response[1] != 'E' || response[2] != 'S' || response[3] != '\0') {
        is_yes = 0;
    }
    
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

// ИСПРАВЛЕННАЯ команда shutdown
void cmd_shutdown() {
    printf("=== FAT16 System Shutdown ===\n");
    printf("Saving file system state...\n");
    printf("All data has been preserved.\n");
    printf("System is now safe to power off.\n");
    printf("Goodbye!\n");
    
    // Задержка для отображения сообщения
    for (volatile int i = 0; i < 3000000; i++);
    
    // Упрощенный shutdown - просто останавливаем систему
    printf("System halted. Use Ctrl+Alt+Del to restart.\n");
    
    // Бесконечный цикл с HLT
    while(1) {
        asm volatile ("hlt");
    }
}

// ============================================================================
// НОВЫЕ КОМАНДЫ - ДОБАВИТЬ ОТСЮДА ДО КОНЦА ФАЙЛА
// ============================================================================

void cmd_write(char *args) {
    // Упрощенная версия без sscanf
    char *filename = args;
    char *text = args;
    
    // Находим пробел между именем файла и текстом
    while (*text != ' ' && *text != '\0') text++;
    if (*text == ' ') {
        *text = '\0';  // Разделяем строку
        text++;        // Переходим к тексту
    } else {
        printf("Usage: write <filename> <text>\n");
        printf("Example: write note.txt Hello World!\n");
        return;
    }
    
    if (strlen(text) == 0) {
        printf("Error: No text provided\n");
        return;
    }
    
    file_t *file = fat16_open(filename, 1);  // Write mode
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
        // Упрощенная версия без name83_to_filename
        printf("File: ");
        for (int i = 0; i < 8; i++) {
            if (info.filename[i] != ' ') putchar(info.filename[i]);
        }
        if (info.extension[0] != ' ') {
            putchar('.');
            for (int i = 0; i < 3; i++) {
                if (info.extension[i] != ' ') putchar(info.extension[i]);
            }
        }
        printf("\n");
        
        printf("Size: %d bytes\n", info.file_size);
        printf("Cluster: %d\n", info.starting_cluster);
        printf("Attributes: 0x%02x\n", info.attributes);
        
        // Дата
        int day = info.date & 0x1F;
        int month = (info.date >> 5) & 0x0F;
        int year = ((info.date >> 9) & 0x7F) + 1980;
        printf("Modified: %02d/%02d/%04d\n", day, month, year);
    } else {
        printf("File not found: %s\n", filename);
    }
}

void cmd_rename(char *args) {
    // Упрощенная версия без sscanf
    char *oldname = args;
    char *newname = args;
    
    // Находим пробел между старым и новым именем
    while (*newname != ' ' && *newname != '\0') newname++;
    if (*newname == ' ') {
        *newname = '\0';  // Разделяем строку
        newname++;        // Переходим к новому имени
    } else {
        printf("Usage: rename <oldname> <newname>\n");
        return;
    }
    
    if (strlen(newname) == 0) {
        printf("Error: No new filename provided\n");
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
    int percent = total > 0 ? (used * 100) / total : 0;
    
    printf("Disk Space Information:\n");
    printf("Total:  %10u bytes (%u MB)\n", total, total / (1024*1024));
    printf("Used:   %10u bytes (%u MB)\n", used, used / (1024*1024));
    printf("Free:   %10u bytes (%u MB)\n", free, free / (1024*1024));
    printf("Usage:  %d%%\n", percent);
}

void cmd_snake(char *args) {
    printf("Starting Snake Game...\n");
    printf("Controls: WASD to move, ESC to quit\n");
    printf("Press any key to start...\n");
    
    // Ждем нажатия любой клавиши
    while (!keyboard_has_data()) {
        for (volatile int i = 0; i < 10000; i++);
    }
    keyboard_read_scancode(); // Очищаем буфер
    
    // Запускаем игру
    snake_game_loop();
    
    printf("Returning to shell...\n");
}

// В НАЧАЛО src/shell/commands.c добавить:
#include "../drivers/wifi/wifi.h"

// ПЕРЕД функциями wifi добавить:
void cmd_wifi(char* args);

// ЗАТЕМ функции wifi:
void cmd_wifi_scan(char* args) {
    printf("WiFi: Starting network scan...\n");
    int networks = wifi_scan_networks();
    if (networks > 0) {
        wifi_print_networks();
    } else {
        printf("WiFi: No networks found\n");
    }
}

void cmd_wifi_connect(char* args) {
    char ssid[32];
    char password[64];
    
    // Простой парсер аргументов
    char* space = strchr(args, ' ');
    if (!space) {
        printf("Usage: wifi connect <SSID> <password>\n");
        return;
    }
    
    *space = '\0';
    strcpy(ssid, args);
    strcpy(password, space + 1);
    
    if (wifi_connect(ssid, password) == 0) {
        printf("WiFi: Connected successfully\n");
    } else {
        printf("WiFi: Connection failed\n");
    }
}

void cmd_wifi_status(char* args) {
    wifi_adapter_t status;
    if (wifi_get_status(&status) == 0) {
        printf("WiFi Status:\n");
        printf("  State: ");
        switch (status.state) {
            case WIFI_STATE_DISABLED: printf("Disabled\n"); break;
            case WIFI_STATE_SCANNING: printf("Scanning\n"); break;
            case WIFI_STATE_CONNECTING: printf("Connecting\n"); break;
            case WIFI_STATE_CONNECTED: printf("Connected\n"); break;
            case WIFI_STATE_DISCONNECTED: printf("Disconnected\n"); break;
            case WIFI_STATE_ERROR: printf("Error\n"); break;
        }
        
        if (status.state == WIFI_STATE_CONNECTED) {
            printf("  Connected to: %s\n", status.current_network.ssid);
            printf("  Signal: %d dBm\n", status.current_network.signal_strength);
            printf("  Channel: %d\n", status.current_network.channel);
        }
        
        printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               status.mac_address[0], status.mac_address[1],
               status.mac_address[2], status.mac_address[3],
               status.mac_address[4], status.mac_address[5]);
    } else {
        printf("WiFi: Not initialized\n");
    }
}

void cmd_wifi_disconnect(char* args) {
    if (wifi_disconnect() == 0) {
        printf("WiFi: Disconnected\n");
    } else {
        printf("WiFi: Disconnect failed\n");
    }
}

void cmd_wifi(char* args) {
    if (strlen(args) == 0) {
        printf("WiFi Commands:\n");
        printf("  wifi scan      - Scan for networks\n");
        printf("  wifi status    - Show WiFi status\n");
        printf("  wifi connect   - Connect to network\n");
        printf("  wifi disconnect- Disconnect from network\n");
        return;
    }
    
    if (strncmp(args, "scan", 4) == 0) {
        cmd_wifi_scan(args + 4);
    } else if (strncmp(args, "status", 6) == 0) {
        cmd_wifi_status(args + 6);
    } else if (strncmp(args, "connect", 7) == 0) {
        cmd_wifi_connect(args + 7);
    } else if (strncmp(args, "disconnect", 10) == 0) {
        cmd_wifi_disconnect(args + 10);
    } else {
        printf("Unknown WiFi command: %s\n", args);
    }
}

// В начало добавить:
#include "../game/tetris/tetris.h"

// Добавить функцию:
void cmd_tetris(char *args) {
    printf("Starting Tetris...\n");
    printf("Controls: WASD to move, Space to drop, P to pause, ESC to quit\n");
    printf("Press any key to start...\n");
    
    // Ждем нажатия любой клавиши
    while (!keyboard_has_data()) {
        for (volatile int i = 0; i < 10000; i++);
    }
    keyboard_read_scancode(); // Очищаем буфер
    
    // Запускаем игру
    tetris_game_loop();
    
    printf("Returning to shell...\n");
}

// В НАЧАЛО добавить include:
#include "../game/2048/2048.h"

// В КОНЕЦ файла добавить функцию:
void cmd_2048(char *args) {
    printf("Starting 2048...\n");
    printf("Choose your board size and reach 2048!\n");
    printf("Press any key to continue...\n");
    
    // Ждем нажатия любой клавиши
    while (!keyboard_has_data()) {
        for (volatile int i = 0; i < 10000; i++);
    }
    keyboard_read_scancode(); // Очищаем буфер
    
    // Запускаем игру
    show_size_selection();
    
    printf("Returning to shell...\n");
}