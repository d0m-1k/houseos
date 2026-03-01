#include <syscall.h>
#include <devctl.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define MAX_LINE 1024
#define MAX_ARGS 32
#define MAX_TOKEN 160
#define MAX_VARS 16
#define MAX_VAR_NAME 24
#define MAX_VAR_VALUE 160
#define MAX_HISTORY 64
#define MAX_HISTORY_LINE 256

static char g_path[128] = "/bin";
static char g_pwd[128] = "/";
static char g_tty_name[16] = "tty?";

static char g_var_name[MAX_VARS][MAX_VAR_NAME];
static char g_var_val[MAX_VARS][MAX_VAR_VALUE];
static uint32_t g_var_count = 0;

static char g_history[MAX_HISTORY][MAX_HISTORY_LINE];
static uint32_t g_history_count = 0;
static uint32_t g_history_next = 0;

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static int is_name_start(char c) {
    return (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static int streq_n(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static int parse_u32_dec(const char *s, uint32_t *out) {
    uint32_t v = 0;
    uint32_t i = 0;
    if (!s || !out || s[0] == '\0') return -1;
    while (s[i]) {
        char c = s[i];
        if (c < '0' || c > '9') return -1;
        v = v * 10u + (uint32_t)(c - '0');
        i++;
    }
    *out = v;
    return 0;
}

static int read_line(char *buf, uint32_t cap) {
    int32_t n = 0;
    while (1) {
        n = read(fileno(stdin), buf, cap - 1);
        if (n > 0) break;
        if (n < 0) return -1;
        sleep(1);
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[n - 1] = '\0';
        n--;
    }
    return n;
}

static char *trim_ws(char *s) {
    uint32_t len;
    if (!s) return s;
    while (*s && is_space(*s)) s++;
    len = (uint32_t)strlen(s);
    while (len > 0 && is_space(s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
    return s;
}

static void history_push(const char *line) {
    uint32_t len;
    char clean[MAX_HISTORY_LINE];
    uint32_t w = 0;
    if (!line || line[0] == '\0') return;
    len = (uint32_t)strlen(line);
    for (uint32_t i = 0; i < len && w + 1 < MAX_HISTORY_LINE; i++) {
        char c = line[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c <= 126)) clean[w++] = c;
    }
    if (w == 0) return;
    clean[w] = '\0';
    memcpy(g_history[g_history_next], clean, w + 1);
    g_history_next = (g_history_next + 1) % MAX_HISTORY;
    if (g_history_count < MAX_HISTORY) g_history_count++;
}

static const char *get_var(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "PATH") == 0) return g_path;
    if (strcmp(name, "PWD") == 0) return g_pwd;
    if (strcmp(name, "TTY") == 0) return g_tty_name;

    for (uint32_t i = 0; i < g_var_count; i++) {
        if (strcmp(g_var_name[i], name) == 0) return g_var_val[i];
    }
    return NULL;
}

static int set_var(const char *name, const char *value) {
    if (!name || !value || !is_name_start(name[0])) return -1;
    for (uint32_t i = 1; name[i]; i++) {
        if (!is_name_char(name[i])) return -1;
    }

    if (strcmp(name, "PATH") == 0) {
        strncpy(g_path, value, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        return 0;
    }
    if (strcmp(name, "PWD") == 0) {
        strncpy(g_pwd, value, sizeof(g_pwd) - 1);
        g_pwd[sizeof(g_pwd) - 1] = '\0';
        return 0;
    }

    for (uint32_t i = 0; i < g_var_count; i++) {
        if (strcmp(g_var_name[i], name) == 0) {
            strncpy(g_var_val[i], value, MAX_VAR_VALUE - 1);
            g_var_val[i][MAX_VAR_VALUE - 1] = '\0';
            return 0;
        }
    }

    if (g_var_count >= MAX_VARS) return -1;
    strncpy(g_var_name[g_var_count], name, MAX_VAR_NAME - 1);
    g_var_name[g_var_count][MAX_VAR_NAME - 1] = '\0';
    strncpy(g_var_val[g_var_count], value, MAX_VAR_VALUE - 1);
    g_var_val[g_var_count][MAX_VAR_VALUE - 1] = '\0';
    g_var_count++;
    return 0;
}

static int unset_var(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "PATH") == 0 || strcmp(name, "PWD") == 0 || strcmp(name, "TTY") == 0) return -1;
    for (uint32_t i = 0; i < g_var_count; i++) {
        if (strcmp(g_var_name[i], name) == 0) {
            for (uint32_t j = i + 1; j < g_var_count; j++) {
                strcpy(g_var_name[j - 1], g_var_name[j]);
                strcpy(g_var_val[j - 1], g_var_val[j]);
            }
            g_var_count--;
            return 0;
        }
    }
    return -1;
}

static int normalize_path(const char *in, char *out, uint32_t cap) {
    char tmp[256];
    uint32_t i = 0;
    uint32_t o = 0;
    uint32_t seg_starts[64];
    uint32_t seg_count = 0;

    if (!in || !out || cap < 2) return -1;

    if (in[0] == '/') {
        strncpy(tmp, in, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
    } else if (strcmp(g_pwd, "/") == 0) {
        tmp[0] = '/';
        strncpy(tmp + 1, in, sizeof(tmp) - 2);
        tmp[sizeof(tmp) - 1] = '\0';
    } else {
        strncpy(tmp, g_pwd, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        i = (uint32_t)strlen(tmp);
        if (i + 1 >= sizeof(tmp)) return -1;
        tmp[i++] = '/';
        tmp[i] = '\0';
        if (i + (uint32_t)strlen(in) >= sizeof(tmp)) return -1;
        strncpy(tmp + i, in, sizeof(tmp) - i - 1);
        tmp[sizeof(tmp) - 1] = '\0';
    }

    out[o++] = '/';
    i = 0;
    while (tmp[i]) {
        while (tmp[i] == '/') i++;
        if (!tmp[i]) break;

        uint32_t start = i;
        while (tmp[i] && tmp[i] != '/') i++;
        uint32_t len = i - start;

        if (len == 1 && tmp[start] == '.') continue;
        if (len == 2 && tmp[start] == '.' && tmp[start + 1] == '.') {
            if (seg_count > 0) {
                o = seg_starts[seg_count - 1];
                seg_count--;
            }
            continue;
        }

        if (o > 1) {
            if (o >= cap - 1) return -1;
            out[o++] = '/';
        }
        if (o + len >= cap) return -1;
        if (seg_count >= 64) return -1;
        seg_starts[seg_count++] = o;
        for (uint32_t j = 0; j < len; j++) out[o++] = tmp[start + j];
    }

    if (o == 0) out[o++] = '/';
    out[o] = '\0';
    return 0;
}

static int try_exec_from_path(const char *name) {
    char full[160];
    char abs_name[160];
    uint32_t start = 0;
    uint32_t name_len = (uint32_t)strlen(name);

    if (strchr(name, '/')) {
        if (normalize_path(name, abs_name, sizeof(abs_name)) != 0) return -1;
        return exec(abs_name);
    }

    for (uint32_t i = 0;; i++) {
        char c = g_path[i];
        if (c == ':' || c == '\0') {
            uint32_t part_len = i - start;
            if (part_len > 0 && part_len + 1 + name_len + 1 < sizeof(full)) {
                memcpy(full, g_path + start, part_len);
                full[part_len] = '/';
                memcpy(full + part_len + 1, name, name_len);
                full[part_len + 1 + name_len] = '\0';
                if (exec(full) == 0) return 0;
            }
            if (c == '\0') break;
            start = i + 1;
        }
    }
    return -1;
}

static void cmd_fbinfo(void) {
    int fd = open("/devices/framebuffer/buffer", 0);
    dev_fb_info_t fi;
    if (fd < 0 || ioctl(fd, DEV_IOCTL_FB_GET_INFO, &fi) != 0) {
        fprintf(stderr, "fb ioctl error\n");
        if (fd >= 0) close(fd);
        return;
    }
    fprintf(stdout, "fb %ux%u bpp=%u pitch=%u\n", fi.width, fi.height, fi.bpp, fi.pitch);
    close(fd);
}

static void cmd_ttyinfo(void) {
    dev_tty_info_t ti;
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) != 0) {
        fprintf(stderr, "tty ioctl error\n");
        return;
    }
    fprintf(stdout, "tty kind=%u idx=%u cols=%u rows=%u cur=%u,%u\n",
        ti.kind, ti.index, ti.cols, ti.rows, ti.cursor_x, ti.cursor_y);
}

static void update_tty_name(void) {
    dev_tty_info_t ti;
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) != 0) {
        strncpy(g_tty_name, "tty?", sizeof(g_tty_name) - 1);
        g_tty_name[sizeof(g_tty_name) - 1] = '\0';
        return;
    }
    if (ti.kind == DEV_TTY_KIND_VESA) {
        g_tty_name[0] = 't';
        g_tty_name[1] = 't';
        g_tty_name[2] = 'y';
        if (ti.index < 10) {
            g_tty_name[3] = (char)('0' + ti.index);
            g_tty_name[4] = '\0';
        } else {
            g_tty_name[3] = (char)('0' + (ti.index / 10));
            g_tty_name[4] = (char)('0' + (ti.index % 10));
            g_tty_name[5] = '\0';
        }
    } else {
        g_tty_name[0] = 't';
        g_tty_name[1] = 't';
        g_tty_name[2] = 'y';
        g_tty_name[3] = 'S';
        if (ti.index < 10) {
            g_tty_name[4] = (char)('0' + ti.index);
            g_tty_name[5] = '\0';
        } else {
            g_tty_name[4] = (char)('0' + (ti.index / 10));
            g_tty_name[5] = (char)('0' + (ti.index % 10));
            g_tty_name[6] = '\0';
        }
    }
}

