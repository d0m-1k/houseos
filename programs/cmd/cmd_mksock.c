#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_mksock(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    if (arg0 + 1 >= argc || normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) return 1;
    return (mksock(path) == 0) ? 0 : 1;
}
