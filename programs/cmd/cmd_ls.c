#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <stdio.h>

int cmd_ls(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    char buf[CMD_BUF_SZ];
    const char *target = (arg0 + 1 < argc) ? argv[arg0 + 1] : cwd;
    if (normalize_path(cwd, target, path, sizeof(path)) != 0) return 1;
    if (list(path, buf, sizeof(buf)) < 0) {
        fprintf(stderr, "ls: path not found\n");
        return 1;
    }
    fprintf(stdout, "%s\n", buf);
    return 0;
}