static void cmd_chvt(const char *arg) {
    uint32_t idx = 0;
    int fd = -1;
    if (parse_u32_dec(arg, &idx) != 0) {
        fprintf(stderr, "usage: chvt <0-7>\n");
        return;
    }
    fd = open("/devices/tty0", 0);
    if (fd < 0) {
        fprintf(stderr, "chvt failed\n");
        return;
    }
    if (ioctl(fd, DEV_IOCTL_TTY_SET_ACTIVE, &idx) != 0) {
        fprintf(stderr, "chvt failed\n");
        close(fd);
        return;
    }
    close(fd);
}

static void cmd_kbdinfo(void) {
    int fd = open("/devices/keyboard", 0);
    dev_keyboard_info_t ki;
    if (fd < 0 || ioctl(fd, DEV_IOCTL_KBD_GET_INFO, &ki) != 0) {
        fprintf(stderr, "kbd ioctl error\n");
        if (fd >= 0) close(fd);
        return;
    }
    fprintf(stdout, "kbd layout=%u caps=%u num=%u scroll=%u\n",
        ki.layout, ki.caps_lock, ki.num_lock, ki.scroll_lock);
    close(fd);
}

static void cmd_mouseinfo(void) {
    int fd = open("/devices/mouse", 0);
    dev_mouse_info_t mi;
    if (fd < 0 || ioctl(fd, DEV_IOCTL_MOUSE_GET_INFO, &mi) != 0) {
        fprintf(stderr, "mouse ioctl error\n");
        if (fd >= 0) close(fd);
        return;
    }
    fprintf(stdout, "mouse x=%d y=%d btn=%u\n", mi.x, mi.y, mi.buttons);
    close(fd);
}

