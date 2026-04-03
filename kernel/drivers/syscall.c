#include <drivers/syscall.h>
#include <asm/idt.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <asm/modes.h>
#include <drivers/filesystem/vfs.h>
#include <drivers/filesystem/fat32.h>
#include <drivers/elf_loader.h>
#include <drivers/tty.h>
#include <drivers/serial.h>
#include <string.h>

enum {
    SYS_YIELD = 0,
    SYS_SLEEP = 1,
    SYS_GET_TICKS = 2,
    SYS_EXIT = 3,
    SYS_READ = 4,
    SYS_WRITE = 5,
    SYS_EXEC = 6,
    SYS_IOCTL = 7,
    SYS_OPEN = 8,
    SYS_CLOSE = 9,
    SYS_MKDIR = 10,
    SYS_UNLINK = 11,
    SYS_RMDIR = 12,
    SYS_LIST = 13,
    SYS_APPEND = 14,
    SYS_SPAWN = 15,
    SYS_TASK_STATE = 16,
    SYS_INIT_SPAWN_SHELLS = 17,
    SYS_MKFIFO = 18,
    SYS_MKSOCK = 19,
    SYS_EXECV = 20,
    SYS_SPAWNV = 21,
    SYS_MOUNT = 22,
    SYS_LIST_MOUNTS = 23,
    SYS_LINK = 24,
    SYS_UMOUNT = 25,
    SYS_SOCKET = 26,
    SYS_BIND = 27,
    SYS_SENDTO = 28,
    SYS_RECVFROM = 29,
    SYS_SETSOCKOPT = 30,
    SYS_TASK_KILL = 31,
};

vfs_t *g_root_fs_for_syscalls = NULL;
static void *g_devfs_ctx = NULL;

#define FD_MAX 32
enum {
    FD_KIND_VFS = 0,
    FD_KIND_UDP = 1,
};

typedef struct {
    uint8_t used;
    uint8_t kind;
    int16_t sock_id;
    char path[256];
} fd_entry_t;

typedef struct {
    char path[256];
    char tty[256];
    char cmdline[512];
} spawn_req_t;

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

#define AF_INET 2u
#define SOCK_DGRAM 2u
#define IPPROTO_UDP 17u
#define INADDR_ANY 0x00000000u
#define INADDR_LOOPBACK 0x7F000001u
#define UDP_MAX_SOCKETS 8u
#define UDP_QUEUE_MAX 8u
#define UDP_PAYLOAD_MAX 512u
#define UDP_EPHEMERAL_START 49152u
#define UDP_EPHEMERAL_END 65535u

typedef struct {
    uint8_t used;
    uint32_t src_addr;
    uint16_t src_port;
    uint16_t len;
    uint8_t payload[UDP_PAYLOAD_MAX];
} udp_datagram_t;

typedef struct {
    uint8_t used;
    uint8_t bound;
    uint32_t bind_addr;
    uint16_t bind_port;
    uint16_t rcv_timeout_ms;
    udp_datagram_t q[UDP_QUEUE_MAX];
    uint8_t q_head;
    uint8_t q_tail;
    uint8_t q_len;
} udp_socket_t;

static udp_socket_t g_udp_sockets[UDP_MAX_SOCKETS];
static uint16_t g_udp_next_ephemeral = UDP_EPHEMERAL_START;

#define USER_ARG_MAX 32
#define USER_ARG_TOKEN 128

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static uint16_t be16_to_cpu(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}

static uint16_t cpu_to_be16(uint16_t x) {
    return be16_to_cpu(x);
}

static uint32_t be32_to_cpu(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}

static uint32_t cpu_to_be32(uint32_t x) {
    return be32_to_cpu(x);
}

static int udp_find_bound(uint32_t addr, uint16_t port) {
    for (uint32_t i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!g_udp_sockets[i].used || !g_udp_sockets[i].bound) continue;
        if (g_udp_sockets[i].bind_port != port) continue;
        if (g_udp_sockets[i].bind_addr == addr || g_udp_sockets[i].bind_addr == INADDR_ANY || addr == INADDR_ANY) return (int)i;
    }
    return -1;
}

