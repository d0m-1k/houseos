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
#include <kerrno.h>
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
    SYS_WAITPID = 32,
    SYS_DUP = 33,
    SYS_DUP2 = 34,
    SYS_GETPID = 35,
    SYS_GETPPID = 36,
    SYS_STAT = 37,
    SYS_FSTAT = 38,
    SYS_LSEEK = 39,
    SYS_PIPE = 40,
    SYS_FCNTL = 41,
    SYS_FORK = 42,
    SYS_POLL = 43,
    SYS_SELECT = 44,
};

vfs_t *g_root_fs_for_syscalls = NULL;
static void *g_devfs_ctx = NULL;
volatile uint32_t g_in_syscall = 0;

#define FD_MAX 32
enum {
    FD_KIND_VFS = 0,
    FD_KIND_UDP = 1,
};

typedef struct {
    uint8_t used;
    uint8_t kind;
    uint8_t pipe_auto_unlink;
    uint8_t reserved0;
    int16_t sock_id;
    uint16_t reserved1;
    uint32_t fd_flags;
    uint32_t open_flags;
    uint32_t offset;
    char path[256];
} fd_entry_t;

typedef struct {
    uint32_t st_mode;
    int32_t st_size;
} syscall_stat_t;

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
} syscall_saved_regs_t;

typedef struct {
    uint32_t user_eip;
    uint32_t user_esp;
    uint32_t user_eflags;
    uint32_t user_cr3;
    uint32_t user_slot;
    uint32_t user_phys;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    char tty_path[64];
    char prog_path[256];
    char cmdline[512];
} fork_child_ctx_t;

typedef struct {
    int32_t fd;
    int16_t events;
    int16_t revents;
} syscall_pollfd_t;

typedef struct {
    int32_t nfds;
    uint32_t *readfds;
    uint32_t *writefds;
    uint32_t *exceptfds;
    int32_t timeout_ms;
} syscall_select_req_t;

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
#define SOL_SOCKET 1u
#define SO_RCVTIMEO 20u
#define SO_SNDTIMEO 21u
#define MSG_DONTWAIT 0x40u
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
static uint32_t g_pipe_seq = 1;

static int fd_has_path_ref(const char *path, int exclude_fd);

#define USER_ARG_MAX 32
#define USER_ARG_TOKEN 128
#define O_CREAT 0x0040u
#define O_APPEND 0x0400u
#define O_NONBLOCK 0x0800u

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1u

#define POLLIN   0x0001
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

