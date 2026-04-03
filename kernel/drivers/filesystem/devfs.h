#pragma once

#include <drivers/filesystem/memfs.h>
#include <drivers/filesystem/vfs.h>

typedef struct {
    memfs *fs;
} devfs_t;

int devfs_init(devfs_t *devfs, size_t mem_size);

int devfs_create_dir(devfs_t *devfs, const char *path);
int devfs_create_device_ops(
    devfs_t *devfs, const char *path, uint32_t flags,
    memfs_dev_read_t read_cb, memfs_dev_write_t write_cb, memfs_dev_ioctl_t ioctl_cb, void *ctx
);

extern const vfs_ops_t g_devfs_vfs_ops;
