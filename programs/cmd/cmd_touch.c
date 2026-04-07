#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_touch(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    int fd;
    if (arg0 + 1 >= argc || arg0 + 2 < argc) {
        fprintf(stderr, "usage: touch <path>\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "touch: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }
    fd = open(path, 1);
    if (fd < 0) {
        fprintf(stderr, "touch: open failed: %s\n", path);
        return 1;
    }
    close(fd);
    return 0;
}
