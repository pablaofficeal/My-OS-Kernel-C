#include "fat32.h"

#include "../drivers/storage/block_device.h"
#include "../kernel/klog.h"
#include "../lib/string.h"
#include <stddef.h>
#include "../drivers/storage/limine_uefi.h"
#include "../drivers/storage/limine_vbr.h"
#include "../kernel_blob.h"

#define FAT32_ATTRIBUTE_DIRECTORY 0x10
#define FAT32_ATTRIBUTE_VOLUME_ID 0x08
#define FAT32_ATTRIBUTE_READ_ONLY 0x01
#define FAT32_ATTRIBUTE_ARCHIVE   0x20
#define FAT32_ATTRIBUTE_LFN       0x0F
#define FAT32_DELETED_ENTRY       0xE5
#define FAT32_END_OF_CHAIN        0x0FFFFFF8
#define FAT32_MAX_OPEN_FILES      16
#define FAT32_DESCRIPTOR_BASE     3
#define FAT32_MAX_COMPONENT       12
#define FAT32_FORMAT_RESERVED_SECTORS 32
#define FAT32_FORMAT_FAT_COUNT         2
#define FAT32_FORMAT_BLANK_SCAN        2048
#define FAT32_FORMAT_MAX_SECTORS       UINT32_MAX
#define FAT32_ESP_START_LBA 2048
#define FAT32_ESP_RESERVED FAT32_FORMAT_RESERVED_SECTORS

static const uint8_t required_volume_label[11]={
    'P','U','R','E','C','O','S',' ',' ',' ',' '
};

struct fat32_volume {
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t total_sectors;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    bool mounted;
};

struct fat32_entry_ref {
    uint32_t sector_lba;
    uint32_t first_cluster;
    uint32_t size;
    uint16_t offset;
    uint8_t attributes;
    uint8_t short_name[11];
    bool has_lfn;
};

struct fat32_handle {
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t cluster_index;
    uint32_t size;
    uint32_t position;
    bool used;
};

struct fat32_format_layout {
    uint32_t total_sectors;
    uint32_t fat_size;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
};

static struct fat32_volume volume;
static struct fat32_handle handles[FAT32_MAX_OPEN_FILES];
static uint8_t sector_buffer[BLOCK_SECTOR_SIZE] __attribute__((aligned(2)));
static uint8_t second_sector_buffer[BLOCK_SECTOR_SIZE] __attribute__((aligned(2)));

static uint16_t read_u16(const uint8_t *data){
    return (uint16_t)data[0]|((uint16_t)data[1]<<8);
}

static void write_u16(uint8_t *data, uint16_t value){
    data[0]=(uint8_t)value;
    data[1]=(uint8_t)(value>>8);
}

static uint32_t read_u32(const uint8_t *data){
    return (uint32_t)data[0]|((uint32_t)data[1]<<8)
        |((uint32_t)data[2]<<16)|((uint32_t)data[3]<<24);
}

static void write_u32(uint8_t *data, uint32_t value){
    data[0]=(uint8_t)value;
    data[1]=(uint8_t)(value>>8);
    data[2]=(uint8_t)(value>>16);
    data[3]=(uint8_t)(value>>24);
}

static uint8_t uppercase_ascii(uint8_t character){
    if(character>='a' && character<='z') return character-'a'+'A';
    return character;
}

static bool valid_short_character(uint8_t character){
    if((character>='A' && character<='Z') || (character>='0' && character<='9')) return true;
    const char *allowed="$%'-_@~`!(){}^#&";
    while(*allowed){
        if(character==(uint8_t)*allowed) return true;
        allowed++;
    }
    return false;
}

static bool make_short_name(const char *component, uint8_t output[11]){
    if(!component || !component[0]) return false;
    memset(output,' ',11);

    uint8_t base_length=0;
    uint8_t extension_length=0;
    bool extension=false;
    for(uint8_t index=0;component[index];index++){
        uint8_t character=(uint8_t)component[index];
        if(character=='.'){
            if(extension || base_length==0) return false;
            extension=true;
            continue;
        }
        character=uppercase_ascii(character);
        if(!valid_short_character(character)) return false;
        if(!extension){
            if(base_length>=8) return false;
            output[base_length++]=character;
        } else {
            if(extension_length>=3) return false;
            output[8+extension_length++]=character;
        }
    }
    return base_length>0 && (!extension || extension_length>0);
}

static void short_name_to_text(const uint8_t input[11], char output[13]){
    uint8_t length=0;
    for(uint8_t index=0;index<8 && input[index]!=' ';index++){
        output[length++]=(char)input[index];
    }
    uint8_t extension_length=0;
    while(extension_length<3 && input[8+extension_length]!=' ') extension_length++;
    if(extension_length){
        output[length++]='.';
        for(uint8_t index=0;index<extension_length;index++){
            output[length++]=(char)input[8+index];
        }
    }
    output[length]='\0';
}

static bool next_component(const char **path, char component[FAT32_MAX_COMPONENT+1],
                           bool *has_more){
    const char *cursor=*path;
    while(*cursor=='/') cursor++;
    if(!*cursor) return false;

    uint8_t length=0;
    while(*cursor && *cursor!='/'){
        if(length>=FAT32_MAX_COMPONENT) return false;
        component[length++]=*cursor++;
    }
    component[length]='\0';
    while(*cursor=='/') cursor++;
    *has_more=*cursor!='\0';
    *path=cursor;
    return true;
}

static bool valid_cluster(uint32_t cluster){
    return cluster>=2 && cluster<volume.cluster_count+2;
}

static uint32_t cluster_lba(uint32_t cluster){
    return volume.data_lba+(cluster-2)*volume.sectors_per_cluster;
}

static int32_t fat_next_cluster(uint32_t cluster, uint32_t *next){
    if(!valid_cluster(cluster) || !next) return FS_ERROR_INVALID;
    uint32_t fat_offset=cluster*4;
    uint32_t lba=volume.fat_lba+fat_offset/BLOCK_SECTOR_SIZE;
    uint16_t offset=(uint16_t)(fat_offset%BLOCK_SECTOR_SIZE);
    if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
    *next=read_u32(&sector_buffer[offset])&0x0FFFFFFF;
    return 0;
}

static int32_t fat_write_entry(uint32_t cluster, uint32_t value){
    if(!valid_cluster(cluster)) return FS_ERROR_INVALID;
    uint32_t fat_offset=cluster*4;
    uint32_t sector_offset=fat_offset/BLOCK_SECTOR_SIZE;
    uint16_t offset=(uint16_t)(fat_offset%BLOCK_SECTOR_SIZE);

    for(uint8_t fat_index=0;fat_index<volume.fat_count;fat_index++){
        uint32_t lba=volume.fat_lba+(uint32_t)fat_index*volume.fat_size+sector_offset;
        if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
        uint32_t current=read_u32(&sector_buffer[offset]);
        write_u32(&sector_buffer[offset],(current&0xF0000000)|(value&0x0FFFFFFF));
        if(!block_device_write(lba,sector_buffer)) return FS_ERROR_IO;
    }
    return 0;
}

