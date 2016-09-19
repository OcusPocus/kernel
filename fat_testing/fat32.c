#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>
#include <errno.h>
#include "fat32.h"

int x;

struct f32 {
    FILE *f;
    uint32_t *FAT;
    struct bios_parameter_block bpb;
    uint32_t partition_begin_sector;
    uint32_t fat_begin_sector;
    uint32_t cluster_begin_sector;
    uint32_t cluster_size;    
};

static void read_bpb(f32 *fs, struct bios_parameter_block *bpb);
static uint32_t sector_for_cluster(f32 *fs, uint32_t cluster);

static void trim_spaces(char *c, int max) {
    int i = 0;
    while(*c != ' ' && i++ < max) {
        c++;
    }
    if(*c == ' ') *c = 0;
}

f32 *makeFilesystem(char *fatSystem) {
    f32 *fs = malloc(sizeof (struct f32));
    fs->f = fopen(fatSystem, "r");
    rewind(fs->f);
    read_bpb(fs, &fs->bpb);

    trim_spaces(fs->bpb.system_id, 8);
    if(strcmp(fs->bpb.system_id, "FAT32") != 0) {
        fclose(fs->f);
        free(fs);
        return NULL;
    }
    
    fs->partition_begin_sector = 0;
    fs->fat_begin_sector = fs->partition_begin_sector + fs->bpb.reserved_sectors;
    fs->cluster_begin_sector = fs->fat_begin_sector + (fs->bpb.FAT_count * fs->bpb.count_sectors_per_FAT32);
    fs->cluster_size = 512 * fs->bpb.sectors_per_cluster;

    // Load the FAT
    uint32_t bytes_per_fat = 512 * fs->bpb.count_sectors_per_FAT32;
    fs->FAT = malloc(bytes_per_fat);
    int sector_i;
    for(sector_i = 0; sector_i < fs->bpb.count_sectors_per_FAT32; sector_i++) {
        char *sector = getSector(fs, fs->fat_begin_sector + sector_i);
        int integer_j;
        for(integer_j = 0; integer_j < 512; integer_j++) {
            fs->FAT[sector_i * (512 / 4) + integer_j]
                = readi32(sector, integer_j * 4);
        }
        free(sector);
    }
    return fs;
}

void destroyFilesystem(f32 *fs) {
    fflush(fs->f);
    fclose(fs->f);
    free(fs->FAT);
    free(fs);
}

char *getSector(f32 *fs, uint32_t sector) {
    char *buff = malloc(512); // sector is 512 bytes.
    fseek(fs->f, sector * 512, SEEK_SET);
    int readcount = 0;
    while(readcount < 512) {
        readcount += fread(buff + readcount, 512 - readcount, 1, fs->f);
        if(feof) break;
    }
    return buff;
}

uint16_t readi16(char *buff, size_t offset)
{
    unsigned char *ubuff = buff + offset;
    return ubuff[1] << 8 | ubuff[0];
}
uint32_t readi32(char *buff, size_t offset) {
    unsigned char *ubuff = buff + offset;
    return
        ((ubuff[3] << 24) & 0xFF000000) |
        ((ubuff[2] << 16) & 0x00FF0000) |
        ((ubuff[1] << 8) & 0x0000FF00) |
        (ubuff[0] & 0x000000FF);
}

/**
 * 11 2 The number of Bytes per sector (remember, all numbers are in the little-endian format).
 * 13 1 Number of sectors per cluster.
 * 14 2 Number of reserved sectors. The boot record sectors are included in this value.
 * 16 1 Number of File Allocation Tables (FAT's) on the storage media. Often this value is 2.
 * 17 2 Number of directory entries (must be set so that the root directory occupies entire sectors).
 * 19 2 The total sectors in the logical volume. If this value is 0, it means there are more than 65535 sectors in the volume, and the actual count is stored in "Large Sectors (bytes 32-35).
 * 21 1 This Byte indicates the media descriptor type.
 * 22 2 Number of sectors per FAT. FAT12/FAT16 only.
 * 24 2 Number of sectors per track.
 * 26 2 Number of heads or sides on the storage media.
 * 28 4 Number of hidden sectors. (i.e. the LBA of the beginning of the partition.)
 * 32 4 Large amount of sector on media. This field is set if there are more than 65535 sectors in the volume.
 */

