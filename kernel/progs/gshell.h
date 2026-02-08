#pragma once

#include <stdint.h>
#include <progs/shell.h>
#include <drivers/filesystem/memfs.h>

void gshell_run(memfs *fs);