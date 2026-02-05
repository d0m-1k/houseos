#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MAX_ARGS 16

struct shell_args {
    int argc;
    char *argv[MAX_ARGS];
};

void shell_run(void);