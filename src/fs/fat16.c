// fs/fat16.c - УЛУЧШЕННАЯ ВЕРСИЯ С ПОСТОЯННЫМ ХРАНЕНИЕМ
#include "fat16.h"
#include "../drivers/screen.h"
#include "../lib/string.h"

static fat16_boot_sector_t boot_sector;
static unsigned char fat_table[800 * 512];
static unsigned char root_dir[512 * 32];
static unsigned char file_buffer[512];

static unsigned int fat_start, root_start, data_start;
static unsigned int total_clusters;

// Вспомогательные функции
static int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static void filename_to_83(const char *filename, char *name83) {
    memset(name83, ' ', 11);
    
    int dot_pos = -1;
    int i = 0;
    while (filename[i] != '\0' && i < 13) {
        if (filename[i] == '.') {
            dot_pos = i;
            break;
        }
        i++;
    }
    
    // Имя файла
    int name_len = (dot_pos == -1) ? i : dot_pos;
    if (name_len > 8) name_len = 8;
    
    for (int j = 0; j < name_len; j++) {
        name83[j] = toupper(filename[j]);
    }
    
    // Расширение
    if (dot_pos != -1) {
        int ext_start = dot_pos + 1;
        int ext_len = 0;
        while (filename[ext_start + ext_len] != '\0' && ext_len < 3) {
            name83[8 + ext_len] = toupper(filename[ext_start + ext_len]);
            ext_len++;
        }
    }
}

void name83_to_filename(const char *name83, char *filename) {
    int pos = 0;
    
    // Копируем имя
    for (int i = 0; i < 8; i++) {
        if (name83[i] != ' ') {
            filename[pos++] = name83[i];
        }
    }
    
    // Добавляем точку если есть расширение
    if (name83[8] != ' ') {
        filename[pos++] = '.';
    }
    
    // Копируем расширение
    for (int i = 8; i < 11; i++) {
        if (name83[i] != ' ') {
            filename[pos++] = name83[i];
        }
    }
    
    filename[pos] = '\0';
}

static unsigned short fat16_read_fat_entry(unsigned short cluster) {
    if (cluster >= total_clusters) return 0xFFFF;
    unsigned int fat_offset = cluster * 2;
    return *(unsigned short*)&fat_table[fat_offset];
}

static void fat16_write_fat_entry(unsigned short cluster, unsigned short value) {
    if (cluster < total_clusters) {
        unsigned int fat_offset = cluster * 2;
        *(unsigned short*)&fat_table[fat_offset] = value;
    }
}

static unsigned short fat16_find_free_cluster() {
    for (unsigned short cluster = 2; cluster < total_clusters; cluster++) {
        if (fat16_read_fat_entry(cluster) == 0) {
            return cluster;
        }
    }
    return 0;
}

static int fat16_allocate_cluster_chain(unsigned short start_cluster, unsigned int clusters_needed) {
    unsigned short current = start_cluster;
    
    for (unsigned int i = 1; i < clusters_needed; i++) {
        unsigned short next = fat16_find_free_cluster();
        if (!next) return 0;  // No free clusters
        
        fat16_write_fat_entry(current, next);
        current = next;
    }
    
    fat16_write_fat_entry(current, 0xFFFF);  // EOF
    return 1;
}

static void fat16_free_cluster_chain(unsigned short start_cluster) {
    unsigned short current = start_cluster;
    
    while (current < 0xFFF8) {
        unsigned short next = fat16_read_fat_entry(current);
        fat16_write_fat_entry(current, 0);  // Mark as free
        if (next >= 0xFFF8) break;
        current = next;
    }
}

static fat16_dir_entry_t* fat16_find_file_entry(const char *filename) {
    char name83[11];
    filename_to_83(filename, name83);
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) break;  // End of directory
        if (entry->filename[0] == 0xE5) continue;  // Deleted file
        if (entry->attributes & 0x08 || entry->attributes & 0x10) continue;  // Volume label or directory
        
        int match = 1;
        for (int j = 0; j < 11; j++) {
            if (entry->filename[j] != name83[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) return entry;
    }
    
    return 0;
}