static int udp_port_in_use(uint32_t addr, uint16_t port, int exclude_sid) {
    for (uint32_t i = 0; i < UDP_MAX_SOCKETS; i++) {
        const udp_socket_t *s = &g_udp_sockets[i];
        if ((int)i == exclude_sid || !s->used || !s->bound) continue;
        if (s->bind_port != port) continue;
        if (s->bind_addr == INADDR_ANY || addr == INADDR_ANY || s->bind_addr == addr) return 1;
    }
    return 0;
}

static int udp_autobind(int sid) {
    uint32_t tries = (UDP_EPHEMERAL_END - UDP_EPHEMERAL_START + 1u);
    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS) return -1;
    for (uint32_t i = 0; i < tries; i++) {
        uint16_t p = g_udp_next_ephemeral;
        g_udp_next_ephemeral++;
        if (g_udp_next_ephemeral < UDP_EPHEMERAL_START) g_udp_next_ephemeral = UDP_EPHEMERAL_START;
        if (!udp_port_in_use(INADDR_ANY, p, sid)) {
            g_udp_sockets[sid].bound = 1;
            g_udp_sockets[sid].bind_addr = INADDR_ANY;
            g_udp_sockets[sid].bind_port = p;
            return 0;
        }
    }
    return -1;
}

static int udp_socket_alloc(void) {
    for (uint32_t i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!g_udp_sockets[i].used) {
            memset(&g_udp_sockets[i], 0, sizeof(g_udp_sockets[i]));
            g_udp_sockets[i].used = 1;
            return (int)i;
        }
    }
    return -1;
}

static void udp_socket_free(int sid) {
    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS) return;
    memset(&g_udp_sockets[sid], 0, sizeof(g_udp_sockets[sid]));
}

static int udp_bind_socket(int sid, const syscall_sockaddr_in_t *ua, uint32_t addrlen) {
    uint32_t addr;
    uint16_t port;
    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS || !ua) return -1;
    if (addrlen < sizeof(syscall_sockaddr_in_t)) return -1;
    if (ua->sin_family != AF_INET) return -1;
    port = be16_to_cpu(ua->sin_port);
    addr = be32_to_cpu(ua->sin_addr);
    if (port == 0) return -1;
    if (addr != INADDR_ANY && addr != INADDR_LOOPBACK) return -1;
    if (udp_port_in_use(addr, port, sid)) return -1;
    g_udp_sockets[sid].bound = 1;
    g_udp_sockets[sid].bind_addr = addr;
    g_udp_sockets[sid].bind_port = port;
    return 0;
}

static int udp_sendto_socket(int sid, const syscall_udp_send_req_t *req) {
    uint32_t dst_addr;
    uint16_t dst_port;
    int dst_sid;
    udp_socket_t *src;
    udp_socket_t *dst;
    udp_datagram_t *pkt;

    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS || !req || !req->buf || !req->addr) return -1;
    if (req->addrlen < sizeof(syscall_sockaddr_in_t)) return -1;
    if (req->len > UDP_PAYLOAD_MAX) return -1;
    if (req->addr->sin_family != AF_INET) return -1;

    dst_addr = be32_to_cpu(req->addr->sin_addr);
    dst_port = be16_to_cpu(req->addr->sin_port);
    if (dst_port == 0) return -1;
    if (dst_addr != INADDR_LOOPBACK && dst_addr != INADDR_ANY) return -1;

    src = &g_udp_sockets[sid];
    if (!src->bound && udp_autobind(sid) != 0) return -1;

    dst_sid = udp_find_bound((dst_addr == INADDR_ANY) ? INADDR_LOOPBACK : dst_addr, dst_port);
    if (dst_sid < 0) return -1;
    dst = &g_udp_sockets[dst_sid];
    if (dst->q_len >= UDP_QUEUE_MAX) return -1;

    pkt = &dst->q[dst->q_tail];
    memset(pkt, 0, sizeof(*pkt));
    pkt->used = 1;
    pkt->src_addr = (src->bind_addr == INADDR_ANY) ? INADDR_LOOPBACK : src->bind_addr;
    pkt->src_port = src->bind_port;
    pkt->len = (uint16_t)req->len;
    if (pkt->len) memcpy(pkt->payload, req->buf, pkt->len);

    dst->q_tail = (uint8_t)((dst->q_tail + 1u) % UDP_QUEUE_MAX);
    dst->q_len++;
    return (int)req->len;
}

