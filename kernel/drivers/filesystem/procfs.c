#include <drivers/filesystem/procfs.h>

#include <asm/mm.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <drivers/syscall.h>
#include <version.h>
#include <string.h>

typedef enum {
    PROC_NODE_NONE = 0,
    PROC_NODE_ROOT_DIR,
    PROC_NODE_SYS_FILE,
    PROC_NODE_PID_DIR,
    PROC_NODE_PID_FILE,
    PROC_NODE_PID_FD_DIR,
    PROC_NODE_PID_FD_FILE,
} proc_node_kind_t;

typedef enum {
    PROC_SYS_UPTIME = 0,
    PROC_SYS_MEMINFO = 1,
    PROC_SYS_STAT = 2,
    PROC_SYS_VERSION = 3,
} proc_sys_file_t;

typedef enum {
    PROC_PID_CMDLINE = 0,
    PROC_PID_STATUS = 1,
    PROC_PID_STAT = 2,
    PROC_PID_MEM = 3,
    PROC_PID_TTY = 4,
    PROC_PID_EXE = 5,
} proc_pid_file_t;

typedef struct {
    proc_node_kind_t kind;
    uint32_t pid;
    uint32_t fd;
    uint32_t file_id;
} proc_path_t;

static char g_proc_sys_text[256];
static char g_proc_pid_text[768];

#define PROC_SYS_TEXT_CAP ((size_t)sizeof(g_proc_sys_text))
#define PROC_PID_TEXT_CAP ((size_t)sizeof(g_proc_pid_text))

static int proc_append_text(char *out, size_t cap, size_t *len, const char *text) {
    size_t n;
    if (!out || !len || !text) return -1;
    n = strlen(text);
    if (*len + n >= cap) return -1;
    memcpy(out + *len, text, n);
    *len += n;
    out[*len] = '\0';
    return 0;
}

static int proc_append_char(char *out, size_t cap, size_t *len, char c) {
    if (!out || !len || *len + 1 >= cap) return -1;
    out[*len] = c;
    (*len)++;
    out[*len] = '\0';
    return 0;
}

static int proc_append_u32(char *out, size_t cap, size_t *len, uint32_t v) {
    char tmp[16];
    uint32_t pos = 0;
    if (v == 0) return proc_append_char(out, cap, len, '0');
    while (v > 0 && pos < sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (pos > 0) {
        if (proc_append_char(out, cap, len, tmp[--pos]) != 0) return -1;
    }
    return 0;
}

static int proc_append_i32(char *out, size_t cap, size_t *len, int32_t v) {
    if (v < 0) {
        if (proc_append_char(out, cap, len, '-') != 0) return -1;
        return proc_append_u32(out, cap, len, (uint32_t)(-v));
    }
    return proc_append_u32(out, cap, len, (uint32_t)v);
}

static const char *proc_task_state_name(task_state_t st) {
    switch (st) {
        case TASK_READY: return "READY";
        case TASK_RUNNING: return "RUNNING";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

static int proc_parse_u32_token(const char *s, uint32_t *value_out, uint32_t *used_out) {
    uint32_t v = 0;
    uint32_t i = 0;
    if (!s || s[0] < '0' || s[0] > '9' || !value_out) return -1;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10u + (uint32_t)(s[i] - '0');
        i++;
    }
    *value_out = v;
    if (used_out) *used_out = i;
    return 0;
}

static int proc_parse_path(const char *path, proc_path_t *out) {
    const char *p;
    uint32_t used = 0;
    uint32_t pid = 0;
    uint32_t fd = 0;
    if (!path || !out || path[0] != '/') return -1;
    memset(out, 0, sizeof(*out));
    if (strcmp(path, "/") == 0) {
        out->kind = PROC_NODE_ROOT_DIR;
        return 0;
    }
    if (strcmp(path, "/uptime") == 0) {
        out->kind = PROC_NODE_SYS_FILE;
        out->file_id = PROC_SYS_UPTIME;
        return 0;
    }
    if (strcmp(path, "/meminfo") == 0) {
        out->kind = PROC_NODE_SYS_FILE;
        out->file_id = PROC_SYS_MEMINFO;
        return 0;
    }
    if (strcmp(path, "/stat") == 0) {
        out->kind = PROC_NODE_SYS_FILE;
        out->file_id = PROC_SYS_STAT;
        return 0;
    }
    if (strcmp(path, "/version") == 0) {
        out->kind = PROC_NODE_SYS_FILE;
        out->file_id = PROC_SYS_VERSION;
        return 0;
    }

    p = path + 1;
    if (strncmp(p, "self", 4) == 0 && (p[4] == '\0' || p[4] == '/')) {
        if (!current_task) return -1;
        pid = current_task->pid;
        p += 4;
    } else {
        if (proc_parse_u32_token(p, &pid, &used) != 0) return -1;
        p += used;
    }
    out->pid = pid;
    if (*p == '\0') {
        out->kind = PROC_NODE_PID_DIR;
        return 0;
    }
    if (*p != '/') return -1;
    p++;

    if (strcmp(p, "fd") == 0) {
        out->kind = PROC_NODE_PID_FD_DIR;
        return 0;
    }
    if (strncmp(p, "fd/", 3) == 0) {
        if (proc_parse_u32_token(p + 3, &fd, &used) != 0 || p[3 + used] != '\0') return -1;
        out->kind = PROC_NODE_PID_FD_FILE;
        out->fd = fd;
        return 0;
    }
    if (strcmp(p, "cmdline") == 0) {
        out->kind = PROC_NODE_PID_FILE;
        out->file_id = PROC_PID_CMDLINE;
        return 0;
    }
    if (strcmp(p, "status") == 0) {
        out->kind = PROC_NODE_PID_FILE;
        out->file_id = PROC_PID_STATUS;
        return 0;
    }
    if (strcmp(p, "stat") == 0) {
        out->kind = PROC_NODE_PID_FILE;
        out->file_id = PROC_PID_STAT;
        return 0;
    }
    if (strcmp(p, "mem") == 0) {
        out->kind = PROC_NODE_PID_FILE;
        out->file_id = PROC_PID_MEM;
        return 0;
    }
    if (strcmp(p, "tty") == 0) {
        out->kind = PROC_NODE_PID_FILE;
        out->file_id = PROC_PID_TTY;
        return 0;
    }
    if (strcmp(p, "exe") == 0) {
        out->kind = PROC_NODE_PID_FILE;
        out->file_id = PROC_PID_EXE;
        return 0;
    }
    return -1;
}

static ssize_t proc_root_list(char *out, size_t out_size) {
    size_t len = 0;
    if (!out || out_size == 0) return -1;
    out[0] = '\0';
    if (proc_append_text(out, out_size, &len, "uptime\nmeminfo\nstat\nversion\nself/\n") != 0) return -1;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].pid == 0) continue;
        if (proc_append_u32(out, out_size, &len, tasks[i].pid) != 0) return -1;
        if (proc_append_text(out, out_size, &len, "/\n") != 0) return -1;
    }
    if (len > 0 && out[len - 1] == '\n') out[--len] = '\0';
    return (ssize_t)len;
}

