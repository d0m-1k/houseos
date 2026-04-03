#pragma once

#include <drivers/filesystem/devfs.h>

void disk_init(devfs_t *devfs);
int disk_read_kernel(uint32_t lba, uint32_t count, void *buffer);
int disk_write_kernel(uint32_t lba, uint32_t count, const void *buffer);
int disk_get_partition_info(uint32_t index, uint32_t *start_lba, uint32_t *total_sectors);
