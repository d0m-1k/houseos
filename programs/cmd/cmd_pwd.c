#include "commands.h"
#include <stdio.h>

int cmd_pwd(int argc, char **argv, int arg0, const char *cwd) {
    (void)argc;
    (void)argv;
    (void)arg0;
    fprintf(stdout, "%s\n", cwd);
    return 0;
}
