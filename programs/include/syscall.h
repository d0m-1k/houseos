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
    SYSCALL_EXECV = 20,
    SYSCALL_SPAWNV = 21,
    SYSCALL_MOUNT = 22,
    SYSCALL_LIST_MOUNTS = 23,
    SYSCALL_LINK = 24,
    SYSCALL_UMOUNT = 25,
    SYSCALL_SOCKET = 26,
    SYSCALL_BIND = 27,
    SYSCALL_SENDTO = 28,
    SYSCALL_RECVFROM = 29,
    SYSCALL_SETSOCKOPT = 30,
    SYSCALL_TASK_KILL = 31,
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
int32_t execv(const char *path, const char *cmdline);
int32_t ioctl(int fd, uint32_t req, void *arg);
int32_t open(const char *path, uint32_t flags);
int32_t close(int fd);
int32_t mkdir(const char *path);
int32_t mkfifo(const char *path);
int32_t mksock(const char *path);
int32_t unlink(const char *path);
int32_t rmdir(const char *path);
int32_t link(const char *oldpath, const char *newpath);
int32_t list(const char *path, char *buf, uint32_t size);
int32_t append(int fd, const void *buf, uint32_t size);
int32_t spawn(const char *path, const char *tty);
int32_t spawnv(const char *path, const char *tty, const char *cmdline);
int32_t task_state(int32_t pid);
int32_t task_kill(int32_t pid);
int32_t mount(const char *fs_name, const char *mount_path);
int32_t umount(const char *mount_path);
int32_t list_mounts(char *buf, uint32_t size);
void init_spawn_shells(void);

typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} __attribute__((packed)) syscall_sockaddr_in_t;

typedef struct {
    const void *buf;
    uint32_t len;
    uint32_t flags;
    const syscall_sockaddr_in_t *addr;
    uint32_t addrlen;
} syscall_udp_send_req_t;

typedef struct {
    void *buf;
    uint32_t len;
    uint32_t flags;
    syscall_sockaddr_in_t *addr;
    uint32_t *addrlen;
} syscall_udp_recv_req_t;

typedef struct {
    uint32_t level;
    uint32_t optname;
    const void *optval;
    uint32_t optlen;
} syscall_sockopt_req_t;

int32_t socket(int32_t domain, int32_t type, int32_t protocol);
int32_t bind(int32_t sockfd, const void *addr, uint32_t addrlen);
int32_t sendto(int32_t sockfd, const void *buf, uint32_t len, uint32_t flags, const void *addr, uint32_t addrlen);
int32_t recvfrom(int32_t sockfd, void *buf, uint32_t len, uint32_t flags, void *addr, uint32_t *addrlen);
int32_t setsockopt(int32_t sockfd, int32_t level, int32_t optname, const void *optval, uint32_t optlen);