static ssize_t proc_pid_list(task_t *task, char *out, size_t out_size) {
    size_t len = 0;
    (void)task;
    if (!out || out_size == 0) return -1;
    out[0] = '\0';
    if (proc_append_text(out, out_size, &len, "fd/\ncmdline\nstatus\nstat\nmem\ntty\nexe") != 0) return -1;
    return (ssize_t)len;
}

static ssize_t proc_pid_fd_list(uint32_t pid, char *out, size_t out_size) {
    size_t len = 0;
    char fd_path[256];
    if (!out || out_size == 0) return -1;
    out[0] = '\0';
    for (uint32_t fd = 0; fd < syscall_task_fd_max(); fd++) {
        if (syscall_task_fd_path(pid, fd, fd_path, sizeof(fd_path)) != 0) continue;
        if (len > 0) {
            if (proc_append_char(out, out_size, &len, '\n') != 0) return -1;
        }
        if (proc_append_u32(out, out_size, &len, fd) != 0) return -1;
    }
    return (ssize_t)len;
}

static ssize_t proc_read_system_file(uint32_t id, void *buf, size_t size) {
    char *text = g_proc_sys_text;
    size_t len = 0;
    if (!buf) return -1;
    text[0] = '\0';
    switch (id) {
        case PROC_SYS_UPTIME:
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, timer_get_ticks() / 100u) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\n") != 0) return -1;
            break;
        case PROC_SYS_MEMINFO:
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "HeapTotal: ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, (uint32_t)get_total_heap()) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\nHeapUsed: ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, (uint32_t)get_used_heap()) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\nHeapFree: ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, (uint32_t)get_free_heap()) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\n") != 0) return -1;
            break;
        case PROC_SYS_STAT: {
            uint32_t ready = 0, running = 0, blocked = 0, terminated = 0;
            for (int i = 0; i < task_count; i++) {
                switch (tasks[i].state) {
                    case TASK_READY: ready++; break;
                    case TASK_RUNNING: running++; break;
                    case TASK_BLOCKED: blocked++; break;
                    case TASK_TERMINATED: terminated++; break;
                }
            }
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "tasks ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, (uint32_t)task_count) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\nready ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, ready) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\nrunning ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, running) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\nblocked ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, blocked) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\nterminated ") != 0) return -1;
            if (proc_append_u32(text, PROC_SYS_TEXT_CAP, &len, terminated) != 0) return -1;
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, "\n") != 0) return -1;
            break;
        }
        case PROC_SYS_VERSION:
            if (proc_append_text(text, PROC_SYS_TEXT_CAP, &len, HOUSEOS_RELEASE_STR "\n") != 0) return -1;
            break;
        default:
            return -1;
    }
    if (len > size) len = size;
    memcpy(buf, text, len);
    return (ssize_t)len;
}

