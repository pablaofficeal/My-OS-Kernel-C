#include "fat32.h"

#include "../drivers/storage/block_device.h"
#include "../kernel/klog.h"
#include "../kernel/scheduler.h"
#include "../lib/string.h"
#include <stddef.h>
#include "../boot/install_source.h"

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
#define FAT32_ESP_SECTORS (512U*1024U*1024U/BLOCK_SECTOR_SIZE)
#define FAT32_ESP_RESERVED FAT32_FORMAT_RESERVED_SECTORS
#define FAT32_LFN_CHARACTER_CAPACITY 13
#define GPT_ENTRY_COUNT 128
#define GPT_ENTRY_SIZE 128
#define GPT_ENTRY_SECTORS 32
#define GPT_HEADER_SIZE 92

static const uint8_t required_volume_label[11]={
    'P','U','R','E','C','O','S',' ',' ',' ',' '
};

static const uint8_t esp_volume_label[11]={
    'P','U','R','E','C','-','E','S','P',' ',' '
};

static const uint8_t gpt_esp_type_guid[16]={
    0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,
    0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B
};

static const uint8_t gpt_basic_data_type_guid[16]={
    0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,
    0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7
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

static uint8_t lfn_checksum(const uint8_t short_name[11]);

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

static uint64_t read_u64(const uint8_t *data){
    return (uint64_t)read_u32(data)|((uint64_t)read_u32(data+4)<<32);
}

static void write_u64(uint8_t *data, uint64_t value){
    write_u32(data,(uint32_t)value);
    write_u32(data+4,(uint32_t)(value>>32));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size){
    for(uint32_t index=0;index<size;index++){
        crc^=data[index];
        for(uint8_t bit=0;bit<8;bit++){
            uint32_t mask=(uint32_t)-(int32_t)(crc&1U);
            crc=(crc>>1)^(0xEDB88320U&mask);
        }
    }
    return crc;
}

static uint32_t crc32(const uint8_t *data, uint32_t size){
    return ~crc32_update(0xFFFFFFFFU,data,size);
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

static bool lfn_entry_matches(const uint8_t *entry, const char *component){
    static const uint8_t offsets[FAT32_LFN_CHARACTER_CAPACITY]={
        1,3,5,7,9,14,16,18,20,22,24,28,30
    };
    if(entry[0]!=0x41 || entry[11]!=FAT32_ATTRIBUTE_LFN
       || entry[12]!=0 || read_u16(&entry[26])!=0){
        return false;
    }
    for(uint8_t index=0;index<FAT32_LFN_CHARACTER_CAPACITY;index++){
        uint16_t character=read_u16(&entry[offsets[index]]);
        if(character==0) return component[index]=='\0';
        if(character==0xFFFF || component[index]=='\0'
           || character!=(uint8_t)component[index]){
            return false;
        }
    }
    return component[FAT32_LFN_CHARACTER_CAPACITY]=='\0';
}

static int32_t find_lfn_entry(uint32_t directory_cluster, const char *component,
                              struct fat32_entry_ref *result){
    if(!valid_cluster(directory_cluster)) return FS_ERROR_NOT_DIR;
    uint32_t cluster=directory_cluster;
    bool pending_match=false;
    uint8_t pending_checksum=0;

    for(uint32_t visited=0;visited<volume.cluster_count;visited++){
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            uint32_t lba=first_lba+sector;
            if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
            for(uint16_t offset=0;offset<BLOCK_SECTOR_SIZE;offset+=32){
                uint8_t first=sector_buffer[offset];
                if(first==0) return FS_ERROR_NOT_FOUND;
                if(first==FAT32_DELETED_ENTRY){
                    pending_match=false;
                    continue;
                }
                uint8_t attributes=sector_buffer[offset+11];
                if(attributes==FAT32_ATTRIBUTE_LFN){
                    pending_match=lfn_entry_matches(&sector_buffer[offset],component);
                    pending_checksum=sector_buffer[offset+13];
                    continue;
                }
                if(pending_match
                   && pending_checksum==lfn_checksum(&sector_buffer[offset])){
                    if(result){
                        result->sector_lba=lba;
                        result->offset=offset;
                        result->attributes=attributes;
                        result->first_cluster=
                            ((uint32_t)read_u16(&sector_buffer[offset+20])<<16)
                            |read_u16(&sector_buffer[offset+26]);
                        result->size=read_u32(&sector_buffer[offset+28]);
                        memcpy(result->short_name,&sector_buffer[offset],11);
                        result->has_lfn=true;
                    }
                    return 0;
                }
                pending_match=false;
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
        struct fat32_entry_ref entry;
        int32_t status=make_short_name(component,short_name)
            ? find_entry(directory,short_name,&entry)
            : find_lfn_entry(directory,component,&entry);
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
        if((cluster&0xFFU)==0) scheduler_yield();
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

static uint8_t lfn_checksum(const uint8_t short_name[11]){
    uint8_t checksum=0;
    for(uint8_t index=0;index<11;index++){
        checksum=(uint8_t)(((checksum&1)<<7)|(checksum>>1));
        checksum=(uint8_t)(checksum+short_name[index]);
    }
    return checksum;
}

static void write_lfn_character(uint8_t *entry, uint8_t index, uint16_t character){
    static const uint8_t offsets[FAT32_LFN_CHARACTER_CAPACITY]={
        1,3,5,7,9,14,16,18,20,22,24,28,30
    };
    write_u16(&entry[offsets[index]],character);
}

static bool fill_lfn_entry(uint8_t *entry, const char *long_name,
                           const uint8_t short_name[11]){
    uint8_t length=0;
    while(long_name[length]){
        if(length>=FAT32_LFN_CHARACTER_CAPACITY) return false;
        length++;
    }
    if(length==0) return false;

    memset(entry,0xFF,32);
    entry[0]=0x41;
    entry[11]=FAT32_ATTRIBUTE_LFN;
    entry[12]=0;
    entry[13]=lfn_checksum(short_name);
    write_u16(&entry[26],0);
    for(uint8_t index=0;index<FAT32_LFN_CHARACTER_CAPACITY;index++){
        uint16_t character=index<length ? (uint8_t)long_name[index]
            : index==length ? 0 : 0xFFFF;
        write_lfn_character(entry,index,character);
    }
    return true;
}

static int32_t find_free_entry_pair(uint32_t directory_cluster,
                                    uint32_t *lba_result,
                                    uint16_t *offset_result){
    uint32_t cluster=directory_cluster;
    for(uint32_t visited=0;visited<volume.cluster_count;visited++){
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            uint32_t lba=first_lba+sector;
            if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
            for(uint16_t offset=0;offset+64<=BLOCK_SECTOR_SIZE;offset+=32){
                uint8_t first=sector_buffer[offset];
                uint8_t second=sector_buffer[offset+32];
                bool first_free=first==0 || first==FAT32_DELETED_ENTRY;
                bool second_free=first==0 || second==0
                    || second==FAT32_DELETED_ENTRY;
                if(first_free && second_free){
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

static int32_t create_lfn_file_entry(uint32_t parent, const char *long_name,
                                     const uint8_t short_name[11]){
    int32_t status=find_entry(parent,short_name,0);
    if(status==0) return FS_ERROR_EXISTS;
    if(status!=FS_ERROR_NOT_FOUND) return status;

    uint32_t lba;
    uint16_t offset;
    status=find_free_entry_pair(parent,&lba,&offset);
    if(status<0) return status;
    if(!block_device_read(lba,sector_buffer)) return FS_ERROR_IO;
    bool was_end_marker=sector_buffer[offset]==0;
    if(!fill_lfn_entry(&sector_buffer[offset],long_name,short_name)){
        return FS_ERROR_UNSUPPORTED;
    }
    fill_directory_entry(&sector_buffer[offset+32],short_name,
                         FAT32_ATTRIBUTE_ARCHIVE,0);
    if(was_end_marker && offset+64<BLOCK_SECTOR_SIZE) sector_buffer[offset+64]=0;
    return block_device_write(lba,sector_buffer) ? 0 : FS_ERROR_IO;
}

static int32_t write_lfn_file(const char *directory_path, const char *long_name,
                              const char *alias_path, const char *alias_name,
                              const void *buffer, uint32_t count){
    uint32_t parent;
    int32_t status=resolve_directory(directory_path,&parent);
    if(status<0) return status;
    uint8_t short_name[11];
    if(!make_short_name(alias_name,short_name)) return FS_ERROR_INVALID;
    status=create_lfn_file_entry(parent,long_name,short_name);
    if(status<0 && status!=FS_ERROR_EXISTS) return status;
    return fat32_write_file(alias_path,buffer,count);
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
    if(memcmp(&sector_buffer[71],required_volume_label,11)!=0
       && memcmp(&sector_buffer[71],esp_volume_label,11)!=0){
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

static bool mount_gpt_partition(const uint8_t type_guid[16]){
    if(!block_device_read(1,sector_buffer)) return false;
    if(memcmp(sector_buffer,"EFI PART",8)!=0) return false;

    uint64_t entries_lba64=read_u64(&sector_buffer[72]);
    uint32_t entry_count=read_u32(&sector_buffer[80]);
    uint32_t entry_size=read_u32(&sector_buffer[84]);
    if(entries_lba64>UINT32_MAX || entry_count==0 || entry_count>GPT_ENTRY_COUNT
       || entry_size!=GPT_ENTRY_SIZE){
        return false;
    }

    uint32_t entries_lba=(uint32_t)entries_lba64;
    uint32_t entries_per_sector=BLOCK_SECTOR_SIZE/GPT_ENTRY_SIZE;
    for(uint32_t index=0;index<entry_count;index++){
        if(index%entries_per_sector==0){
            uint32_t entry_sector=entries_lba+index/entries_per_sector;
            if(!block_device_read(entry_sector,second_sector_buffer)) return false;
        }
        uint32_t offset=(index%entries_per_sector)*GPT_ENTRY_SIZE;
        if(memcmp(&second_sector_buffer[offset],type_guid,16)!=0) continue;
        uint64_t first_lba=read_u64(&second_sector_buffer[offset+32]);
        if(first_lba>UINT32_MAX) continue;
        if(mount_boot_sector((uint32_t)first_lba)) return true;
    }
    return false;
}

bool fat32_init(void){
    if(volume.mounted) return true;
    if(!block_device_init()) return false;

    uint32_t disk_count=block_device_count();
    for(uint32_t disk=0;disk<disk_count;disk++){
        if(!block_device_select(disk) || !block_device_read(0,sector_buffer)) continue;

        // A normal UEFI installation keeps system data outside the ESP.
        if(mount_gpt_partition(gpt_basic_data_type_guid)) return true;
        if(!block_device_read(0,sector_buffer)) continue;

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
        return 0;
    }

    uint8_t *output=(uint8_t*)buffer;
    uint32_t total_read=0;
    uint32_t cluster_size=(uint32_t)volume.sectors_per_cluster*BLOCK_SECTOR_SIZE;
    uint32_t yield_counter=0;
    while(total_read<count && handle->position<handle->size){
        if((yield_counter++ & 0x3)==0) scheduler_yield();
        uint32_t cluster=handle->current_cluster;
        if(!valid_cluster(cluster)){
            return FS_ERROR_INVALID;
        }
        uint32_t target_cluster_index=handle->position/cluster_size;
        while(handle->cluster_index<target_cluster_index){
            uint32_t next;
            int32_t status=fat_next_cluster(cluster,&next);
            if(status<0) return status;
            if(!valid_cluster(next)){
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

int32_t fat32_close(int32_t descriptor){
    int32_t index=descriptor-FAT32_DESCRIPTOR_BASE;
    if(index<0 || index>=FAT32_MAX_OPEN_FILES || !handles[index].used)
        return FS_ERROR_INVALID;
    handles[index].used=false;
    return 0;
}

int32_t fat32_delete(const char *path){
    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status<0) return status;
    if(entry.attributes&FAT32_ATTRIBUTE_READ_ONLY) return FS_ERROR_READ_ONLY;
    if(entry.has_lfn) return FS_ERROR_UNSUPPORTED;
    if(entry.attributes&FAT32_ATTRIBUTE_DIRECTORY){
        struct fs_directory_entry child;
        status=fat32_list(path,&child,1);
        if(status<0) return status;
        if(status>0) return FS_ERROR_NOT_BLANK;
    } else {
        for(uint8_t index=0;index<FAT32_MAX_OPEN_FILES;index++){
            if(entry.first_cluster!=0 && handles[index].used
               && handles[index].first_cluster==entry.first_cluster){
                return FS_ERROR_BUSY;
            }
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
    uint32_t sectors_written=0;
    while(written<count){
        if(!valid_cluster(cluster)) return FS_ERROR_INVALID;
        uint32_t first_lba=cluster_lba(cluster);
        for(uint8_t sector=0;sector<volume.sectors_per_cluster;sector++){
            if((sectors_written++&0x0FU)==0) scheduler_yield();
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

int32_t fat32_append_file(const char *path, const void *buffer, uint32_t count){
    if(!path || !path[0] || (!buffer && count) || count>0x7FFFFFFF) return FS_ERROR_INVALID;

    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status==FS_ERROR_NOT_FOUND){
        status=fat32_create_file(path);
        if(status<0) return status;
        status=resolve_entry(path,&entry,0);
    }
    if(status<0) return status;
    if(entry.attributes&(FAT32_ATTRIBUTE_DIRECTORY|FAT32_ATTRIBUTE_VOLUME_ID)) return FS_ERROR_NOT_FILE;
    if(entry.attributes&FAT32_ATTRIBUTE_READ_ONLY) return FS_ERROR_READ_ONLY;
    if(!count) return 0;
    if((uint64_t)entry.size+count>0xFFFFFFFFULL) return FS_ERROR_NO_SPACE;
    for(uint8_t index=0;index<FAT32_MAX_OPEN_FILES;index++){
        if(entry.first_cluster && handles[index].used
           && handles[index].first_cluster==entry.first_cluster) return FS_ERROR_BUSY;
    }

    uint32_t cluster_size=(uint32_t)volume.sectors_per_cluster*BLOCK_SECTOR_SIZE;
    uint32_t old_clusters=(uint32_t)(((uint64_t)entry.size+cluster_size-1)
                                     /cluster_size);
    uint32_t new_size=entry.size+count;
    uint32_t required_clusters=(uint32_t)(((uint64_t)new_size+cluster_size-1)
                                          /cluster_size);
    uint32_t cluster=entry.first_cluster;

    if(old_clusters==0){
        status=allocate_cluster(&cluster);
        if(status<0) return status;
        entry.first_cluster=cluster;
    } else {
        for(uint32_t index=1;index<old_clusters;index++){
            uint32_t next;
            status=fat_next_cluster(cluster,&next);
            if(status<0 || !valid_cluster(next)) return status<0 ? status : FS_ERROR_INVALID;
            cluster=next;
        }
    }
    for(uint32_t index=old_clusters;index<required_clusters;index++){
        uint32_t next;
        status=allocate_cluster(&next);
        if(status<0) return status;
        status=fat_write_entry(cluster,next);
        if(status<0) return status;
        cluster=next;
    }

    cluster=entry.first_cluster;
    uint32_t target_index=entry.size/cluster_size;
    for(uint32_t index=0;index<target_index;index++){
        uint32_t next;
        status=fat_next_cluster(cluster,&next);
        if(status<0 || !valid_cluster(next)) return status<0 ? status : FS_ERROR_INVALID;
        cluster=next;
    }

    const uint8_t *data=(const uint8_t*)buffer;
    uint32_t written=0;
    uint32_t offset_in_cluster=entry.size%cluster_size;
    while(written<count){
        uint32_t sector=offset_in_cluster/BLOCK_SECTOR_SIZE;
        uint32_t offset=offset_in_cluster%BLOCK_SECTOR_SIZE;
        uint32_t amount=BLOCK_SECTOR_SIZE-offset;
        if(amount>count-written) amount=count-written;
        if(offset!=0 || amount<BLOCK_SECTOR_SIZE){
            if(!block_device_read(cluster_lba(cluster)+sector,sector_buffer)) return FS_ERROR_IO;
        } else {
            memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
        }
        memcpy(&sector_buffer[offset],&data[written],amount);
        if(!block_device_write(cluster_lba(cluster)+sector,sector_buffer)) return FS_ERROR_IO;
        written+=amount;
        offset_in_cluster+=amount;
        if(offset_in_cluster==cluster_size && written<count){
            uint32_t next;
            status=fat_next_cluster(cluster,&next);
            if(status<0 || !valid_cluster(next)) return status<0 ? status : FS_ERROR_INVALID;
            cluster=next;
            offset_in_cluster=0;
        }
    }

    if(!block_device_read(entry.sector_lba,sector_buffer)) return FS_ERROR_IO;
    write_u16(&sector_buffer[entry.offset+20],(uint16_t)(entry.first_cluster>>16));
    write_u16(&sector_buffer[entry.offset+26],(uint16_t)entry.first_cluster);
    write_u32(&sector_buffer[entry.offset+28],new_size);
    if(!block_device_write(entry.sector_lba,sector_buffer)) return FS_ERROR_IO;
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
        if((lba&0x3FU)==0) scheduler_yield();
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
        if((index&0x3FU)==0) scheduler_yield();
        if(!block_device_write(first_lba+index,sector_buffer)) return false;
    }
    return true;
}

static void build_format_boot_sector(const struct fat32_format_layout *layout,
                                     uint32_t hidden_sectors,
                                     const uint8_t volume_label[11]){
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
    write_u32(&sector_buffer[28],hidden_sectors);
    write_u32(&sector_buffer[32],layout->total_sectors);
    write_u32(&sector_buffer[36],layout->fat_size);
    write_u32(&sector_buffer[44],2);
    write_u16(&sector_buffer[48],1);
    write_u16(&sector_buffer[50],6);
    sector_buffer[64]=0x80;
    sector_buffer[66]=0x29;
    write_u32(&sector_buffer[67],0x50555245);
    memcpy(&sector_buffer[71],volume_label,11);
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

    build_format_boot_sector(layout,0,required_volume_label);
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

static void generate_guid(uint8_t guid[16], const char *serial, uint32_t salt){
    uint32_t state=2166136261U^salt;
    while(serial && *serial){
        state^=(uint8_t)*serial++;
        state*=16777619U;
    }
    for(uint8_t index=0;index<16;index++){
        state=state*1664525U+1013904223U;
        guid[index]=(uint8_t)(state>>24);
    }
    guid[7]=(uint8_t)((guid[7]&0x0F)|0x40);
    guid[8]=(uint8_t)((guid[8]&0x3F)|0x80);
}

static void write_gpt_name(uint8_t *entry, const char *name){
    for(uint8_t index=0;name[index] && index<36;index++){
        write_u16(&entry[56+index*2],(uint8_t)name[index]);
    }
}

static void build_gpt_header(uint32_t current_lba, uint32_t backup_lba,
                             uint32_t entries_lba, uint32_t last_usable_lba,
                             const uint8_t disk_guid[16], uint32_t entries_crc){
    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    memcpy(&sector_buffer[0],"EFI PART",8);
    write_u32(&sector_buffer[8],0x00010000);
    write_u32(&sector_buffer[12],GPT_HEADER_SIZE);
    write_u64(&sector_buffer[24],current_lba);
    write_u64(&sector_buffer[32],backup_lba);
    write_u64(&sector_buffer[40],34);
    write_u64(&sector_buffer[48],last_usable_lba);
    memcpy(&sector_buffer[56],disk_guid,16);
    write_u64(&sector_buffer[72],entries_lba);
    write_u32(&sector_buffer[80],GPT_ENTRY_COUNT);
    write_u32(&sector_buffer[84],GPT_ENTRY_SIZE);
    write_u32(&sector_buffer[88],entries_crc);
    write_u32(&sector_buffer[16],crc32(sector_buffer,GPT_HEADER_SIZE));
}

static bool write_gpt_layout(uint32_t total_sectors, const char *serial){
    uint32_t data_start=FAT32_ESP_START_LBA+FAT32_ESP_SECTORS;
    if(total_sectors<=data_start+GPT_ENTRY_SECTORS+65535){
        klogf(KLOG_ERROR,"write_gpt: total %u too small",total_sectors);
        return false;
    }

    uint32_t backup_header_lba=total_sectors-1;
    uint32_t backup_entries_lba=backup_header_lba-GPT_ENTRY_SECTORS;
    uint32_t last_usable_lba=backup_entries_lba-1;
    uint8_t disk_guid[16];
    uint8_t esp_guid[16];
    uint8_t data_guid[16];
    generate_guid(disk_guid,serial,0x47505444U^total_sectors);
    generate_guid(esp_guid,serial,0x45535031U^total_sectors);
    generate_guid(data_guid,serial,0x44415441U^total_sectors);

    memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
    uint8_t *partition=&sector_buffer[446];
    partition[1]=0x00; partition[2]=0x02; partition[3]=0x00;
    partition[4]=0xEE;
    partition[5]=0xFF; partition[6]=0xFF; partition[7]=0xFF;
    write_u32(&partition[8],1);
    write_u32(&partition[12],total_sectors-1);
    sector_buffer[510]=0x55;
    sector_buffer[511]=0xAA;
    if(!block_device_write(0,sector_buffer)) return false;

    uint32_t entries_crc=0xFFFFFFFFU;
    for(uint8_t sector=0;sector<GPT_ENTRY_SECTORS;sector++){
        memset(sector_buffer,0,BLOCK_SECTOR_SIZE);
        if(sector==0){
            memcpy(&sector_buffer[0],gpt_esp_type_guid,16);
            memcpy(&sector_buffer[16],esp_guid,16);
            write_u64(&sector_buffer[32],FAT32_ESP_START_LBA);
            write_u64(&sector_buffer[40],data_start-1);
            write_gpt_name(&sector_buffer[0],"PureC ESP");

            memcpy(&sector_buffer[GPT_ENTRY_SIZE],gpt_basic_data_type_guid,16);
            memcpy(&sector_buffer[GPT_ENTRY_SIZE+16],data_guid,16);
            write_u64(&sector_buffer[GPT_ENTRY_SIZE+32],data_start);
            write_u64(&sector_buffer[GPT_ENTRY_SIZE+40],last_usable_lba);
            write_gpt_name(&sector_buffer[GPT_ENTRY_SIZE],"PureC System");
        }
        entries_crc=crc32_update(entries_crc,sector_buffer,BLOCK_SECTOR_SIZE);
        if(!block_device_write(2+sector,sector_buffer)
           || !block_device_write(backup_entries_lba+sector,sector_buffer)){
            return false;
        }
    }
    entries_crc=~entries_crc;

    build_gpt_header(1,backup_header_lba,2,last_usable_lba,
                     disk_guid,entries_crc);
    if(!block_device_write(1,sector_buffer)) return false;
    build_gpt_header(backup_header_lba,1,backup_entries_lba,last_usable_lba,
                     disk_guid,entries_crc);
    if(!block_device_write(backup_header_lba,sector_buffer)) return false;

    klogf(KLOG_OK,"write_gpt: ESP %u..%u data %u..%u",
          FAT32_ESP_START_LBA,data_start-1,data_start,last_usable_lba);
    return true;
}

static bool write_format_metadata_at(uint32_t part_lba,
                                     const struct fat32_format_layout *layout,
                                     const uint8_t volume_label[11]){
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
    build_format_boot_sector(layout,part_lba,volume_label);
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

static int32_t create_directory_checked(const char *path){
    int32_t status=fat32_create_directory(path);
    return status==FS_ERROR_EXISTS?0:status;
}

static const char uefi_limine_config[]=
    "timeout: 10\n"
    "verbose: yes\n"
    "/PureC OS (UEFI primary)\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel.elf\n"
    "    module_path: boot():/boot/kernel-fallback.elf\n"
    "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n"
    "    module_path: boot():/bin/init\n"
    "    module_path: boot():/bin/installer\n"
    "    module_path: boot():/bin/snake\n"
    "    module_path: boot():/bin/program/terminal\n"
    "    module_path: boot():/bin/program/nano\n"
    "    module_path: boot():/lib/libpurec.a\n"
    "/PureC OS (UEFI fallback previous image)\n"
    "    protocol: limine\n"
    "    kernel_path: boot():/boot/kernel-fallback.elf\n"
    "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n"
    "    module_path: boot():/bin/init\n"
    "    module_path: boot():/bin/installer\n"
    "    module_path: boot():/bin/snake\n"
    "    module_path: boot():/bin/program/terminal\n"
    "    module_path: boot():/bin/program/nano\n"
    "    module_path: boot():/lib/libpurec.a\n";

static int32_t write_uefi_config(const char *directory,
                                 const char *alias_path){
    return write_lfn_file(directory,"limine.conf",alias_path,"limine~1.con",
                          uefi_limine_config,sizeof(uefi_limine_config)-1);
}

static int32_t verify_installed_file(const char *path, uint32_t expected_size){
    struct fat32_entry_ref entry;
    int32_t status=resolve_entry(path,&entry,0);
    if(status<0) return status;
    return entry.size==expected_size?0:FS_ERROR_IO;
}

static int32_t install_program_payload(void){
    if(create_directory_checked("/bin")<0
       || create_directory_checked("/bin/program")<0
       || create_directory_checked("/game")<0
       || create_directory_checked("/lib")<0) return FS_ERROR_IO;
    const void *init_image,*installer_image,*snake_image,*terminal_image;
    const void *nano_image,*library_image;
    uint64_t init_size,installer_size,snake_size,terminal_size,nano_size;
    uint64_t library_size;
    if(!boot_get_module("/bin/init",&init_image,&init_size)
       || !boot_get_module("/bin/installer",&installer_image,&installer_size)
       || !boot_get_module("/bin/snake",&snake_image,&snake_size)
       || !boot_get_module("/bin/program/terminal",&terminal_image,&terminal_size)
       || !boot_get_module("/bin/program/nano",&nano_image,&nano_size)
       || !boot_get_module("/lib/libpurec.a",&library_image,&library_size)
       || init_size>UINT32_MAX || installer_size>UINT32_MAX
       || snake_size>UINT32_MAX || terminal_size>UINT32_MAX
       || nano_size>UINT32_MAX || library_size>UINT32_MAX){
        return FS_ERROR_NOT_FOUND;
    }
    int32_t status=fat32_write_file("/bin/init",init_image,(uint32_t)init_size);
    if(status<0) return status;
    status=write_lfn_file("/bin","installer","/bin/instal~1","instal~1",
                          installer_image,(uint32_t)installer_size);
    if(status<0) return status;
    status=fat32_write_file("/bin/snake",snake_image,(uint32_t)snake_size);
    if(status<0) return status;
    status=fat32_write_file("/game/snake",snake_image,(uint32_t)snake_size);
    if(status<0) return status;
    status=fat32_write_file("/bin/program/terminal",terminal_image,
                            (uint32_t)terminal_size);
    if(status<0) return status;
    status=fat32_write_file("/bin/program/nano",nano_image,(uint32_t)nano_size);
    if(status<0) return status;
    return fat32_write_file("/lib/libpurec.a",library_image,
                            (uint32_t)library_size);
}

static int32_t install_uefi_payload(void){
    static const char *directories[]={
        "/EFI","/EFI/BOOT","/EFI/limine","/boot","/boot/limine","/limine",
        "/bin","/bin/program","/game","/lib"
    };
    for(uint8_t index=0;index<sizeof(directories)/sizeof(directories[0]);index++){
        int32_t status=create_directory_checked(directories[index]);
        if(status<0) return status;
    }

    const void *kernel_image;
    const void *fallback_kernel_image;
    const void *efi_loader;
    uint32_t kernel_image_size;
    uint64_t fallback_kernel_image_size;
    uint32_t efi_loader_size;
    if(!boot_get_kernel_image(&kernel_image,&kernel_image_size)){
        return FS_ERROR_NOT_FOUND;
    }
    if(!boot_get_module("/boot/kernel-fallback.elf",&fallback_kernel_image,
                        &fallback_kernel_image_size)
       || fallback_kernel_image_size>UINT32_MAX){
        return FS_ERROR_NOT_FOUND;
    }
    if(!boot_get_efi_loader(&efi_loader,&efi_loader_size)
       || ((const uint8_t*)efi_loader)[0]!=0x4D
       || ((const uint8_t*)efi_loader)[1]!=0x5A){
        return FS_ERROR_NOT_FOUND;
    }
    const void *init_image;
    const void *installer_image;
    const void *snake_image;
    const void *terminal_image;
    const void *nano_image;
    const void *library_image;
    uint64_t init_size,installer_size,snake_size,terminal_size,nano_size;
    uint64_t library_size;
    if(!boot_get_module("/bin/init",&init_image,&init_size)
       || !boot_get_module("/bin/installer",&installer_image,&installer_size)
       || !boot_get_module("/bin/snake",&snake_image,&snake_size)
       || !boot_get_module("/bin/program/terminal",&terminal_image,&terminal_size)
       || !boot_get_module("/bin/program/nano",&nano_image,&nano_size)
       || !boot_get_module("/lib/libpurec.a",&library_image,&library_size)
       || init_size>UINT32_MAX || installer_size>UINT32_MAX
       || snake_size>UINT32_MAX || terminal_size>UINT32_MAX
       || nano_size>UINT32_MAX || library_size>UINT32_MAX){
        return FS_ERROR_NOT_FOUND;
    }

    int32_t status=fat32_write_file("/EFI/BOOT/BOOTX64.EFI",
                                    efi_loader,efi_loader_size);
    if(status<0) return status;
    status=fat32_write_file("/boot/kernel.elf",kernel_image,kernel_image_size);
    if(status<0) return status;
    status=fat32_write_file("/boot/kernel-fallback.elf",fallback_kernel_image,
                            (uint32_t)fallback_kernel_image_size);
    if(status<0) return status;
    status=install_program_payload();
    if(status<0) return status;

    static const struct {
        const char *directory;
        const char *alias_path;
    } config_locations[]={
        {"/","/limine~1.con"},
        {"/boot/limine","/boot/limine/limine~1.con"},
        {"/EFI/limine","/EFI/limine/limine~1.con"},
        {"/EFI/BOOT","/EFI/BOOT/limine~1.con"},
        {"/limine","/limine/limine~1.con"},
        {"/boot","/boot/limine~1.con"}
    };
    for(uint8_t index=0;index<sizeof(config_locations)/sizeof(config_locations[0]);index++){
        status=write_uefi_config(config_locations[index].directory,
                                 config_locations[index].alias_path);
        if(status<0) return status;
    }

    status=verify_installed_file("/EFI/BOOT/BOOTX64.EFI",efi_loader_size);
    if(status<0) return status;
    status=verify_installed_file("/boot/kernel.elf",kernel_image_size);
    if(status<0) return status;
    status=verify_installed_file("/boot/kernel-fallback.elf",
                                 (uint32_t)fallback_kernel_image_size);
    if(status<0) return status;
    status=verify_installed_file("/bin/init",(uint32_t)init_size);
    if(status<0) return status;
    status=verify_installed_file("/bin/installer",(uint32_t)installer_size);
    if(status<0) return status;
    status=verify_installed_file("/bin/snake",(uint32_t)snake_size);
    if(status<0) return status;
    status=verify_installed_file("/game/snake",(uint32_t)snake_size);
    if(status<0) return status;
    status=verify_installed_file("/bin/program/terminal",
                                 (uint32_t)terminal_size);
    if(status<0) return status;
    status=verify_installed_file("/bin/program/nano",(uint32_t)nano_size);
    if(status<0) return status;
    status=verify_installed_file("/lib/libpurec.a",(uint32_t)library_size);
    if(status<0) return status;
    for(uint8_t index=0;index<sizeof(config_locations)/sizeof(config_locations[0]);index++){
        status=verify_installed_file(config_locations[index].alias_path,
                                     sizeof(uefi_limine_config)-1);
        if(status<0) return status;
    }
    return 0;
}

// UEFI install: GPT, a 512 MiB ESP, and a separate system partition.
int32_t fat32_format_uefi_device_progress(
    const char *device_name, const char *serial_confirmation,
    fat32_progress_callback callback){
    if(callback) callback(2,"Validating target disk");
    if(!device_name || !serial_confirmation) return FS_ERROR_INVALID;
    int32_t idx=block_device_find(device_name);
    if(idx<0) return FS_ERROR_NOT_FOUND;
    struct storage_device_info info;
    if(!block_device_get_info((uint32_t)idx,&info)) return FS_ERROR_INVALID;
    if(!info.operational || !info.writable) return FS_ERROR_READ_ONLY;
    if(info.serial[0] && strcmp(info.serial,serial_confirmation)!=0){
        return FS_ERROR_CONFIRMATION;
    }
    if(info.sector_size!=BLOCK_SECTOR_SIZE
       || info.sector_count>FAT32_FORMAT_MAX_SECTORS){
        klogf(KLOG_ERROR,"fat32_uefi: UNSUPPORTED ss=%u expected %u sectors=%llu max=%u",
              info.sector_size,BLOCK_SECTOR_SIZE,info.sector_count,
              FAT32_FORMAT_MAX_SECTORS);
        return FS_ERROR_UNSUPPORTED;
    }
    uint32_t total_sectors=(uint32_t)info.sector_count;
    uint32_t data_start=FAT32_ESP_START_LBA+FAT32_ESP_SECTORS;
    if(total_sectors<=data_start+GPT_ENTRY_SECTORS+65535) return FS_ERROR_TOO_SMALL;
    uint32_t data_sectors=total_sectors-data_start-GPT_ENTRY_SECTORS-1;
    struct fat32_format_layout esp_layout;
    struct fat32_format_layout data_layout;
    if(!calculate_format_layout(FAT32_ESP_SECTORS,&esp_layout)
       || !calculate_format_layout(data_sectors,&data_layout)){
        return FS_ERROR_TOO_SMALL;
    }
    if(!block_device_select((uint32_t)idx)) return FS_ERROR_INVALID;
    if(callback) callback(8,"Writing GPT partition table");
    klogf(KLOG_INFO,"fat32_uefi: %s total %u ESP %u sectors data %u sectors",
          device_name,total_sectors,FAT32_ESP_SECTORS,data_sectors);
    if(!write_gpt_layout(total_sectors,info.serial)){
        klogf(KLOG_ERROR,"fat32_uefi: GPT write failed");
        return FS_ERROR_IO;
    }
    if(callback) callback(18,"Formatting EFI system partition");
    if(!write_format_metadata_at(FAT32_ESP_START_LBA,&esp_layout,
                                 esp_volume_label)){
        klogf(KLOG_ERROR,"fat32_uefi: ESP format failed");
        return FS_ERROR_IO;
    }
    memset(&volume,0,sizeof(volume));
    memset(handles,0,sizeof(handles));
    if(!mount_boot_sector(FAT32_ESP_START_LBA)){
        klogf(KLOG_ERROR,"fat32_uefi: ESP mount failed");
        return FS_ERROR_IO;
    }
    if(callback) callback(45,"Copying bootloader and kernel");
    int32_t status=install_uefi_payload();
    if(status<0){
        klogf(KLOG_ERROR,"fat32_uefi: boot payload failed %d",status);
        return status;
    }

    if(callback) callback(70,"Formatting PureC system partition");
    if(!write_format_metadata_at(data_start,&data_layout,
                                 required_volume_label)){
        klogf(KLOG_ERROR,"fat32_uefi: system partition format failed");
        return FS_ERROR_IO;
    }
    memset(&volume,0,sizeof(volume));
    memset(handles,0,sizeof(handles));
    if(!mount_boot_sector(data_start)){
        klogf(KLOG_ERROR,"fat32_uefi: system partition mount failed");
        return FS_ERROR_IO;
    }
    if(callback) callback(88,"Copying programs to /bin and /game");
    status=install_program_payload();
    if(status<0){
        klogf(KLOG_ERROR,"fat32_uefi: system payload failed %d",status);
        return status;
    }
    if(callback) callback(90,"System partition mounted");
    klogf(KLOG_OK,"fat32_uefi: GPT, ESP payload and system partition ready");
    return 0;
}

int32_t fat32_format_uefi_device(const char *device_name,
                                 const char *serial_confirmation){
    return fat32_format_uefi_device_progress(device_name,serial_confirmation,0);
}
