#include "commands.h"

#include <devctl.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

int cmd_clear(int argc, char **argv, int arg0, const char *cwd) {
    dev_tty_info_t ti;
    uint32_t rows = 25;
    int out = fileno(stdout);
    const char *home = "\x1B[H";

    (void)cwd;
    (void)argv;
    if (arg0 + 1 < argc) {
        fprintf(stderr, "usage: clear\n");
        return 1;
    }

    if (ioctl(out, DEV_IOCTL_TTY_GET_INFO, &ti) == 0 && ti.rows > 0) rows = ti.rows;

    (void)write(out, home, (uint32_t)strlen(home));
    for (uint32_t y = 0; y < rows; y++) {
        const char *line = "\x1B[K\n";
        (void)write(out, line, (uint32_t)strlen(line));
    }
    (void)write(out, home, (uint32_t)strlen(home));
    return 0;
}
