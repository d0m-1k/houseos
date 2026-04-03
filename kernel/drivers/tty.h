#pragma once

#include <drivers/filesystem/memfs.h>
#include <drivers/filesystem/devfs.h>

void tty_init(memfs *root_fs, devfs_t *devfs);
void tty_serial_print(const char *text);
void tty_klog(const char *text);