static int32_t find_entry(uint32_t directory_cluster, const uint8_t short_name[11],
                          struct fat32_entry_ref *result){
    if(!valid_cluster(directory_cluster)) return FS_ERROR_NOT_DIR;
    uint32_t cluster=directory_cluster;
    bool pending_lfn=false;

    for(uint32_t visited=0;visited<volume.cluster_count;visited++){
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            uint32_t lba=first_lba+sector;
            if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
            for(uint16_t offset=0;offset<BLOCK_SECTOR_SIZE;offset+=32){
                uint8_t first=sector_buffer[offset];
                if(first==0) return FS_ERROR_NOT_FOUND;
                if(first==FAT32_DELETED_ENTRY){ pending_lfn=false; continue; }
                uint8_t attributes=sector_buffer[offset+11];
                if(attributes==FAT32_ATTRIBUTE_LFN){ pending_lfn=true; continue; }
                if(!(attributes&FAT32_ATTRIBUTE_VOLUME_ID)
                   && memcmp(&sector_buffer[offset],short_name,11)==0){
                    if(result){
                        result->sector_lba=lba;
                        result->offset=offset;
                        result->attributes=attributes;
                        result->first_cluster=((uint32_t)read_u16(&sector_buffer[offset+20])<<16)
                            |read_u16(&sector_buffer[offset+26]);
                        result->size=read_u32(&sector_buffer[offset+28]);
                        memcpy(result->short_name,&sector_buffer[offset],11);
                        result->has_lfn=pending_lfn;
                    }
                    return 0;
                }
                pending_lfn=false;
            }
        }

        uint32_t next;
        int32_t status=fat_next_cluster(cluster,&next);
        if(status<0) return status;
        if(next>=FAT32_END_OF_CHAIN) return FS_ERROR_NOT_FOUND;
        if(!valid_cluster(next)) return FS_ERROR_INVALID;
        cluster=next;
    }
    return FS_ERROR_INVALID;
}

static int32_t resolve_entry(const char *path, struct fat32_entry_ref *result,
                             uint32_t *parent_cluster){
    if(!volume.mounted || !path || !path[0]) return FS_ERROR_INVALID;
    uint32_t directory=volume.root_cluster;
    const char *cursor=path;
    char component[FAT32_MAX_COMPONENT+1];
    bool has_more;

    while(next_component(&cursor,component,&has_more)){
        uint8_t short_name[11];
        if(!make_short_name(component,short_name)) return FS_ERROR_UNSUPPORTED;
        struct fat32_entry_ref entry;
        int32_t status=find_entry(directory,short_name,&entry);
        if(status<0) return status;
        if(!has_more){
            if(result) *result=entry;
            if(parent_cluster) *parent_cluster=directory;
            return 0;
        }
        if(!(entry.attributes&FAT32_ATTRIBUTE_DIRECTORY)) return FS_ERROR_NOT_DIR;
        if(!valid_cluster(entry.first_cluster)) return FS_ERROR_INVALID;
        directory=entry.first_cluster;
    }
    return FS_ERROR_INVALID;
}

static int32_t resolve_directory(const char *path, uint32_t *cluster){
    if(!volume.mounted || !path || !cluster) return FS_ERROR_INVALID;
    const char *cursor=path;
    while(*cursor=='/') cursor++;
    if(!*cursor){
        *cluster=volume.root_cluster;
        return 0;
    }

    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status<0) return status;
    if(!(entry.attributes&FAT32_ATTRIBUTE_DIRECTORY)) return FS_ERROR_NOT_DIR;
    if(!valid_cluster(entry.first_cluster)) return FS_ERROR_INVALID;
    *cluster=entry.first_cluster;
    return 0;
}

static int32_t resolve_creation_parent(const char *path, uint32_t *parent,
                                       uint8_t short_name[11]){
    if(!volume.mounted || !path || !path[0] || !parent) return FS_ERROR_INVALID;
    uint32_t directory=volume.root_cluster;
    const char *cursor=path;
    char component[FAT32_MAX_COMPONENT+1];
    bool has_more;

    while(next_component(&cursor,component,&has_more)){
        uint8_t current_name[11];
        if(!make_short_name(component,current_name)) return FS_ERROR_UNSUPPORTED;
        if(!has_more){
            *parent=directory;
            memcpy(short_name,current_name,11);
            return 0;
        }

        struct fat32_entry_ref entry;
        int32_t status=find_entry(directory,current_name,&entry);
        if(status<0) return status;
        if(!(entry.attributes&FAT32_ATTRIBUTE_DIRECTORY)) return FS_ERROR_NOT_DIR;
        if(!valid_cluster(entry.first_cluster)) return FS_ERROR_INVALID;
        directory=entry.first_cluster;
    }
    return FS_ERROR_INVALID;
}

static int32_t allocate_cluster(uint32_t *cluster_result){
    if(!cluster_result) return FS_ERROR_INVALID;
    for(uint32_t cluster=2;cluster<volume.cluster_count+2;cluster++){
        uint32_t value;
        int32_t status=fat_next_cluster(cluster,&value);
        if(status<0) return status;
        if(value!=0) continue;

        status=fat_write_entry(cluster,0x0FFFFFFF);
        if(status<0) return status;
        memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            if(!block_device_write(first_lba+sector,sector_buffer)){
                (void)fat_write_entry(cluster,0);
                return FS_ERROR_IO;
            }
        }
        *cluster_result=cluster;
        return 0;
    }
    return FS_ERROR_NO_SPACE;
}

static int32_t find_free_entry(uint32_t directory_cluster, uint32_t *lba_result,
                               uint16_t *offset_result){
    uint32_t cluster=directory_cluster;
    for(uint32_t visited=0;visited<volume.cluster_count;visited++){
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            uint32_t lba=first_lba+sector;
            if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
            for(uint16_t offset=0;offset<BLOCK_SECTOR_SIZE;offset+=32){
                if(sector_buffer[offset]==0 || sector_buffer[offset]==FAT32_DELETED_ENTRY){
                    *lba_result=lba;
                    *offset_result=offset;
                    return 0;
                }
            }
        }
        uint32_t next;
        int32_t status=fat_next_cluster(cluster,&next);
        if(status<0) return status;
        if(next>=FAT32_END_OF_CHAIN){
            uint32_t new_cluster;
            status=allocate_cluster(&new_cluster);
            if(status<0) return status;
            status=fat_write_entry(cluster,new_cluster);
            if(status<0){
                (void)fat_write_entry(new_cluster,0);
                return status;
            }
            *lba_result=cluster_lba(new_cluster);
            *offset_result=0;
            return 0;
        }
        if(!valid_cluster(next)) return FS_ERROR_INVALID;
        cluster=next;
    }
    return FS_ERROR_INVALID;
}

static void fill_directory_entry(uint8_t *entry, const uint8_t short_name[11],
                                 uint8_t attributes, uint32_t first_cluster){
    memset(entry,0,32);
    memcpy(entry,short_name,11);
    entry[11]=attributes;
    write_u16(&entry[20],(uint16_t)(first_cluster>>16));
    write_u16(&entry[26],(uint16_t)first_cluster);
}

