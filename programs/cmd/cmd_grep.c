#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <stdio.h>

int cmd_grep(int argc, char **argv, int arg0, const char *cwd) {
    int fd = fileno(stdin);
    char path[256];
    if (arg0 + 1 >= argc) {
        fprintf(stderr, "usage: grep <pattern> [file]\n");
        return 1;
    }
    if (arg0 + 3 < argc) {
        fprintf(stderr, "usage: grep <pattern> [file]\n");
        return 1;
    }
    if (arg0 + 2 < argc) {
        if (normalize_path(cwd, argv[arg0 + 2], path, sizeof(path)) != 0) {
            fprintf(stderr, "grep: bad path: %s\n", argv[arg0 + 2]);
            return 1;
        }
        fd = open(path, 0);
        if (fd < 0) {
            fprintf(stderr, "grep: open failed: %s\n", path);
            return 1;
        }
    }
    grep_stream(fd, argv[arg0 + 1]);
    if (fd != fileno(stdin)) close(fd);
    return 0;
}
