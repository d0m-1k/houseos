#include <drivers/syscall.h>
#include <asm/idt.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <asm/modes.h>
#include <drivers/filesystem/memfs.h>
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
};

memfs *g_root_fs_for_syscalls = NULL;

#define FD_MAX 32
typedef struct {
    uint8_t used;
    char path[256];
} fd_entry_t;

typedef struct {
    char path[256];
    char tty[256];
} spawn_req_t;

static fd_entry_t g_fds[FD_MAX];
static uint8_t g_fd_init_done = 0;
static fd_entry_t g_task_fds[MAX_TASKS][FD_MAX];
static uint8_t g_task_fd_init[MAX_TASKS];

static const char *g_shell_ttys[] = {
    "/devices/tty1", "/devices/tty2", "/devices/tty3",
    "/devices/tty4", "/devices/tty5", "/devices/tty6", "/devices/tty7",
    "/devices/ttyS0"
};

static int current_task_slot(void) {
    if (!current_task) return -1;
    {
        int slot = (int)(current_task - tasks);
        if (slot < 0 || slot >= MAX_TASKS) return -1;
        return slot;
    }
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
    *done = 1;
}

static fd_entry_t *fd_current(void) {
    int slot = current_task_slot();
    if (slot < 0) {
        fd_table_init(g_fds, &g_fd_init_done, "/devices/tty0");
        return g_fds;
    }
    fd_table_init(g_task_fds[slot], &g_task_fd_init[slot], "/devices/tty0");
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
}

static int fd_alloc(const char *path) {
    fd_entry_t *fds = fd_current();
    for (int i = 3; i < FD_MAX; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            strncpy(fds[i].path, path, sizeof(fds[i].path) - 1);
            fds[i].path[sizeof(fds[i].path) - 1] = '\0';
            return i;
        }
    }
    return -1;
}

static const char *fd_path(uint32_t fd) {
    fd_entry_t *fds = fd_current();
    if (fd >= FD_MAX || !fds[fd].used) return NULL;
    return fds[fd].path;
}

static void spawned_user_task(void *arg) {
    spawn_req_t *req = (spawn_req_t*)arg;
    uint32_t entry = 0;
    uint8_t *stack = NULL;

    cli();
    if (!req) task_exit();
    syscall_bind_stdio(req->tty);

    if (!g_root_fs_for_syscalls || elf_load_from_memfs(g_root_fs_for_syscalls, req->path, &entry) != 0) {
        tty_klog("spawn: elf_load failed\n");
        kfree(req);
        task_exit();
    }

    stack = (uint8_t*)kmalloc(4096);
    if (!stack) {
        tty_klog("spawn: no user stack\n");
        kfree(req);
        task_exit();
    }

    kfree(req);
    jump_to_ring3(entry, (uint32_t)stack + 4096, 0x202);
    task_exit();
}

static int spawn_user_program(const char *path, const char *tty) {
    spawn_req_t *req;
    int pid;
    if (!path || !tty || tty[0] != '/') return -1;
    req = (spawn_req_t*)kmalloc(sizeof(spawn_req_t));
    if (!req) return -1;
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';
    strncpy(req->tty, tty, sizeof(req->tty) - 1);
    req->tty[sizeof(req->tty) - 1] = '\0';
    pid = task_create(spawned_user_task, req);
    if (pid < 0) {
        kfree(req);
        return -1;
    }
    return pid;
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
            return (uint32_t)memfs_read(g_root_fs_for_syscalls, path, (void*)ecx, edx);
        }
        case SYS_WRITE: {
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (!ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)memfs_write(g_root_fs_for_syscalls, path, (const void*)ecx, edx);
        }
        case SYS_EXEC: {
            char path[256];
            uint32_t entry = 0;
            uint8_t *new_stack = NULL;
            uint32_t user_esp = 0;

            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            if (elf_load_from_memfs(g_root_fs_for_syscalls, path, &entry) != 0) return (uint32_t)-1;

            new_stack = (uint8_t*)kmalloc(4096);
            if (!new_stack) return (uint32_t)-1;
            user_esp = (uint32_t)new_stack + 4096;

            f->ip = entry;
            f->sp = user_esp;
            return 0;
        }
        case SYS_IOCTL: {
            const char *path;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)memfs_ioctl(g_root_fs_for_syscalls, path, ecx, (void*)edx);
        }
        case SYS_OPEN: {
            char path[256];
            int inode_fd = -1;
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            inode_fd = memfs_open(g_root_fs_for_syscalls, path);
            if (inode_fd < 0) {
                if (ecx & 1U) {
                    if (!memfs_create_file(g_root_fs_for_syscalls, path)) return (uint32_t)-1;
                    inode_fd = memfs_open(g_root_fs_for_syscalls, path);
                }
                if (inode_fd < 0) return (uint32_t)-1;
            }
            return (uint32_t)fd_alloc(path);
        }
        case SYS_CLOSE: {
            fd_entry_t *fds = fd_current();
            if (ebx >= FD_MAX || !fds[ebx].used) return (uint32_t)-1;
            fds[ebx].used = 0;
            fds[ebx].path[0] = '\0';
            return 0;
        }
        case SYS_MKDIR: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return memfs_create_dir(g_root_fs_for_syscalls, path) ? 0U : (uint32_t)-1;
        }
        case SYS_UNLINK: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)memfs_delete_file(g_root_fs_for_syscalls, path);
        }
        case SYS_RMDIR: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)memfs_delete_dir(g_root_fs_for_syscalls, path);
        }
        case SYS_MKFIFO: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return memfs_create_fifo(g_root_fs_for_syscalls, path) ? 0U : (uint32_t)-1;
        }
        case SYS_MKSOCK: {
            char path[256];
            if (!g_root_fs_for_syscalls) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            return memfs_create_socket(g_root_fs_for_syscalls, path) ? 0U : (uint32_t)-1;
        }
        case SYS_LIST: {
            char path[256];
            ssize_t n;
            if (!g_root_fs_for_syscalls || !ecx || edx == 0) return (uint32_t)-1;
            if (copy_user_path((const char*)ebx, path, sizeof(path)) != 0) return (uint32_t)-1;
            n = memfs_ls_into(g_root_fs_for_syscalls, path, (char*)ecx, (size_t)edx);
            if (n < 0) return (uint32_t)-1;
            return (uint32_t)n;
        }
        case SYS_APPEND: {
            const char *path;
            if (!g_root_fs_for_syscalls || !ecx || edx == 0) return 0;
            path = fd_path(ebx);
            if (!path) return (uint32_t)-1;
            return (uint32_t)memfs_append(g_root_fs_for_syscalls, path, (const void*)ecx, edx);
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
        case SYS_TASK_STATE:
            return (uint32_t)task_state_by_pid(ebx);
        case SYS_INIT_SPAWN_SHELLS: {
            for (uint32_t i = 0; i < (uint32_t)(sizeof(g_shell_ttys) / sizeof(g_shell_ttys[0])); i++) {
                if (spawn_user_program("/bin/shell", g_shell_ttys[i]) < 0) tty_klog("init: spawn shell failed\n");
                else sleep(10);
            }
            task_exit();
            return 0;
        }
        default:
            return (uint32_t)-1;
    }
}

/* syscall_handler is implemented in drivers/syscall_stub.asm */
