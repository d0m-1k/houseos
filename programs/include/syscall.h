#pragma once

#include <stdint.h>

enum {
    SYSCALL_YIELD = 0,
    SYSCALL_SLEEP = 1,
    SYSCALL_GET_TICKS = 2,
    SYSCALL_EXIT = 3,
    SYSCALL_READ = 4,
    SYSCALL_WRITE = 5,
    SYSCALL_EXEC = 6,
    SYSCALL_IOCTL = 7,
    SYSCALL_OPEN = 8,
    SYSCALL_CLOSE = 9,
    SYSCALL_MKDIR = 10,
    SYSCALL_UNLINK = 11,
    SYSCALL_RMDIR = 12,
    SYSCALL_LIST = 13,
    SYSCALL_APPEND = 14,
    SYSCALL_SPAWN = 15,
    SYSCALL_TASK_STATE = 16,
    SYSCALL_INIT_SPAWN_SHELLS = 17,
    SYSCALL_MKFIFO = 18,
    SYSCALL_MKSOCK = 19,
};

uint32_t syscall0(uint32_t n);
uint32_t syscall1(uint32_t n, uint32_t a1);
uint32_t syscall2(uint32_t n, uint32_t a1, uint32_t a2);
uint32_t syscall3(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3);

void yield(void);
void sleep(uint32_t ms);
uint32_t get_ticks(void);
void exit(int code);
int32_t read(int fd, void *buf, uint32_t size);
int32_t write(int fd, const void *buf, uint32_t size);
int32_t exec(const char *path);
int32_t ioctl(int fd, uint32_t req, void *arg);
int32_t open(const char *path, uint32_t flags);
int32_t close(int fd);
int32_t mkdir(const char *path);
int32_t mkfifo(const char *path);
int32_t mksock(const char *path);
int32_t unlink(const char *path);
int32_t rmdir(const char *path);
int32_t list(const char *path, char *buf, uint32_t size);
int32_t append(int fd, const void *buf, uint32_t size);
int32_t spawn(const char *path, const char *tty);
int32_t task_state(int32_t pid);
void init_spawn_shells(void);
