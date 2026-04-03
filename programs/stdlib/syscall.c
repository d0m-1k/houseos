#include <syscall.h>

int errno = 0;

static int32_t syscall_ret(uint32_t raw) {
    int32_t v = (int32_t)raw;
    if (v < 0) {
        errno = -v;
        return -1;
    }
    errno = 0;
    return v;
}

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
    __asm__ __volatile__("ud2");
    for (;;) __asm__ __volatile__("hlt");
}

int32_t read(int fd, void *buf, uint32_t size) { return syscall_ret(syscall3(SYSCALL_READ, (uint32_t)fd, (uint32_t)buf, size)); }
int32_t write(int fd, const void *buf, uint32_t size) { return syscall_ret(syscall3(SYSCALL_WRITE, (uint32_t)fd, (uint32_t)buf, size)); }
int32_t exec(const char *path) { return syscall_ret(syscall1(SYSCALL_EXEC, (uint32_t)path)); }
int32_t execv(const char *path, const char *cmdline) { return syscall_ret(syscall2(SYSCALL_EXECV, (uint32_t)path, (uint32_t)cmdline)); }
int32_t ioctl(int fd, uint32_t req, void *arg) { return syscall_ret(syscall3(SYSCALL_IOCTL, (uint32_t)fd, req, (uint32_t)arg)); }
int32_t open(const char *path, uint32_t flags) { return syscall_ret(syscall2(SYSCALL_OPEN, (uint32_t)path, flags)); }
int32_t close(int fd) { return syscall_ret(syscall1(SYSCALL_CLOSE, (uint32_t)fd)); }
int32_t mkdir(const char *path) { return syscall_ret(syscall1(SYSCALL_MKDIR, (uint32_t)path)); }
int32_t mkfifo(const char *path) { return syscall_ret(syscall1(SYSCALL_MKFIFO, (uint32_t)path)); }
int32_t mksock(const char *path) { return syscall_ret(syscall1(SYSCALL_MKSOCK, (uint32_t)path)); }
int32_t unlink(const char *path) { return syscall_ret(syscall1(SYSCALL_UNLINK, (uint32_t)path)); }
int32_t rmdir(const char *path) { return syscall_ret(syscall1(SYSCALL_RMDIR, (uint32_t)path)); }
int32_t link(const char *oldpath, const char *newpath) { return syscall_ret(syscall2(SYSCALL_LINK, (uint32_t)oldpath, (uint32_t)newpath)); }
int32_t list(const char *path, char *buf, uint32_t size) { return syscall_ret(syscall3(SYSCALL_LIST, (uint32_t)path, (uint32_t)buf, size)); }
int32_t append(int fd, const void *buf, uint32_t size) { return syscall_ret(syscall3(SYSCALL_APPEND, (uint32_t)fd, (uint32_t)buf, size)); }
int32_t spawn(const char *path, const char *tty) { return syscall_ret(syscall2(SYSCALL_SPAWN, (uint32_t)path, (uint32_t)tty)); }
int32_t spawnv(const char *path, const char *tty, const char *cmdline) { return syscall_ret(syscall3(SYSCALL_SPAWNV, (uint32_t)path, (uint32_t)tty, (uint32_t)cmdline)); }
int32_t task_state(int32_t pid) { return syscall_ret(syscall1(SYSCALL_TASK_STATE, (uint32_t)pid)); }
int32_t task_kill(int32_t pid) { return syscall_ret(syscall1(SYSCALL_TASK_KILL, (uint32_t)pid)); }
int32_t waitpid(int32_t pid, int32_t *status, uint32_t options) {
    return syscall_ret(syscall3(SYSCALL_WAITPID, (uint32_t)pid, (uint32_t)status, options));
}
int32_t mount(const char *fs_name, const char *mount_path) { return syscall_ret(syscall2(SYSCALL_MOUNT, (uint32_t)fs_name, (uint32_t)mount_path)); }
int32_t umount(const char *mount_path) { return syscall_ret(syscall1(SYSCALL_UMOUNT, (uint32_t)mount_path)); }
int32_t list_mounts(char *buf, uint32_t size) { return syscall_ret(syscall2(SYSCALL_LIST_MOUNTS, (uint32_t)buf, size)); }
void init_spawn_shells(void) { (void)syscall0(SYSCALL_INIT_SPAWN_SHELLS); }

int32_t socket(int32_t domain, int32_t type, int32_t protocol) {
    return syscall_ret(syscall3(SYSCALL_SOCKET, (uint32_t)domain, (uint32_t)type, (uint32_t)protocol));
}

int32_t bind(int32_t sockfd, const void *addr, uint32_t addrlen) {
    return syscall_ret(syscall3(SYSCALL_BIND, (uint32_t)sockfd, (uint32_t)addr, addrlen));
}

int32_t sendto(int32_t sockfd, const void *buf, uint32_t len, uint32_t flags, const void *addr, uint32_t addrlen) {
    syscall_udp_send_req_t req;
    req.buf = buf;
    req.len = len;
    req.flags = flags;
    req.addr = (const syscall_sockaddr_in_t*)addr;
    req.addrlen = addrlen;
    return syscall_ret(syscall2(SYSCALL_SENDTO, (uint32_t)sockfd, (uint32_t)&req));
}

int32_t recvfrom(int32_t sockfd, void *buf, uint32_t len, uint32_t flags, void *addr, uint32_t *addrlen) {
    syscall_udp_recv_req_t req;
    req.buf = buf;
    req.len = len;
    req.flags = flags;
    req.addr = (syscall_sockaddr_in_t*)addr;
    req.addrlen = addrlen;
    return syscall_ret(syscall2(SYSCALL_RECVFROM, (uint32_t)sockfd, (uint32_t)&req));
}

int32_t setsockopt(int32_t sockfd, int32_t level, int32_t optname, const void *optval, uint32_t optlen) {
    syscall_sockopt_req_t req;
    req.level = (uint32_t)level;
    req.optname = (uint32_t)optname;
    req.optval = optval;
    req.optlen = optlen;
    return syscall_ret(syscall2(SYSCALL_SETSOCKOPT, (uint32_t)sockfd, (uint32_t)&req));
}
