#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"

int cmd_printf(int argc, char **argv, int arg0, const char *cwd) {
    (void)cwd;
    return cmd_printf_impl(argc, argv, arg0);
}