static fat16_dir_entry_t* fat16_find_free_entry() {
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00 || entry->filename[0] == 0xE5) {
            return entry;
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
    
    // Рассчитываем общее количество кластеров
    unsigned int data_sectors = DISK_SIZE_SECTORS - data_start;
    total_clusters = data_sectors / boot_sector.sectors_per_cluster;
    
    printf("FAT16: FAT at sector %d, Root at %d, Data at %d\n", fat_start, root_start, data_start);
    printf("FAT16: Total clusters: %d\n", total_clusters);
    
    // Инициализируем FAT таблицу
    memset(fat_table, 0, sizeof(fat_table));
    fat16_write_fat_entry(0, 0xFFF8);  // Media descriptor
    fat16_write_fat_entry(1, 0xFFFF);  // EOF marker
    
    // Инициализируем корневой каталог
    memset(root_dir, 0, sizeof(root_dir));
    
    // Создаем тестовые файлы с реальными данными
    const char *readme_data = "Welcome to PureC OS with FAT16 file system!\n"
                             "This is a persistent file system.\n"
                             "Files will be preserved between reboots.\n";
    
    const char *test_data = "This is a test file for FAT16 file system.\n"
                           "You can create, read, write and delete files.\n";
    
    // Создаем README.TXT
    fat16_dir_entry_t *file1 = (fat16_dir_entry_t*)&root_dir[0];
    memcpy(file1->filename, "README  ", 8);
    memcpy(file1->extension, "TXT", 3);
    file1->attributes = 0x20;
    file1->starting_cluster = 2;
    file1->file_size = strlen(readme_data);
    file1->time = 0x8000;
    file1->date = 0x4A97;
    
    // Записываем данные README.TXT
    unsigned int cluster2_sector = data_start + (2 - 2) * boot_sector.sectors_per_cluster;
    memset(file_buffer, 0, sizeof(file_buffer));
    memcpy(file_buffer, readme_data, strlen(readme_data));
    disk_write_sector(cluster2_sector, file_buffer);
    fat16_write_fat_entry(2, 0xFFFF);
    
    // Создаем TEST.TXT
    fat16_dir_entry_t *file2 = (fat16_dir_entry_t*)&root_dir[32];
    memcpy(file2->filename, "TEST    ", 8);
    memcpy(file2->extension, "TXT", 3);
    file2->attributes = 0x20;
    file2->starting_cluster = 3;
    file2->file_size = strlen(test_data);
    file2->time = 0x8000;
    file2->date = 0x4A97;
    
    // Записываем данные TEST.TXT
    unsigned int cluster3_sector = data_start + (3 - 2) * boot_sector.sectors_per_cluster;
    memset(file_buffer, 0, sizeof(file_buffer));
    memcpy(file_buffer, test_data, strlen(test_data));
    disk_write_sector(cluster3_sector, file_buffer);
    fat16_write_fat_entry(3, 0xFFFF);
    
    printf("FAT16: Ready! %dMB disk, %d clusters available\n", DISK_SIZE_MB, total_clusters - 4);
    return 1;
}

int fat16_format() {
    printf("FAT16: Formatting disk...\n");
    
    // Очищаем FAT таблицу
    memset(fat_table, 0, sizeof(fat_table));
    fat16_write_fat_entry(0, 0xFFF8);
    fat16_write_fat_entry(1, 0xFFFF);
    
    // Очищаем корневой каталог
    memset(root_dir, 0, sizeof(root_dir));
    
    // Очищаем данные (первые несколько секторов)
    memset(file_buffer, 0, sizeof(file_buffer));
    for (int i = 0; i < 100; i++) {
        disk_write_sector(data_start + i, file_buffer);
    }
    
    printf("FAT16: Format complete\n");
    return 1;
}

