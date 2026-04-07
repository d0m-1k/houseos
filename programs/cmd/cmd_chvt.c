#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>
#include <devctl.h>

int cmd_chvt(int argc, char **argv, int arg0, const char *cwd) {
    uint32_t idx = 0;
    int fd;
    (void)cwd;
    if (arg0 + 1 >= argc || arg0 + 2 < argc || parse_u32_dec(argv[arg0 + 1], &idx) != 0) {
        fprintf(stderr, "usage: chvt <index>\n");
        return 1;
    }
    fd = open("/dev/tty/1", 0);
    if (fd < 0) {
        fprintf(stderr, "chvt: cannot open /dev/tty/1\n");
        return 1;
    }
    if (ioctl(fd, DEV_IOCTL_TTY_SET_ACTIVE, &idx) != 0) {
        close(fd);
        fprintf(stderr, "chvt: switch failed\n");
        return 1;
    }
    close(fd);
    return 0;
}
