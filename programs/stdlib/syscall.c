#include <syscall.h>

uint32_t syscall0(uint32_t n) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

uint32_t syscall1(uint32_t n, uint32_t a1) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

uint32_t syscall2(uint32_t n, uint32_t a1, uint32_t a2) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return ret;
}

uint32_t syscall3(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

void yield(void) { (void)syscall0(SYSCALL_YIELD); }
void sleep(uint32_t ms) { (void)syscall1(SYSCALL_SLEEP, ms); }
uint32_t get_ticks(void) { return syscall0(SYSCALL_GET_TICKS); }

void exit(int code) {
    (void)syscall1(SYSCALL_EXIT, (uint32_t)code);
    for (;;) __asm__ __volatile__("hlt");
}

int32_t read(int fd, void *buf, uint32_t size) { return (int32_t)syscall3(SYSCALL_READ, (uint32_t)fd, (uint32_t)buf, size); }
int32_t write(int fd, const void *buf, uint32_t size) { return (int32_t)syscall3(SYSCALL_WRITE, (uint32_t)fd, (uint32_t)buf, size); }
int32_t exec(const char *path) { return (int32_t)syscall1(SYSCALL_EXEC, (uint32_t)path); }
int32_t ioctl(int fd, uint32_t req, void *arg) { return (int32_t)syscall3(SYSCALL_IOCTL, (uint32_t)fd, req, (uint32_t)arg); }
int32_t open(const char *path, uint32_t flags) { return (int32_t)syscall2(SYSCALL_OPEN, (uint32_t)path, flags); }
int32_t close(int fd) { return (int32_t)syscall1(SYSCALL_CLOSE, (uint32_t)fd); }
int32_t mkdir(const char *path) { return (int32_t)syscall1(SYSCALL_MKDIR, (uint32_t)path); }
int32_t mkfifo(const char *path) { return (int32_t)syscall1(SYSCALL_MKFIFO, (uint32_t)path); }
int32_t mksock(const char *path) { return (int32_t)syscall1(SYSCALL_MKSOCK, (uint32_t)path); }
int32_t unlink(const char *path) { return (int32_t)syscall1(SYSCALL_UNLINK, (uint32_t)path); }
int32_t rmdir(const char *path) { return (int32_t)syscall1(SYSCALL_RMDIR, (uint32_t)path); }
int32_t list(const char *path, char *buf, uint32_t size) { return (int32_t)syscall3(SYSCALL_LIST, (uint32_t)path, (uint32_t)buf, size); }
int32_t append(int fd, const void *buf, uint32_t size) { return (int32_t)syscall3(SYSCALL_APPEND, (uint32_t)fd, (uint32_t)buf, size); }
int32_t spawn(const char *path, const char *tty) { return (int32_t)syscall2(SYSCALL_SPAWN, (uint32_t)path, (uint32_t)tty); }
int32_t task_state(int32_t pid) { return (int32_t)syscall1(SYSCALL_TASK_STATE, (uint32_t)pid); }
void init_spawn_shells(void) { (void)syscall0(SYSCALL_INIT_SPAWN_SHELLS); }
