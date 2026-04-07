#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include <stdint.h>
#include <devctl.h>

enum {
    TASK_READY = 0,
    TASK_RUNNING = 1,
    TASK_BLOCKED = 2,
    TASK_TERMINATED = 3
};

typedef struct {
    char tty[32];
    char prog[64];
    int32_t pid;
} shell_slot_t;

typedef struct {
    char tty[32];
    char prog[64];
    char cmdline[128];
    int32_t pid;
    uint32_t restarts;
} daemon_slot_t;

typedef struct {
    char fs[16];
    char path[64];
} mount_slot_t;

enum {
    MAX_SHELLS = 16,
    MAX_DAEMONS = 8,
    MAX_MOUNTS = 8,
    CFG_BUF_SZ = 4096
};

static shell_slot_t g_shells[MAX_SHELLS];
static uint32_t g_shell_count = 0;
static daemon_slot_t g_daemons[MAX_DAEMONS];
static uint32_t g_daemon_count = 0;
static mount_slot_t g_mounts[MAX_MOUNTS];
static uint32_t g_mount_count = 0;
static uint32_t g_spawn_delay_ms = 10;
static uint32_t g_poll_ms = 100;
static char g_cfg_buf[CFG_BUF_SZ];
static int g_have_vesa = 0;
static int g_gui_fallback_started = 0;
static int32_t g_gui_fallback_pid = -1;
static void ensure_tty2_shell_fallback(void);
static void poll_pause(uint32_t ms);

static void log_tty0(const char *text) {
    int fd = open("/dev/tty/1", 0);
    if (fd >= 0) {
        write(fd, text, (uint32_t)strlen(text));
        close(fd);
    }
    fd = open("/dev/tty/S0", 0);
    if (fd >= 0) {
        write(fd, text, (uint32_t)strlen(text));
        close(fd);
    }
}

static void log_tty0_shell(const char *prefix, const char *tty, const char *suffix) {
    log_tty0(prefix);
    log_tty0(tty);
    log_tty0(suffix);
}

static void log_tty0_u32(const char *prefix, uint32_t v, const char *suffix) {
    char buf[16];
    uint32_t n = 0;
    if (v == 0) {
        buf[n++] = '0';
    } else {
        char rev[16];
        uint32_t rn = 0;
        while (v > 0 && rn < sizeof(rev)) {
            rev[rn++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (rn > 0) buf[n++] = rev[--rn];
    }
    buf[n] = '\0';
    log_tty0(prefix);
    log_tty0(buf);
    log_tty0(suffix);
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) {
        e--;
        *e = '\0';
    }
    return s;
}

static char *next_tok(char **ps) {
    char *s = *ps;
    char *tok;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') {
        *ps = s;
        return NULL;
    }
    tok = s;
    while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) {
        *s = '\0';
        s++;
    }
    *ps = s;
    return tok;
}

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t v = 0;
    if (!s || !*s || !out) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
    }
    *out = v;
    return 0;
}

static int read_text(const char *path, char *buf, uint32_t cap) {
    int fd;
    int32_t got;
    if (!path || !buf || cap < 2) return -1;
    fd = open(path, 0);
    if (fd < 0) return -1;
    got = read(fd, buf, cap - 1);
    if (got < 0) got = 0;
    buf[(uint32_t)got] = '\0';
    close(fd);
    return (got > 0) ? 0 : -1;
}

static int add_shell_cfg(const char *tty, const char *prog) {
    shell_slot_t *s;
    if (!tty || !prog || g_shell_count >= MAX_SHELLS) return -1;
    s = &g_shells[g_shell_count];
    strncpy(s->tty, tty, sizeof(s->tty) - 1);
    s->tty[sizeof(s->tty) - 1] = '\0';
    strncpy(s->prog, prog, sizeof(s->prog) - 1);
    s->prog[sizeof(s->prog) - 1] = '\0';
    s->pid = -1;
    g_shell_count++;
    return 0;
}