static ssize_t proc_read_pid_text(task_t *task, uint32_t file_id, void *buf, size_t size) {
    char *text = g_proc_pid_text;
    char fd_path[256];
    size_t len = 0;
    if (!task || !buf) return -1;
    text[0] = '\0';
    switch (file_id) {
        case PROC_PID_CMDLINE:
            if (task->cmdline[0]) proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->cmdline);
            else if (task->prog_path[0]) proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->prog_path);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\n");
            break;
        case PROC_PID_STATUS:
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "Pid:\t");
            proc_append_u32(text, PROC_PID_TEXT_CAP, &len, task->pid);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nState:\t");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, proc_task_state_name(task->state));
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nTty:\t");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->tty_path[0] ? task->tty_path : "-");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nExe:\t");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->prog_path[0] ? task->prog_path : "-");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nCmdline:\t");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->cmdline[0] ? task->cmdline : "-");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nExitStatus:\t");
            proc_append_i32(text, PROC_PID_TEXT_CAP, &len, task->exit_status);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nTermSignal:\t");
            proc_append_u32(text, PROC_PID_TEXT_CAP, &len, task->term_signal);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\nUserSlot:\t");
            if (task->user_slot == (uint32_t)-1) proc_append_text(text, PROC_PID_TEXT_CAP, &len, "-1");
            else proc_append_u32(text, PROC_PID_TEXT_CAP, &len, task->user_slot);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\n");
            break;
        case PROC_PID_STAT:
            proc_append_u32(text, PROC_PID_TEXT_CAP, &len, task->pid);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, " ");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, proc_task_state_name(task->state));
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, " ");
            proc_append_i32(text, PROC_PID_TEXT_CAP, &len, task->exit_status);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, " ");
            proc_append_u32(text, PROC_PID_TEXT_CAP, &len, task->term_signal);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, " ");
            if (task->user_slot == (uint32_t)-1) proc_append_text(text, PROC_PID_TEXT_CAP, &len, "0");
            else proc_append_u32(text, PROC_PID_TEXT_CAP, &len, task->user_phys_base);
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\n");
            break;
        case PROC_PID_TTY:
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->tty_path[0] ? task->tty_path : "-");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\n");
            break;
        case PROC_PID_EXE:
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, task->prog_path[0] ? task->prog_path : "-");
            proc_append_text(text, PROC_PID_TEXT_CAP, &len, "\n");
            break;
        default:
            return -1;
    }
    if (file_id == PROC_PID_MEM) return -1;
    if (len > size) len = size;
    memcpy(buf, text, len);
    (void)fd_path;
    return (ssize_t)len;
}

static ssize_t proc_read_pid_mem(task_t *task, void *buf, size_t size) {
    size_t to_copy;
    if (!task || !buf) return -1;
    if (task->user_slot == (uint32_t)-1 || task->user_phys_base == 0) return 0;
    to_copy = (size < USER_SLOT_SIZE_PHYS) ? size : USER_SLOT_SIZE_PHYS;
    memcpy(buf, (const void*)(uintptr_t)task->user_phys_base, to_copy);
    return (ssize_t)to_copy;
}

static ssize_t proc_read_pid_fd(uint32_t pid, uint32_t fd, void *buf, size_t size) {
    char path[256];
    size_t len;
    if (!buf) return -1;
    if (syscall_task_fd_path(pid, fd, path, sizeof(path)) != 0) return -1;
    len = strlen(path);
    if (len + 1 <= sizeof(path)) {
        path[len++] = '\n';
        path[len] = '\0';
    }
    if (len > size) len = size;
    memcpy(buf, path, len);
    return (ssize_t)len;
}

static int proc_open_op(void *fs_ctx, const char *path) {
    proc_path_t pp;
    (void)fs_ctx;
    return (proc_parse_path(path, &pp) == 0) ? 0 : -1;
}

static int proc_close_op(void *fs_ctx, int fd) {
    (void)fs_ctx;
    (void)fd;
    return 0;
}

