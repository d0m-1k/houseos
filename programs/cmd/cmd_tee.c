#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_tee(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    char buf[CMD_BUF_SZ];
    int fd;
    int32_t n;
    if (arg0 + 1 >= argc || arg0 + 2 < argc) {
        fprintf(stderr, "usage: tee <path>\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "tee: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }
    fd = open(path, 1);
    if (fd < 0) {
        fprintf(stderr, "tee: open failed: %s\n", path);
        return 1;
    }
    while ((n = read(fileno(stdin), buf, sizeof(buf))) > 0) {
        write(fileno(stdout), buf, (uint32_t)n);
        if (append(fd, buf, (uint32_t)n) < 0) {
            close(fd);
            fprintf(stderr, "tee: write failed: %s\n", path);
            return 1;
        }
    }
    close(fd);
    if (n < 0) {
        fprintf(stderr, "tee: read failed\n");
        return 1;
    }
    return 0;
}