static int32_t create_directory_entry(uint32_t parent, const uint8_t short_name[11],
                                      uint8_t attributes, uint32_t first_cluster){
    uint32_t lba;
    uint16_t offset;
    int32_t status=find_free_entry(parent,&lba,&offset);
    if(status<0) return status;
    if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
    bool was_end_marker=sector_buffer[offset]==0;
    fill_directory_entry(&sector_buffer[offset],short_name,attributes,first_cluster);
    if(was_end_marker && offset+32<BLOCK_SECTOR_SIZE) sector_buffer[offset+32]=0;
    return block_device_write(lba,sector_buffer) ? 0 : FS_ERROR_IO;
}

static int32_t clear_cluster_chain(uint32_t first_cluster){
    if(first_cluster==0) return 0;
    uint32_t cluster=first_cluster;
    for(uint32_t visited=0;visited<volume.cluster_count;visited++){
        uint32_t next;
        int32_t status=fat_next_cluster(cluster,&next);
        if(status<0) return status;
        status=fat_write_entry(cluster,0);
        if(status<0) return status;
        if(next>=FAT32_END_OF_CHAIN) return 0;
        if(!valid_cluster(next)) return FS_ERROR_INVALID;
        cluster=next;
    }
    return FS_ERROR_INVALID;
}

static bool mount_boot_sector(uint32_t partition_lba){
    if(!block_device_read(partition_lba,sector_buffer)){
        klogf(KLOG_DEBUG,"mount: read LBA %u failed",partition_lba);
        return false;
    }
    if(sector_buffer[510]!=0x55 || sector_buffer[511]!=0xAA){
        klogf(KLOG_DEBUG,"mount: LBA %u no boot sig %02x %02x",partition_lba,sector_buffer[510],sector_buffer[511]);
        return false;
    }
    if(memcmp(&sector_buffer[71],required_volume_label,11)!=0){
        klogf(KLOG_DEBUG,"mount: LBA %u label mismatch",partition_lba);
        return false;
    }

    uint16_t bytes_per_sector=read_u16(&sector_buffer[11]);
    uint8_t sectors_per_cluster=sector_buffer[13];
    uint16_t reserved_sectors=read_u16(&sector_buffer[14]);
    uint8_t fat_count=sector_buffer[16];
    uint16_t root_entry_count=read_u16(&sector_buffer[17]);
    uint16_t fat16_size=read_u16(&sector_buffer[22]);
    uint32_t total_sectors=read_u16(&sector_buffer[19]);
    if(total_sectors==0) total_sectors=read_u32(&sector_buffer[32]);
    uint32_t fat_size=read_u32(&sector_buffer[36]);
    uint32_t root_cluster=read_u32(&sector_buffer[44]);

    if(bytes_per_sector!=BLOCK_SECTOR_SIZE || sectors_per_cluster==0
       || (sectors_per_cluster&(sectors_per_cluster-1))!=0
       || reserved_sectors==0 || fat_count==0 || fat_size==0 || total_sectors==0
       || root_entry_count!=0 || fat16_size!=0){
        klogf(KLOG_DEBUG,"mount: LBA %u bpb invalid bps=%u spc=%u res=%u fats=%u fsz=%u tot=%u rootEC=%u f16=%u",
              partition_lba,bytes_per_sector,sectors_per_cluster,reserved_sectors,fat_count,fat_size,total_sectors,root_entry_count,fat16_size);
        return false;
    }

    uint64_t fat_sectors=(uint64_t)fat_count*fat_size;
    if((uint64_t)reserved_sectors+fat_sectors>=total_sectors){
        klogf(KLOG_DEBUG,"mount: LBA %u fat+res %llu >= tot %u",partition_lba,(unsigned long long)fat_sectors+reserved_sectors,total_sectors);
        return false;
    }
    uint32_t data_sectors=total_sectors-reserved_sectors-(uint32_t)fat_sectors;
    uint32_t cluster_count=data_sectors/sectors_per_cluster;
    if(cluster_count<65525 || root_cluster<2 || root_cluster>=cluster_count+2){
        klogf(KLOG_DEBUG,"mount: LBA %u clusters %u root %u",partition_lba,cluster_count,root_cluster);
        return false;
    }
    if((uint64_t)fat_size*(BLOCK_SECTOR_SIZE/4)<cluster_count+2){
        klogf(KLOG_DEBUG,"mount: LBA %u fat too small %u clusters %u",partition_lba,fat_size,cluster_count);
        return false;
    }
    if((uint64_t)partition_lba+total_sectors>FAT32_FORMAT_MAX_SECTORS){
        klogf(KLOG_DEBUG,"mount: LBA %u tot %u exceeds limit",partition_lba,total_sectors);
        return false;
    }

    volume.partition_lba=partition_lba;
    volume.fat_lba=partition_lba+reserved_sectors;
    volume.data_lba=volume.fat_lba+(uint32_t)fat_sectors;
    volume.fat_size=fat_size;
    volume.root_cluster=root_cluster;
    volume.cluster_count=cluster_count;
    volume.total_sectors=total_sectors;
    volume.sectors_per_cluster=sectors_per_cluster;
    volume.fat_count=fat_count;
    volume.mounted=true;
    memset(handles,0,sizeof(handles));
    return true;
}

bool fat32_init(void){
    if(volume.mounted) return true;
    if(!block_device_init()) return false;

    uint32_t disk_count=block_device_count();
    for(uint32_t disk=0;disk<disk_count;disk++){
        if(!block_device_select(disk) || !block_device_read(0,sector_buffer)) continue;

        uint32_t partition_lbas[4];
        uint8_t partition_count=0;
        for(uint8_t index=0;index<4;index++){
            uint16_t offset=(uint16_t)(446+index*16);
            uint8_t type=sector_buffer[offset+4];
            if(type==0x0B || type==0x0C || type==0x1B || type==0x1C || type==0xEF){
                uint32_t lba=read_u32(&sector_buffer[offset+8]);
                if(lba) partition_lbas[partition_count++]=lba;
            }
        }
        for(uint8_t index=0;index<partition_count;index++){
            if(mount_boot_sector(partition_lbas[index])) return true;
        }
        if(mount_boot_sector(0)) return true;
        // Fallback для ESP: пробуем напрямую 2048 (UEFI) даже если тип не распознан
        if(mount_boot_sector(2048)){
            klogf(KLOG_INFO,"fat32: fallback mount LBA 2048 succeeded");
            return true;
        }
    }
    // Глобальный fallback: попробуем 2048 на всех дисках если MBR не распознан
    for(uint32_t disk=0;disk<disk_count;disk++){
        if(!block_device_select(disk)) continue;
        if(mount_boot_sector(2048)) return true;
    }
    return false;
}

bool fat32_is_mounted(void){ return volume.mounted; }

const char *fat32_device_name(void){ return block_device_name(); }