#define S_IFMT   0170000u
#define S_IFIFO  0010000u
#define S_IFCHR  0020000u
#define S_IFDIR  0040000u
#define S_IFREG  0100000u
#define S_IFSOCK 0140000u
#define S_IRUSR  0400u
#define S_IWUSR  0200u
#define S_IXUSR  0100u

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
    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS || !ua) return -K_EINVAL;
    if (addrlen < sizeof(syscall_sockaddr_in_t)) return -K_EINVAL;
    if (ua->sin_family != AF_INET) return -K_EINVAL;
    port = be16_to_cpu(ua->sin_port);
    addr = be32_to_cpu(ua->sin_addr);
    if (port == 0) return -K_EINVAL;
    if (addr != INADDR_ANY && addr != INADDR_LOOPBACK) return -K_EINVAL;
    if (udp_port_in_use(addr, port, sid)) return -K_EBUSY;
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

    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS || !req || !req->buf || !req->addr) return -K_EINVAL;
    if (req->addrlen < sizeof(syscall_sockaddr_in_t)) return -K_EINVAL;
    if (req->len > UDP_PAYLOAD_MAX) return -K_EINVAL;
    if (req->addr->sin_family != AF_INET) return -K_EINVAL;

    dst_addr = be32_to_cpu(req->addr->sin_addr);
    dst_port = be16_to_cpu(req->addr->sin_port);
    if (dst_port == 0) return -K_EINVAL;
    if (dst_addr != INADDR_LOOPBACK && dst_addr != INADDR_ANY) return -K_EINVAL;

    src = &g_udp_sockets[sid];
    if (!src->bound && udp_autobind(sid) != 0) return -K_EAGAIN;

    dst_sid = udp_find_bound((dst_addr == INADDR_ANY) ? INADDR_LOOPBACK : dst_addr, dst_port);
    if (dst_sid < 0) return -K_ENOENT;
    dst = &g_udp_sockets[dst_sid];
    if (dst->q_len >= UDP_QUEUE_MAX) return -K_EAGAIN;

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
    uint32_t waited = 0;

    if (sid < 0 || sid >= (int)UDP_MAX_SOCKETS || !req || !req->buf) return -K_EINVAL;
    s = &g_udp_sockets[sid];
    while (s->q_len == 0) {
        if (req->flags & MSG_DONTWAIT) return -K_EAGAIN;
        if (s->rcv_timeout_ms > 0 && waited >= s->rcv_timeout_ms) return -K_ETIMEDOUT;
        sleep(10);
        waited += 10;
    }

    pkt = &s->q[s->q_head];
    if (!pkt->used) return -K_EIO;

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
    char *args;
    uint32_t arg_ptr[USER_ARG_MAX];
    uint32_t argc = 0;
    uint32_t i = 0;
    uint32_t sp;

    if (!user_esp_out) return -1;
    if (!cmdline) cmdline = "";
    args = (char*)kmalloc(USER_ARG_MAX * USER_ARG_TOKEN);
    if (!args) return -1;
    memset(args, 0, USER_ARG_MAX * USER_ARG_TOKEN);

    while (cmdline[i]) {
        uint32_t tlen = 0;
        int in_sq = 0;
        int in_dq = 0;

        while (cmdline[i] && is_space(cmdline[i])) i++;
        if (!cmdline[i]) break;
        if (argc >= USER_ARG_MAX) {
            kfree(args);
            return -1;
        }

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
            if (tlen + 1 >= USER_ARG_TOKEN) {
                kfree(args);
                return -1;
            }
            args[argc * USER_ARG_TOKEN + tlen++] = c;
            i++;
        }
        if (in_sq || in_dq) {
            kfree(args);
            return -1;
        }
        args[argc * USER_ARG_TOKEN + tlen] = '\0';
        argc++;
    }

    sp = USER_STACK_TOP & ~3u;

    for (int32_t a = (int32_t)argc - 1; a >= 0; a--) {
        char *arg = args + ((uint32_t)a * USER_ARG_TOKEN);
        uint32_t len = (uint32_t)strlen(arg) + 1;
        if (sp < USER_VADDR_BASE + len + 256) {
            kfree(args);
            return -1;
        }
        sp -= len;
        memcpy((void*)(uintptr_t)sp, arg, len);
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

    kfree(args);
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

static int append_text_safe(char *dst, uint32_t cap, uint32_t *len, const char *src) {
    uint32_t i = 0;
    if (!dst || !len || !src) return -1;
    while (src[i]) {
        if (*len + 1u >= cap) return -1;
        dst[*len] = src[i];
        (*len)++;
        i++;
    }
    dst[*len] = '\0';
    return 0;
}

static int append_char_safe(char *dst, uint32_t cap, uint32_t *len, char c) {
    if (!dst || !len) return -1;
    if (*len + 1u >= cap) return -1;
    dst[*len] = c;
    (*len)++;
    dst[*len] = '\0';
    return 0;
}

static const char *path_basename_local(const char *path) {
    const char *last = path;
    if (!path) return "";
    while (*path) {
        if (*path == '/') last = path + 1;
        path++;
    }
    return last;
}

static const char *skip_first_token(const char *s) {
    if (!s) return s;
    while (*s && is_space(*s)) s++;
    while (*s && !is_space(*s)) s++;
    while (*s && is_space(*s)) s++;
    return s;
}

static int build_interp_cmdline(const char *interp_path, const char *target_path, const char *orig_cmdline, char *out, uint32_t out_cap) {
    uint32_t len = 0;
    const char *tail = orig_cmdline;
    const char *base = path_basename_local(target_path);
    if (!interp_path || !target_path || !out || out_cap < 4u) return -1;
    out[0] = '\0';
    if (append_text_safe(out, out_cap, &len, interp_path) != 0) return -1;
    if (append_char_safe(out, out_cap, &len, ' ') != 0) return -1;
    if (append_text_safe(out, out_cap, &len, target_path) != 0) return -1;
    if (tail && tail[0]) {
        while (*tail && is_space(*tail)) tail++;
        if (strncmp(tail, target_path, strlen(target_path)) == 0 &&
            (tail[strlen(target_path)] == '\0' || is_space(tail[strlen(target_path)]))) {
            tail = skip_first_token(tail);
        } else if (base[0] != '\0' &&
                   strncmp(tail, base, strlen(base)) == 0 &&
                   (tail[strlen(base)] == '\0' || is_space(tail[strlen(base)]))) {
            tail = skip_first_token(tail);
        }
    }
    if (tail && tail[0]) {
        if (append_char_safe(out, out_cap, &len, ' ') != 0) return -1;
        if (append_text_safe(out, out_cap, &len, tail) != 0) return -1;
    }
    return 0;
}

static int resolve_exec_entry(
    const char *prog_path,
    const char *orig_cmdline,
    uint32_t *entry_out,
    char *stack_cmdline,
    uint32_t stack_cmdline_cap
) {
    char interp[256];
    uint32_t entry = 0;
    if (!prog_path || !entry_out || !stack_cmdline || stack_cmdline_cap < 2u) return -1;
    stack_cmdline[0] = '\0';

    if (elf_load_from_vfs_ex(g_root_fs_for_syscalls, prog_path, &entry, interp, sizeof(interp)) != 0) return -1;
    if (interp[0] != '\0') {
        uint32_t interp_entry = 0;
        if (elf_load_from_vfs(g_root_fs_for_syscalls, interp, &interp_entry) != 0) return -1;
        if (build_interp_cmdline(interp, prog_path, orig_cmdline, stack_cmdline, stack_cmdline_cap) != 0) return -1;
        *entry_out = interp_entry;
        return 0;
    }

    if (orig_cmdline && orig_cmdline[0]) {
        strncpy(stack_cmdline, orig_cmdline, stack_cmdline_cap - 1u);
        stack_cmdline[stack_cmdline_cap - 1u] = '\0';
    } else {
        stack_cmdline[0] = '\0';
    }
    *entry_out = entry;
    return 0;
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
    fds[0].pipe_auto_unlink = 0;
    fds[1].pipe_auto_unlink = 0;
    fds[2].pipe_auto_unlink = 0;
    fds[0].sock_id = -1;
    fds[1].sock_id = -1;
    fds[2].sock_id = -1;
    fds[0].open_flags = 0;
    fds[1].open_flags = 0;
    fds[2].open_flags = 0;
    fds[0].fd_flags = 0;
    fds[1].fd_flags = 0;
    fds[2].fd_flags = 0;
    fds[0].offset = 0;
    fds[1].offset = 0;
    fds[2].offset = 0;
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
    fds[0].pipe_auto_unlink = 0;
    fds[1].pipe_auto_unlink = 0;
    fds[2].pipe_auto_unlink = 0;
    fds[0].sock_id = -1;
    fds[1].sock_id = -1;
    fds[2].sock_id = -1;
    fds[0].open_flags = 0;
    fds[1].open_flags = 0;
    fds[2].open_flags = 0;
    fds[0].fd_flags = 0;
    fds[1].fd_flags = 0;
    fds[2].fd_flags = 0;
    fds[0].offset = 0;
    fds[1].offset = 0;
    fds[2].offset = 0;
}

void syscall_set_devfs_ctx(void *ctx) {
    g_devfs_ctx = ctx;
}

static int fd_alloc(const char *path, uint32_t open_flags) {
    fd_entry_t *fds = fd_current();
    for (int i = 3; i < FD_MAX; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].kind = FD_KIND_VFS;
            fds[i].pipe_auto_unlink = 0;
            fds[i].sock_id = -1;
            fds[i].open_flags = open_flags;
            fds[i].fd_flags = 0;
            fds[i].offset = 0;
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
            fds[i].pipe_auto_unlink = 0;
            fds[i].sock_id = (int16_t)sid;
            fds[i].open_flags = 0;
            fds[i].fd_flags = 0;
            fds[i].offset = 0;
            fds[i].path[0] = '\0';
            return i;
        }
    }
    return -1;
}

