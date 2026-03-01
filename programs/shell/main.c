#include <syscall.h>
#include <devctl.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static char g_path[128] = "/bin";
static char g_pwd[128] = "/";
static char g_tty_name[16] = "tty?";

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

static const char *get_var(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "PATH") == 0) return g_path;
    if (strcmp(name, "PWD") == 0) return g_pwd;
    return NULL;
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

int main(void) {
    char line[1024];
    char buf[512];
    char abs[160];

    update_tty_name();
    printf("houseos shell in %s\n", g_tty_name);
    printf("commands: ls cat mkdir mkfifo mksock touch rm rmdir tee run path setpath pwd cd chvt fbinfo ttyinfo kbdinfo mouseinfo exit\n");

    for (;;) {
        printf("sh:%s> ", g_pwd);
        if (read_line(line, sizeof(line)) <= 0) continue;

        if (strcmp(line, "path") == 0) {
            fprintf(stdout, "PATH=%s\nPWD=%s\n", g_path, g_pwd);
            continue;
        }
        if (streq_n(line, "setpath ", 8)) {
            strncpy(g_path, line + 8, sizeof(g_path) - 1);
            g_path[sizeof(g_path) - 1] = '\0';
            continue;
        }
        if (strcmp(line, "pwd") == 0) {
            fprintf(stdout, "%s\n", g_pwd);
            continue;
        }
        if (strcmp(line, "cd") == 0) {
            g_pwd[0] = '/';
            g_pwd[1] = '\0';
            continue;
        }
        if (streq_n(line, "cd ", 3)) {
            if (normalize_path(line + 3, abs, sizeof(abs)) != 0 || list(abs, buf, sizeof(buf)) < 0) {
                fprintf(stderr, "cd failed\n");
            } else {
                strncpy(g_pwd, abs, sizeof(g_pwd) - 1);
                g_pwd[sizeof(g_pwd) - 1] = '\0';
            }
            continue;
        }
        if (streq_n(line, "chvt ", 5)) {
            cmd_chvt(line + 5);
            continue;
        }
        if (strcmp(line, "fbinfo") == 0) {
            cmd_fbinfo();
            continue;
        }
        if (strcmp(line, "ttyinfo") == 0) {
            cmd_ttyinfo();
            continue;
        }
        if (strcmp(line, "kbdinfo") == 0) {
            cmd_kbdinfo();
            continue;
        }
        if (strcmp(line, "mouseinfo") == 0) {
            cmd_mouseinfo();
            continue;
        }
        if (streq_n(line, "echo ", 5)) {
            if (line[5] == '$') {
                const char *v = get_var(line + 6);
                if (v) fprintf(stdout, "%s\n", v);
                else fprintf(stdout, "\n");
            } else {
                fprintf(stdout, "%s\n", line + 5);
            }
            continue;
        }
        if (strcmp(line, "ls") == 0) {
            int32_t n = list(g_pwd, buf, sizeof(buf));
            if (n < 0) fprintf(stderr, "ls failed\n");
            else fprintf(stdout, "%s\n", buf);
            continue;
        }
        if (streq_n(line, "ls ", 3)) {
            if (normalize_path(line + 3, abs, sizeof(abs)) != 0) {
                fprintf(stderr, "ls failed\n");
                continue;
            }
            int32_t n = list(abs, buf, sizeof(buf));
            if (n < 0) fprintf(stderr, "ls failed\n");
            else fprintf(stdout, "%s\n", buf);
            continue;
        }
        if (streq_n(line, "cat ", 4)) {
            if (normalize_path(line + 4, abs, sizeof(abs)) != 0) {
                fprintf(stderr, "cat open failed\n");
                continue;
            }
            int fd = open(abs, 0);
            if (fd < 0) {
                fprintf(stderr, "cat open failed\n");
                continue;
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
            continue;
        }
        if (streq_n(line, "mkdir ", 6)) {
            if (normalize_path(line + 6, abs, sizeof(abs)) != 0 || mkdir(abs) != 0) fprintf(stderr, "mkdir failed\n");
            continue;
        }
        if (streq_n(line, "mkfifo ", 7)) {
            if (normalize_path(line + 7, abs, sizeof(abs)) != 0 || mkfifo(abs) != 0) fprintf(stderr, "mkfifo failed\n");
            continue;
        }
        if (streq_n(line, "mksock ", 7)) {
            if (normalize_path(line + 7, abs, sizeof(abs)) != 0 || mksock(abs) != 0) fprintf(stderr, "mksock failed\n");
            continue;
        }
        if (streq_n(line, "touch ", 6)) {
            if (normalize_path(line + 6, abs, sizeof(abs)) != 0) {
                fprintf(stderr, "touch failed\n");
                continue;
            }
            int fd = open(abs, 1);
            if (fd < 0) fprintf(stderr, "touch failed\n");
            else close(fd);
            continue;
        }
        if (streq_n(line, "rm ", 3)) {
            if (normalize_path(line + 3, abs, sizeof(abs)) != 0 || unlink(abs) != 0) fprintf(stderr, "rm failed\n");
            continue;
        }
        if (streq_n(line, "rmdir ", 6)) {
            if (normalize_path(line + 6, abs, sizeof(abs)) != 0 || rmdir(abs) != 0) fprintf(stderr, "rmdir failed\n");
            continue;
        }
        if (streq_n(line, "tee ", 4)) {
            if (normalize_path(line + 4, abs, sizeof(abs)) != 0) {
                fprintf(stderr, "tee open failed\n");
                continue;
            }
            int fd = open(abs, 1);
            char ln[1024];
            int32_t n;
            if (fd < 0) {
                fprintf(stderr, "tee open failed\n");
                continue;
            }
            n = read(fileno(stdin), ln, sizeof(ln) - 1);
            if (n > 0) {
                ln[n] = '\0';
                write(fileno(stdout), ln, (uint32_t)n);
                append(fd, ln, (uint32_t)n);
            }
            close(fd);
            continue;
        }
        if (streq_n(line, "run ", 4)) {
            if (try_exec_from_path(line + 4) != 0) fprintf(stderr, "exec failed\n");
            continue;
        }
        if (strcmp(line, "exit") == 0) {
            exit(0);
        }

        if (try_exec_from_path(line) != 0) fprintf(stderr, "unknown command\n");
    }
}
