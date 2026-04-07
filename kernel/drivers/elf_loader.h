#pragma once

#include <drivers/filesystem/vfs.h>
#include <stdint.h>

int elf_load_from_vfs(vfs_t *fs, const char *path, uint32_t *entry_out);
int elf_load_from_vfs_ex(vfs_t *fs, const char *path, uint32_t *entry_out, char *interp_out, uint32_t interp_cap);
int elf_get_last_error(void);
