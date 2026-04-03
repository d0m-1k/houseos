#include "commands.h"
#include <stdio.h>
#include "cmd_common.h"

int cmd_hexdump(int argc, char **argv, int arg0, const char *cwd) {
    return cmd_hexdump_impl(argc, argv, arg0, cwd);
}
