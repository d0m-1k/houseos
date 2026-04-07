#include "commands.h"
#include <syscall.h>
#include <devctl.h>
#include <stdio.h>

int cmd_mouseinfo(int argc, char **argv, int arg0, const char *cwd) {
    dev_mouse_info_t mi;
    int fd;
    (void)cwd;
    if (arg0 + 1 < argc) {
        fprintf(stderr, "usage: mouseinfo\n");
        return 1;
    }
    (void)argv;
    fd = open("/dev/mouse", 0);
    if (fd < 0) {
        fprintf(stderr, "mouseinfo: cannot open /dev/mouse\n");
        return 1;
    }
    if (ioctl(fd, DEV_IOCTL_MOUSE_GET_INFO, &mi) != 0) {
        close(fd);
        fprintf(stderr, "mouseinfo: ioctl failed\n");
        return 1;
    }
    fprintf(stdout, "mouse x=%d y=%d btn=%u\n", mi.x, mi.y, mi.buttons);
    close(fd);
    return 0;
}