int32_t fat32_open(const char *path){
    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status<0) return status;
    if(entry.attributes&(FAT32_ATTRIBUTE_DIRECTORY|FAT32_ATTRIBUTE_VOLUME_ID)){
        return FS_ERROR_NOT_FILE;
    }

    for(uint8_t index=0;index<FAT32_MAX_OPEN_FILES;index++){
        if(!handles[index].used){
            handles[index].used=true;
            handles[index].first_cluster=entry.first_cluster;
            handles[index].current_cluster=entry.first_cluster;
            handles[index].cluster_index=0;
            handles[index].size=entry.size;
            handles[index].position=0;
            return FAT32_DESCRIPTOR_BASE+index;
        }
    }
    return FS_ERROR_NO_SPACE;
}

int32_t fat32_read(int32_t descriptor, void *buffer, uint32_t count){
    if(!buffer && count) return FS_ERROR_INVALID;
    if(count>0x7FFFFFFF) return FS_ERROR_INVALID;
    int32_t index=descriptor-FAT32_DESCRIPTOR_BASE;
    if(index<0 || index>=FAT32_MAX_OPEN_FILES || !handles[index].used){
        return FS_ERROR_INVALID;
    }
    struct fat32_handle *handle=&handles[index];
    if(handle->position>=handle->size){
        handle->used=false;
        return 0;
    }

    uint8_t *output=(uint8_t*)buffer;
    uint32_t total_read=0;
    uint32_t cluster_size=(uint32_t)volume.sectors_per_cluster*BLOCK_SECTOR_SIZE;
    while(total_read<count && handle->position<handle->size){
        uint32_t cluster=handle->current_cluster;
        if(!valid_cluster(cluster)){
            handle->used=false;
            return FS_ERROR_INVALID;
        }
        uint32_t target_cluster_index=handle->position/cluster_size;
        while(handle->cluster_index<target_cluster_index){
            uint32_t next;
            int32_t status=fat_next_cluster(cluster,&next);
            if(status<0){ handle->used=false; return status; }
            if(!valid_cluster(next)){
                handle->used=false;
                return FS_ERROR_INVALID;
            }
            cluster=next;
            handle->current_cluster=next;
            handle->cluster_index++;
        }

        uint32_t offset_in_cluster=handle->position%cluster_size;
        uint32_t sector=offset_in_cluster/BLOCK_SECTOR_SIZE;
        uint32_t offset=offset_in_cluster%BLOCK_SECTOR_SIZE;
        if(!block_device_read(cluster_lba(cluster)+sector,sector_buffer)){
            handle->used=false;
            return FS_ERROR_IO;
        }

        uint32_t amount=BLOCK_SECTOR_SIZE-offset;
        if(amount>count-total_read) amount=count-total_read;
        if(amount>handle->size-handle->position) amount=handle->size-handle->position;
        memcpy(output+total_read,sector_buffer+offset,amount);
        handle->position+=amount;
        total_read+=amount;
    }
    return (int32_t)total_read;
}

int32_t fat32_delete(const char *path){
    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status<0) return status;
    if(entry.attributes&FAT32_ATTRIBUTE_DIRECTORY) return FS_ERROR_NOT_FILE;
    if(entry.attributes&FAT32_ATTRIBUTE_READ_ONLY) return FS_ERROR_READ_ONLY;
    if(entry.has_lfn) return FS_ERROR_UNSUPPORTED;
    for(uint8_t index=0;index<FAT32_MAX_OPEN_FILES;index++){
        if(entry.first_cluster!=0 && handles[index].used
           && handles[index].first_cluster==entry.first_cluster){
            return FS_ERROR_BUSY;
        }
    }

    if(!block_device_read(entry.sector_lba,sector_buffer)) return FS_ERROR_IO;
    sector_buffer[entry.offset]=FAT32_DELETED_ENTRY;
    if(!block_device_write(entry.sector_lba,sector_buffer)) return FS_ERROR_IO;
    return clear_cluster_chain(entry.first_cluster);
}

int32_t fat32_rename(const char *path, const char *new_name){
    struct fat32_entry_ref entry;
    uint32_t parent;
    int32_t status=resolve_entry(path,&entry,&parent);
    if(status<0) return status;
    if(entry.attributes&FAT32_ATTRIBUTE_DIRECTORY) return FS_ERROR_NOT_FILE;
    if(entry.attributes&FAT32_ATTRIBUTE_READ_ONLY) return FS_ERROR_READ_ONLY;
    if(entry.has_lfn) return FS_ERROR_UNSUPPORTED;

    uint8_t short_name[11];
    if(!make_short_name(new_name,short_name)) return FS_ERROR_UNSUPPORTED;
    if(memcmp(short_name,entry.short_name,11)==0) return 0;
    status=find_entry(parent,short_name,0);
    if(status==0) return FS_ERROR_EXISTS;
    if(status!=FS_ERROR_NOT_FOUND) return status;

    if(!block_device_read(entry.sector_lba,sector_buffer)) return FS_ERROR_IO;
    memcpy(&sector_buffer[entry.offset],short_name,11);
    return block_device_write(entry.sector_lba,sector_buffer) ? 0 : FS_ERROR_IO;
}

int32_t fat32_move(const char *path, const char *destination_directory){
    struct fat32_entry_ref source;
    uint32_t source_parent;
    int32_t status=resolve_entry(path,&source,&source_parent);
    if(status<0) return status;
    if(source.attributes&FAT32_ATTRIBUTE_DIRECTORY) return FS_ERROR_NOT_FILE;
    if(source.attributes&FAT32_ATTRIBUTE_READ_ONLY) return FS_ERROR_READ_ONLY;
    if(source.has_lfn) return FS_ERROR_UNSUPPORTED;

    uint32_t destination_cluster;
    status=resolve_directory(destination_directory,&destination_cluster);
    if(status<0) return status;
    if(destination_cluster==source_parent) return 0;
    status=find_entry(destination_cluster,source.short_name,0);
    if(status==0) return FS_ERROR_EXISTS;
    if(status!=FS_ERROR_NOT_FOUND) return status;

    uint32_t destination_lba;
    uint16_t destination_offset;
    status=find_free_entry(destination_cluster,&destination_lba,&destination_offset);
    if(status<0) return status;

    if(!block_device_read(source.sector_lba,second_sector_buffer)) return FS_ERROR_IO;
    if(!block_device_read(destination_lba,sector_buffer)) return FS_ERROR_IO;
    memcpy(&sector_buffer[destination_offset],&second_sector_buffer[source.offset],32);
    if(!block_device_write(destination_lba,sector_buffer)) return FS_ERROR_IO;

    if(!block_device_read(source.sector_lba,sector_buffer)) return FS_ERROR_IO;
    sector_buffer[source.offset]=FAT32_DELETED_ENTRY;
    return block_device_write(source.sector_lba,sector_buffer) ? 0 : FS_ERROR_IO;
}