/**
 * 36 4 Sectors per FAT. The size of the FAT in sectors.
 * 40 2 Flags.
 * 42 2 FAT version number. The high byte is the major version and the low byte is the minor version. FAT drivers should respect this field.
 * 44 4 The cluster number of the root directory. Often this field is set to 2.
 * 48 2 The sector number of the FSInfo structure.
 * 50 2 The sector number of the backup boot sector.
 * 52 12 Reserved. When the volume is formated these bytes should be zero.
 * 64 1 Drive number. The values here are identical to the values returned by the BIOS interrupt 0x13. 0x00 for a floppy disk and 0x80 for hard disks.
 * 65 1 Flags in Windows NT. Reserved otherwise.
 * 66 1 Signature (must be 0x28 or 0x29).
 * 67 4 VolumeID 'Serial' number. Used for tracking volumes between computers. You can ignore this if you want.
 * 71 11 Volume label string. This field is padded with spaces.
 * 82 8 System identifier string. Always "FAT32   ". The spec says never to trust the contents of this string for any use.
 * 90 420 Boot code.
 */

static void read_bpb(f32 *fs, struct bios_parameter_block *bpb) {
    char *sector0 = getSector(fs, 0);

    bpb->bytes_per_sector = readi16(sector0, 11);;
    bpb->sectors_per_cluster = sector0[13];
    bpb->reserved_sectors = readi16(sector0, 14);
    bpb->FAT_count = sector0[16];
    bpb->dir_entries = readi16(sector0, 17);
    bpb->total_sectors = readi16(sector0, 19);
    bpb->media_descriptor_type = sector0[21];
    bpb->count_sectors_per_FAT12_16 = readi16(sector0, 22);
    bpb->count_sectors_per_track = readi16(sector0, 24);
    bpb->count_heads_or_sizes_on_media = readi16(sector0, 26);
    bpb->count_hidden_sectors = readi32(sector0, 28);
    bpb->large_sectors_on_media = readi32(sector0, 32);
    // EBR
    bpb->count_sectors_per_FAT32 = readi32(sector0, 36);
    bpb->flags = readi16(sector0, 40);
    bpb->FAT_version = readi16(sector0, 42);
    bpb->cluster_number_root_dir = readi32(sector0, 44);
    bpb->sector_number_FSInfo = readi16(sector0, 48);
    bpb->sector_number_backup_boot_sector = readi16(sector0, 50);
    // Skip 12 bytes
    bpb->drive_number = sector0[64];
    bpb->windows_flags = sector0[65];
    bpb->signature = sector0[66];
    bpb->volume_id = readi32(sector0, 67);
    memcpy(&bpb->volume_label, sector0 + 71, 11); bpb->volume_label[11] = 0;
    memcpy(&bpb->system_id, sector0 + 82, 8); bpb->system_id[8] = 0;
    free(sector0);
}

const struct bios_parameter_block *getBPB(f32 *fs) {
    return &fs->bpb;
}

static uint32_t sector_for_cluster(f32 *fs, uint32_t cluster) {
    return fs->cluster_begin_sector + ((cluster - 2) * fs->bpb.sectors_per_cluster);
}

// CLUSTER NUMBERS START AT 2 (for some reason...)
char *getCluster(f32 *fs, uint32_t cluster_number) {
    //printf("cluster number: 0x%08x [%u]\n", cluster_number, cluster_number);
    uint32_t sector = sector_for_cluster(fs, cluster_number);
    //printf("sector: 0x%08x [%u]\n", sector, sector);
    uint32_t desired_amount = fs->cluster_size;

    char *buff = malloc(desired_amount);
    //printf("File offset: 0x%08x [%u]\n", sector * 512, sector * 512);
    fseek(fs->f, sector * 512, SEEK_SET);
    int readcount = 0;
    while(readcount < desired_amount) {
        readcount += fread(buff + readcount, 1, desired_amount - readcount, fs->f);
        if(feof(fs->f)) {
            printf("Error. found end of file.\n");
            exit(1);
        }
        else if(ferror(fs->f)) {
            printf("ERROR!");
            perror("Error reading file!");
            exit(1);
        }
    }
    return buff;
}

uint32_t get_next_cluster_id(f32 *fs, uint32_t cluster) {
    return fs->FAT[cluster] & 0x0FFFFFFF;
}

void populate_root_dir(f32 *fs, struct directory *dir) {
    populate_dir(fs, dir, 2);
}

