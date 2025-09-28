// fs/fat16.c - ИСПРАВЛЕННАЯ ВЕРСИЯ
#include "fat16.h"
#include "../drivers/screen.h"
#include "../lib/string.h"

#define DISK_SIZE_MB 500
#define DISK_SIZE_SECTORS (DISK_SIZE_MB * 1024 * 1024 / 512)

static fat16_boot_sector_t boot_sector;
static unsigned char fat_table[800 * 512];  // Статический буфер для FAT
static unsigned char root_dir[512 * 32];    // Статический буфер для корневого каталога
static unsigned char file_buffer[512];

static unsigned int fat_start, root_start, data_start;
static unsigned int fat_size, root_size;

// Вспомогательные функции
static int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static void filename_to_83(const char *filename, char *name83) {
    for (int i = 0; i < 11; i++) name83[i] = ' ';
    
    int dot_pos = -1;
    int i = 0;
    while (filename[i] != '\0') {
        if (filename[i] == '.') {
            dot_pos = i;
            break;
        }
        i++;
    }
    
    int name_len = (dot_pos == -1) ? i : dot_pos;
    if (name_len > 8) name_len = 8;
    
    for (int j = 0; j < name_len; j++) {
        name83[j] = toupper(filename[j]);
    }
    
    if (dot_pos != -1) {
        int ext_len = 0;
        while (filename[dot_pos + 1 + ext_len] != '\0' && ext_len < 3) {
            name83[8 + ext_len] = toupper(filename[dot_pos + 1 + ext_len]);
            ext_len++;
        }
    }
}

static unsigned short fat16_read_fat_entry(unsigned short cluster) {
    unsigned int fat_offset = cluster * 2;
    if (fat_offset >= sizeof(fat_table)) return 0xFFFF;
    return *(unsigned short*)&fat_table[fat_offset];
}

static void fat16_write_fat_entry(unsigned short cluster, unsigned short value) {
    unsigned int fat_offset = cluster * 2;
    if (fat_offset < sizeof(fat_table)) {
        *(unsigned short*)&fat_table[fat_offset] = value;
    }
}

static unsigned short fat16_find_free_cluster() {
    for (unsigned short cluster = 2; cluster < 0xFFF0; cluster++) {
        if (fat16_read_fat_entry(cluster) == 0) {
            return cluster;
        }
    }
    return 0;
}

