#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <stdio.h>

int cmd_cat(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    char buf[CMD_BUF_SZ];

    if (arg0 + 1 >= argc) {
        int32_t n = read(fileno(stdin), buf, sizeof(buf) - 1);
        if (n > 0) write(fileno(stdout), buf, (uint32_t)n);
        return 0;
    }
    if (arg0 + 2 < argc) {
        fprintf(stderr, "usage: cat [file]\n");
        return 1;
    }

    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "cat: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }
    {
        int fd = open(path, 0);
        int32_t n;
        if (fd < 0) {
            fprintf(stderr, "cat open failed\n");
            return 1;
        }
        n = read(fd, buf, sizeof(buf));
        if (n > 0) write(fileno(stdout), buf, (uint32_t)n);
        else if (n < 0) {
            close(fd);
            fprintf(stderr, "cat open failed\n");
            return 1;
        }
        close(fd);
    }
    return 0;
}
