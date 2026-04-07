#include <unistd.h>
#include <sys/wait.h>
#include <syscall.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <stdarg.h>

typedef struct DIR {
    int used;
    char path[256];
    char listbuf[1024];
    int32_t listlen;
    uint32_t pos;
    struct dirent ent;
} DIR_IMPL;

static DIR_IMPL g_dirs[4];
static char g_cwd[256] = "/";

static int append_text(char *dst, uint32_t cap, const char *src) {
    uint32_t d = (uint32_t)strlen(dst);
    uint32_t s = (uint32_t)strlen(src);
    if (d + s + 1 > cap) return -1;
    memcpy(dst + d, src, s + 1);
    return 0;
}

static int append_quoted(char *dst, uint32_t cap, const char *arg) {
    if (append_text(dst, cap, "\"") != 0) return -1;
    for (uint32_t i = 0; arg && arg[i]; i++) {
        char c[2];
        if (arg[i] == '"' || arg[i] == '\\') {
            if (append_text(dst, cap, "\\") != 0) return -1;
        }
        c[0] = arg[i];
        c[1] = '\0';
        if (append_text(dst, cap, c) != 0) return -1;
    }
    if (append_text(dst, cap, "\"") != 0) return -1;
    return 0;
}

pid_t getuid(void) { return 0; }
pid_t geteuid(void) { return 0; }
pid_t getgid(void) { return 0; }
pid_t getegid(void) { return 0; }

int chdir(const char *path) {
    char tmp[4];
    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    if (list(path, tmp, sizeof(tmp)) < 0) return -1;
    strncpy(g_cwd, path, sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    return 0;
}

char *getcwd(char *buf, size_t size) {
    size_t need = strlen(g_cwd) + 1;
    if (!buf || size < need) {
        errno = EINVAL;
        return NULL;
    }
    memcpy(buf, g_cwd, need);
    return buf;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    char cmdline[512];
    (void)envp;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    cmdline[0] = '\0';
    if (argv) {
        for (uint32_t i = 0; argv[i]; i++) {
            if (i > 0 && append_text(cmdline, sizeof(cmdline), " ") != 0) {
                errno = EINVAL;
                return -1;
            }
            if (append_quoted(cmdline, sizeof(cmdline), argv[i]) != 0) {
                errno = EINVAL;
                return -1;
            }
        }
    }
    if (cmdline[0] == '\0') return exec(path);
    return execv(path, cmdline);
}

int execvp(const char *file, char *const argv[]) {
    char path[256];
    if (!file || file[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (file[0] == '/') return execve(file, argv, NULL);
    path[0] = '\0';
    if (append_text(path, sizeof(path), "/bin/") != 0 || append_text(path, sizeof(path), file) != 0) {
        errno = EINVAL;
        return -1;
    }
    return execve(path, argv, NULL);
}

int execl(const char *path, const char *arg0, ...) {
    char *argv[32];
    va_list ap;
    uint32_t n = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    va_start(ap, arg0);
    argv[n++] = (char*)arg0;
    while (n + 1 < 32) {
        char *a = va_arg(ap, char*);
        argv[n++] = a;
        if (!a) break;
    }
    argv[31] = NULL;
    va_end(ap);
    return execve(path, argv, NULL);
}

int fsync(int fd) {
    (void)fd;
    return 0;
}

int kill(pid_t pid, int sig) {
    if (pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    if (sig == 0) {
        if (task_state((int32_t)pid) < 0) {
            errno = ESRCH;
            return -1;
        }
        return 0;
    }
    if (sig != SIGTERM && sig != SIGKILL && sig != SIGINT) {
        errno = EINVAL;
        return -1;
    }
    return task_kill((int32_t)pid, sig);
}

pid_t wait(int32_t *status) {
    return waitpid(-1, status, 0);
}

DIR *opendir(const char *path) {
    DIR_IMPL *d = NULL;
    if (!path || path[0] != '/') {
        errno = EINVAL;
        return NULL;
    }
    for (uint32_t i = 0; i < 4; i++) {
        if (!g_dirs[i].used) {
            d = &g_dirs[i];
            break;
        }
    }
    if (!d) {
        errno = ENFILE;
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->used = 1;
    strncpy(d->path, path, sizeof(d->path) - 1);
    d->path[sizeof(d->path) - 1] = '\0';
    d->listlen = list(path, d->listbuf, sizeof(d->listbuf));
    if (d->listlen < 0) {
        d->used = 0;
        return NULL;
    }
    d->pos = 0;
    return (DIR*)d;
}

struct dirent *readdir(DIR *dirp) {
    DIR_IMPL *d = (DIR_IMPL*)dirp;
    uint32_t k = 0;
    if (!d || !d->used) {
        errno = EINVAL;
        return NULL;
    }
    while (d->pos < (uint32_t)d->listlen &&
           (d->listbuf[d->pos] == '\n' || d->listbuf[d->pos] == '\r')) d->pos++;
    if (d->pos >= (uint32_t)d->listlen || d->listbuf[d->pos] == '\0') return NULL;
    memset(&d->ent, 0, sizeof(d->ent));
    while (d->pos < (uint32_t)d->listlen && d->listbuf[d->pos] &&
           d->listbuf[d->pos] != '\n' && d->listbuf[d->pos] != '\r' &&
           k + 1 < sizeof(d->ent.d_name)) {
        d->ent.d_name[k++] = d->listbuf[d->pos++];
    }
    d->ent.d_name[k] = '\0';
    while (d->pos < (uint32_t)d->listlen &&
           d->listbuf[d->pos] && d->listbuf[d->pos] != '\n') d->pos++;
    if (d->pos < (uint32_t)d->listlen && d->listbuf[d->pos] == '\n') d->pos++;
    if (k > 0 && d->ent.d_name[k - 1] == '*') {
        d->ent.d_name[k - 1] = '\0';
        d->ent.d_type = DT_DIR;
    } else {
        d->ent.d_type = DT_UNKNOWN;
    }
    return &d->ent;
}

int closedir(DIR *dirp) {
    DIR_IMPL *d = (DIR_IMPL*)dirp;
    if (!d || !d->used) {
        errno = EINVAL;
        return -1;
    }
    d->used = 0;
    return 0;
}

void rewinddir(DIR *dirp) {
    DIR_IMPL *d = (DIR_IMPL*)dirp;
    if (!d || !d->used) return;
    d->pos = 0;
}

int uname(struct utsname *buf) {
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    strncpy(buf->sysname, "HouseOS", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "houseos", sizeof(buf->nodename) - 1);
    strncpy(buf->release, "0.1", sizeof(buf->release) - 1);
    strncpy(buf->version, "posix-layer", sizeof(buf->version) - 1);
    strncpy(buf->machine, "i386", sizeof(buf->machine) - 1);
    return 0;
}
