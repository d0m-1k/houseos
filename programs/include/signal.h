#pragma once

#include <sys/types.h>

#define SIGINT 2
#define SIGKILL 9
#define SIGTERM 15

int kill(pid_t pid, int sig);
