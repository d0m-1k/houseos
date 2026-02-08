#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <drivers/filesystem/memfs.h>

#define MAX_ARGS 16
#define MAX_COMMANDS 64

struct shell_args {
    int argc;
    char *argv[MAX_ARGS];
};

struct shell_command {
    char *name;
    uint32_t (*func)(struct shell_args *);
};

static struct shell_command commands[MAX_COMMANDS];
static size_t commands_count = 0;

void shell_run(memfs *fs);
void register_command(const char *name, uint32_t (*func)(struct shell_args *));