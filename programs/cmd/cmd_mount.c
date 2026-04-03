#include "commands.h"
#include "cmd_common.h"
#include <syscall.h>
#include <stdio.h>
#include <string.h>

int cmd_mount(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    if (arg0 + 1 >= argc) {
        char mounts[1024];
        int32_t n = list_mounts(mounts, sizeof(mounts));
        if (n < 0) {
            fprintf(stderr, "mount failed\n");
            return 1;
        }
        write(fileno(stdout), mounts, (uint32_t)n);
        return 0;
    }
    if (arg0 + 2 >= argc) {
        fprintf(stderr, "usage: mount [<fs> <mountpoint>]\n");
        return 1;
    }
    if (normalize_path(cwd, argv[arg0 + 2], path, sizeof(path)) != 0) return 1;
    {
        char probe[16];
        if (list(path, probe, sizeof(probe)) < 0) {
            fprintf(stderr, "mount: target path not found\n");
            return 1;
        }
    }
    if (mount(argv[arg0 + 1], path) != 0) {
        fprintf(stderr, "mount failed\n");
        return 1;
    }
    return 0;
}
