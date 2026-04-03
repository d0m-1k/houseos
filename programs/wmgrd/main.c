#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <devctl.h>
#include <hgui/app.h>

#define PORT_COMPOSD 7712

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t buttons;
} dev_mouse_info_t_local;

typedef struct {
    char mode[16];
    char ip[32];
    uint32_t mx;
    uint32_t my;
    uint32_t btn;
} wm_state_t;

static hq_application_t g_ui_app;
static hq_widget_t g_ui_root;
static uint32_t g_target_tty = 2u;

static int parse_state_value(const char *key, char *out, uint32_t cap) {
    char buf[256];
    char *line;
    char *nl;
    uint32_t klen;
    int fd = open("/run/netd.state", 0);
    int32_t n;
    if (!out || cap < 2u || !key) return -1;
    out[0] = '\0';
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1u);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    klen = (uint32_t)strlen(key);
    line = buf;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            strncpy(out, line + klen + 1u, cap - 1u);
            out[cap - 1u] = '\0';
            return 0;
        }
        if (!nl) break;
        line = nl + 1;
    }
    return -1;
}

static int buf_append(char *dst, uint32_t cap, const char *s) {
    uint32_t a;
    uint32_t b;
    if (!dst || !s || cap == 0u) return -1;
    a = (uint32_t)strlen(dst);
    b = (uint32_t)strlen(s);
    if (a + b + 1u > cap) return -1;
    memcpy(dst + a, s, b + 1u);
    return 0;
}

static void u32_to_dec(uint32_t v, char *out, uint32_t cap) {
    char t[16];
    uint32_t i = 0;
    uint32_t j = 0;
    if (!out || cap < 2u) return;
    if (v == 0u) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v && i < sizeof(t)) {
        t[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (j < i && j + 1u < cap) {
        out[j] = t[i - 1u - j];
        j++;
    }
    out[j] = '\0';
}

static void s32_to_dec(int32_t v, char *out, uint32_t cap) {
    uint32_t uv;
    if (!out || cap < 2u) return;
    if (v < 0) {
        if (cap < 3u) {
            out[0] = '\0';
            return;
        }
        out[0] = '-';
        uv = 0u - (uint32_t)v;
        u32_to_dec(uv, out + 1u, cap - 1u);
        return;
    }
    u32_to_dec((uint32_t)v, out, cap);
}

static void fill_state_from_mouse(wm_state_t *st, int fdm) {
    dev_mouse_info_t mi;
    if (!st) return;
    st->mx = 0u;
    st->my = 0u;
    st->btn = 0u;
    if (fdm < 0) return;
    if (ioctl(fdm, DEV_IOCTL_MOUSE_GET_INFO, &mi) != 0) return;
    if (mi.x > 0) st->mx = (uint32_t)mi.x;
    if (mi.y > 0) st->my = (uint32_t)mi.y;
    st->btn = mi.buttons;
}

static void build_state_msg(char *msg, uint32_t cap, const wm_state_t *st) {
    char n1[16];
    char n2[16];
    char n3[16];
    if (!msg || cap < 2u || !st) return;
    u32_to_dec(st->mx, n1, sizeof(n1));
    u32_to_dec(st->my, n2, sizeof(n2));
    u32_to_dec(st->btn, n3, sizeof(n3));

    strcpy(msg, "STATE mode=");
    (void)buf_append(msg, cap, st->mode);
    (void)buf_append(msg, cap, " ip=");
    (void)buf_append(msg, cap, st->ip);
    (void)buf_append(msg, cap, " mx=");
    (void)buf_append(msg, cap, n1);
    (void)buf_append(msg, cap, " my=");
    (void)buf_append(msg, cap, n2);
    (void)buf_append(msg, cap, " btn=");
    (void)buf_append(msg, cap, n3);
}

static void build_key_msg(char *msg, uint32_t cap, const dev_keyboard_event_t *ev) {
    char a[16];
    char s[16];
    char p[8];
    char sh[8];
    char ct[8];
    char al[8];
    if (!msg || cap < 2u || !ev) return;
    s32_to_dec((int32_t)ev->ascii, a, sizeof(a));
    u32_to_dec((uint32_t)ev->scancode, s, sizeof(s));
    u32_to_dec((uint32_t)ev->pressed, p, sizeof(p));
    u32_to_dec((uint32_t)ev->shift, sh, sizeof(sh));
    u32_to_dec((uint32_t)ev->ctrl, ct, sizeof(ct));
    u32_to_dec((uint32_t)ev->alt, al, sizeof(al));

    strcpy(msg, "KEY a=");
    (void)buf_append(msg, cap, a);
    (void)buf_append(msg, cap, " s=");
    (void)buf_append(msg, cap, s);
    (void)buf_append(msg, cap, " p=");
    (void)buf_append(msg, cap, p);
    (void)buf_append(msg, cap, " sh=");
    (void)buf_append(msg, cap, sh);
    (void)buf_append(msg, cap, " ct=");
    (void)buf_append(msg, cap, ct);
    (void)buf_append(msg, cap, " al=");
    (void)buf_append(msg, cap, al);
}

static int state_diff(const wm_state_t *a, const wm_state_t *b) {
    if (!a || !b) return 1;
    if (strcmp(a->mode, b->mode) != 0) return 1;
    if (strcmp(a->ip, b->ip) != 0) return 1;
    if (a->mx != b->mx || a->my != b->my || a->btn != b->btn) return 1;
    return 0;
}

static void detect_target_tty(void) {
    dev_tty_info_t ti;
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) == 0 && ti.index > 0u) {
        g_target_tty = ti.index;
    } else {
        g_target_tty = 2u;
    }
}

