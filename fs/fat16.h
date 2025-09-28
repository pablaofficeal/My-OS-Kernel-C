// fs/fat16.h
#ifndef FAT16_H
#define FAT16_H

#include "disk.h"

#define DISK_SIZE_MB 500
#define DISK_SIZE_SECTORS (DISK_SIZE_MB * 1024 * 1024 / 512)
#define FAT16_SECTOR_SIZE 512
#define FAT16_ROOT_ENTRIES 512
#define FAT16_CLUSTER_SIZE 4096  // 8 sectors * 512 bytes

// FAT16 Boot Sector
typedef struct {
    unsigned char jmp[3];
    char oem[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char fat_copies;
    unsigned short root_entries;
    unsigned short total_sectors_small;
    unsigned char media_descriptor;
    unsigned short sectors_per_fat;
    unsigned short sectors_per_track;
    unsigned short heads;
    unsigned int hidden_sectors;
    unsigned int total_sectors_large;
    
    // Extended BPB
    unsigned char drive_number;
    unsigned char reserved;
    unsigned char signature;
    unsigned int volume_id;
    char volume_label[11];
    char file_system[8];
} __attribute__((packed)) fat16_boot_sector_t;

// FAT16 Directory Entry
typedef struct {
    char filename[8];
    char extension[3];
    unsigned char attributes;
    unsigned char reserved[10];
    unsigned short time;
    unsigned short date;
    unsigned short starting_cluster;
    unsigned int file_size;
} __attribute__((packed)) fat16_dir_entry_t;

// File handle
typedef struct {
    char filename[13];  // 8.3 + null terminator
    unsigned int size;
    unsigned short first_cluster;
    unsigned int current_position;
    unsigned short current_cluster;
    int is_open;
    int mode;  // 0=read, 1=write, 2=append
} file_t;

// FAT16 functions
int fat16_init();
int fat16_format();
int fat16_list_files();
int fat16_file_exists(const char *filename);
file_t *fat16_open(const char *filename, int mode);
int fat16_read(file_t *file, char *buffer, unsigned int size);
int fat16_write(file_t *file, const char *buffer, unsigned int size);
int fat16_create(const char *filename);
int fat16_delete(const char *filename);
void fat16_close(file_t *file);
unsigned int fat16_get_free_space();
unsigned int fat16_get_total_space();
int fat16_rename(const char *oldname, const char *newname);
int fat16_get_file_info(const char *filename, fat16_dir_entry_t *info);

#endif