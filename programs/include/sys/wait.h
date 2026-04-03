#pragma once

#include <stdint.h>

#define WNOHANG 1u

int32_t waitpid(int32_t pid, int32_t *status, uint32_t options);
