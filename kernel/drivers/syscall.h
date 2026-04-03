#pragma once

#include <stdint.h>

void syscall_bind_stdio(const char *path);
void syscall_set_devfs_ctx(void *ctx);
int syscall_task_fd_path(uint32_t pid, uint32_t fd, char *out, uint32_t cap);
uint32_t syscall_task_fd_max(void);
void syscall_handler(void);
