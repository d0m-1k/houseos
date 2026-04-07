#include "commands.h"
#include <stdio.h>
#include <syscall.h>
#include <devctl.h>

int cmd_poweroff(int argc, char **argv, int arg0, const char *cwd) {
    int fd;
    (void)cwd;
    if (arg0 + 1 < argc) {
        fprintf(stderr, "usage: poweroff\n");
        return 1;
    }
    (void)argv;
    fd = open("/dev/power", 0);
    if (fd < 0) {
        fprintf(stderr, "poweroff: cannot open /dev/power\n");
        return 1;
    }
    if (ioctl(fd, DEV_IOCTL_POWER_POWEROFF, 0) != 0) {
        close(fd);
        fprintf(stderr, "poweroff: ioctl failed\n");
        return 1;
    }
    close(fd);
    return 0;
}
