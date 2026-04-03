#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_tee(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    char buf[CMD_BUF_SZ];
    int fd;
    int32_t n;
    if (arg0 + 1 >= argc || normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) return 1;
    fd = open(path, 1);
    if (fd < 0) return 1;
    while ((n = read(fileno(stdin), buf, sizeof(buf))) > 0) {
        write(fileno(stdout), buf, (uint32_t)n);
        append(fd, buf, (uint32_t)n);
    }
    close(fd);
    return (n < 0) ? 1 : 0;
}