static int fd_dup_from_to(uint32_t oldfd, uint32_t newfd, int fixed_target) {
    fd_entry_t *fds = fd_current();
    uint32_t dst = newfd;

    if (oldfd >= FD_MAX || !fds[oldfd].used) return -K_EBADF;
    if (fds[oldfd].kind == FD_KIND_UDP) return -K_ENOTSUP;

    if (!fixed_target) {
        for (dst = 0; dst < FD_MAX; dst++) {
            if (!fds[dst].used) break;
        }
        if (dst >= FD_MAX) return -K_ENFILE;
    } else {
        if (dst >= FD_MAX) return -K_EBADF;
        if (dst == oldfd) return (int)dst;
        if (fds[dst].used) {
            if (fds[dst].kind == FD_KIND_UDP && fds[dst].sock_id >= 0) {
                udp_socket_free((int)fds[dst].sock_id);
            }
            fds[dst].used = 0;
            fds[dst].kind = FD_KIND_VFS;
            fds[dst].pipe_auto_unlink = 0;
            fds[dst].sock_id = -1;
            fds[dst].fd_flags = 0;
            fds[dst].open_flags = 0;
            fds[dst].offset = 0;
            fds[dst].path[0] = '\0';
        }
    }

    fds[dst].used = 1;
    fds[dst].kind = fds[oldfd].kind;
    fds[dst].pipe_auto_unlink = fds[oldfd].pipe_auto_unlink;
    fds[dst].sock_id = fds[oldfd].sock_id;
    fds[dst].fd_flags = 0;
    fds[dst].open_flags = fds[oldfd].open_flags;
    fds[dst].offset = fds[oldfd].offset;
    strncpy(fds[dst].path, fds[oldfd].path, sizeof(fds[dst].path) - 1);
    fds[dst].path[sizeof(fds[dst].path) - 1] = '\0';
    return (int)dst;
}

