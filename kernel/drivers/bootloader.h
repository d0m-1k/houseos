#pragma once

#include <drivers/filesystem/devfs.h>
#include <stdint.h>

int bootloader_dev_init(devfs_t *devfs);
uint32_t bootloader_get_root_disk(void);
uint32_t bootloader_get_flags(void);
uint32_t bootloader_get_video_output(void);