static int append_char(char *dst, uint32_t cap, uint32_t *len, char c) {
    if (*len + 1 >= cap) return -1;
    dst[*len] = c;
    (*len)++;
    dst[*len] = '\0';
    return 0;
}

static int append_text(char *dst, uint32_t cap, uint32_t *len, const char *src) {
    if (!src) return 0;
    for (uint32_t i = 0; src[i]; i++) {
        if (append_char(dst, cap, len, src[i]) != 0) return -1;
    }
    return 0;
}

static int parse_argv(const char *cmd, char argv[MAX_ARGS][MAX_TOKEN], int *argc_out) {
    int argc = 0;
    uint32_t i = 0;

    while (cmd[i]) {
        uint32_t tlen = 0;
        int in_sq = 0;
        int in_dq = 0;

        while (cmd[i] && is_space(cmd[i])) i++;
        if (!cmd[i]) break;
        if (cmd[i] == '#') break;

        if (argc >= MAX_ARGS) return -1;
        argv[argc][0] = '\0';

        while (cmd[i]) {
            char c = cmd[i];
            if (!in_sq && !in_dq && is_space(c)) break;

            if (!in_dq && c == '\'') {
                in_sq = !in_sq;
                i++;
                continue;
            }
            if (!in_sq && c == '"') {
                in_dq = !in_dq;
                i++;
                continue;
            }
            if (!in_sq && c == '\\' && cmd[i + 1]) {
                i++;
                c = cmd[i];
                if (append_char(argv[argc], MAX_TOKEN, &tlen, c) != 0) return -1;
                i++;
                continue;
            }
            if (!in_sq && c == '$') {
                char name[MAX_VAR_NAME];
                uint32_t nlen = 0;
                const char *v;
                i++;
                if (!is_name_start(cmd[i])) {
                    if (append_char(argv[argc], MAX_TOKEN, &tlen, '$') != 0) return -1;
                    continue;
                }
                while (cmd[i] && is_name_char(cmd[i]) && nlen + 1 < sizeof(name)) {
                    name[nlen++] = cmd[i++];
                }
                name[nlen] = '\0';
                v = get_var(name);
                if (append_text(argv[argc], MAX_TOKEN, &tlen, v ? v : "") != 0) return -1;
                continue;
            }

            if (append_char(argv[argc], MAX_TOKEN, &tlen, c) != 0) return -1;
            i++;
        }

        if (in_sq || in_dq) return -1;
        argc++;
    }

    *argc_out = argc;
    return 0;
}

