#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_rm(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    if (arg0 + 1 >= argc || arg0 + 2 < argc) {
        fprintf(stderr, "usage: rm <path>\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "rm: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "rm: cannot remove %s\n", path);
        return 1;
    }
    return 0;
}