static void close_cloexec_fds(void) {
    fd_entry_t *fds = fd_current();
    for (int i = 3; i < FD_MAX; i++) {
        if (!fds[i].used) continue;
        if ((fds[i].fd_flags & FD_CLOEXEC) == 0) continue;
        if (fds[i].kind == FD_KIND_UDP && fds[i].sock_id >= 0) {
            udp_socket_free((int)fds[i].sock_id);
        }
        if (fds[i].kind == FD_KIND_VFS &&
            fds[i].pipe_auto_unlink &&
            !fd_has_path_ref(fds[i].path, i)) {
            (void)vfs_unlink(g_root_fs_for_syscalls, fds[i].path);
        }
        fds[i].used = 0;
        fds[i].kind = FD_KIND_VFS;
        fds[i].pipe_auto_unlink = 0;
        fds[i].sock_id = -1;
        fds[i].fd_flags = 0;
        fds[i].open_flags = 0;
        fds[i].offset = 0;
        fds[i].path[0] = '\0';
    }
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

static int fd_has_path_ref(const char *path, int exclude_fd) {
    fd_entry_t *fds = fd_current();
    if (!path || path[0] == '\0') return 0;
    for (int i = 0; i < FD_MAX; i++) {
        if (i == exclude_fd) continue;
        if (!fds[i].used || fds[i].kind != FD_KIND_VFS) continue;
        if (strcmp(fds[i].path, path) == 0) return 1;
    }
    return 0;
}

static uint32_t vfs_mode_from_info(const vfs_info_t *info) {
    if (!info) return 0;
    switch (info->type) {
        case VFS_NODE_DIR: return S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;
        case VFS_NODE_FIFO: return S_IFIFO | S_IRUSR | S_IWUSR;
        case VFS_NODE_SOCKET: return S_IFSOCK | S_IRUSR | S_IWUSR;
        case VFS_NODE_DEVICE:
        case VFS_NODE_CHARDEV:
        case VFS_NODE_BLOCKDEV: return S_IFCHR | S_IRUSR | S_IWUSR;
        case VFS_NODE_FILE:
        default: return S_IFREG | S_IRUSR | S_IWUSR;
    }
}

static int16_t fd_poll_revents(int32_t fd, int16_t events) {
    const char *path;
    vfs_info_t info;
    int16_t revents = 0;
    if (fd < 0 || (uint32_t)fd >= FD_MAX) return POLLNVAL;
    path = fd_path((uint32_t)fd);
    if (!path) {
        int sid = fd_udp_sid((uint32_t)fd);
        if (sid < 0) return POLLNVAL;
        if ((events & POLLIN) && g_udp_sockets[sid].q_len > 0) revents |= POLLIN;
        if (events & POLLOUT) revents |= POLLOUT;
        return revents;
    }
    if (vfs_get_info(g_root_fs_for_syscalls, path, &info) != 0) return POLLNVAL;
    if (events & POLLOUT) revents |= POLLOUT;
    if (events & POLLIN) {
        if (info.type == VFS_NODE_FIFO || info.type == VFS_NODE_SOCKET) {
            if (info.size > 0) revents |= POLLIN;
        } else {
            revents |= POLLIN;
        }
    }
    return revents;
}

static void spawned_user_task(void *arg) {
    spawn_req_t *req = (spawn_req_t*)arg;
    uint32_t entry = 0;
    uint32_t user_esp = USER_STACK_TOP;
    char stack_cmdline[512];
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

    if (!g_root_fs_for_syscalls ||
        resolve_exec_entry(req->path, req->cmdline[0] ? req->cmdline : NULL, &entry, stack_cmdline, sizeof(stack_cmdline)) != 0) {
        (void)elf_get_last_error();
        kfree(req);
        task_exit();
    }

    if (build_user_stack_from_cmdline(stack_cmdline[0] ? stack_cmdline : NULL, &user_esp) != 0) {
        kfree(req);
        task_exit();
    }

    kfree(req);
    jump_to_ring3(entry, user_esp, 0x202u);
    task_exit();
}

static void spawned_fork_child(void *arg) {
    fork_child_ctx_t *ctx = (fork_child_ctx_t*)arg;
    fork_child_ctx_t local;
    if (!ctx || !current_task) task_exit();
    memcpy(&local, ctx, sizeof(local));
    kfree(ctx);

    current_task->cr3 = local.user_cr3;
    current_task->user_slot = local.user_slot;
    current_task->user_phys_base = local.user_phys;
    strncpy(current_task->tty_path, local.tty_path, sizeof(current_task->tty_path) - 1);
    current_task->tty_path[sizeof(current_task->tty_path) - 1] = '\0';
    strncpy(current_task->prog_path, local.prog_path, sizeof(current_task->prog_path) - 1);
    current_task->prog_path[sizeof(current_task->prog_path) - 1] = '\0';
    strncpy(current_task->cmdline, local.cmdline, sizeof(current_task->cmdline) - 1);
    current_task->cmdline[sizeof(current_task->cmdline) - 1] = '\0';
    mm_switch_cr3(current_task->cr3);

    sti();
    jump_to_ring3_state(
        local.user_eip, local.user_esp, local.user_eflags,
        0, local.ebx, local.ecx, local.edx, local.esi, local.edi, local.ebp
    );
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
    uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx,
    struct interrupt_frame *f, syscall_saved_regs_t *regs
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
            return timer_get_ticks();
        case SYS_EXIT:
            if (current_task) {
                current_task->exit_status = (int32_t)ebx;
                current_task->term_signal = 0;
            }
            task_exit();
            return 0;
        case SYS_READ: {
            fd_entry_t *fds = fd_current();
            const char *path;
            vfs_info_t info;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)(-K_EBADF);
            if (vfs_get_info(g_root_fs_for_syscalls, path, &info) == 0 && info.type == VFS_NODE_FILE) {
                uint32_t off = fds[ebx].offset;
                uint32_t to_copy;
                uint8_t *tmp;
                if (off >= info.size) return 0;
                to_copy = ((uint32_t)edx < (info.size - off)) ? (uint32_t)edx : (info.size - off);
                tmp = (uint8_t*)kmalloc(info.size ? info.size : 1);
                if (!tmp) return (uint32_t)(-K_ENOMEM);
                {
                    ssize_t n = vfs_read(g_root_fs_for_syscalls, path, tmp, info.size);
                    if (n < 0) {
                        kfree(tmp);
                        return (uint32_t)(-K_EIO);
                    }
                }
                memcpy((void*)ecx, tmp + off, to_copy);
                kfree(tmp);
                fds[ebx].offset = off + to_copy;
                return to_copy;
            }
            {
                ssize_t n = vfs_read(g_root_fs_for_syscalls, path, (void*)ecx, edx);
                if (n == 0 && (fds[ebx].open_flags & O_NONBLOCK) &&
                    (vfs_get_info(g_root_fs_for_syscalls, path, &info) == 0) &&
                    (info.type == VFS_NODE_FIFO || info.type == VFS_NODE_SOCKET)) {
                    return (uint32_t)(-K_EAGAIN);
                }
                if (n < 0) return (uint32_t)(-K_EIO);
                return (uint32_t)n;
            }
        }
        case SYS_WRITE: {
            fd_entry_t *fds = fd_current();
            const char *path;
            vfs_info_t info;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)(-K_EBADF);
            if (vfs_get_info(g_root_fs_for_syscalls, path, &info) == 0 && info.type == VFS_NODE_FILE) {
                uint32_t off = fds[ebx].offset;
                uint32_t in_size = (uint32_t)edx;
                uint32_t dst_size;
                uint8_t *buf;
                if (fds[ebx].open_flags & O_APPEND) off = info.size;
                dst_size = (off + in_size > info.size) ? (off + in_size) : info.size;
                buf = (uint8_t*)kmalloc(dst_size ? dst_size : 1);
                if (!buf) return (uint32_t)(-K_ENOMEM);
                memset(buf, 0, dst_size);
                if (info.size > 0) {
                    ssize_t rn = vfs_read(g_root_fs_for_syscalls, path, buf, info.size);
                    if (rn < 0) {
                        kfree(buf);
                        return (uint32_t)(-K_EIO);
                    }
                }
                memcpy(buf + off, (const void*)ecx, in_size);
                if (vfs_write(g_root_fs_for_syscalls, path, buf, dst_size) < 0) {
                    kfree(buf);
                    return (uint32_t)(-K_EIO);
                }
                kfree(buf);
                fds[ebx].offset = off + in_size;
                return in_size;
            }
            {
                ssize_t n = vfs_write(g_root_fs_for_syscalls, path, (const void*)ecx, edx);
                if (n < 0) return (uint32_t)(-K_EIO);
                return (uint32_t)n;
            }
        }
        case SYS_EXEC: {
            char path[256];
            char stack_cmdline[512];
            uint32_t entry = 0;
            uint32_t user_esp = USER_STACK_TOP;

            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            if (ensure_current_task_user_space() != 0) return (uint32_t)(-K_ENOMEM);
            if (resolve_exec_entry(path, NULL, &entry, stack_cmdline, sizeof(stack_cmdline)) != 0) return (uint32_t)(-K_ENOENT);
            if (build_user_stack_from_cmdline(stack_cmdline[0] ? stack_cmdline : NULL, &user_esp) != 0) return (uint32_t)(-K_EINVAL);
            if (current_task) {
                strncpy(current_task->prog_path, path, sizeof(current_task->prog_path) - 1);
                current_task->prog_path[sizeof(current_task->prog_path) - 1] = '\0';
                current_task->cmdline[0] = '\0';
            }

            if (f && ((f->cs & 3u) == 0u)) {
                close_cloexec_fds();
                sti();
                jump_to_ring3(entry, user_esp, 0x202u);
                __builtin_unreachable();
            }
            if (!f) return (uint32_t)(-K_EINVAL);
            close_cloexec_fds();
            f->ip = entry;
            f->sp = user_esp;
            return 0;
        }
        case SYS_EXECV: {
            char path[256];
            char cmdline[512];
            char stack_cmdline[512];
            uint32_t entry = 0;
            uint32_t user_esp = USER_STACK_TOP;

            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            if (copy_user_string((const char*)ecx, cmdline, sizeof(cmdline)) != 0) return (uint32_t)(-K_EINVAL);
            if (ensure_current_task_user_space() != 0) return (uint32_t)(-K_ENOMEM);
            if (resolve_exec_entry(path, cmdline, &entry, stack_cmdline, sizeof(stack_cmdline)) != 0) return (uint32_t)(-K_ENOENT);
            if (build_user_stack_from_cmdline(stack_cmdline[0] ? stack_cmdline : NULL, &user_esp) != 0) return (uint32_t)(-K_EINVAL);
            if (current_task) {
                strncpy(current_task->prog_path, path, sizeof(current_task->prog_path) - 1);
                current_task->prog_path[sizeof(current_task->prog_path) - 1] = '\0';
                strncpy(current_task->cmdline, stack_cmdline, sizeof(current_task->cmdline) - 1);
                current_task->cmdline[sizeof(current_task->cmdline) - 1] = '\0';
            }

            if (f && ((f->cs & 3u) == 0u)) {
                close_cloexec_fds();
                sti();
                jump_to_ring3(entry, user_esp, 0x202u);
                __builtin_unreachable();
            }
            if (!f) return (uint32_t)(-K_EINVAL);
            close_cloexec_fds();
            f->ip = entry;
            f->sp = user_esp;
            return 0;
        }
        case SYS_IOCTL: {
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            path = fd_path(ebx);
            if (!path) return (uint32_t)(-K_EBADF);
            return (uint32_t)vfs_ioctl(g_root_fs_for_syscalls, path, ecx, (void*)edx);
        }
        case SYS_OPEN: {
            char path[256];
            int inode_fd = -1;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            inode_fd = vfs_open(g_root_fs_for_syscalls, path);
            if (inode_fd < 0) {
                if ((ecx & O_CREAT) || (ecx & 1U)) {
                    if (vfs_create_file(g_root_fs_for_syscalls, path) != 0) return (uint32_t)(-K_EACCES);
                    inode_fd = vfs_open(g_root_fs_for_syscalls, path);
                }
                if (inode_fd < 0) return (uint32_t)(-K_ENOENT);
            }
            {
                int fd = fd_alloc(path, ecx);
                if (fd < 0) return (uint32_t)(-K_ENFILE);
                return (uint32_t)fd;
            }
        }
        case SYS_CLOSE: {
            fd_entry_t *fds = fd_current();
            if (ebx >= FD_MAX || !fds[ebx].used) return (uint32_t)(-K_EBADF);
            if (fds[ebx].kind == FD_KIND_UDP && fds[ebx].sock_id >= 0) {
                udp_socket_free((int)fds[ebx].sock_id);
            }
            if (fds[ebx].kind == FD_KIND_VFS &&
                fds[ebx].pipe_auto_unlink &&
                !fd_has_path_ref(fds[ebx].path, (int)ebx)) {
                (void)vfs_unlink(g_root_fs_for_syscalls, fds[ebx].path);
            }
            fds[ebx].used = 0;
            fds[ebx].kind = FD_KIND_VFS;
            fds[ebx].pipe_auto_unlink = 0;
            fds[ebx].sock_id = -1;
            fds[ebx].fd_flags = 0;
            fds[ebx].open_flags = 0;
            fds[ebx].offset = 0;
            fds[ebx].path[0] = '\0';
            return 0;
        }
        case SYS_MKDIR: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            return (uint32_t)((vfs_mkdir(g_root_fs_for_syscalls, path) == 0) ? 0 : -1);
        }
        case SYS_UNLINK: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            return (uint32_t)vfs_unlink(g_root_fs_for_syscalls, path);
        }
        case SYS_RMDIR: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            return (uint32_t)vfs_rmdir(g_root_fs_for_syscalls, path);
        }
        case SYS_MKFIFO: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            return (uint32_t)((vfs_mkfifo(g_root_fs_for_syscalls, path) == 0) ? 0 : -1);
        }
        case SYS_MKSOCK: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            return (uint32_t)((vfs_mksock(g_root_fs_for_syscalls, path) == 0) ? 0 : -1);
        }
        case SYS_LIST: {
            char path[256];
            ssize_t n;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ecx || edx == 0) return (uint32_t)(-K_EINVAL);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            n = vfs_list(g_root_fs_for_syscalls, path, (char*)ecx, (size_t)edx);
            if (n < 0) return (uint32_t)(-K_EIO);
            return (uint32_t)n;
        }
        case SYS_APPEND: {
            fd_entry_t *fds = fd_current();
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)(-K_EBADF);
            {
                ssize_t n = vfs_append(g_root_fs_for_syscalls, path, (const void*)ecx, edx);
                if (n < 0) return (uint32_t)(-K_EIO);
                if (n > 0) {
                    vfs_info_t info;
                    if (vfs_get_info(g_root_fs_for_syscalls, path, &info) == 0) {
                        fds[ebx].offset = info.size;
                    }
                }
                return (uint32_t)n;
            }
        }
        case SYS_SPAWN: {
            int pid;
            char path[256];
            char tty[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0 ||
                copy_user_string((const char*)ecx, tty, sizeof(tty)) != 0 ||
                tty[0] != '/') return (uint32_t)(-K_EINVAL);
            pid = spawn_user_program(path, tty);
            if (pid < 0) return (uint32_t)(-K_EIO);
            return (uint32_t)pid;
        }
        case SYS_SPAWNV: {
            int pid;
            char path[256];
            char tty[256];
            char cmdline[512];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0 ||
                copy_user_string((const char*)ecx, tty, sizeof(tty)) != 0 ||
                copy_user_string((const char*)edx, cmdline, sizeof(cmdline)) != 0 ||
                tty[0] != '/') return (uint32_t)(-K_EINVAL);
            pid = spawn_user_program_ex(path, tty, cmdline);
            if (pid < 0) return (uint32_t)(-K_EIO);
            return (uint32_t)pid;
        }
        case SYS_TASK_STATE:
            return (uint32_t)task_state_by_pid(ebx);
        case SYS_TASK_KILL: {
            uint32_t sig = ecx;
            if (sig == 0) sig = 15u;
            if (sig != 2u && sig != 9u && sig != 15u) return (uint32_t)(-K_EINVAL);
            return (uint32_t)((task_terminate_by_pid(ebx, (int32_t)(128u + sig), sig) == 0) ? 0 : -1);
        }
        case SYS_DUP:
            return (uint32_t)fd_dup_from_to(ebx, 0, 0);
        case SYS_DUP2:
            return (uint32_t)fd_dup_from_to(ebx, ecx, 1);
        case SYS_GETPID:
            return (uint32_t)(current_task ? current_task->pid : 0);
        case SYS_GETPPID:
            return (uint32_t)(current_task ? current_task->ppid : 0);
        case SYS_STAT: {
            char path[256];
            vfs_info_t info;
            syscall_stat_t st;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ecx) return (uint32_t)(-K_EINVAL);
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)(-K_EINVAL);
            if (vfs_get_info(g_root_fs_for_syscalls, path, &info) != 0) return (uint32_t)(-K_ENOENT);
            st.st_mode = vfs_mode_from_info(&info);
            st.st_size = (int32_t)info.size;
            memcpy((void*)ecx, &st, sizeof(st));
            return 0;
        }
        case SYS_FSTAT: {
            const char *path;
            vfs_info_t info;
            syscall_stat_t st;
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ecx) return (uint32_t)(-K_EINVAL);
            path = fd_path(ebx);
            if (!path) return (uint32_t)(-K_EBADF);
            if (vfs_get_info(g_root_fs_for_syscalls, path, &info) != 0) return (uint32_t)(-K_EIO);
            st.st_mode = vfs_mode_from_info(&info);
            st.st_size = (int32_t)info.size;
            memcpy((void*)ecx, &st, sizeof(st));
            return 0;
        }
        case SYS_LSEEK: {
            fd_entry_t *fds = fd_current();
            const char *path;
            vfs_info_t info;
            int32_t cur;
            int32_t off = (int32_t)ecx;
            int32_t np = 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)(-K_EBADF);
            if (vfs_get_info(g_root_fs_for_syscalls, path, &info) != 0) return (uint32_t)(-K_EIO);
            if (info.type != VFS_NODE_FILE) return (uint32_t)(-K_ENOTSUP);
            cur = (int32_t)fds[ebx].offset;
            if ((int32_t)edx == SEEK_SET) np = off;
            else if ((int32_t)edx == SEEK_CUR) np = cur + off;
            else if ((int32_t)edx == SEEK_END) np = (int32_t)info.size + off;
            else return (uint32_t)(-K_EINVAL);
            if (np < 0) return (uint32_t)(-K_EINVAL);
            fds[ebx].offset = (uint32_t)np;
            return (uint32_t)np;
        }
        case SYS_PIPE: {
            char path[256];
            int fdr = -1;
            int fdw = -1;
            int32_t *fds_out = (int32_t*)ebx;
            fd_entry_t *fds = fd_current();
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!fds_out) return (uint32_t)(-K_EINVAL);
            for (uint32_t tries = 0; tries < 32; tries++) {
                uint32_t pid = current_task ? current_task->pid : 0;
                uint32_t seq = g_pipe_seq++;
                if (seq == 0) seq = g_pipe_seq++;
                path[0] = '\0';
                strcpy(path, "/tmp/.pipe_");
                {
                    char nbuf[16];
                    utoa(pid, nbuf, 10);
                    strcat(path, nbuf);
                    strcat(path, "_");
                    utoa(seq, nbuf, 10);
                    strcat(path, nbuf);
                }
                if (vfs_mkfifo(g_root_fs_for_syscalls, path) == 0) {
                    fdr = fd_alloc(path, 0);
                    if (fdr < 0) {
                        (void)vfs_unlink(g_root_fs_for_syscalls, path);
                        return (uint32_t)(-K_ENFILE);
                    }
                    fdw = fd_alloc(path, 0);
                    if (fdw < 0) {
                        fd_entry_t *t = fd_current();
                        t[fdr].used = 0;
                        t[fdr].kind = FD_KIND_VFS;
                        t[fdr].pipe_auto_unlink = 0;
                        t[fdr].sock_id = -1;
                        t[fdr].fd_flags = 0;
                        t[fdr].open_flags = 0;
                        t[fdr].offset = 0;
                        t[fdr].path[0] = '\0';
                        (void)vfs_unlink(g_root_fs_for_syscalls, path);
                        return (uint32_t)(-K_ENFILE);
                    }
                    fds[fdr].pipe_auto_unlink = 1;
                    fds[fdw].pipe_auto_unlink = 1;
                    fds_out[0] = fdr;
                    fds_out[1] = fdw;
                    return 0;
                }
            }
            return (uint32_t)(-K_EBUSY);
        }
        case SYS_FCNTL: {
            fd_entry_t *fds = fd_current();
            if (ebx >= FD_MAX || !fds[ebx].used) return (uint32_t)(-K_EBADF);
            if ((int32_t)ecx == F_GETFL) return fds[ebx].open_flags;
            if ((int32_t)ecx == F_SETFL) {
                uint32_t keep = fds[ebx].open_flags & ~(O_APPEND | O_NONBLOCK);
                uint32_t setv = edx & (O_APPEND | O_NONBLOCK);
                fds[ebx].open_flags = keep | setv;
                return 0;
            }
            if ((int32_t)ecx == F_GETFD) return (fds[ebx].fd_flags & FD_CLOEXEC) ? FD_CLOEXEC : 0;
            if ((int32_t)ecx == F_SETFD) {
                fds[ebx].fd_flags = (edx & FD_CLOEXEC) ? FD_CLOEXEC : 0;
                return 0;
            }
            if ((int32_t)ecx == F_DUPFD) {
                uint32_t minfd = edx;
                for (uint32_t i = (minfd < 3u ? 3u : minfd); i < FD_MAX; i++) {
                    if (!fds[i].used) {
                        int r = fd_dup_from_to(ebx, i, 1);
                        return (r < 0) ? (uint32_t)r : (uint32_t)r;
                    }
                }
                return (uint32_t)(-K_ENFILE);
            }
            return (uint32_t)(-K_EINVAL);
        }
        case SYS_FORK: {
            fork_child_ctx_t *ctx;
            uint32_t child_slot = 0;
            uint32_t child_phys = 0;
            uint32_t child_cr3 = 0;
            uint32_t ret_esp = 0;
            int pid;
            task_t *child_task;
            int pslot;
            int cslot;

            if (!current_task || !f || !regs) return (uint32_t)(-K_EINVAL);
            if (current_task->user_slot == (uint32_t)-1 || current_task->user_phys_base == 0) {
                return (uint32_t)(-K_ENOSYS);
            }

            if (mm_user_slot_alloc(&child_slot, &child_phys) != 0) return (uint32_t)(-K_ENOMEM);
            memcpy((void*)(uintptr_t)child_phys, (const void*)(uintptr_t)current_task->user_phys_base, USER_SLOT_SIZE_PHYS);
            child_cr3 = mm_user_cr3_create(child_phys);
            if (!child_cr3) {
                mm_user_slot_free(child_slot);
                return (uint32_t)(-K_ENOMEM);
            }

            ctx = (fork_child_ctx_t*)kmalloc(sizeof(*ctx));
            if (!ctx) {
                mm_user_cr3_destroy(child_cr3);
                mm_user_slot_free(child_slot);
                return (uint32_t)(-K_ENOMEM);
            }
            memset(ctx, 0, sizeof(*ctx));
            ret_esp = ((f->cs & 3u) == 0u) ? ((uint32_t)(uintptr_t)f + 12u) : f->sp;
            ctx->user_eip = f->ip;
            ctx->user_esp = ret_esp;
            ctx->user_eflags = f->flags;
            ctx->user_cr3 = child_cr3;
            ctx->user_slot = child_slot;
            ctx->user_phys = child_phys;
            ctx->ebx = regs->ebx;
            ctx->ecx = regs->ecx;
            ctx->edx = regs->edx;
            ctx->esi = regs->esi;
            ctx->edi = regs->edi;
            ctx->ebp = regs->ebp;
            strncpy(ctx->tty_path, current_task->tty_path, sizeof(ctx->tty_path) - 1);
            strncpy(ctx->prog_path, current_task->prog_path, sizeof(ctx->prog_path) - 1);
            strncpy(ctx->cmdline, current_task->cmdline, sizeof(ctx->cmdline) - 1);

            pid = task_create(spawned_fork_child, ctx);
            if (pid < 0) {
                kfree(ctx);
                mm_user_cr3_destroy(child_cr3);
                mm_user_slot_free(child_slot);
                return (uint32_t)(-K_ENFILE);
            }

            child_task = task_find_by_pid((uint32_t)pid);
            if (!child_task) return (uint32_t)(-K_EIO);
            child_task->cr3 = child_cr3;
            child_task->user_slot = child_slot;
            child_task->user_phys_base = child_phys;
            strncpy(child_task->tty_path, current_task->tty_path, sizeof(child_task->tty_path) - 1);
            child_task->tty_path[sizeof(child_task->tty_path) - 1] = '\0';
            strncpy(child_task->prog_path, current_task->prog_path, sizeof(child_task->prog_path) - 1);
            child_task->prog_path[sizeof(child_task->prog_path) - 1] = '\0';
            strncpy(child_task->cmdline, current_task->cmdline, sizeof(child_task->cmdline) - 1);
            child_task->cmdline[sizeof(child_task->cmdline) - 1] = '\0';

            pslot = current_task_slot();
            cslot = (int)(child_task - tasks);
            if (cslot >= 0 && cslot < MAX_TASKS) {
                if (pslot >= 0 && pslot < MAX_TASKS && g_task_fd_init[pslot]) {
                    memcpy(g_task_fds[cslot], g_task_fds[pslot], sizeof(g_task_fds[cslot]));
                    g_task_fd_init[cslot] = 1;
                } else if (g_fd_init_done) {
                    memcpy(g_task_fds[cslot], g_fds, sizeof(g_task_fds[cslot]));
                    g_task_fd_init[cslot] = 1;
                } else {
                    g_task_fd_init[cslot] = 0;
                }
            }

            return (uint32_t)pid;
        }
        case SYS_POLL: {
            syscall_pollfd_t *pfds = (syscall_pollfd_t*)ebx;
            uint32_t nfds = ecx;
            int32_t timeout_ms = (int32_t)edx;
            uint32_t waited = 0;
            if (!pfds && nfds > 0) return (uint32_t)(-K_EINVAL);
            while (1) {
                int32_t ready = 0;
                for (uint32_t i = 0; i < nfds; i++) {
                    int16_t rev = fd_poll_revents(pfds[i].fd, pfds[i].events);
                    pfds[i].revents = rev;
                    if (rev) ready++;
                }
                if (ready > 0) return (uint32_t)ready;
                if (timeout_ms == 0) return 0;
                if (timeout_ms > 0 && (int32_t)waited >= timeout_ms) return 0;
                sleep(10);
                waited += 10;
            }
        }
        case SYS_SELECT: {
            syscall_select_req_t req;
            uint32_t *rfds;
            uint32_t *wfds;
            uint32_t *efds;
            uint32_t waited = 0;
            int32_t timeout_ms;
            if (!ebx) return (uint32_t)(-K_EINVAL);
            memcpy(&req, (const void*)ebx, sizeof(req));
            if (req.nfds < 0 || req.nfds > (int32_t)FD_MAX) return (uint32_t)(-K_EINVAL);
            rfds = req.readfds;
            wfds = req.writefds;
            efds = req.exceptfds;
            timeout_ms = req.timeout_ms;
            while (1) {
                uint32_t rmask = 0;
                uint32_t wmask = 0;
                uint32_t emask = 0;
                int32_t ready = 0;
                for (int32_t fd = 0; fd < req.nfds; fd++) {
                    uint32_t bit = (1u << fd);
                    int16_t events = 0;
                    int16_t rev;
                    int fd_ready = 0;
                    if (rfds && (*rfds & bit)) events |= POLLIN;
                    if (wfds && (*wfds & bit)) events |= POLLOUT;
                    if (efds && (*efds & bit)) events |= POLLERR;
                    if (!events) continue;
                    rev = fd_poll_revents(fd, events);
                    if ((rev & POLLIN) && rfds) { rmask |= bit; fd_ready = 1; }
                    if ((rev & POLLOUT) && wfds) { wmask |= bit; fd_ready = 1; }
                    if ((rev & (POLLERR | POLLHUP | POLLNVAL)) && efds) { emask |= bit; fd_ready = 1; }
                    if (fd_ready) ready++;
                }
                if (rfds) *rfds = rmask;
                if (wfds) *wfds = wmask;
                if (efds) *efds = emask;
                if (ready > 0) return (uint32_t)ready;
                if (timeout_ms == 0) return 0;
                if (timeout_ms > 0 && (int32_t)waited >= timeout_ms) return 0;
                sleep(10);
                waited += 10;
            }
        }
        case SYS_WAITPID: {
            int32_t status = 0;
            int32_t r = task_waitpid((int32_t)ebx, ecx ? &status : NULL, edx);
            if (r < 0) return (uint32_t)(-K_ECHILD);
            if (ecx) *(int32_t*)ecx = status;
            return (uint32_t)r;
        }
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
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_string((const char*)ebx, fs_name, sizeof(fs_name)) != 0) return (uint32_t)(-K_EINVAL);
            if (copy_user_path((const char*)ecx, mount_path, sizeof(mount_path)) != 0) return (uint32_t)(-K_EINVAL);
            if (strcmp(fs_name, "devfs") == 0) ctx = g_devfs_ctx;
            else if (strncmp(fs_name, "/dev/disk/", 10) == 0) {
                char norm[256];
                uint32_t slot = VFS_MAX_MOUNTS;
                if (normalize_mount_path_local(mount_path, norm, sizeof(norm)) != 0) return (uint32_t)(-K_EINVAL);
                for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
                    if (!g_mount_fat_used[i]) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= VFS_MAX_MOUNTS) return (uint32_t)(-K_ENFILE);
                if (fat32_init_devpath(&g_mount_fat_ctx[slot], fs_name) != 0) return (uint32_t)(-K_ENODEV);
                g_mount_fat_used[slot] = 1;
                strncpy(g_mount_fat_path[slot], norm, sizeof(g_mount_fat_path[slot]) - 1);
                g_mount_fat_path[slot][sizeof(g_mount_fat_path[slot]) - 1] = '\0';
                ctx = &g_mount_fat_ctx[slot];
                fs_drv = "fat32";
            }
            if (!ctx) return (uint32_t)(-K_ENODEV);
            if (vfs_mount(g_root_fs_for_syscalls, mount_path, fs_drv, ctx) != 0) {
                for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
                    if (&g_mount_fat_ctx[i] == (fat32_fs_t*)ctx) {
                        g_mount_fat_used[i] = 0;
                        g_mount_fat_path[i][0] = '\0';
                        break;
                    }
                }
                return (uint32_t)(-K_EBUSY);
            }
            if (strncmp(fs_name, "/dev/disk/", 10) == 0) {
                (void)vfs_set_mount_source(g_root_fs_for_syscalls, mount_path, fs_name);
            }
            return 0;
        }
        case SYS_UMOUNT: {
            char mount_path[256];
            char norm[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, mount_path, sizeof(mount_path)) != 0) return (uint32_t)(-K_EINVAL);
            if (normalize_mount_path_local(mount_path, norm, sizeof(norm)) != 0) return (uint32_t)(-K_EINVAL);
            if (vfs_umount(g_root_fs_for_syscalls, mount_path) != 0) return (uint32_t)(-K_EBUSY);
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
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (!ebx || ecx == 0) return (uint32_t)(-K_EINVAL);
            {
                ssize_t n = vfs_list_mounts(g_root_fs_for_syscalls, (char*)ebx, (size_t)ecx);
                if (n < 0) return (uint32_t)(-K_EIO);
                return (uint32_t)n;
            }
        }
        case SYS_LINK: {
            char oldpath[256];
            char newpath[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)(-K_ENODEV);
            if (copy_user_path((const char*)ebx, oldpath, sizeof(oldpath)) != 0) return (uint32_t)(-K_EINVAL);
            if (copy_user_path((const char*)ecx, newpath, sizeof(newpath)) != 0) return (uint32_t)(-K_EINVAL);
            return (uint32_t)((vfs_link(g_root_fs_for_syscalls, oldpath, newpath) == 0) ? 0 : -K_EIO);
        }
        case SYS_SOCKET: {
            int sid;
            int fd;
            if (ebx != AF_INET || ecx != SOCK_DGRAM || (edx != 0 && edx != IPPROTO_UDP)) return (uint32_t)(-K_EINVAL);
            sid = udp_socket_alloc();
            if (sid < 0) return (uint32_t)(-K_ENFILE);
            fd = fd_alloc_udp(sid);
            if (fd < 0) {
                udp_socket_free(sid);
                return (uint32_t)(-K_ENFILE);
            }
            return (uint32_t)fd;
        }
        case SYS_BIND: {
            int sid = fd_udp_sid(ebx);
            if (sid < 0) return (uint32_t)(-K_EBADF);
            return (uint32_t)udp_bind_socket(sid, (const syscall_sockaddr_in_t*)ecx, edx);
        }
        case SYS_SENDTO: {
            syscall_udp_send_req_t req;
            int sid = fd_udp_sid(ebx);
            if (sid < 0) return (uint32_t)(-K_EBADF);
            if (!ecx) return (uint32_t)(-K_EINVAL);
            memcpy(&req, (const void*)ecx, sizeof(req));
            return (uint32_t)udp_sendto_socket(sid, &req);
        }
        case SYS_RECVFROM: {
            syscall_udp_recv_req_t req;
            int sid = fd_udp_sid(ebx);
            if (sid < 0) return (uint32_t)(-K_EBADF);
            if (!ecx) return (uint32_t)(-K_EINVAL);
            memcpy(&req, (const void*)ecx, sizeof(req));
            return (uint32_t)udp_recvfrom_socket(sid, &req);
        }
        case SYS_SETSOCKOPT: {
            syscall_sockopt_req_t req;
            int sid = fd_udp_sid(ebx);
            if (sid < 0) return (uint32_t)(-K_EBADF);
            if (!ecx) return (uint32_t)(-K_EINVAL);
            memcpy(&req, (const void*)ecx, sizeof(req));
            if (req.level != SOL_SOCKET) return (uint32_t)(-K_EINVAL);
            if (req.optname == SO_RCVTIMEO) {
                uint32_t ms = 0;
                if (!req.optval || req.optlen < sizeof(uint32_t)) return (uint32_t)(-K_EINVAL);
                memcpy(&ms, req.optval, sizeof(ms));
                if (ms > 600000u) ms = 600000u;
                g_udp_sockets[sid].rcv_timeout_ms = (uint16_t)((ms > 65535u) ? 65535u : ms);
                return 0;
            }
            if (req.optname == SO_SNDTIMEO) return 0;
            return (uint32_t)(-K_ENOTSUP);
        }
        default:
            return (uint32_t)(-K_ENOSYS);
    }
}