static int add_mount_cfg(const char *fs, const char *path) {
    mount_slot_t *m;
    if (!fs || !path || g_mount_count >= MAX_MOUNTS) return -1;
    m = &g_mounts[g_mount_count];
    strncpy(m->fs, fs, sizeof(m->fs) - 1);
    m->fs[sizeof(m->fs) - 1] = '\0';
    strncpy(m->path, path, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = '\0';
    g_mount_count++;
    return 0;
}

static int add_daemon_cfg(const char *tty, const char *prog, const char *cmdline) {
    daemon_slot_t *d;
    if (!tty || !prog || g_daemon_count >= MAX_DAEMONS) return -1;
    d = &g_daemons[g_daemon_count];
    strncpy(d->tty, tty, sizeof(d->tty) - 1);
    d->tty[sizeof(d->tty) - 1] = '\0';
    strncpy(d->prog, prog, sizeof(d->prog) - 1);
    d->prog[sizeof(d->prog) - 1] = '\0';
    if (cmdline) {
        strncpy(d->cmdline, cmdline, sizeof(d->cmdline) - 1);
        d->cmdline[sizeof(d->cmdline) - 1] = '\0';
    } else {
        d->cmdline[0] = '\0';
    }
    d->pid = -1;
    d->restarts = 0;
    g_daemon_count++;
    return 0;
}

static void load_defaults(void) {
    g_shell_count = 0;
    g_daemon_count = 0;
    g_mount_count = 0;
    g_spawn_delay_ms = 10;
    g_poll_ms = 100;
    add_mount_cfg("devfs", "/dev");
    add_shell_cfg("/dev/tty/1", "/bin/sh");
    add_shell_cfg("/dev/tty/2", "/bin/sh");
    add_shell_cfg("/dev/tty/3", "/bin/sh");
    add_shell_cfg("/dev/tty/4", "/bin/sh");
    add_shell_cfg("/dev/tty/5", "/bin/sh");
    add_shell_cfg("/dev/tty/6", "/bin/sh");
    add_shell_cfg("/dev/tty/7", "/bin/sh");
    add_shell_cfg("/dev/tty/8", "/bin/sh");
    add_shell_cfg("/dev/tty/S0", "/bin/sh");
}

static void parse_init_conf_line(char *line) {
    char *p;
    char *cmd;
    char *a;
    char *b;
    uint32_t v;
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';
    p = trim(line);
    if (*p == '\0') return;
    cmd = next_tok(&p);
    a = next_tok(&p);
    b = next_tok(&p);
    if (!cmd) return;

    if (strcmp(cmd, "shell") == 0) {
        if (a) add_shell_cfg(a, b ? b : "/bin/sh");
        return;
    }
    if (strcmp(cmd, "daemon") == 0) {
        char *rest = trim(p);
        if (a && b) add_daemon_cfg(a, b, (*rest) ? rest : NULL);
        return;
    }
    if (strcmp(cmd, "spawn_delay_ms") == 0) {
        if (a && parse_u32(a, &v) == 0) g_spawn_delay_ms = v;
        return;
    }
    if (strcmp(cmd, "poll_ms") == 0) {
        if (a && parse_u32(a, &v) == 0) g_poll_ms = v;
        return;
    }
}

static void load_init_conf(void) {
    char *line;
    char *nl;
    load_defaults();
    if (read_text("/etc/init.conf", g_cfg_buf, sizeof(g_cfg_buf)) != 0) return;

    g_shell_count = 0;
    g_daemon_count = 0;
    line = g_cfg_buf;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        parse_init_conf_line(line);
        if (!nl) break;
        line = nl + 1;
    }
    if (g_shell_count == 0) {
        add_shell_cfg("/dev/tty/1", "/bin/sh");
        add_shell_cfg("/dev/tty/2", "/bin/sh");
        add_shell_cfg("/dev/tty/3", "/bin/sh");
        add_shell_cfg("/dev/tty/4", "/bin/sh");
        add_shell_cfg("/dev/tty/5", "/bin/sh");
        add_shell_cfg("/dev/tty/6", "/bin/sh");
        add_shell_cfg("/dev/tty/7", "/bin/sh");
        add_shell_cfg("/dev/tty/8", "/bin/sh");
    }
}