static int udp_recvfrom_socket(int sid, syscall_udp_recv_req_t *req) {
    udp_socket_t *s;
    udp_datagram_t *pkt;
    uint32_t to_copy;

    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS || !req || !req->buf) return -1;
    s = &g_udp_sockets[sid];
    if (s->q_len == 0) return -1;

    pkt = &s->q[s->q_head];
    if (!pkt->used) return -1;

    to_copy = (req->len < pkt->len) ? req->len : pkt->len;
    if (to_copy) memcpy(req->buf, pkt->payload, to_copy);

    if (req->addr && req->addrlen && *req->addrlen >= sizeof(syscall_sockaddr_in_t)) {
        memset(req->addr, 0, sizeof(*req->addr));
        req->addr->sin_family = AF_INET;
        req->addr->sin_port = cpu_to_be16(pkt->src_port);
        req->addr->sin_addr = cpu_to_be32(pkt->src_addr);
        *req->addrlen = sizeof(syscall_sockaddr_in_t);
    }

    memset(pkt, 0, sizeof(*pkt));
    s->q_head = (uint8_t)((s->q_head + 1u) % UDP_QUEUE_MAX);
    s->q_len--;
    return (int)to_copy;
}

static int build_user_stack_from_cmdline(const char *cmdline, uint32_t *user_esp_out) {
    char args[USER_ARG_MAX][USER_ARG_TOKEN];
    uint32_t arg_ptr[USER_ARG_MAX];
    uint32_t argc = 0;
    uint32_t i = 0;
    uint32_t sp;

    if (!user_esp_out) return -1;
    if (!cmdline) cmdline = "";

    while (cmdline[i]) {
        uint32_t tlen = 0;
        int in_sq = 0;
        int in_dq = 0;

        while (cmdline[i] && is_space(cmdline[i])) i++;
        if (!cmdline[i]) break;
        if (argc >= USER_ARG_MAX) return -1;

        while (cmdline[i]) {
            char c = cmdline[i];
            if (!in_sq && !in_dq && is_space(c)) break;
            if (c == '\\' && cmdline[i + 1]) {
                i++;
                c = cmdline[i];
            } else if (c == '\'' && !in_dq) {
                in_sq = !in_sq;
                i++;
                continue;
            } else if (c == '"' && !in_sq) {
                in_dq = !in_dq;
                i++;
                continue;
            }
            if (tlen + 1 >= USER_ARG_TOKEN) return -1;
            args[argc][tlen++] = c;
            i++;
        }
        if (in_sq || in_dq) return -1;
        args[argc][tlen] = '\0';
        argc++;
    }

    sp = USER_STACK_TOP & ~3u;

    for (int32_t a = (int32_t)argc - 1; a >= 0; a--) {
        uint32_t len = (uint32_t)strlen(args[a]) + 1;
        if (sp < USER_VADDR_BASE + len + 256) return -1;
        sp -= len;
        memcpy((void*)(uintptr_t)sp, args[a], len);
        arg_ptr[a] = sp;
    }

    sp &= ~3u;
    sp -= 4;
    *(uint32_t*)(uintptr_t)sp = 0;
    for (int32_t a = (int32_t)argc - 1; a >= 0; a--) {
        sp -= 4;
        *(uint32_t*)(uintptr_t)sp = arg_ptr[a];
    }
    sp -= 4;
    *(uint32_t*)(uintptr_t)sp = argc;

    *user_esp_out = sp;
    return 0;
}

static fd_entry_t g_fds[FD_MAX];
static uint8_t g_fd_init_done = 0;
static fd_entry_t g_task_fds[MAX_TASKS][FD_MAX];
static uint8_t g_task_fd_init[MAX_TASKS];
static fat32_fs_t g_mount_fat_ctx[VFS_MAX_MOUNTS];
static uint8_t g_mount_fat_used[VFS_MAX_MOUNTS];
static char g_mount_fat_path[VFS_MAX_MOUNTS][256];