static int exec_single(const char *cmd) {
    char argv[MAX_ARGS][MAX_TOKEN];
    int argc = 0;
    char abs[160];
    char buf[512];

    if (parse_argv(cmd, argv, &argc) != 0) {
        fprintf(stderr, "parse error\n");
        return -1;
    }
    if (argc == 0) return 0;

    if (strcmp(argv[0], "help") == 0) {
        fprintf(stdout, "builtins: help echo cd pwd export unset env history exit\n");
        fprintf(stdout, "commands: ls cat mkdir mkfifo mksock touch rm rmdir tee run path setpath chvt fbinfo ttyinfo kbdinfo mouseinfo\n");
        return 0;
    }

    if (strcmp(argv[0], "env") == 0 || strcmp(argv[0], "path") == 0) {
        fprintf(stdout, "PATH=%s\nPWD=%s\nTTY=%s\n", g_path, g_pwd, g_tty_name);
        for (uint32_t i = 0; i < g_var_count; i++) {
            fprintf(stdout, "%s=%s\n", g_var_name[i], g_var_val[i]);
        }
        return 0;
    }

    if (strcmp(argv[0], "setpath") == 0) {
        if (argc < 2) {
            fprintf(stderr, "usage: setpath <PATH>\n");
            return -1;
        }
        strncpy(g_path, argv[1], sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        return 0;
    }

    if (strcmp(argv[0], "export") == 0) {
        char *eq;
        if (argc < 2) {
            fprintf(stderr, "usage: export NAME=VALUE\n");
            return -1;
        }
        eq = strchr(argv[1], '=');
        if (!eq) {
            fprintf(stderr, "usage: export NAME=VALUE\n");
            return -1;
        }
        *eq = '\0';
        if (set_var(argv[1], eq + 1) != 0) {
            fprintf(stderr, "export failed\n");
            return -1;
        }
        return 0;
    }

    if (strcmp(argv[0], "unset") == 0) {
        if (argc < 2 || unset_var(argv[1]) != 0) {
            fprintf(stderr, "unset failed\n");
            return -1;
        }
        return 0;
    }

    if (strcmp(argv[0], "history") == 0) {
        uint32_t base = (g_history_count < MAX_HISTORY) ? 0 : g_history_next;
        for (uint32_t i = 0; i < g_history_count; i++) {
            uint32_t idx = (base + i) % MAX_HISTORY;
            fprintf(stdout, "%u  %s\n", i + 1, g_history[idx]);
        }
        return 0;
    }

    if (strcmp(argv[0], "pwd") == 0) {
        fprintf(stdout, "%s\n", g_pwd);
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {
        const char *target = (argc >= 2) ? argv[1] : "/";
        if (normalize_path(target, abs, sizeof(abs)) != 0 || list(abs, buf, sizeof(buf)) < 0) {
            fprintf(stderr, "cd failed\n");
            return -1;
        }
        strncpy(g_pwd, abs, sizeof(g_pwd) - 1);
        g_pwd[sizeof(g_pwd) - 1] = '\0';
        return 0;
    }

    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) fprintf(stdout, " ");
            fprintf(stdout, "%s", argv[i]);
        }
        fprintf(stdout, "\n");
        return 0;
    }

    if (strcmp(argv[0], "chvt") == 0) {
        if (argc < 2) {
            fprintf(stderr, "usage: chvt <0-7>\n");
            return -1;
        }
        cmd_chvt(argv[1]);
        return 0;
    }

    if (strcmp(argv[0], "fbinfo") == 0) { cmd_fbinfo(); return 0; }
    if (strcmp(argv[0], "ttyinfo") == 0) { cmd_ttyinfo(); return 0; }
    if (strcmp(argv[0], "kbdinfo") == 0) { cmd_kbdinfo(); return 0; }
    if (strcmp(argv[0], "mouseinfo") == 0) { cmd_mouseinfo(); return 0; }

    if (strcmp(argv[0], "ls") == 0) {
        const char *target = (argc >= 2) ? argv[1] : g_pwd;
        if (normalize_path(target, abs, sizeof(abs)) != 0) {
            fprintf(stderr, "ls failed\n");
            return -1;
        }
        {
            int32_t n = list(abs, buf, sizeof(buf));
            if (n < 0) fprintf(stderr, "ls failed\n");
            else fprintf(stdout, "%s\n", buf);
        }
        return 0;
    }

    if (strcmp(argv[0], "cat") == 0) {
        int fd;
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0) {
            fprintf(stderr, "cat open failed\n");
            return -1;
        }
        fd = open(abs, 0);
        if (fd < 0) {
            fprintf(stderr, "cat open failed\n");
            return -1;
        }
        for (;;) {
            int32_t n = read(fd, buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';
            fprintf(stdout, "%s", buf);
            if (n < (int32_t)(sizeof(buf) - 1)) break;
        }
        fprintf(stdout, "\n");
        close(fd);
        return 0;
    }

    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0 || mkdir(abs) != 0) {
            fprintf(stderr, "mkdir failed\n");
            return -1;
        }
        return 0;
    }
    if (strcmp(argv[0], "mkfifo") == 0) {
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0 || mkfifo(abs) != 0) {
            fprintf(stderr, "mkfifo failed\n");
            return -1;
        }
        return 0;
    }
    if (strcmp(argv[0], "mksock") == 0) {
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0 || mksock(abs) != 0) {
            fprintf(stderr, "mksock failed\n");
            return -1;
        }
        return 0;
    }
    if (strcmp(argv[0], "touch") == 0) {
        int fd;
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0) {
            fprintf(stderr, "touch failed\n");
            return -1;
        }
        fd = open(abs, 1);
        if (fd < 0) {
            fprintf(stderr, "touch failed\n");
            return -1;
        }
        close(fd);
        return 0;
    }
    if (strcmp(argv[0], "rm") == 0) {
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0 || unlink(abs) != 0) {
            fprintf(stderr, "rm failed\n");
            return -1;
        }
        return 0;
    }
    if (strcmp(argv[0], "rmdir") == 0) {
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0 || rmdir(abs) != 0) {
            fprintf(stderr, "rmdir failed\n");
            return -1;
        }
        return 0;
    }
    if (strcmp(argv[0], "tee") == 0) {
        int fd;
        char ln[MAX_LINE];
        int32_t n;
        if (argc < 2 || normalize_path(argv[1], abs, sizeof(abs)) != 0) {
            fprintf(stderr, "tee open failed\n");
            return -1;
        }
        fd = open(abs, 1);
        if (fd < 0) {
            fprintf(stderr, "tee open failed\n");
            return -1;
        }
        n = read(fileno(stdin), ln, sizeof(ln) - 1);
        if (n > 0) {
            ln[n] = '\0';
            write(fileno(stdout), ln, (uint32_t)n);
            append(fd, ln, (uint32_t)n);
        }
        close(fd);
        return 0;
    }
    if (strcmp(argv[0], "run") == 0) {
        if (argc < 2 || try_exec_from_path(argv[1]) != 0) {
            fprintf(stderr, "exec failed\n");
            return -1;
        }
        return 0;
    }

    if (strcmp(argv[0], "exit") == 0) {
        exit(0);
        return 0;
    }

    if (try_exec_from_path(argv[0]) != 0) {
        fprintf(stderr, "unknown command\n");
        return -1;
    }
    return 0;
}