static void load_fstab(void) {
    char *line;
    char *nl;
    g_mount_count = 0;
    if (read_text("/etc/fstab", g_cfg_buf, sizeof(g_cfg_buf)) != 0) {
        add_mount_cfg("devfs", "/dev");
        return;
    }
    line = g_cfg_buf;
    while (*line) {
        char *p;
        char *fs;
        char *mp;
        char *hash;
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        hash = strchr(line, '#');
        if (hash) *hash = '\0';
        p = trim(line);
        fs = next_tok(&p);
        mp = next_tok(&p);
        if (fs && mp) add_mount_cfg(fs, mp);
        if (!nl) break;
        line = nl + 1;
    }
    if (g_mount_count == 0) add_mount_cfg("devfs", "/dev");
}

static void apply_mounts(void) {
    for (uint32_t i = 0; i < g_mount_count; i++) {
        if (mount(g_mounts[i].fs, g_mounts[i].path) != 0) {
            log_tty0("init: mount failed ");
            log_tty0(g_mounts[i].fs);
            log_tty0(" ");
            log_tty0(g_mounts[i].path);
            log_tty0("\n");
        }
    }
}

static int spawn_shell(shell_slot_t *s) {
    int32_t pid;
    pid = spawn(s->prog, s->tty);
    if (pid < 0) return -1;
    s->pid = pid;
    return 0;
}

static int spawn_daemon(daemon_slot_t *d) {
    int32_t pid;
    int vfd;
    if (strcmp(d->prog, "/bin/guishell") == 0 ||
        strcmp(d->prog, "/bin/gfxd") == 0 ||
        strcmp(d->prog, "/bin/composd") == 0 ||
        strcmp(d->prog, "/bin/wmgrd") == 0) {
        vfd = open("/dev/vesa", 0);
        if (vfd < 0) return -2;
        close(vfd);
    }
    if (d->cmdline[0]) pid = spawnv(d->prog, d->tty, d->cmdline);
    else pid = spawn(d->prog, d->tty);
    if (pid < 0) return -1;
    d->pid = pid;
    return 0;
}

static int is_gui_stack_prog(const char *prog) {
    if (!prog) return 0;
    return (strcmp(prog, "/bin/gfxd") == 0 ||
            strcmp(prog, "/bin/composd") == 0 ||
            strcmp(prog, "/bin/wmgrd") == 0) ? 1 : 0;
}

static void disable_gui_stack_daemons(void) {
    uint32_t i;
    for (i = 0; i < g_daemon_count; i++) {
        if (is_gui_stack_prog(g_daemons[i].prog)) g_daemons[i].pid = -2;
    }
}

static void ensure_tty2_gui_fallback(void) {
    if (g_gui_fallback_started) return;
    g_gui_fallback_pid = spawn("/bin/guishell", "/dev/tty/2");
    if (g_gui_fallback_pid >= 0) {
        g_gui_fallback_started = 1;
        disable_gui_stack_daemons();
        log_tty0("init: fallback /bin/guishell on tty2\n");
        return;
    }
    log_tty0("init: fallback /bin/guishell failed, use /bin/sh on tty2\n");
    ensure_tty2_shell_fallback();
}

static void ensure_tty2_shell_fallback(void) {
    uint32_t i;
    for (i = 0; i < g_shell_count; i++) {
        if (strcmp(g_shells[i].tty, "/dev/tty/2") == 0) {
            if (g_shells[i].pid < 0) (void)spawn_shell(&g_shells[i]);
            return;
        }
    }
    if (add_shell_cfg("/dev/tty/2", "/bin/sh") == 0) {
        (void)spawn_shell(&g_shells[g_shell_count - 1u]);
    }
}