static const char *g_shell_ttys[] = {
    "/dev/tty/3", "/dev/tty/4", "/dev/tty/5", "/dev/tty/6",
    "/dev/tty/7", "/dev/tty/8",
    "/dev/tty/S0"
};

static int current_task_slot(void) {
    if (!current_task) return -1;
    {
        int slot = (int)(current_task - tasks);
        if (slot < 0 || slot >= MAX_TASKS) return -1;
        return slot;
    }
}

uint32_t syscall_task_fd_max(void) {
    return FD_MAX;
}

int syscall_task_fd_path(uint32_t pid, uint32_t fd, char *out, uint32_t cap) {
    task_t *task;
    int slot;
    fd_entry_t *fds;

    if (!out || cap < 2) return -1;
    task = task_find_by_pid(pid);
    if (!task) return -1;
    slot = (int)(task - tasks);
    if (slot < 0 || slot >= MAX_TASKS) return -1;
    if (fd >= FD_MAX) return -1;
    fds = g_task_fds[slot];
    if (!g_task_fd_init[slot] || !fds[fd].used) return -1;
    if (fds[fd].kind == FD_KIND_VFS) {
        if (fds[fd].path[0] == '\0') return -1;
        strncpy(out, fds[fd].path, cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    if (fds[fd].kind == FD_KIND_UDP) {
        strncpy(out, "udp:", cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    return -1;
}

static int ensure_current_task_user_space(void) {
    uint32_t slot_idx = 0;
    uint32_t slot_phys = 0;
    uint32_t user_cr3 = 0;

    if (!current_task) return -1;
    if (current_task->user_slot != (uint32_t)-1 && current_task->cr3) {
        mm_switch_cr3(current_task->cr3);
        return 0;
    }

    if (mm_user_slot_alloc(&slot_idx, &slot_phys) != 0) return -1;
    user_cr3 = mm_user_cr3_create(slot_phys);
    if (!user_cr3) {
        mm_user_slot_free(slot_idx);
        return -1;
    }

    current_task->user_slot = slot_idx;
    current_task->user_phys_base = slot_phys;
    current_task->cr3 = user_cr3;
    mm_switch_cr3(current_task->cr3);
    return 0;
}

static int copy_user_path(const char *user, char *out, uint32_t cap) {
    if (!user || !out || cap < 2) return -1;
    for (uint32_t i = 0; i < cap; i++) {
        out[i] = user[i];
        if (out[i] == '\0') return (out[0] == '/') ? 0 : -1;
    }
    return -1;
}

static int copy_user_string(const char *user, char *out, uint32_t cap) {
    if (!user || !out || cap < 2) return -1;
    for (uint32_t i = 0; i < cap; i++) {
        out[i] = user[i];
        if (out[i] == '\0') return 0;
    }
    return -1;
}

static int normalize_mount_path_local(const char *in, char *out, uint32_t cap) {
    uint32_t n;
    if (!in || !out || cap < 2) return -1;
    if (in[0] != '/') return -1;
    n = (uint32_t)strlen(in);
    if (n == 0 || n >= cap) return -1;
    if (n > 1 && in[n - 1] == '/') n--;
    memcpy(out, in, n);
    out[n] = '\0';
    return 0;
}

static void fd_table_init(fd_entry_t *fds, uint8_t *done, const char *tty_path) {
    if (*done) return;
    memset(fds, 0, sizeof(fd_entry_t) * FD_MAX);
    strncpy(fds[0].path, tty_path, sizeof(fds[0].path) - 1);
    strncpy(fds[1].path, tty_path, sizeof(fds[1].path) - 1);
    strncpy(fds[2].path, tty_path, sizeof(fds[2].path) - 1);
    fds[0].path[sizeof(fds[0].path) - 1] = '\0';
    fds[1].path[sizeof(fds[1].path) - 1] = '\0';
    fds[2].path[sizeof(fds[2].path) - 1] = '\0';
    fds[0].used = 1;
    fds[1].used = 1;
    fds[2].used = 1;
    fds[0].kind = FD_KIND_VFS;
    fds[1].kind = FD_KIND_VFS;
    fds[2].kind = FD_KIND_VFS;
    fds[0].sock_id = -1;
    fds[1].sock_id = -1;
    fds[2].sock_id = -1;
    *done = 1;
}

static fd_entry_t *fd_current(void) {
    int slot = current_task_slot();
    if (slot < 0) {
        fd_table_init(g_fds, &g_fd_init_done, "/dev/tty/1");
        return g_fds;
    }
    fd_table_init(g_task_fds[slot], &g_task_fd_init[slot], "/dev/tty/1");
    return g_task_fds[slot];
}

void syscall_bind_stdio(const char *path) {
    fd_entry_t *fds;
    if (!path || path[0] == '\0') return;
    fds = fd_current();
    strncpy(fds[0].path, path, sizeof(fds[0].path) - 1);
    strncpy(fds[1].path, path, sizeof(fds[1].path) - 1);
    strncpy(fds[2].path, path, sizeof(fds[2].path) - 1);
    fds[0].path[sizeof(fds[0].path) - 1] = '\0';
    fds[1].path[sizeof(fds[1].path) - 1] = '\0';
    fds[2].path[sizeof(fds[2].path) - 1] = '\0';
    fds[0].used = 1;
    fds[1].used = 1;
    fds[2].used = 1;
    fds[0].kind = FD_KIND_VFS;
    fds[1].kind = FD_KIND_VFS;
    fds[2].kind = FD_KIND_VFS;
    fds[0].sock_id = -1;
    fds[1].sock_id = -1;
    fds[2].sock_id = -1;
}

void syscall_set_devfs_ctx(void *ctx) {
    g_devfs_ctx = ctx;
}

static int fd_alloc(const char *path) {
    fd_entry_t *fds = fd_current();
    for (int i = 3; i < FD_MAX; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].kind = FD_KIND_VFS;
            fds[i].sock_id = -1;
            strncpy(fds[i].path, path, sizeof(fds[i].path) - 1);
            fds[i].path[sizeof(fds[i].path) - 1] = '\0';
            return i;
        }
    }
    return -1;
}

static int fd_alloc_udp(int sid) {
    fd_entry_t *fds = fd_current();
    for (int i = 3; i < FD_MAX; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].kind = FD_KIND_UDP;
            fds[i].sock_id = (int16_t)sid;
            fds[i].path[0] = '\0';
            return i;
        }
    }
    return -1;
}

