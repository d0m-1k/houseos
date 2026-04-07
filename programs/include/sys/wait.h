#pragma once

#include <stdint.h>
#include <sys/types.h>

#define WNOHANG 1u

pid_t waitpid(pid_t pid, int32_t *status, uint32_t options);
pid_t wait(int32_t *status);

#define WIFEXITED(s) ((((s) & 0x7F) == 0))
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s) ((((s) & 0x7F) != 0))
#define WTERMSIG(s) ((s) & 0x7F)
