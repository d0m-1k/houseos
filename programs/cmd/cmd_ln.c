#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <stdio.h>

int cmd_ln(int argc, char **argv, int arg0, const char *cwd) {
    char oldp[256];
    char newp[256];
    if (arg0 + 2 >= argc || arg0 + 3 < argc) {
        fprintf(stderr, "usage: ln <oldpath> <newpath>\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], oldp, sizeof(oldp)) != 0) {
        fprintf(stderr, "ln: bad oldpath: %s\n", argv[arg0 + 1]);
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 2], newp, sizeof(newp)) != 0) {
        fprintf(stderr, "ln: bad newpath: %s\n", argv[arg0 + 2]);
        return 1;
    }
    if (link(oldp, newp) != 0) {
        fprintf(stderr, "ln failed\n");
        return 1;
    }
    return 0;
}