static int split_outside_quotes(const char *s, char delim, uint32_t *pos_out) {
    int in_sq = 0;
    int in_dq = 0;
    uint32_t i = 0;
    while (s[i]) {
        char c = s[i];
        if (c == '\\' && s[i + 1]) {
            i += 2;
            continue;
        }
        if (c == '\'' && !in_dq) in_sq = !in_sq;
        else if (c == '"' && !in_sq) in_dq = !in_dq;
        else if (c == delim && !in_sq && !in_dq) {
            *pos_out = i;
            return 1;
        }
        i++;
    }
    return 0;
}

typedef struct {
    char cmd[MAX_LINE];
    char in_path[160];
    char out_path[160];
    char err_path[160];
    int out_append;
    int err_append;
} redir_spec_t;

static void redir_spec_init(redir_spec_t *r) {
    r->cmd[0] = '\0';
    r->in_path[0] = '\0';
    r->out_path[0] = '\0';
    r->err_path[0] = '\0';
    r->out_append = 0;
    r->err_append = 0;
}

static int read_file_into(const char *path, char *out, uint32_t cap) {
    int fd;
    uint32_t len = 0;
    fd = open(path, 0);
    if (fd < 0) return -1;
    while (len + 1 < cap) {
        int32_t n = read(fd, out + len, cap - len - 1);
        if (n <= 0) break;
        len += (uint32_t)n;
    }
    out[len] = '\0';
    close(fd);
    return 0;
}