int32_t fat32_list(const char *path, struct fs_directory_entry *entries,
                   uint32_t capacity){
    if(!entries || capacity==0 || capacity>0x7FFFFFFF) return FS_ERROR_INVALID;

    uint32_t cluster;
    int32_t status=resolve_directory(path,&cluster);
    if(status<0) return status;

    uint32_t count=0;
    for(uint32_t visited=0;visited<volume.cluster_count;visited++){
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            if(!block_device_read(first_lba+sector,sector_buffer)) return FS_ERROR_IO;
            for(uint16_t offset=0;offset<BLOCK_SECTOR_SIZE;offset+=32){
                uint8_t first=sector_buffer[offset];
                if(first==0) return (int32_t)count;
                if(first==FAT32_DELETED_ENTRY) continue;

                uint8_t attributes=sector_buffer[offset+11];
                if(attributes==FAT32_ATTRIBUTE_LFN
                   || (attributes&FAT32_ATTRIBUTE_VOLUME_ID)
                   || first=='.'){
                    continue;
                }

                short_name_to_text(&sector_buffer[offset],entries[count].name);
                entries[count].size=read_u32(&sector_buffer[offset+28]);
                entries[count].attributes=attributes;
                count++;
                if(count==capacity) return (int32_t)count;
            }
        }

        uint32_t next;
        status=fat_next_cluster(cluster,&next);
        if(status<0) return status;
        if(next>=FAT32_END_OF_CHAIN) return (int32_t)count;
        if(!valid_cluster(next)) return FS_ERROR_INVALID;
        cluster=next;
    }
    return FS_ERROR_INVALID;
}

int32_t fat32_create_file(const char *path){
    uint32_t parent;
    uint8_t short_name[11];
    int32_t status=resolve_creation_parent(path,&parent,short_name);
    if(status<0) return status;

    status=find_entry(parent,short_name,0);
    if(status==0) return FS_ERROR_EXISTS;
    if(status!=FS_ERROR_NOT_FOUND) return status;
    return create_directory_entry(parent,short_name,FAT32_ATTRIBUTE_ARCHIVE,0);
}

static int32_t allocate_file_chain(uint32_t cluster_count,
                                   uint32_t *first_cluster){
    *first_cluster=0;
    uint32_t previous=0;
    for(uint32_t index=0;index<cluster_count;index++){
        uint32_t cluster;
        int32_t status=allocate_cluster(&cluster);
        if(status<0){
            if(*first_cluster) (void)clear_cluster_chain(*first_cluster);
            return status;
        }
        if(!*first_cluster) *first_cluster=cluster;
        if(previous){
            status=fat_write_entry(previous,cluster);
            if(status<0){
                (void)fat_write_entry(cluster,0);
                (void)clear_cluster_chain(*first_cluster);
                *first_cluster=0;
                return status;
            }
        }
        previous=cluster;
    }
    return 0;
}

static int32_t write_file_chain(uint32_t first_cluster, const uint8_t *data,
                                uint32_t count){
    uint32_t cluster=first_cluster;
    uint32_t written=0;
    while(written<count){
        if(!valid_cluster(cluster)) return FS_ERROR_INVALID;
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
            uint32_t amount=count-written;
            if(amount>BLOCK_SECTOR_SIZE) amount=BLOCK_SECTOR_SIZE;
            if(amount) memcpy(sector_buffer,data+written,amount);
            if(!block_device_write(first_lba+sector,sector_buffer)){
                return FS_ERROR_IO;
            }
            written+=amount;
            if(written==count) return 0;
        }
        uint32_t next;
        int32_t status=fat_next_cluster(cluster,&next);
        if(status<0) return status;
        if(!valid_cluster(next)) return FS_ERROR_INVALID;
        cluster=next;
    }
    return 0;
}

int32_t fat32_write_file(const char *path, const void *buffer, uint32_t count){
    if(!path || !path[0] || (!buffer && count) || count>0x7FFFFFFF){
        return FS_ERROR_INVALID;
    }

    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status==FS_ERROR_NOT_FOUND){
        status=fat32_create_file(path);
        if(status<0) return status;
        status=resolve_entry(path,&entry,0);
    }
    if(status<0) return status;
    if(entry.attributes&(FAT32_ATTRIBUTE_DIRECTORY|FAT32_ATTRIBUTE_VOLUME_ID)){
        return FS_ERROR_NOT_FILE;
    }
    if(entry.attributes&FAT32_ATTRIBUTE_READ_ONLY) return FS_ERROR_READ_ONLY;
    if(entry.has_lfn) return FS_ERROR_UNSUPPORTED;
    for(uint8_t index=0;index<FAT32_MAX_OPEN_FILES;index++){
        if(entry.first_cluster && handles[index].used
           && handles[index].first_cluster==entry.first_cluster){
            return FS_ERROR_BUSY;
        }
    }

    uint32_t cluster_size=(uint32_t)volume.sectors_per_cluster*BLOCK_SECTOR_SIZE;
    uint32_t required_clusters=(uint32_t)(((uint64_t)count+cluster_size-1)
                                          /cluster_size);
    if(required_clusters>volume.cluster_count) return FS_ERROR_NO_SPACE;

    uint32_t new_first_cluster;
    status=allocate_file_chain(required_clusters,&new_first_cluster);
    if(status<0) return status;
    if(count){
        status=write_file_chain(new_first_cluster,(const uint8_t*)buffer,count);
        if(status<0){
            (void)clear_cluster_chain(new_first_cluster);
            return status;
        }
    }

    if(!block_device_read(entry.sector_lba,sector_buffer)){
        (void)clear_cluster_chain(new_first_cluster);
        return FS_ERROR_IO;
    }
    write_u16(&sector_buffer[entry.offset+20],(uint16_t)(new_first_cluster>>16));
    write_u16(&sector_buffer[entry.offset+26],(uint16_t)new_first_cluster);
    write_u32(&sector_buffer[entry.offset+28],count);
    if(!block_device_write(entry.sector_lba,sector_buffer)){
        (void)clear_cluster_chain(new_first_cluster);
        return FS_ERROR_IO;
    }

    (void)clear_cluster_chain(entry.first_cluster);
    return (int32_t)count;
}

int32_t fat32_create_directory(const char *path){
    uint32_t parent;
    uint8_t short_name[11];
    int32_t status=resolve_creation_parent(path,&parent,short_name);
    if(status<0) return status;

    status=find_entry(parent,short_name,0);
    if(status==0) return FS_ERROR_EXISTS;
    if(status!=FS_ERROR_NOT_FOUND) return status;

    uint32_t directory_cluster;
    status=allocate_cluster(&directory_cluster);
    if(status<0) return status;

    static const uint8_t dot_name[11]={'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    static const uint8_t dot_dot_name[11]={'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};
    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    fill_directory_entry(&sector_buffer[0],dot_name,FAT32_ATTRIBUTE_DIRECTORY,
                         directory_cluster);
    fill_directory_entry(&sector_buffer[32],dot_dot_name,FAT32_ATTRIBUTE_DIRECTORY,parent);
    if(!block_device_write(cluster_lba(directory_cluster),sector_buffer)){
        (void)fat_write_entry(directory_cluster,0);
        return FS_ERROR_IO;
    }

    status=create_directory_entry(parent,short_name,FAT32_ATTRIBUTE_DIRECTORY,
                                  directory_cluster);
    if(status<0) (void)fat_write_entry(directory_cluster,0);
    return status;
}

