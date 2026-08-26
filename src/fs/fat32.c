#include "fat32.h"

#include "../drivers/storage/block_device.h"
#include "../lib/string.h"
#include <stddef.h>

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
    if(!block_device_read(partition_lba,sector_buffer)) return false;
    if(sector_buffer[510]!=0x55 || sector_buffer[511]!=0xAA) return false;
    if(memcmp(&sector_buffer[71],required_volume_label,11)!=0) return false;

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
        return false;
    }

    uint64_t fat_sectors=(uint64_t)fat_count*fat_size;
    if((uint64_t)reserved_sectors+fat_sectors>=total_sectors) return false;
    uint32_t data_sectors=total_sectors-reserved_sectors-(uint32_t)fat_sectors;
    uint32_t cluster_count=data_sectors/sectors_per_cluster;
    if(cluster_count<65525 || root_cluster<2 || root_cluster>=cluster_count+2) return false;
    if((uint64_t)fat_size*(BLOCK_SECTOR_SIZE/4)<cluster_count+2) return false;
    if((uint64_t)partition_lba+total_sectors>0x10000000ULL) return false;

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
            if(type==0x0B || type==0x0C || type==0x1B || type==0x1C){
                uint32_t lba=read_u32(&sector_buffer[offset+8]);
                if(lba) partition_lbas[partition_count++]=lba;
            }
        }
        for(uint8_t index=0;index<partition_count;index++){
            if(mount_boot_sector(partition_lbas[index])) return true;
        }
        if(mount_boot_sector(0)) return true;
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