static int write_text_to_file(const char *path, const char *text, int append_mode) {
    int fd = open(path, 1);
    uint32_t n;
    if (fd < 0) return -1;
    n = (uint32_t)strlen(text);
    if (append_mode) {
        if (append(fd, text, n) < 0) {
            close(fd);
            return -1;
        }
    } else {
        if (write(fd, text, n) < 0) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return 0;
}

static int parse_redirections(const char *src, redir_spec_t *out) {
    uint32_t i = 0;
    uint32_t w = 0;
    int in_sq = 0;
    int in_dq = 0;
    redir_spec_init(out);

    while (src[i]) {
        char c = src[i];

        if (c == '\\' && src[i + 1]) {
            if (w + 1 >= sizeof(out->cmd)) return -1;
            out->cmd[w++] = src[i++];
            out->cmd[w++] = src[i++];
            continue;
        }
        if (c == '\'' && !in_dq) {
            in_sq = !in_sq;
            if (w + 1 >= sizeof(out->cmd)) return -1;
            out->cmd[w++] = src[i++];
            continue;
        }
        if (c == '"' && !in_sq) {
            in_dq = !in_dq;
            if (w + 1 >= sizeof(out->cmd)) return -1;
            out->cmd[w++] = src[i++];
            continue;
        }

        if (!in_sq && !in_dq) {
            int is_err = 0;
            int is_out = 0;
            int is_in = 0;
            int is_append = 0;
            uint32_t j;
            char raw[160];
            char abs[160];
            uint32_t rlen = 0;

            if (c == '>' || c == '<') {
                is_out = (c == '>');
                is_in = (c == '<');
                if (is_out && src[i + 1] == '>') {
                    is_append = 1;
                    i += 2;
                } else {
                    i++;
                }
            } else if (c == '2' && src[i + 1] == '>') {
                is_err = 1;
                i += 2;
            } else if (c == '&' && src[i + 1] == '2' && src[i + 2] == '>') {
                is_err = 1;
                i += 3;
            } else {
                if (w + 1 >= sizeof(out->cmd)) return -1;
                out->cmd[w++] = src[i++];
                continue;
            }

            while (src[i] && is_space(src[i])) i++;
            if (!src[i]) return -1;

            if (src[i] == '"' || src[i] == '\'') {
                char q = src[i++];
                while (src[i] && src[i] != q) {
                    if (rlen + 1 >= sizeof(raw)) return -1;
                    raw[rlen++] = src[i++];
                }
                if (src[i] != q) return -1;
                i++;
            } else {
                j = i;
                while (src[j] && !is_space(src[j]) && src[j] != '>' && src[j] != '<' && src[j] != '|') j++;
                while (i < j) {
                    if (rlen + 1 >= sizeof(raw)) return -1;
                    raw[rlen++] = src[i++];
                }
            }
            raw[rlen] = '\0';
            if (normalize_path(raw, abs, sizeof(abs)) != 0) return -1;

            if (is_in) {
                strncpy(out->in_path, abs, sizeof(out->in_path) - 1);
                out->in_path[sizeof(out->in_path) - 1] = '\0';
            } else if (is_out) {
                strncpy(out->out_path, abs, sizeof(out->out_path) - 1);
                out->out_path[sizeof(out->out_path) - 1] = '\0';
                out->out_append = is_append;
            } else if (is_err) {
                strncpy(out->err_path, abs, sizeof(out->err_path) - 1);
                out->err_path[sizeof(out->err_path) - 1] = '\0';
                out->err_append = 0;
            }
            continue;
        }

        if (w + 1 >= sizeof(out->cmd)) return -1;
        out->cmd[w++] = src[i++];
    }

    out->cmd[w] = '\0';
    return 0;
}

static int capture_simple_io(const char *cmd, const char *in_data, char *out, uint32_t out_cap, char *err, uint32_t err_cap) {
    char argv[MAX_ARGS][MAX_TOKEN];
    int argc = 0;
    uint32_t olen = 0;
    uint32_t elen = 0;
    char abs[160];
    char buf[256];

    out[0] = '\0';
    err[0] = '\0';
    if (parse_argv(cmd, argv, &argc) != 0 || argc == 0) return -1;

    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1 && append_char(out, out_cap, &olen, ' ') != 0) return -1;
            if (append_text(out, out_cap, &olen, argv[i]) != 0) return -1;
        }
        if (append_char(out, out_cap, &olen, '\n') != 0) return -1;
        return 0;
    }

    if (strcmp(argv[0], "pwd") == 0) {
        if (append_text(out, out_cap, &olen, g_pwd) != 0) return -1;
        if (append_char(out, out_cap, &olen, '\n') != 0) return -1;
        return 0;
    }

    if (strcmp(argv[0], "ls") == 0) {
        const char *target = (argc >= 2) ? argv[1] : g_pwd;
        int32_t n;
        if (normalize_path(target, abs, sizeof(abs)) != 0) {
            (void)append_text(err, err_cap, &elen, "ls failed\n");
            return -1;
        }
        n = list(abs, buf, sizeof(buf));
        if (n < 0) {
            (void)append_text(err, err_cap, &elen, "ls failed\n");
            return -1;
        }
        if (append_text(out, out_cap, &olen, buf) != 0) return -1;
        if (append_char(out, out_cap, &olen, '\n') != 0) return -1;
        return 0;
    }

    if (strcmp(argv[0], "cat") == 0) {
        int fd;
        if (argc == 1) {
            if (append_text(out, out_cap, &olen, in_data ? in_data : "") != 0) return -1;
            return 0;
        }
        if (normalize_path(argv[1], abs, sizeof(abs)) != 0) {
            (void)append_text(err, err_cap, &elen, "cat open failed\n");
            return -1;
        }
        fd = open(abs, 0);
        if (fd < 0) {
            (void)append_text(err, err_cap, &elen, "cat open failed\n");
            return -1;
        }
        for (;;) {
            int32_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            for (int32_t i = 0; i < n; i++) {
                if (append_char(out, out_cap, &olen, buf[i]) != 0) {
                    close(fd);
                    return -1;
                }
            }
            if (n < (int32_t)sizeof(buf)) break;
        }
        close(fd);
        return 0;
    }

    if (strcmp(argv[0], "help") == 0) {
        (void)append_text(out, out_cap, &olen, "builtins: help echo cd pwd export unset env history exit\n");
        return 0;
    }

    (void)append_text(err, err_cap, &elen, "unknown command\n");
    return -1;
}