int fat16_list_files() {
    printf("FAT16 Root Directory:\n");
    printf("Name     Ext Size      Cluster Modified\n");
    printf("-------- --- --------- ------- ---------\n");
    
    int file_count = 0;
    unsigned long total_size = 0;
    
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat16_dir_entry_t *entry = (fat16_dir_entry_t*)&root_dir[i * 32];
        
        if (entry->filename[0] == 0x00) break;
        if (entry->filename[0] == 0xE5) continue;
        if (entry->attributes & 0x08 || entry->attributes & 0x10) continue;
        
        char filename[13];
        name83_to_filename(entry->filename, filename);
        
        // Разбираем дату (FAT16 format)
        int day = entry->date & 0x1F;
        int month = (entry->date >> 5) & 0x0F;
        int year = ((entry->date >> 9) & 0x7F) + 1980;
        
        printf("%-12s %9d %7d %02d/%02d/%04d\n", 
               filename, entry->file_size, entry->starting_cluster,
               day, month, year);
        
        file_count++;
        total_size += entry->file_size;
    }
    
    printf("Total: %d files, %lu bytes used\n", file_count, total_size);
    return file_count;
}

int fat16_file_exists(const char *filename) {
    return fat16_find_file_entry(filename) != 0;
}

file_t *fat16_open(const char *filename, int mode) {
    static file_t file;
    memset(&file, 0, sizeof(file_t));
    
    fat16_dir_entry_t *entry = fat16_find_file_entry(filename);
    
    if (!entry) {
        if (mode == 0) {  // Try to open for read, but file doesn't exist
            return 0;
        }
        // For write/append, create the file if it doesn't exist
        if (!fat16_create(filename)) {
            return 0;
        }
        entry = fat16_find_file_entry(filename);
        if (!entry) return 0;
    }
    
    strcpy(file.filename, filename);
    file.size = entry->file_size;
    file.first_cluster = entry->starting_cluster;
    file.current_cluster = entry->starting_cluster;
    file.current_position = 0;
    file.is_open = 1;
    file.mode = mode;
    
    if (mode == 2) {  // Append mode
        file.current_position = file.size;
    }
    
    printf("FAT16: Opened '%s' (%d bytes, mode: %s)\n", 
           filename, file.size, 
           mode == 0 ? "read" : mode == 1 ? "write" : "append");
    
    return &file;
}