// Основные функции FAT16
int fat16_init() {
    printf("FAT16: Initializing %dMB file system...\n", DISK_SIZE_MB);
    
    // Инициализируем загрузочный сектор
    memset(&boot_sector, 0, sizeof(boot_sector));
    
    boot_sector.jmp[0] = 0xEB;
    boot_sector.jmp[1] = 0x3C;
    boot_sector.jmp[2] = 0x90;
    
    memcpy(boot_sector.oem, "MYOS    ", 8);
    boot_sector.bytes_per_sector = 512;
    boot_sector.sectors_per_cluster = 8;
    boot_sector.reserved_sectors = 1;
    boot_sector.fat_copies = 2;
    boot_sector.root_entries = 512;
    boot_sector.total_sectors_small = 0;
    boot_sector.media_descriptor = 0xF8;
    boot_sector.sectors_per_fat = 800;
    boot_sector.sectors_per_track = 63;
    boot_sector.heads = 16;
    boot_sector.hidden_sectors = 0;
    boot_sector.total_sectors_large = DISK_SIZE_SECTORS;
    
    memcpy(boot_sector.file_system, "FAT16   ", 8);
    
    // Рассчитываем позиции
    fat_start = boot_sector.reserved_sectors;
    root_start = fat_start + (boot_sector.fat_copies * boot_sector.sectors_per_fat);
    data_start = root_start + ((boot_sector.root_entries * 32) / boot_sector.bytes_per_sector);
    
    printf("FAT16: FAT at sector %d, Root at %d, Data at %d\n", 
           fat_start, root_start, data_start);
    
    // Инициализируем FAT таблицу
    memset(fat_table, 0, sizeof(fat_table));
    fat16_write_fat_entry(0, 0xFFF8);
    fat16_write_fat_entry(1, 0xFFFF);
    
    // Инициализируем корневой каталог
    memset(root_dir, 0, sizeof(root_dir));
    
    // Создаем тестовые файлы с реальными данными
    fat16_dir_entry_t *file1 = (fat16_dir_entry_t*)&root_dir[0];
    memcpy(file1->filename, "README  ", 8);
    memcpy(file1->extension, "TXT", 3);
    file1->attributes = 0x20;
    file1->starting_cluster = 2;
    file1->file_size = 45;  // Размер реальных данных
    file1->time = 0x8000;
    file1->date = 0x4A97;
    
    fat16_dir_entry_t *file2 = (fat16_dir_entry_t*)&root_dir[32];
    memcpy(file2->filename, "TEST    ", 8);
    memcpy(file2->extension, "TXT", 3);
    file2->attributes = 0x20;
    file2->starting_cluster = 3;
    file2->file_size = 25;  // Размер реальных данных
    file2->time = 0x8000;
    file2->date = 0x4A97;
    
    // Записываем реальные данные в кластеры
    const char *readme_data = "Welcome to PureC OS with FAT16 file system!\n";
    const char *test_data = "This is a test file.\n";
    
    // Записываем данные README.TXT в кластер 2
    unsigned int cluster2_sector = data_start + (2 - 2) * boot_sector.sectors_per_cluster;
    for (int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        memset(file_buffer, 0, sizeof(file_buffer));
        if (i == 0) {
            memcpy(file_buffer, readme_data, strlen(readme_data));
        }
        disk_write_sector(cluster2_sector + i, file_buffer);
    }
    
    // Записываем данные TEST.TXT в кластер 3  
    unsigned int cluster3_sector = data_start + (3 - 2) * boot_sector.sectors_per_cluster;
    for (int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        memset(file_buffer, 0, sizeof(file_buffer));
        if (i == 0) {
            memcpy(file_buffer, test_data, strlen(test_data));
        }
        disk_write_sector(cluster3_sector + i, file_buffer);
    }
    
    fat16_write_fat_entry(2, 0xFFFF);
    fat16_write_fat_entry(3, 0xFFFF);
    
    printf("FAT16: Ready! %dMB disk\n", DISK_SIZE_MB);
    return 1;
}

int fat16_list_files() {
    printf("FAT16 Root Directory (%dMB disk):\n", DISK_SIZE_MB);
    printf("Name     Ext Size      Cluster\n");
    printf("-------- --- --------- -------\n");
    
    int file_count = 0;
    unsigned long total_size = 0;
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) break;
        if (entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x08 || entry->attributes & 0x10) continue;
        
        // Выводим имя файла
        for (int j = 0; j < 8; j++) {
            if (entry->filename[j] != ' ') {
                putchar(entry->filename[j]);
            }
        }
        
        // Выводим расширение
        putchar('.');
        for (int j = 0; j < 3; j++) {
            if (entry->extension[j] != ' ') {
                putchar(entry->extension[j]);
            }
        }
        
        // Выводим размер и кластер
        printf(" %9d bytes cluster %d\n", entry->file_size, entry->starting_cluster);
        
        file_count++;
        total_size += entry->file_size;
    }
    
    printf("Total: %d files, %lu bytes used\n", file_count, total_size);
    return file_count;
}

int fat16_file_exists(const char *filename) {
    char name83[11];
    filename_to_83(filename, name83);
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) break;
        if (entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x08 || entry->attributes & 0x10) continue;
        
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (entry->filename[j] != name83[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) return 1;
    }
    
    return 0;
}

file_t *fat16_open(const char *filename) {
    static file_t file;
    memset(&file, 0, sizeof(file_t));
    
    char name83[11];
    filename_to_83(filename, name83);
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) break;
        if (entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x08 || entry->attributes & 0x10) continue;
        
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (entry->filename[j] != name83[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            strcpy(file.filename, filename);
            file.size = entry->file_size;
            file.first_cluster = entry->starting_cluster;
            file.current_cluster = entry->starting_cluster;
            file.current_position = 0;
            file.is_open = 1;
            
            printf("FAT16: Opened '%s' (%d bytes)\n", filename, file.size);
            return &file;
        }
    }
    
    return 0;
}