static int exec_with_redir_or_pipe(char *seg) {
    uint32_t pos = 0;
    char left[MAX_LINE];
    char right[MAX_LINE];
    redir_spec_t lspec;
    redir_spec_t rspec;
    char in_data[2048];
    char out_data[2048];
    char err_data[512];

    if (split_outside_quotes(seg, '|', &pos)) {
        int rc;
        if (pos >= sizeof(left)) return -1;
        memcpy(left, seg, pos);
        left[pos] = '\0';
        strncpy(right, seg + pos + 1, sizeof(right) - 1);
        right[sizeof(right) - 1] = '\0';

        if (parse_redirections(trim_ws(left), &lspec) != 0 || parse_redirections(trim_ws(right), &rspec) != 0) {
            fprintf(stderr, "parse error\n");
            return -1;
        }

        in_data[0] = '\0';
        if (lspec.in_path[0] && read_file_into(lspec.in_path, in_data, sizeof(in_data)) != 0) {
            fprintf(stderr, "redirect failed\n");
            return -1;
        }
        rc = capture_simple_io(trim_ws(lspec.cmd), in_data, out_data, sizeof(out_data), err_data, sizeof(err_data));
        if (lspec.err_path[0]) (void)write_text_to_file(lspec.err_path, err_data, lspec.err_append);
        else if (err_data[0]) fprintf(stderr, "%s", err_data);
        if (rc != 0) return -1;

        in_data[0] = '\0';
        if (rspec.in_path[0]) {
            if (read_file_into(rspec.in_path, in_data, sizeof(in_data)) != 0) {
                fprintf(stderr, "redirect failed\n");
                return -1;
            }
        } else {
            strncpy(in_data, out_data, sizeof(in_data) - 1);
            in_data[sizeof(in_data) - 1] = '\0';
        }

        rc = capture_simple_io(trim_ws(rspec.cmd), in_data, out_data, sizeof(out_data), err_data, sizeof(err_data));
        if (rspec.out_path[0]) {
            if (write_text_to_file(rspec.out_path, out_data, rspec.out_append) != 0) fprintf(stderr, "redirect failed\n");
        } else {
            write(fileno(stdout), out_data, (uint32_t)strlen(out_data));
        }
        if (rspec.err_path[0]) (void)write_text_to_file(rspec.err_path, err_data, rspec.err_append);
        else if (err_data[0]) fprintf(stderr, "%s", err_data);
        return rc;
    }

    if (strchr(seg, '>') || strchr(seg, '<')) {
        int rc;
        if (parse_redirections(trim_ws(seg), &lspec) != 0) {
            fprintf(stderr, "parse error\n");
            return -1;
        }
        in_data[0] = '\0';
        if (lspec.in_path[0] && read_file_into(lspec.in_path, in_data, sizeof(in_data)) != 0) {
            fprintf(stderr, "redirect failed\n");
            return -1;
        }
        rc = capture_simple_io(trim_ws(lspec.cmd), in_data, out_data, sizeof(out_data), err_data, sizeof(err_data));
        if (lspec.out_path[0]) {
            if (write_text_to_file(lspec.out_path, out_data, lspec.out_append) != 0) {
                fprintf(stderr, "redirect failed\n");
                return -1;
            }
        } else {
            write(fileno(stdout), out_data, (uint32_t)strlen(out_data));
        }
        if (lspec.err_path[0]) {
            (void)write_text_to_file(lspec.err_path, err_data, lspec.err_append);
        } else if (err_data[0]) {
            fprintf(stderr, "%s", err_data);
        }
        return rc;
    }

    return exec_single(seg);
}

