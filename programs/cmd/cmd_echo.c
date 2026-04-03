#include "commands.h"
#include <stdio.h>

int cmd_echo(int argc, char **argv, int arg0, const char *cwd) {
    (void)cwd;
    for (int i = arg0 + 1; i < argc; i++) {
        if (i > arg0 + 1) fprintf(stdout, " ");
        fprintf(stdout, "%s", argv[i]);
    }
    fprintf(stdout, "\n");
    return 0;
}
