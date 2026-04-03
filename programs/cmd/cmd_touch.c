#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_touch(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    int fd;
    if (arg0 + 1 >= argc || normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) return 1;
    fd = open(path, 1);
    if (fd < 0) return 1;
    close(fd);
    return 0;
}