static int exec_line(char *line) {
    uint32_t i = 0;
    uint32_t start = 0;
    int in_sq = 0;
    int in_dq = 0;

    while (1) {
        char c = line[i];
        int split = 0;
        if (c == '\\' && line[i + 1]) {
            i += 2;
            continue;
        }
        if (c == '\'' && !in_dq) in_sq = !in_sq;
        else if (c == '"' && !in_sq) in_dq = !in_dq;

        if (((c == ';' || c == '\n' || c == '\r') && !in_sq && !in_dq) || c == '\0') split = 1;

        if (split) {
            char save = line[i];
            char *seg;
            line[i] = '\0';
            seg = trim_ws(line + start);
            if (seg[0]) (void)exec_with_redir_or_pipe(seg);
            line[i] = save;
            if (c == '\0') break;
            start = i + 1;
        }
        i++;
    }

    if (in_sq || in_dq) {
        fprintf(stderr, "parse error\n");
        return -1;
    }
    return 0;
}

int main(void) {
    char line[MAX_LINE];

    update_tty_name();
    (void)set_var("PATH", g_path);
    (void)set_var("PWD", g_pwd);
    printf("houseos shell in %s\n", g_tty_name);
    printf("bash-like mode: quotes, ;, $VAR, export/unset/history/help\n");

    for (;;) {
        update_tty_name();
        printf("sh:%s$ ", g_pwd);
        if (read_line(line, sizeof(line)) <= 0) continue;
        history_push(line);
        (void)exec_line(line);
    }
}