static bool sector_is_zero(const uint8_t *sector){
    for(uint16_t index=0;index<BLOCK_SECTOR_SIZE;index++){
        if(sector[index]!=0) return false;
    }
    return true;
}

static int32_t verify_blank_device(uint32_t total_sectors){
    uint32_t scan_count=total_sectors<FAT32_FORMAT_BLANK_SCAN
        ? total_sectors : FAT32_FORMAT_BLANK_SCAN;
    for(uint32_t lba=0;lba<scan_count;lba++){
        if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
        if(!sector_is_zero(sector_buffer)) return FS_ERROR_NOT_BLANK;
    }
    if(total_sectors>scan_count){
        if(!block_device_read(total_sectors-1,sector_buffer)) return FS_ERROR_IO;
        if(!sector_is_zero(sector_buffer)) return FS_ERROR_NOT_BLANK;
    }
    return 0;
}

static bool calculate_format_layout(uint32_t total_sectors,
                                    struct fat32_format_layout *layout){
    if(!layout || total_sectors<65590 || total_sectors>FAT32_FORMAT_MAX_SECTORS){
        return false;
    }

    uint8_t sectors_per_cluster;
    if(total_sectors<=532480) sectors_per_cluster=1;
    else if(total_sectors<=16777216) sectors_per_cluster=8;
    else if(total_sectors<=33554432) sectors_per_cluster=16;
    else if(total_sectors<=67108864) sectors_per_cluster=32;
    else sectors_per_cluster=64;

    uint32_t fat_size=1;
    uint32_t cluster_count=0;
    for(uint8_t iteration=0;iteration<32;iteration++){
        uint64_t overhead=FAT32_FORMAT_RESERVED_SECTORS
            +(uint64_t)FAT32_FORMAT_FAT_COUNT*fat_size;
        if(overhead>=total_sectors) return false;
        cluster_count=(uint32_t)((total_sectors-overhead)/sectors_per_cluster);
        uint32_t required=(uint32_t)(((uint64_t)(cluster_count+2)*4
                                      +BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE);
        if(required==fat_size) break;
        fat_size=required;
    }
    if(cluster_count<65525 || cluster_count>0x0FFFFFF5) return false;

    layout->total_sectors=total_sectors;
    layout->fat_size=fat_size;
    layout->cluster_count=cluster_count;
    layout->sectors_per_cluster=sectors_per_cluster;
    return true;
}

static bool write_zero_range(uint32_t first_lba, uint32_t count){
    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    for(uint32_t index=0;index<count;index++){
        if(!block_device_write(first_lba+index,sector_buffer)) return false;
    }
    return true;
}

static void build_format_boot_sector(const struct fat32_format_layout *layout){
    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    sector_buffer[0]=0xEB;
    sector_buffer[1]=0x58;
    sector_buffer[2]=0x90;
    memcpy(&sector_buffer[3],"PURECOS ",8);
    write_u16(&sector_buffer[11],BLOCK_SECTOR_SIZE);
    sector_buffer[13]=layout->sectors_per_cluster;
    write_u16(&sector_buffer[14],FAT32_FORMAT_RESERVED_SECTORS);
    sector_buffer[16]=FAT32_FORMAT_FAT_COUNT;
    sector_buffer[21]=0xF8;
    write_u16(&sector_buffer[24],63);
    write_u16(&sector_buffer[26],255);
    write_u32(&sector_buffer[32],layout->total_sectors);
    write_u32(&sector_buffer[36],layout->fat_size);
    write_u32(&sector_buffer[44],2);
    write_u16(&sector_buffer[48],1);
    write_u16(&sector_buffer[50],6);
    sector_buffer[64]=0x80;
    sector_buffer[66]=0x29;
    write_u32(&sector_buffer[67],0x50555245);
    memcpy(&sector_buffer[71],required_volume_label,11);
    memcpy(&sector_buffer[82],"FAT32   ",8);
    sector_buffer[510]=0x55;
    sector_buffer[511]=0xAA;
}

static bool write_format_metadata(const struct fat32_format_layout *layout){
    uint32_t fat_start=FAT32_FORMAT_RESERVED_SECTORS;
    uint32_t data_start=fat_start+FAT32_FORMAT_FAT_COUNT*layout->fat_size;
    if(!write_zero_range(0,FAT32_FORMAT_RESERVED_SECTORS)) return false;
    if(!write_zero_range(fat_start,FAT32_FORMAT_FAT_COUNT*layout->fat_size)){
        return false;
    }
    if(!write_zero_range(data_start,layout->sectors_per_cluster)) return false;

    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    write_u32(&sector_buffer[0],0x0FFFFFF8);
    write_u32(&sector_buffer[4],0xFFFFFFFF);
    write_u32(&sector_buffer[8],0x0FFFFFFF);
    for(uint8_t fat=0;fat<FAT32_FORMAT_FAT_COUNT;fat++){
        uint32_t lba=fat_start+(uint32_t)fat*layout->fat_size;
        if(!block_device_write(lba,sector_buffer)) return false;
    }

    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    write_u32(&sector_buffer[0],0x41615252);
    write_u32(&sector_buffer[484],0x61417272);
    write_u32(&sector_buffer[488],layout->cluster_count-1);
    write_u32(&sector_buffer[492],3);
    write_u32(&sector_buffer[508],0xAA550000);
    if(!block_device_write(1,sector_buffer)
       || !block_device_write(7,sector_buffer)){
        return false;
    }

    build_format_boot_sector(layout);
    if(!block_device_write(6,sector_buffer)) return false;
    return block_device_write(0,sector_buffer);
}

int32_t fat32_format_device(const char *device_name,
                            const char *serial_confirmation,
                            const char *erase_confirmation){
    if(!device_name || !device_name[0] || !serial_confirmation
       || !erase_confirmation){
        klogf(KLOG_ERROR,"fat32_format: INVALID args dev='%s'",device_name?device_name:"(null)");
        return FS_ERROR_INVALID;
    }
    // Разрешаем форматировать другой диск даже когда примонтирован текущий том.
    // BUSY только если цель совпадает с примонтированным.
    if(volume.mounted){
        const char *mounted=block_device_name();
        if(mounted && strcmp(mounted,device_name)==0){
            klogf(KLOG_WARN,"fat32_format: BUSY mounted='%s' target='%s'",mounted,device_name);
            return FS_ERROR_BUSY;
        }
        klogf(KLOG_INFO,"fat32_format: volume mounted on '%s', formatting other dev '%s' (unmount not needed)",mounted?mounted:"?",device_name);
    }

    int32_t device_index=block_device_find(device_name);
    if(device_index<0){
        klogf(KLOG_ERROR,"fat32_format: NOT_FOUND '%s'",device_name);
        return FS_ERROR_NOT_FOUND;
    }
    struct storage_device_info info;
    if(!block_device_get_info((uint32_t)device_index,&info)){
        klogf(KLOG_ERROR,"fat32_format: cannot get info for '%s' idx=%d",device_name,device_index);
        return FS_ERROR_INVALID;
    }
    klogf(KLOG_INFO,"fat32_format: dev='%s' serial='%s' model='%s' sectors=%llu ss=%u op=%u wr=%u transport=%u",
          info.name,info.serial,info.model,info.sector_count,info.sector_size,info.operational,info.writable,info.transport);
    if(!info.operational || !info.writable){
        klogf(KLOG_ERROR,"fat32_format: READ_ONLY op=%u wr=%u",info.operational,info.writable);
        return FS_ERROR_READ_ONLY;
    }
    if(!info.serial[0] || strcmp(info.serial,serial_confirmation)!=0
       || strcmp(erase_confirmation,"ERASE")!=0){
        klogf(KLOG_WARN,"fat32_format: CONFIRMATION failed serial='%s' expected='%s' erase='%s'",serial_confirmation,info.serial,erase_confirmation);
        return FS_ERROR_CONFIRMATION;
    }
    if(info.sector_size!=BLOCK_SECTOR_SIZE
       || info.sector_count>FAT32_FORMAT_MAX_SECTORS){
        klogf(KLOG_ERROR,"fat32_format: UNSUPPORTED ss=%u expected %u sectors=%llu max=%u",
              info.sector_size,BLOCK_SECTOR_SIZE,info.sector_count,FAT32_FORMAT_MAX_SECTORS);
        return FS_ERROR_UNSUPPORTED;
    }

    struct fat32_format_layout layout;
    if(!calculate_format_layout((uint32_t)info.sector_count,&layout)){
        klogf(KLOG_ERROR,"fat32_format: TOO_SMALL sectors=%llu",info.sector_count);
        return FS_ERROR_TOO_SMALL;
    }
    klogf(KLOG_INFO,"fat32_format: layout sectors=%u fat=%u clusters=%u spc=%u",
          layout.total_sectors,layout.fat_size,layout.cluster_count,layout.sectors_per_cluster);
    if(!block_device_select((uint32_t)device_index)){
        klogf(KLOG_ERROR,"fat32_format: SELECT failed idx=%d",device_index);
        return FS_ERROR_INVALID;
    }
    int32_t status=verify_blank_device(layout.total_sectors);
    if(status<0){
        if(status==FS_ERROR_NOT_BLANK) klogf(KLOG_WARN,"fat32_format: NOT_BLANK dev='%s' (use --force or zero disk)",device_name);
        else klogf(KLOG_ERROR,"fat32_format: verify_blank failed %d",status);
        return status;
    }
    if(!write_format_metadata(&layout)){
        klogf(KLOG_ERROR,"fat32_format: IO write metadata failed");
        return FS_ERROR_IO;
    }

    memset(&volume,0,sizeof(volume));
    memset(handles,0,sizeof(handles));
    bool mounted=fat32_init();
    klogf(mounted?KLOG_OK:KLOG_ERROR,"fat32_format: %s dev='%s' mount=%u",mounted?"formatted and mounted":"mount after format failed",device_name,mounted);
    return mounted ? 0 : FS_ERROR_IO;
}

// Внутренняя версия с force (игнорирует NOT_BLANK)
int32_t fat32_format_device_force(const char *device_name, const char *serial_confirmation){
    if(!device_name || !serial_confirmation) return FS_ERROR_INVALID;
    // force = ERASE уже подтверждён инсталлером
    int32_t device_index=block_device_find(device_name);
    if(device_index<0) return FS_ERROR_NOT_FOUND;
    struct storage_device_info info;
    if(!block_device_get_info((uint32_t)device_index,&info)) return FS_ERROR_INVALID;
    if(!info.operational || !info.writable) return FS_ERROR_READ_ONLY;
    if(info.sector_size!=BLOCK_SECTOR_SIZE || info.sector_count>FAT32_FORMAT_MAX_SECTORS) return FS_ERROR_UNSUPPORTED;
    struct fat32_format_layout layout;
    if(!calculate_format_layout((uint32_t)info.sector_count,&layout)) return FS_ERROR_TOO_SMALL;
    if(!block_device_select((uint32_t)device_index)) return FS_ERROR_INVALID;
    // пропускаем verify_blank - инсталлер хочет перезаписать
    klogf(KLOG_WARN,"fat32_format_force: skipping blank check dev='%s' sectors=%u",device_name,layout.total_sectors);
    if(!write_format_metadata(&layout)) return FS_ERROR_IO;
    memset(&volume,0,sizeof(volume));
    memset(handles,0,sizeof(handles));
    return fat32_init()?0:FS_ERROR_IO;
}

static bool write_mbr_esp(uint32_t total_sectors){
    if(total_sectors <= FAT32_ESP_START_LBA){
        klogf(KLOG_ERROR,"write_mbr_esp: total %u too small",total_sectors);
        return false;
    }
    uint32_t part_sectors = total_sectors - FAT32_ESP_START_LBA;
    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    // Копируем BIOS boot code из limine_vbr (первые 440 байт) для dual BIOS+UEFI загрузки
    // limine_vbr 512б содержит MBR код для superfloppy, берём 0..439
    for(uint32_t i=0;i<440;i++) sector_buffer[i]=limine_vbr[i];
    // Перезатираем BPB (3..61) нулями т.к. это MBR, не VBR
    for(uint32_t i=3;i<62;i++) sector_buffer[i]=0;
    // MBR signature будет перезаписан ниже
    uint8_t *p = &sector_buffer[446];
    p[0]=0x80; // bootable для BIOS
    p[1]=0x00; p[2]=0x02; p[3]=0x00;
    p[4]=0xEF;
    p[5]=0xFF; p[6]=0xFF; p[7]=0xFF;
    write_u32(&p[8], FAT32_ESP_START_LBA);
    write_u32(&p[12], part_sectors);
    sector_buffer[510]=0x55; sector_buffer[511]=0xAA;
    klogf(KLOG_INFO,"write_mbr_esp: writing MBR LBA0 part %u sectors %u (BIOS+UEFI)",FAT32_ESP_START_LBA,part_sectors);
    if(!block_device_write(0, sector_buffer)){
        klogf(KLOG_ERROR,"write_mbr_esp: block_device_write LBA0 failed (dev %s)",block_device_name());
        return false;
    }
    // Verify readback
    uint8_t verify[BLOCK_SECTOR_SIZE];
    if(!block_device_read(0, verify) || verify[510]!=0x55 || verify[511]!=0xAA){
        klogf(KLOG_WARN,"write_mbr_esp: verify failed");
    }
    return true;
}

static bool write_format_metadata_at(uint32_t part_lba, const struct fat32_format_layout *layout){
    uint32_t fat_start = part_lba + FAT32_ESP_RESERVED;
    uint32_t data_start = fat_start + FAT32_FORMAT_FAT_COUNT * layout->fat_size;
    klogf(KLOG_INFO,"write_format_at: part %u fat %u data %u fat_size %u spc %u",part_lba,fat_start,data_start,layout->fat_size,layout->sectors_per_cluster);
    if(!write_zero_range(part_lba, FAT32_ESP_RESERVED)){
        klogf(KLOG_ERROR,"write_format_at: zero reserved %u count %u failed",part_lba,FAT32_ESP_RESERVED);
        return false;
    }
    if(!write_zero_range(fat_start, FAT32_FORMAT_FAT_COUNT * layout->fat_size)){
        klogf(KLOG_ERROR,"write_format_at: zero FAT %u count %u failed",fat_start,FAT32_FORMAT_FAT_COUNT*layout->fat_size);
        return false;
    }
    if(!write_zero_range(data_start, layout->sectors_per_cluster)){
        klogf(KLOG_ERROR,"write_format_at: zero data %u failed",data_start);
        return false;
    }

    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    write_u32(&sector_buffer[0],0x0FFFFFF8);
    write_u32(&sector_buffer[4],0xFFFFFFFF);
    write_u32(&sector_buffer[8],0x0FFFFFFF);
    for(uint8_t fat=0;fat<FAT32_FORMAT_FAT_COUNT;fat++){
        uint32_t lba = fat_start + (uint32_t)fat * layout->fat_size;
        if(!block_device_write(lba, sector_buffer)){
            klogf(KLOG_ERROR,"write_format_at: FAT%u LBA %u failed",fat,lba);
            return false;
        }
    }
    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    write_u32(&sector_buffer[0],0x41615252);
    write_u32(&sector_buffer[484],0x61417272);
    write_u32(&sector_buffer[488],layout->cluster_count-1);
    write_u32(&sector_buffer[492],3);
    write_u32(&sector_buffer[508],0xAA550000);
    if(!block_device_write(part_lba+1, sector_buffer)){
        klogf(KLOG_ERROR,"write_format_at: FSInfo LBA %u failed",part_lba+1);
        return false;
    }
    if(!block_device_write(part_lba+7, sector_buffer)){
        klogf(KLOG_ERROR,"write_format_at: FSInfo backup LBA %u failed",part_lba+7);
        return false;
    }

    // Boot sector at part_lba
    build_format_boot_sector(layout);
    if(!block_device_write(part_lba+6, sector_buffer)){
        klogf(KLOG_ERROR,"write_format_at: backup boot LBA %u failed",part_lba+6);
        return false;
    }
    if(!block_device_write(part_lba, sector_buffer)){
        klogf(KLOG_ERROR,"write_format_at: boot LBA %u failed",part_lba);
        return false;
    }
    klogf(KLOG_OK,"write_format_at: OK part %u",part_lba);
    return true;
}

// UEFI install: MBR ESP + FAT32 + файлы загрузчика/ядра
int32_t fat32_format_uefi_device(const char *device_name, const char *serial_confirmation){
    if(!device_name || !serial_confirmation) return FS_ERROR_INVALID;
    int32_t idx=block_device_find(device_name);
    if(idx<0) return FS_ERROR_NOT_FOUND;
    struct storage_device_info info;
    if(!block_device_get_info((uint32_t)idx,&info)) return FS_ERROR_INVALID;
    if(!info.operational || !info.writable) return FS_ERROR_READ_ONLY;
    if(info.sector_size!=BLOCK_SECTOR_SIZE
       || info.sector_count>FAT32_FORMAT_MAX_SECTORS){
        klogf(KLOG_ERROR,"fat32_uefi: UNSUPPORTED ss=%u expected %u sectors=%llu max=%u",
              info.sector_size,BLOCK_SECTOR_SIZE,info.sector_count,
              FAT32_FORMAT_MAX_SECTORS);
        return FS_ERROR_UNSUPPORTED;
    }
    if(info.sector_count <= FAT32_ESP_START_LBA+65535) return FS_ERROR_TOO_SMALL; // нужен минимум ~32MB ESP
    uint32_t part_sectors = (uint32_t)info.sector_count - FAT32_ESP_START_LBA;
    struct fat32_format_layout layout;
    if(!calculate_format_layout(part_sectors,&layout)) return FS_ERROR_TOO_SMALL;
    if(!block_device_select((uint32_t)idx)) return FS_ERROR_INVALID;
    klogf(KLOG_INFO,"fat32_uefi: formatting %s total %u part %u fat %u clusters %u spc %u",
          device_name,(uint32_t)info.sector_count,part_sectors,layout.fat_size,layout.cluster_count,layout.sectors_per_cluster);
    // Создаём MBR ESP
    if(!write_mbr_esp((uint32_t)info.sector_count)){
        klogf(KLOG_ERROR,"fat32_uefi: MBR write failed");
        return FS_ERROR_IO;
    }
    // Форматируем партицию
    if(!write_format_metadata_at(FAT32_ESP_START_LBA,&layout)){
        klogf(KLOG_ERROR,"fat32_uefi: FAT write failed");
        return FS_ERROR_IO;
    }
    memset(&volume,0,sizeof(volume));
    memset(handles,0,sizeof(handles));
    if(!fat32_init()){
        klogf(KLOG_ERROR,"fat32_uefi: mount after format failed");
        return FS_ERROR_IO;
    }
    klogf(KLOG_OK,"fat32_uefi: formatted ESP %s part_lba %u",device_name,FAT32_ESP_START_LBA);
    // Создаём структуру для UEFI: /EFI/BOOT/BOOTX64.EFI + /boot/kernel.elf + limine.conf
    // Директории
    (void)fat32_create_directory("/EFI");
    (void)fat32_create_directory("/EFI/BOOT");
    (void)fat32_create_directory("/boot");
    (void)fat32_create_directory("/boot/limine");
    // Пишем BOOTX64.EFI
    {
        int32_t st = fat32_write_file("/EFI/BOOT/BOOTX64.EFI", limine_uefi, limine_uefi_len);
        if(st<0) klogf(KLOG_WARN,"fat32_uefi: BOOTX64.EFI write failed %d",st);
        else klogf(KLOG_OK,"fat32_uefi: BOOTX64.EFI %u bytes written",limine_uefi_len);
    }
    // Пишем kernel
    {
        int32_t st = fat32_write_file("/boot/kernel.elf", kernel_blob, kernel_blob_len);
        if(st<0) klogf(KLOG_WARN,"fat32_uefi: kernel write failed %d",st);
        else klogf(KLOG_OK,"fat32_uefi: kernel %u bytes written",kernel_blob_len);
    }
    // limine.cfg - используем 8.3 валидное имя (limine.conf требует LFN, наш драйвер 8.3)
    {
        const char *conf="timeout: 10\nverbose: yes\n/PureC OS (UEFI)\n    protocol: limine\n    kernel_path: boot():/boot/kernel.elf\n";
        int32_t st = fat32_write_file("/boot/limine/limine.cfg", conf, strlen(conf));
        if(st<0) klogf(KLOG_WARN,"fat32_uefi: limine.cfg write failed %d",st);
        else klogf(KLOG_OK,"fat32_uefi: limine.cfg written");
        (void)fat32_write_file("/limine.cfg", conf, strlen(conf));
        // также пробуем limine.conf через LFN обход (если драйвер позволит 4-симв ext)
        // Для совместимости с Limine который ищет limine.conf - создаём копию с коротким именем limine~1.conf через прямой сектор (если нужно)
        // Пока оставляем limine.cfg, Limine также ищет limine.cfg как fallback
    }
    return 0;
}