void populate_dir(f32 *fs, struct directory *dir, uint32_t cluster) {
    uint32_t dirs_per_cluster = fs->cluster_size / 32;
    uint32_t max_dirs = 0;
    dir->entries = 0; 
    uint32_t entry_count = 0;
    
    while(1) {
        //printf("Loading cluster %u\n", cluster);
        max_dirs += dirs_per_cluster;
        dir->entries = realloc(dir->entries, max_dirs * sizeof (struct dir_entry));
        
        char *root_cluster = getCluster(fs, cluster);

        uint32_t i;
        for(i = 0; i < max_dirs; i++) {
            char *entry = root_cluster + (i * 32); // Shift by i 32-byte records.
            unsigned char first_byte = *entry;
            if(first_byte == 0x00 || first_byte == 0xE5) {
                // This directory entry has never been written
                // or it has been deleted.
                continue;
            }
            
            uint8_t attrs = entry[11];
            if(attrs == LFN) {
                // We'll add long-filename support later.
                continue;
            }
            
            memcpy(dir->entries[entry_count].dir_name, entry, 11);
            dir->entries[entry_count].dir_name[11] = 0;
            dir->entries[entry_count].dir_attrs = attrs;
            uint16_t first_cluster_high = readi16(entry, 20);
            uint16_t first_cluster_low = readi16(entry, 26);
//            printf("bits low: %x, bits high: %x\n", first_cluster_low, first_cluster_high);
            dir->entries[entry_count].first_cluster = first_cluster_high << 16 | first_cluster_low;
//            dir->entries[entry_count].first_cluster_high = readi16(entry, 20);
//            dir->entries[entry_count].first_cluster_low = readi16(entry, 26);
            dir->entries[entry_count].file_size = readi32(entry, 28);
            
            // Trim up the filename.
            char extension[4];
            memcpy(extension, dir->entries[entry_count].dir_name + 8, 3);
            extension[3] = 0;
            trim_spaces(extension, 3);

            dir->entries[entry_count].dir_name[8] = 0;
            trim_spaces(dir->entries[entry_count].dir_name, 8);

            if(strlen(extension) > 0) {
                uint32_t len = strlen(dir->entries[entry_count].dir_name);
                dir->entries[entry_count].dir_name[len++] = '.';
                memcpy(dir->entries[entry_count].dir_name + len, extension, 4);
            }
//            
//
//            trim_spaces(dir->entries[entry_count].dir_name, 8);
//            trim_spaces(dir->entries[entry_count].dir_name + 8, 3);
//            uint8_t last_char = strlen(dir->entries[entry_count].dir_name);
//            
//            if(last_char == 11) {
//                // The filename is full.
//                int i;
//                for(i = 11; i > 8; i--) {
//                    dir->entries[entry_count].dir_name[i] = dir->entries[entry_count].dir_name[i-1];
//                }
//                dir->entries[entry_count].dir_name[8] = '.';
//            }
//            else if(strlen(dir->entries[entry_count].dir_name + 8)) {
//                dir->entries[entry_count].dir_name[last_char] = '.';
//                last_char++;
//                int i;
//                for(i = 0; i < 3; i++) {
//                    dir->entries[entry_count].dir_name[last_char + i]
//                        = dir->entries[entry_count].dir_name[8 + i];
//                }
//                dir->entries[entry_count].dir_name[last_char + i] = 0;
//            }
            
            entry_count++;
        }
        dir->num_entries = entry_count;
        free(root_cluster);
        cluster = get_next_cluster_id(fs, cluster);
        if(cluster >= 0x0FFFFFF8) break;
    }
}

void free_directory(f32 *fs, struct directory *dir){
    free(dir->entries);
}

char *readFile(f32 *fs, struct dir_entry *dirent) {
    //printf("Reading file!\n");
    char *file = malloc(dirent->file_size);
    char *filecurrptr = file;
    uint32_t cluster = dirent->first_cluster;
    uint32_t copiedbytes = 0;
    while(1) {
        //printf("Getting cluster %u\n", cluster);
        char *cbytes = getCluster(fs, cluster);

        uint32_t remaining = dirent->file_size - copiedbytes;
        uint32_t to_copy = remaining > fs->cluster_size ? fs->cluster_size : remaining;
        
        memcpy(filecurrptr, cbytes, to_copy);

        filecurrptr += fs->cluster_size;
        copiedbytes += to_copy;
        
        cluster = get_next_cluster_id(fs, cluster);
        if(cluster >= 0x0FFFFFF8) break;
    }
    return file;
}

void print_directory(f32 *fs, struct directory *dir) {
    uint32_t i;
    for(i = 0; i < dir->num_entries; i++) {
        printf("[%d] %-12s %c %8d bytes ", //first cluster: %u",
               i,
               dir->entries[i].dir_name,
               dir->entries[i].dir_attrs & DIRECTORY?'D':' ',
               dir->entries[i].file_size, dir->entries[i].first_cluster);
        uint32_t cluster = dir->entries[i].first_cluster;
        uint32_t cluster_count = 1;
        while(1) {
            cluster = fs->FAT[cluster];
            //printf(" %u", cluster);
            if(cluster >= 0x0FFFFFF8) break;
            //if(cluster == 0) break;
            cluster_count++;
        }
        printf("clusters: [%u]\n", cluster_count);
    }
}