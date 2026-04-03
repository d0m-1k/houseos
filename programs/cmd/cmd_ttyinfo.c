#include "commands.h"
#include <syscall.h>
#include <devctl.h>
#include <stdio.h>

int cmd_ttyinfo(int argc, char **argv, int arg0, const char *cwd) {
    dev_tty_info_t ti;
    (void)argc; (void)argv; (void)arg0; (void)cwd;
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) != 0) return 1;
    fprintf(stdout, "tty kind=%u idx=%u cols=%u rows=%u cur=%u,%u\n",
        ti.kind, ti.index, ti.cols, ti.rows, ti.cursor_x, ti.cursor_y);
    return 0;
}
