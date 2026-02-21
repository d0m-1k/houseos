#pragma once

#include <drivers/filesystem/memfs.h>

struct gshell_args {
    memfs *fs;
};

void gshell_run(void *arg);