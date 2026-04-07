#include "commands.h"
#include "cmd_common.h"

#include <stdio.h>
#include <syscall.h>
#include <string.h>

int cmd_cp(int argc, char **argv, int arg0, const char *cwd) {
    char src[256];
    char dst[256];
    char buf[CMD_BUF_SZ];
    int in_fd;
    int out_fd;
    int32_t n;
    int first = 1;

    if (arg0 + 2 >= argc) {
        fprintf(stderr, "usage: cp SRC DST\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], src, sizeof(src)) != 0 ||
        normalize_path(cwd, argv[arg0 + 2], dst, sizeof(dst)) != 0) {
        fprintf(stderr, "cp failed\n");
        return 1;
    }
    if (strcmp(src, dst) == 0) return 0;

    in_fd = open(src, 0);
    if (in_fd < 0) {
        fprintf(stderr, "cp failed\n");
        return 1;
    }
    out_fd = open(dst, 1);
    if (out_fd < 0) {
        close(in_fd);
        fprintf(stderr, "cp failed\n");
        return 1;
    }

    n = read(in_fd, buf, sizeof(buf));
    if (n > 0) {
        int32_t wr = first ? write(out_fd, buf, (uint32_t)n) : append(out_fd, buf, (uint32_t)n);
        if (wr != n) {
            close(out_fd);
            close(in_fd);
            fprintf(stderr, "cp failed\n");
            return 1;
        }
        first = 0;
    }

    close(out_fd);
    close(in_fd);
    if (n < 0) {
        fprintf(stderr, "cp failed\n");
        return 1;
    }
    return 0;
}