static const char *fd_path(uint32_t fd) {
    fd_entry_t *fds = fd_current();
    if (fd >= FD_MAX || !fds[fd].used || fds[fd].kind != FD_KIND_VFS) return NULL;
    return fds[fd].path;
}

static int fd_udp_sid(uint32_t fd) {
    fd_entry_t *fds = fd_current();
    if (fd >= FD_MAX || !fds[fd].used || fds[fd].kind != FD_KIND_UDP) return -1;
    if (fds[fd].sock_id < 0) return -1;
    return (int)fds[fd].sock_id;
}

static void spawned_user_task(void *arg) {
    spawn_req_t *req = (spawn_req_t*)arg;
    uint32_t entry = 0;
    uint32_t user_esp = USER_STACK_TOP;
    if (!req) task_exit();
    syscall_bind_stdio(req->tty);
    if (current_task) {
        strncpy(current_task->tty_path, req->tty, sizeof(current_task->tty_path) - 1);
        current_task->tty_path[sizeof(current_task->tty_path) - 1] = '\0';
        strncpy(current_task->prog_path, req->path, sizeof(current_task->prog_path) - 1);
        current_task->prog_path[sizeof(current_task->prog_path) - 1] = '\0';
        strncpy(current_task->cmdline, req->cmdline, sizeof(current_task->cmdline) - 1);
        current_task->cmdline[sizeof(current_task->cmdline) - 1] = '\0';
    }
    if (ensure_current_task_user_space() != 0) {
        kfree(req);
        task_exit();
    }

    if (!g_root_fs_for_syscalls || elf_load_from_vfs(g_root_fs_for_syscalls, req->path, &entry) != 0) {
        (void)elf_get_last_error();
        kfree(req);
        task_exit();
    }

    if (build_user_stack_from_cmdline(req->cmdline[0] ? req->cmdline : NULL, &user_esp) != 0) {
        kfree(req);
        task_exit();
    }

    kfree(req);
    jump_to_ring3(entry, user_esp, 0x202);
    task_exit();
}

