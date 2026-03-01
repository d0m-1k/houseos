#pragma once

#include <drivers/filesystem/memfs.h>

void tty_init(memfs *root_fs);
void tty_serial_print(const char *text);
void tty_klog(const char *text);
