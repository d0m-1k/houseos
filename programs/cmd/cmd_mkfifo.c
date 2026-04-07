#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"
#include <syscall.h>

int cmd_mkfifo(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    if (arg0 + 1 >= argc || arg0 + 2 < argc) {
        fprintf(stderr, "usage: mkfifo <path>\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "mkfifo: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }
    if (mkfifo(path) != 0) {
        fprintf(stderr, "mkfifo failed: %s\n", path);
        return 1;
    }
    return 0;
}
