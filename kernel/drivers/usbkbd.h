#pragma once

#include <stdint.h>
#include <drivers/filesystem/devfs.h>

void usbkbd_init(devfs_t *devfs);
void usbkbd_poll(void);
