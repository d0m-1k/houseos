#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <stdio.h>

int cmd_umount(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    if (arg0 + 1 >= argc) {
        fprintf(stderr, "usage: umount <mountpoint>\n");
        return 1;
    }
    if (arg0 + 2 < argc) {
        fprintf(stderr, "usage: umount <mountpoint>\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "umount: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }
    if (umount(path) != 0) {
        fprintf(stderr, "umount failed\n");
        return 1;
    }
    return 0;
}
