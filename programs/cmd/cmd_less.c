#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_less(int argc, char **argv, int arg0, const char *cwd) {
    int fd = fileno(stdin);
    char path[256];
    if (arg0 + 1 < argc) {
        if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) return 1;
        fd = open(path, 0);
        if (fd < 0) return 1;
    }
    less_stream(fd);
    if (fd != fileno(stdin)) close(fd);
    return 0;
}