static ssize_t proc_read_op(void *fs_ctx, const char *path, void *buf, size_t size) {
    proc_path_t pp;
    task_t *task;
    (void)fs_ctx;
    if (proc_parse_path(path, &pp) != 0) return -1;
    if (pp.kind == PROC_NODE_SYS_FILE) return proc_read_system_file(pp.file_id, buf, size);
    if (pp.pid == 0) return -1;
    task = task_find_by_pid(pp.pid);
    if (!task) return -1;
    if (pp.kind == PROC_NODE_PID_FILE) {
        if (pp.file_id == PROC_PID_MEM) return proc_read_pid_mem(task, buf, size);
        return proc_read_pid_text(task, pp.file_id, buf, size);
    }
    if (pp.kind == PROC_NODE_PID_FD_FILE) return proc_read_pid_fd(pp.pid, pp.fd, buf, size);
    return -1;
}

static ssize_t proc_write_op(void *fs_ctx, const char *path, const void *buf, size_t size) {
    (void)fs_ctx;
    (void)path;
    (void)buf;
    (void)size;
    return -1;
}

static ssize_t proc_append_op(void *fs_ctx, const char *path, const void *buf, size_t size) {
    (void)fs_ctx;
    (void)path;
    (void)buf;
    (void)size;
    return -1;
}

static int proc_ioctl_op(void *fs_ctx, const char *path, uint32_t request, void *arg) {
    (void)fs_ctx;
    (void)path;
    (void)request;
    (void)arg;
    return -1;
}

static int proc_mkdir_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static int proc_create_file_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static int proc_link_op(void *fs_ctx, const char *oldpath, const char *newpath) {
    (void)fs_ctx;
    (void)oldpath;
    (void)newpath;
    return -1;
}

static int proc_unlink_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static int proc_rmdir_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static int proc_mkfifo_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static int proc_mksock_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static ssize_t proc_list_op(void *fs_ctx, const char *path, char *out, size_t out_size) {
    proc_path_t pp;
    task_t *task;
    (void)fs_ctx;
    if (proc_parse_path(path, &pp) != 0) return -1;
    if (pp.kind == PROC_NODE_ROOT_DIR) return proc_root_list(out, out_size);
    if (pp.kind == PROC_NODE_PID_DIR) {
        task = task_find_by_pid(pp.pid);
        if (!task) return -1;
        return proc_pid_list(task, out, out_size);
    }
    if (pp.kind == PROC_NODE_PID_FD_DIR) return proc_pid_fd_list(pp.pid, out, out_size);
    return -1;
}

static int proc_get_info_op(void *fs_ctx, const char *path, vfs_info_t *out) {
    proc_path_t pp;
    task_t *task;
    char tmp[2048];
    ssize_t n;
    (void)fs_ctx;
    if (!out || proc_parse_path(path, &pp) != 0) return -1;
    memset(out, 0, sizeof(*out));
    out->mode = 0444u;
    if (pp.kind == PROC_NODE_ROOT_DIR || pp.kind == PROC_NODE_PID_DIR || pp.kind == PROC_NODE_PID_FD_DIR) {
        out->type = VFS_NODE_DIR;
        out->mode = 0555u;
        return 0;
    }
    if (pp.kind == PROC_NODE_SYS_FILE) {
        out->type = VFS_NODE_FILE;
        n = proc_read_system_file(pp.file_id, tmp, sizeof(tmp));
        out->size = (n > 0) ? (uint32_t)n : 0u;
        return 0;
    }
    task = task_find_by_pid(pp.pid);
    if (!task) return -1;
    if (pp.kind == PROC_NODE_PID_FILE) {
        out->type = VFS_NODE_FILE;
        if (pp.file_id == PROC_PID_MEM) out->size = USER_SLOT_SIZE_PHYS;
        else {
            n = proc_read_pid_text(task, pp.file_id, tmp, sizeof(tmp));
            out->size = (n > 0) ? (uint32_t)n : 0u;
        }
        return 0;
    }
    if (pp.kind == PROC_NODE_PID_FD_FILE) {
        out->type = VFS_NODE_FILE;
        n = proc_read_pid_fd(pp.pid, pp.fd, tmp, sizeof(tmp));
        if (n < 0) return -1;
        out->size = (uint32_t)n;
        return 0;
    }
    return -1;
}

const vfs_ops_t g_procfs_vfs_ops = {
    proc_open_op,
    proc_close_op,
    proc_read_op,
    proc_write_op,
    proc_append_op,
    proc_ioctl_op,
    proc_mkdir_op,
    proc_create_file_op,
    proc_link_op,
    proc_unlink_op,
    proc_rmdir_op,
    proc_mkfifo_op,
    proc_mksock_op,
    proc_list_op,
    proc_get_info_op
};
