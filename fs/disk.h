// fs/disk.h
#ifndef DISK_H
#define DISK_H

#define SECTOR_SIZE 512

// Disk functions
void disk_read_sector(unsigned int lba, unsigned char *buffer);
void disk_write_sector(unsigned int lba, unsigned char *buffer);
int disk_init();

#endif