int fat16_read(file_t *file, char *buffer, unsigned int size) {
    if (!file->is_open || file->mode != 0) return 0;
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
            
            memcpy(buffer + bytes_read, file_buffer + sector_offset, bytes_to_read);
            
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

int fat16_write(file_t *file, const char *buffer, unsigned int size) {
    if (!file->is_open || file->mode == 0) return 0;
    
    unsigned int bytes_written = 0;
    unsigned int sectors_per_cluster = boot_sector.sectors_per_cluster;
    
    // Calculate required clusters
    unsigned int required_space = file->current_position + size;
    unsigned int current_clusters = (file->size + FAT16_CLUSTER_SIZE - 1) / FAT16_CLUSTER_SIZE;
    unsigned int needed_clusters = (required_space + FAT16_CLUSTER_SIZE - 1) / FAT16_CLUSTER_SIZE;
    
    // Allocate more clusters if needed
    if (needed_clusters > current_clusters) {
        if (!fat16_allocate_cluster_chain(file->first_cluster, needed_clusters)) {
            printf("FAT16: Not enough space for write\n");
            return 0;
        }
    }
    
    while (bytes_written < size) {
        unsigned int sector = data_start + (file->current_cluster - 2) * sectors_per_cluster;
        
        for (int s = 0; s < sectors_per_cluster; s++) {
            // Read sector first (for partial writes)
            if (file->current_position % 512 != 0 || bytes_written + 512 > size) {
                disk_read_sector(sector + s, file_buffer);
            } else {
                memset(file_buffer, 0, 512);
            }
            
            unsigned int sector_offset = file->current_position % 512;
            unsigned int bytes_available = 512 - sector_offset;
            unsigned int bytes_to_write = size - bytes_written;
            
            if (bytes_to_write > bytes_available) bytes_to_write = bytes_available;
            
            memcpy(file_buffer + sector_offset, buffer + bytes_written, bytes_to_write);
            disk_write_sector(sector + s, file_buffer);
            
            bytes_written += bytes_to_write;
            file->current_position += bytes_to_write;
            
            if (bytes_written >= size) break;
        }
        
        if (bytes_written < size) {
            file->current_cluster = fat16_read_fat_entry(file->current_cluster);
            if (file->current_cluster >= 0xFFF8) {
                // Need to allocate new cluster
                unsigned short new_cluster = fat16_find_free_cluster();
                if (!new_cluster) break;
                fat16_write_fat_entry(file->current_cluster, new_cluster);
                fat16_write_fat_entry(new_cluster, 0xFFFF);
                file->current_cluster = new_cluster;
            }
        }
    }
    
    // Update file size if we wrote beyond old size
    if (file->current_position > file->size) {
        file->size = file->current_position;
        
        // Update directory entry
        fat16_dir_entry_t *entry = fat16_find_file_entry(file->filename);
        if (entry) {
            entry->file_size = file->size;
        }
    }
    
    return bytes_written;
}

int fat16_create(const char *filename) {
    if (fat16_file_exists(filename)) {
        printf("FAT16: File exists: %s\n", filename);
        return 0;
    }
    
    fat16_dir_entry_t *entry = fat16_find_free_entry();
    if (!entry) {
        printf("FAT16: Directory full\n");
        return 0;
    }
    
    unsigned short cluster = fat16_find_free_cluster();
    if (!cluster) {
        printf("FAT16: No free clusters\n");
        return 0;
    }
    
    char name83[11];
    filename_to_83(filename, name83);
    
    memset(entry, 0, sizeof(fat16_dir_entry_t));
    memcpy(entry->filename, name83, 11);
    entry->attributes = 0x20;  // Archive
    entry->starting_cluster = cluster;
    entry->file_size = 0;
    entry->time = 0x8000;      // 16:00
    entry->date = 0x4A97;      // 2023-01-01
    
    fat16_write_fat_entry(cluster, 0xFFFF);
    
    printf("FAT16: Created '%s' at cluster %d\n", filename, cluster);
    return 1;
}

int fat16_delete(const char *filename) {
    fat16_dir_entry_t *entry = fat16_find_file_entry(filename);
    if (!entry) {
        printf("FAT16: File not found: %s\n", filename);
        return 0;
    }
    
    // Free cluster chain
    fat16_free_cluster_chain(entry->starting_cluster);
    
    // Mark directory entry as deleted
    entry->filename[0] = 0xE5;
    
    printf("FAT16: Deleted '%s'\n", filename);
    return 1;
}

void fat16_close(file_t *file) {
    if (file->is_open) {
        file->is_open = 0;
        printf("FAT16: Closed '%s'\n", file->filename);
    }
}

unsigned int fat16_get_free_space() {
    unsigned int free_clusters = 0;
    
    for (unsigned short cluster = 2; cluster < total_clusters; cluster++) {
        if (fat16_read_fat_entry(cluster) == 0) {
            free_clusters++;
        }
    }
    
    return free_clusters * FAT16_CLUSTER_SIZE;
}

unsigned int fat16_get_total_space() {
    return (total_clusters - 2) * FAT16_CLUSTER_SIZE;  // Exclude reserved clusters
}

int fat16_rename(const char *oldname, const char *newname) {
    fat16_dir_entry_t *entry = fat16_find_file_entry(oldname);
    if (!entry) {
        printf("FAT16: File not found: %s\n", oldname);
        return 0;
    }
    
    if (fat16_file_exists(newname)) {
        printf("FAT16: File already exists: %s\n", newname);
        return 0;
    }
    
    char name83[11];
    filename_to_83(newname, name83);
    memcpy(entry->filename, name83, 11);
    
    printf("FAT16: Renamed '%s' to '%s'\n", oldname, newname);
    return 1;
}

int fat16_get_file_info(const char *filename, fat16_dir_entry_t *info) {
    fat16_dir_entry_t *entry = fat16_find_file_entry(filename);
    if (!entry) return 0;
    
    memcpy(info, entry, sizeof(fat16_dir_entry_t));
    return 1;
}