int fat16_read(file_t *file, char *buffer, unsigned int size) {
    if (!file->is_open) return 0;
    if (file->current_position >= file->size) return 0;
    
    unsigned int bytes_read = 0;
    unsigned int sectors_per_cluster = boot_sector.sectors_per_cluster;
    
    while (bytes_read < size && file->current_position < file->size) {
        unsigned int sector = data_start + (file->current_cluster - 2) * sectors_per_cluster;
        
        for (int s = 0; s < sectors_per_cluster; s++) {
            disk_read_sector(sector + s, file_buffer);
            
            unsigned int sector_offset = file->current_position % 512;
            unsigned int bytes_available = 512 - sector_offset;
            unsigned int bytes_to_read = size - bytes_read;
            
            if (bytes_to_read > bytes_available) bytes_to_read = bytes_available;
            if (bytes_to_read > file->size - file->current_position) {
                bytes_to_read = file->size - file->current_position;
            }
            
            for (unsigned int i = 0; i < bytes_to_read; i++) {
                buffer[bytes_read + i] = file_buffer[sector_offset + i];
            }
            
            bytes_read += bytes_to_read;
            file->current_position += bytes_to_read;
            
            if (file->current_position >= file->size) break;
        }
        
        if (file->current_position < file->size) {
            file->current_cluster = fat16_read_fat_entry(file->current_cluster);
            if (file->current_cluster >= 0xFFF8) break;
        }
    }
    
    return bytes_read;
}

int fat16_create(const char *filename) {
    char name83[11];
    filename_to_83(filename, name83);
    
    if (fat16_file_exists(filename)) {
        printf("FAT16: File exists: %s\n", filename);
        return 0;
    }
    
    // Ищем свободную запись
    fat16_dir_entry_t *free_entry = 0;
    int entry_index = -1;
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) {
            free_entry = entry;
            entry_index = i;
            break;
        }
        
        if (entry->filename[0] == 0xE5) {
            free_entry = entry;
            entry_index = i;
            break;
        }
    }
    
    if (!free_entry) {
        printf("FAT16: Directory full\n");
        return 0;
    }
    
    unsigned short free_cluster = fat16_find_free_cluster();
    if (!free_cluster) {
        printf("FAT16: No free clusters\n");
        return 0;
    }
    
    printf("FAT16: Creating '%s' at entry %d, cluster %d\n", filename, entry_index, free_cluster);
    
    memset(free_entry, 0, sizeof(fat16_dir_entry_t));
// Копируем имя (8 символов)
    for (int i = 0; i < 8; i++) {
        free_entry->filename[i] = name83[i];
    }
// Копируем расширение (3 символа)  
    for (int i = 0; i < 3; i++) {
        free_entry->extension[i] = name83[8 + i];
    }
    
    free_entry->attributes = 0x20;
    free_entry->starting_cluster = free_cluster;
    free_entry->file_size = 0;
    free_entry->time = 0x8000;
    free_entry->date = 0x4A97;
    
    fat16_write_fat_entry(free_cluster, 0xFFFF);
    
    printf("FAT16: File created successfully\n");
    return 1;
}

int fat16_delete(const char *filename) {
    char name83[11];
    filename_to_83(filename, name83);
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) break;
        if (entry->filename[0] == 0xE5) continue;
        
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (entry->filename[j] != name83[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            unsigned short cluster = entry->starting_cluster;
            fat16_write_fat_entry(cluster, 0);
            entry->filename[0] = 0xE5;
            
            printf("FAT16: Deleted '%s'\n", filename);
            return 1;
        }
    }
    
    printf("FAT16: File not found: %s\n", filename);
    return 0;
}

void fat16_close(file_t *file) {
    if (file->is_open) {
        file->is_open = 0;
    }
}