static int is_target_tty_active(void) {
    uint32_t idx = 0xFFFFFFFFu;
    int fd;
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_ACTIVE, &idx) == 0) {
        return (idx == g_target_tty) ? 1 : 0;
    }
    fd = open("/dev/tty/1", 0);
    if (fd >= 0) {
        idx = 0xFFFFFFFFu;
        if (ioctl(fd, DEV_IOCTL_TTY_GET_ACTIVE, &idx) == 0) {
            close(fd);
            return (idx == g_target_tty) ? 1 : 0;
        }
        close(fd);
    }
    return 0;
}

int main(int argc, char **argv) {
    int sock;
    int fdm = -1;
    int fdk = -1;
    struct sockaddr_in dst;
    wm_state_t st;
    wm_state_t prev;
    char msg[220];
    uint32_t net_tick = 0;
    uint32_t force_tick = 0;
    int active = 0;
    (void)argc;
    (void)argv;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return exec("/bin/sh");

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(PORT_COMPOSD);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    fdm = open("/dev/mouse", 0);
    fdk = open("/dev/keyboard", 0);
    detect_target_tty();
    strcpy(st.mode, "unknown");
    strcpy(st.ip, "0.0.0.0");
    st.mx = 0u;
    st.my = 0u;
    st.btn = 0u;
    prev = st;

    hq_widget_init(&g_ui_root, 1u);
    hq_widget_set_geometry(&g_ui_root, (ui_rect_t){0, 0, 1, 1});
    hq_app_init(&g_ui_app, &g_ui_root);

    while (g_ui_app.running) {
        (void)hq_app_process_once(&g_ui_app);
        active = is_target_tty_active();
        if (net_tick == 0u) {
            char mode[16];
            char ip[32];
            strcpy(mode, "unknown");
            strcpy(ip, "0.0.0.0");
            (void)parse_state_value("mode", mode, sizeof(mode));
            (void)parse_state_value("ip", ip, sizeof(ip));
            strncpy(st.mode, mode, sizeof(st.mode) - 1u);
            st.mode[sizeof(st.mode) - 1u] = '\0';
            strncpy(st.ip, ip, sizeof(st.ip) - 1u);
            st.ip[sizeof(st.ip) - 1u] = '\0';
        }

        fill_state_from_mouse(&st, fdm);
        if (state_diff(&st, &prev) || force_tick == 0u) {
            build_state_msg(msg, sizeof(msg), &st);
            (void)sendto(sock, msg, (uint32_t)strlen(msg), 0, (const void*)&dst, sizeof(dst));
            prev = st;
            force_tick = 600u;
        }

        if (active && fdk >= 0) {
            dev_keyboard_event_t ev;
            while (ioctl(fdk, DEV_IOCTL_KBD_GET_EVENT, &ev) == 0) {
                build_key_msg(msg, sizeof(msg), &ev);
                (void)sendto(sock, msg, (uint32_t)strlen(msg), 0, (const void*)&dst, sizeof(dst));
            }
        }

        sleep(20);
        net_tick = (net_tick + 20u) % 250u;
        if (force_tick > 20u) force_tick -= 20u;
        else force_tick = 0u;
    }
    return 0;
}