static int spawn_user_program_ex(const char *path, const char *tty, const char *cmdline) {
    spawn_req_t *req;
    int pid;
    if (!path || !tty || tty[0] != '/') return -1;
    req = (spawn_req_t*)kmalloc(sizeof(spawn_req_t));
    if (!req) return -1;
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';
    strncpy(req->tty, tty, sizeof(req->tty) - 1);
    req->tty[sizeof(req->tty) - 1] = '\0';
    if (cmdline) {
        strncpy(req->cmdline, cmdline, sizeof(req->cmdline) - 1);
        req->cmdline[sizeof(req->cmdline) - 1] = '\0';
    } else {
        req->cmdline[0] = '\0';
    }
    pid = task_create(spawned_user_task, req);
    if (pid < 0) {
        kfree(req);
        return -1;
    }
    return pid;
}

static int spawn_user_program(const char *path, const char *tty) {
    return spawn_user_program_ex(path, tty, NULL);
}

uint32_t do_syscall_impl(
    uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx, struct interrupt_frame *f
) {
    fd_current();

    switch (eax) {
        case SYS_YIELD:
            task_yield();
            return 0;
        case SYS_SLEEP:
            sleep(ebx);
            return 0;
        case SYS_GET_TICKS:
            return 0;
        case SYS_EXIT:
            task_exit();
            return 0;
        case SYS_READ: {
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (!ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)vfs_read(g_root_fs_for_syscalls, path, (void*)ecx, edx);
        }
        case SYS_WRITE: {
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (!ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)vfs_write(g_root_fs_for_syscalls, path, (const void*)ecx, edx);
        }
        case SYS_EXEC: {
            char path[256];
            uint32_t entry = 0;
            uint32_t user_esp = USER_STACK_TOP;

            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            if (ensure_current_task_user_space() != 0) return (uint32_t)-1;
            if (elf_load_from_vfs(g_root_fs_for_syscalls, path, &entry) != 0) return (uint32_t)-1;
            if (build_user_stack_from_cmdline(NULL, &user_esp) != 0) return (uint32_t)-1;

            f->ip = entry;
            f->sp = user_esp;
            return 0;
        }
        case SYS_EXECV: {
            char path[256];
            char cmdline[512];
            uint32_t entry = 0;
            uint32_t user_esp = USER_STACK_TOP;

            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            if (copy_user_string((const char*)ecx, cmdline, sizeof(cmdline)) != 0) return (uint32_t)-1;
            if (ensure_current_task_user_space() != 0) return (uint32_t)-1;
            if (elf_load_from_vfs(g_root_fs_for_syscalls, path, &entry) != 0) return (uint32_t)-1;
            if (build_user_stack_from_cmdline(cmdline, &user_esp) != 0) return (uint32_t)-1;

            f->ip = entry;
            f->sp = user_esp;
            return 0;
        }
        case SYS_IOCTL: {
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)vfs_ioctl(g_root_fs_for_syscalls, path, ecx, (void*)edx);
        }
        case SYS_OPEN: {
            char path[256];
            int inode_fd = -1;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            inode_fd = vfs_open(g_root_fs_for_syscalls, path);
            if (inode_fd < 0) {
                if (ecx & 1U) {
                    if (vfs_create_file(g_root_fs_for_syscalls, path) != 0) return (uint32_t)-1;
                    inode_fd = vfs_open(g_root_fs_for_syscalls, path);
                }
                if (inode_fd < 0) return (uint32_t)-1;
            }
            return (uint32_t)fd_alloc(path);
        }
        case SYS_CLOSE: {
            fd_entry_t *fds = fd_current();
            if (ebx >= FD_MAX || !fds[ebx].used) return (uint32_t)-1;
            if (fds[ebx].kind == FD_KIND_UDP && fds[ebx].sock_id >= 0) {
                udp_socket_free((int)fds[ebx].sock_id);
            }
            fds[ebx].used = 0;
            fds[ebx].kind = FD_KIND_VFS;
            fds[ebx].sock_id = -1;
            fds[ebx].path[0] = '\0';
            return 0;
        }
        case SYS_MKDIR: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)((vfs_mkdir(g_root_fs_for_syscalls, path) == 0) ? 0 : -1);
        }
        case SYS_UNLINK: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)vfs_unlink(g_root_fs_for_syscalls, path);
        }
        case SYS_RMDIR: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)vfs_rmdir(g_root_fs_for_syscalls, path);
        }
        case SYS_MKFIFO: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)((vfs_mkfifo(g_root_fs_for_syscalls, path) == 0) ? 0 : -1);
        }
        case SYS_MKSOCK: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)((vfs_mksock(g_root_fs_for_syscalls, path) == 0) ? 0 : -1);
        }
        case SYS_LIST: {
            char path[256];
            ssize_t n;
            if (!g_root_fs_for_syscalls || !ecx || edx == 0) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            n = vfs_list(g_root_fs_for_syscalls, path, (char*)ecx, (size_t)edx);
            if (n < 0) return (uint32_t)-1;
            return (uint32_t)n;
        }
        case SYS_APPEND: {
            const char *path;
            if (!g_root_fs_for_syscalls || !ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)vfs_append(g_root_fs_for_syscalls, path, (const void*)ecx, edx);
        }
        case SYS_SPAWN: {
            int pid;
            char path[256];
            char tty[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0 ||
                copy_user_string((const char*)ecx, tty, sizeof(tty)) != 0 ||
                tty[0] != '/') return (uint32_t)-1;
            pid = spawn_user_program(path, tty);
            if (pid < 0) return (uint32_t)-1;
            return (uint32_t)pid;
        }
        case SYS_SPAWNV: {
            int pid;
            char path[256];
            char tty[256];
            char cmdline[512];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0 ||
                copy_user_string((const char*)ecx, tty, sizeof(tty)) != 0 ||
                copy_user_string((const char*)edx, cmdline, sizeof(cmdline)) != 0 ||
                tty[0] != '/') return (uint32_t)-1;
            pid = spawn_user_program_ex(path, tty, cmdline);
            if (pid < 0) return (uint32_t)-1;
            return (uint32_t)pid;
        }
        case SYS_TASK_STATE:
            return (uint32_t)task_state_by_pid(ebx);
        case SYS_TASK_KILL:
            return (uint32_t)((task_terminate_by_pid(ebx, -1, 2u) == 0) ? 0 : -1);
        case SYS_INIT_SPAWN_SHELLS: {
            for (uint32_t i = 0; i < (uint32_t)(sizeof(g_shell_ttys) / sizeof(g_shell_ttys[0])); i++) {
                if (spawn_user_program("/bin/sh", g_shell_ttys[i]) >= 0) sleep(10);
            }
            task_exit();
            return 0;
        }
        case SYS_MOUNT: {
            char fs_name[32];
            char mount_path[256];
            void *ctx = NULL;
            const char *fs_drv = fs_name;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_string((const char*)ebx, fs_name, sizeof(fs_name)) != 0) return (uint32_t)-1;
            if (copy_user_path((const char*)ecx, mount_path, sizeof(mount_path)) != 0) return (uint32_t)-1;
            if (strcmp(fs_name, "devfs") == 0) ctx = g_devfs_ctx;
            else if (strncmp(fs_name, "/dev/disk/", 10) == 0) {
                char norm[256];
                uint32_t slot = VFS_MAX_MOUNTS;
                if (normalize_mount_path_local(mount_path, norm, sizeof(norm)) != 0) return (uint32_t)-1;
                for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
                    if (!g_mount_fat_used[i]) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= VFS_MAX_MOUNTS) return (uint32_t)-1;
                if (fat32_init_devpath(&g_mount_fat_ctx[slot], fs_name) != 0) return (uint32_t)-1;
                g_mount_fat_used[slot] = 1;
                strncpy(g_mount_fat_path[slot], norm, sizeof(g_mount_fat_path[slot]) - 1);
                g_mount_fat_path[slot][sizeof(g_mount_fat_path[slot]) - 1] = '\0';
                ctx = &g_mount_fat_ctx[slot];
                fs_drv = "fat32";
            }
            if (!ctx) return (uint32_t)-1;
            if (vfs_mount(g_root_fs_for_syscalls, mount_path, fs_drv, ctx) != 0) {
                for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
                    if (&g_mount_fat_ctx[i] == (fat32_fs_t*)ctx) {
                        g_mount_fat_used[i] = 0;
                        g_mount_fat_path[i][0] = '\0';
                        break;
                    }
                }
                return (uint32_t)-1;
            }
            if (strncmp(fs_name, "/dev/disk/", 10) == 0) {
                (void)vfs_set_mount_source(g_root_fs_for_syscalls, mount_path, fs_name);
            }
            return 0;
        }
        case SYS_UMOUNT: {
            char mount_path[256];
            char norm[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, mount_path, sizeof(mount_path)) != 0) return (uint32_t)-1;
            if (normalize_mount_path_local(mount_path, norm, sizeof(norm)) != 0) return (uint32_t)-1;
            if (vfs_umount(g_root_fs_for_syscalls, mount_path) != 0) return (uint32_t)-1;
            for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
                if (!g_mount_fat_used[i]) continue;
                if (strcmp(g_mount_fat_path[i], norm) == 0) {
                    g_mount_fat_used[i] = 0;
                    g_mount_fat_path[i][0] = '\0';
                    memset(&g_mount_fat_ctx[i], 0, sizeof(g_mount_fat_ctx[i]));
                    break;
                }
            }
            return 0;
        }
        case SYS_LIST_MOUNTS: {
            if (!g_root_fs_for_syscalls || !ebx || ecx == 0) return (uint32_t)-1;
            {
                ssize_t n = vfs_list_mounts(g_root_fs_for_syscalls, (char*)ebx, (size_t)ecx);
                if (n < 0) return (uint32_t)-1;
                return (uint32_t)n;
            }
        }
        case SYS_LINK: {
            char oldpath[256];
            char newpath[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, oldpath, sizeof(oldpath)) != 0) return (uint32_t)-1;
            if (copy_user_path((const char*)ecx, newpath, sizeof(newpath)) != 0) return (uint32_t)-1;
            return (uint32_t)((vfs_link(g_root_fs_for_syscalls, oldpath, newpath) == 0) ? 0 : -1);
        }
        case SYS_SOCKET: {
            int sid;
            int fd;
            if (ebx != AF_INET || ecx != SOCK_DGRAM || (edx != 0 && edx != IPPROTO_UDP)) return (uint32_t)-1;
            sid = udp_socket_alloc();
            if (sid < 0) return (uint32_t)-1;
            fd = fd_alloc_udp(sid);
            if (fd < 0) {
                udp_socket_free(sid);
                return (uint32_t)-1;
            }
            return (uint32_t)fd;
        }
        case SYS_BIND: {
            int sid = fd_udp_sid(ebx);
            if (sid < 0) return (uint32_t)-1;
            return (uint32_t)udp_bind_socket(sid, (const syscall_sockaddr_in_t*)ecx, edx);
        }
        case SYS_SENDTO: {
            syscall_udp_send_req_t req;
            int sid = fd_udp_sid(ebx);
            if (sid < 0 || !ecx) return (uint32_t)-1;
            memcpy(&req, (const void*)ecx, sizeof(req));
            return (uint32_t)udp_sendto_socket(sid, &req);
        }
        case SYS_RECVFROM: {
            syscall_udp_recv_req_t req;
            int sid = fd_udp_sid(ebx);
            if (sid < 0 || !ecx) return (uint32_t)-1;
            memcpy(&req, (const void*)ecx, sizeof(req));
            return (uint32_t)udp_recvfrom_socket(sid, &req);
        }
        case SYS_SETSOCKOPT: {
            syscall_sockopt_req_t req;
            int sid = fd_udp_sid(ebx);
            (void)sid;
            if (sid < 0 || !ecx) return (uint32_t)-1;
            memcpy(&req, (const void*)ecx, sizeof(req));
            if (req.level != 1u) return (uint32_t)-1;
            if (req.optname == 20u || req.optname == 21u) return 0;
            return (uint32_t)-1;
        }
        default:
            return (uint32_t)-1;
    }
}

/* syscall_handler is implemented in drivers/syscall_stub.asm */
