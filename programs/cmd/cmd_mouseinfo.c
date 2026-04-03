#include "commands.h"
#include <syscall.h>
#include <devctl.h>
#include <stdio.h>

int cmd_mouseinfo(int argc, char **argv, int arg0, const char *cwd) {
    dev_mouse_info_t mi;
    int fd;
    (void)argc; (void)argv; (void)arg0; (void)cwd;
    fd = open("/dev/mouse", 0);
    if (fd < 0 || ioctl(fd, DEV_IOCTL_MOUSE_GET_INFO, &mi) != 0) return 1;
    fprintf(stdout, "mouse x=%d y=%d btn=%u\n", mi.x, mi.y, mi.buttons);
    close(fd);
    return 0;
}
