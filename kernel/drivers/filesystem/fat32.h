#pragma once

#include <drivers/filesystem/vfs.h>
#include <stdint.h>

typedef struct {
    uint8_t disk_kind;
    uint8_t disk_index;
    uint16_t disk_reserved;

    uint32_t part_start_lba;
    uint32_t part_total_sectors;

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;

    uint32_t fat_start_lba;
    uint32_t data_start_lba;
} fat32_fs_t;

int fat32_init(fat32_fs_t *fs, uint32_t partition_index);
int fat32_init_named(fat32_fs_t *fs, const char *disk_name, uint32_t partition_index);
int fat32_init_devpath(fat32_fs_t *fs, const char *dev_path);

extern const vfs_ops_t g_fat32_vfs_ops;