static void poll_pause(uint32_t ms) {
    sleep(ms ? ms : 1u);
}

int main(void) {
    load_init_conf();
    load_fstab();
    apply_mounts();
    {
        int vfd = open("/dev/vesa", 0);
        if (vfd >= 0) {
            g_have_vesa = 1;
            close(vfd);
        } else {
            g_have_vesa = 0;
        }
    }
    {
        int tfd = open("/dev/tty/1", 0);
        if (tfd >= 0) {
            uint32_t idx = 1;
            (void)ioctl(tfd, DEV_IOCTL_TTY_SET_ACTIVE, &idx);
            close(tfd);
        }
    }
    log_tty0("init: start\n");

    for (uint32_t i = 0; i < g_daemon_count; i++) {
        int rc = spawn_daemon(&g_daemons[i]);
        if (rc == -1) {
            log_tty0_shell("init: daemon spawn failed on ", g_daemons[i].tty, "\n");
            if (strcmp(g_daemons[i].tty, "/dev/tty/2") == 0) {
                ensure_tty2_gui_fallback();
            }
        } else if (rc == -2) {
            log_tty0_shell("init: daemon precheck failed on ", g_daemons[i].tty, "\n");
            if (strcmp(g_daemons[i].tty, "/dev/tty/2") == 0) {
                log_tty0("init: /dev/vesa unavailable on tty2\n");
                g_have_vesa = 0;
                ensure_tty2_gui_fallback();
            }
        }
    }

    for (uint32_t i = 0; i < g_shell_count; i++) {
        if (spawn_shell(&g_shells[i]) != 0) {
            log_tty0_shell("init: spawn failed on ", g_shells[i].tty, "\n");
        }
    }

    while (1) {
        for (uint32_t i = 0; i < g_daemon_count; i++) {
            int32_t st;
            int rc;
            if (g_daemons[i].pid == -2) continue;
            if (g_daemons[i].pid < 0) {
                (void)spawn_daemon(&g_daemons[i]);
                continue;
            }
            st = task_state(g_daemons[i].pid);
            if (st == TASK_TERMINATED || st < 0) {
                g_daemons[i].restarts++;
                g_daemons[i].pid = -1;
                if (is_gui_stack_prog(g_daemons[i].prog) &&
                    strcmp(g_daemons[i].tty, "/dev/tty/2") == 0 &&
                    g_daemons[i].restarts >= 8u) {
                    log_tty0("init: gui stack unstable, enabling guishell fallback\n");
                    ensure_tty2_gui_fallback();
                    continue;
                }
                rc = spawn_daemon(&g_daemons[i]);
                if (rc == -1) {
                    log_tty0_shell("init: daemon respawn failed on ", g_daemons[i].tty, "\n");
                    if (strcmp(g_daemons[i].tty, "/dev/tty/2") == 0) {
                        ensure_tty2_gui_fallback();
                    }
                } else if (rc == -2) {
                    log_tty0_shell("init: daemon precheck failed on ", g_daemons[i].tty, "\n");
                    if (strcmp(g_daemons[i].tty, "/dev/tty/2") == 0) {
                        log_tty0("init: /dev/vesa unavailable on tty2\n");
                        g_have_vesa = 0;
                        ensure_tty2_gui_fallback();
                    }
                } else {
                    g_daemons[i].restarts = 0;
                }
            }
        }
        for (uint32_t i = 0; i < g_shell_count; i++) {
            int32_t st;
            if (g_shells[i].pid < 0) {
                (void)spawn_shell(&g_shells[i]);
                continue;
            }
            st = task_state(g_shells[i].pid);
            if (st == TASK_TERMINATED || st < 0) {
                g_shells[i].pid = -1;
                if (spawn_shell(&g_shells[i]) != 0) {
                    log_tty0_shell("init: respawn failed on ", g_shells[i].tty, "\n");
                }
            }
        }
        poll_pause(g_poll_ms);
    }
}
