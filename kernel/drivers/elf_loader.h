#pragma once

#include <drivers/filesystem/memfs.h>
#include <stdint.h>

int elf_load_from_memfs(memfs *fs, const char *path, uint32_t *entry_out);
int elf_get_last_error(void);
