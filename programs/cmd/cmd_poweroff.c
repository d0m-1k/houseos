#include "commands.h"
#include <stdio.h>
#include <syscall.h>
#include <devctl.h>

int cmd_poweroff(int argc, char **argv, int arg0, const char *cwd) {
    int fd;
    (void)argc; (void)argv; (void)arg0; (void)cwd;
    fd = open("/dev/power", 0);
    if (fd < 0) return 1;
    (void)ioctl(fd, DEV_IOCTL_POWER_POWEROFF, 0);
    close(fd);
    return 0;
}
