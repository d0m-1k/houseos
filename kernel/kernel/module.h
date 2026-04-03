#pragma once

#include <drivers/filesystem/memfs.h>
#include <drivers/filesystem/vfs.h>
#include <drivers/filesystem/devfs.h>
#include <stdint.h>

typedef struct {
    memfs *mem_root;
    vfs_t *vfs;
    devfs_t *devfs;
    int vesa_ok;
} kernel_boot_ctx_t;

typedef int (*kernel_module_init_fn)(kernel_boot_ctx_t *ctx);

enum {
    KERNEL_MODULE_REQUIRED = 1u << 0,
};

typedef struct {
    const char *name;
    uint32_t flags;
    kernel_module_init_fn init;
} kernel_module_t;

int kernel_modules_run(const kernel_module_t *mods, uint32_t count, kernel_boot_ctx_t *ctx);
