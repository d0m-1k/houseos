#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <devctl.h>
#include <hgui/app.h>

#define PORT_GFXD 7711
#define PORT_COMPOSD 7712

#define BTN_TERMINAL 0
#define BTN_SETTINGS 1
#define BTN_TASKMGR 2
#define BTN_REBOOT 3
#define BTN_POWEROFF 4
#define BTN_COUNT 5

#define EVT_QUEUE_CAP 64

#define TERM_COLS_MAX 120
#define TERM_ROWS_MAX 48

typedef struct {
    char mode[16];
    char ip[32];
    uint32_t mx;
    uint32_t my;
    uint32_t btn;
} wm_state_t;

typedef struct {
    int32_t ascii;
    uint32_t scancode;
    uint32_t pressed;
    uint32_t shift;
    uint32_t ctrl;
    uint32_t alt;
} key_event_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    const char *label;
    uint32_t id;
} ui_button_t;

typedef enum {
    EVT_NONE = 0,
    EVT_STATE = 1,
    EVT_KEY = 2,
} evt_type_t;

typedef struct {
    evt_type_t type;
    wm_state_t st;
    key_event_t key;
} ui_event_t;

static wm_state_t g_state;
static wm_state_t g_prev;
static ui_button_t g_btn[BTN_COUNT];

static int g_sock_in = -1;
static int g_sock_out = -1;
static int g_fd_power = -1;
static struct sockaddr_in g_dst;

static uint32_t g_width = 1024;
static uint32_t g_height = 768;
static uint32_t g_target_tty = 2;
static uint32_t g_prev_btn_down = 0;

static char g_toast[96] = "";
static uint32_t g_toast_ttl_ms = 0;

static ui_event_t g_evt_q[EVT_QUEUE_CAP];
static uint32_t g_evt_head = 0;
static uint32_t g_evt_tail = 0;
static uint32_t g_evt_count = 0;

static int g_fd_ptmx = -1;
static int g_fd_ptm = -1;
static dev_pty_alloc_t g_pty;
static int32_t g_term_pid = -1;
static int g_term_visible = 0;
static int g_term_focus = 0;
static int g_term_ready = 0;

static uint32_t g_term_cols = 0;
static uint32_t g_term_rows = 0;
static uint32_t g_term_cur_x = 0;
static uint32_t g_term_cur_y = 0;
static char g_term_buf[TERM_ROWS_MAX][TERM_COLS_MAX + 1];

static uint32_t g_tick_ms = 0;
static hq_application_t g_ui_app;
static hq_widget_t g_ui_root;

static int is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char *next_tok(char **ps) {
    char *s = *ps;
    char *tok;
    while (*s && is_space(*s)) s++;
    if (!*s) {
        *ps = s;
        return 0;
    }
    tok = s;
    while (*s && !is_space(*s)) s++;
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

static int parse_s32(const char *s, int32_t *out) {
    int neg = 0;
    int32_t v = 0;
    if (!s || !*s || !out) return -1;
    if (*s == '-') {
        neg = 1;
        s++;
        if (!*s) return -1;
    }
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (int32_t)(*s - '0');
        s++;
    }
    *out = neg ? -v : v;
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

static int buf_append_u32(char *dst, uint32_t cap, uint32_t v) {
    char n[16];
    u32_to_dec(v, n, sizeof(n));
    return buf_append(dst, cap, n);
}

static int point_in(uint32_t x, uint32_t y, const ui_button_t *b) {
    if (!b) return 0;
    if (x < b->x || y < b->y) return 0;
    if (x >= b->x + b->w || y >= b->y + b->h) return 0;
    return 1;
}

static void send_cmd(const char *cmd) {
    (void)sendto(g_sock_out, cmd, (uint32_t)strlen(cmd), 0, (const void*)&g_dst, sizeof(g_dst));
}

static void send_clear(const char *hex) {
    char cmd[24];
    strcpy(cmd, "CLR ");
    (void)buf_append(cmd, sizeof(cmd), hex);
    send_cmd(cmd);
}

static void send_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *hex) {
    char cmd[96];
    cmd[0] = '\0';
    (void)buf_append(cmd, sizeof(cmd), "RECT ");
    (void)buf_append_u32(cmd, sizeof(cmd), x);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), y);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), w);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), h);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append(cmd, sizeof(cmd), hex);
    send_cmd(cmd);
}

static void send_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *hex) {
    char cmd[96];
    cmd[0] = '\0';
    (void)buf_append(cmd, sizeof(cmd), "FRAME ");
    (void)buf_append_u32(cmd, sizeof(cmd), x);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), y);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), w);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), h);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append(cmd, sizeof(cmd), hex);
    send_cmd(cmd);
}

static void send_text(uint32_t x, uint32_t y, uint32_t scale, const char *hex, const char *text) {
    char cmd[240];
    cmd[0] = '\0';
    (void)buf_append(cmd, sizeof(cmd), "TEXT ");
    (void)buf_append_u32(cmd, sizeof(cmd), x);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), y);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append_u32(cmd, sizeof(cmd), scale);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append(cmd, sizeof(cmd), hex);
    (void)buf_append(cmd, sizeof(cmd), " ");
    (void)buf_append(cmd, sizeof(cmd), text ? text : "");
    send_cmd(cmd);
}

static void draw_cursor(uint32_t x, uint32_t y) {
    uint32_t cx = x;
    uint32_t cy = y;
    if (cx + 9u >= g_width) cx = (g_width > 10u) ? (g_width - 10u) : 0u;
    if (cy + 13u >= g_height) cy = (g_height > 14u) ? (g_height - 14u) : 0u;
    send_rect(cx + 1u, cy + 1u, 2u, 13u, "000000");
    send_rect(cx + 1u, cy + 1u, 9u, 2u, "000000");
    send_rect(cx, cy, 2u, 13u, "F5F8FB");
    send_rect(cx, cy, 9u, 2u, "F5F8FB");
}

static void evt_push(const ui_event_t *ev) {
    if (!ev) return;
    if (g_evt_count >= EVT_QUEUE_CAP) {
        g_evt_head = (g_evt_head + 1u) % EVT_QUEUE_CAP;
        g_evt_count--;
    }
    g_evt_q[g_evt_tail] = *ev;
    g_evt_tail = (g_evt_tail + 1u) % EVT_QUEUE_CAP;
    g_evt_count++;
}

static int evt_pop(ui_event_t *ev) {
    if (!ev || g_evt_count == 0u) return 0;
    *ev = g_evt_q[g_evt_head];
    g_evt_head = (g_evt_head + 1u) % EVT_QUEUE_CAP;
    g_evt_count--;
    return 1;
}

static void set_toast(const char *msg) {
    strncpy(g_toast, msg ? msg : "", sizeof(g_toast) - 1u);
    g_toast[sizeof(g_toast) - 1u] = '\0';
    g_toast_ttl_ms = 2200u;
}

static int make_target_tty_path(char *out, uint32_t cap) {
    char n[16];
    if (!out || cap < 8u) return -1;
    out[0] = '\0';
    if (buf_append(out, cap, "/dev/tty/") != 0) return -1;
    u32_to_dec(g_target_tty, n, sizeof(n));
    if (buf_append(out, cap, n) != 0) return -1;
    return 0;
}

static int launch_app(const char *path, const char *label) {
    char tty[24];
    int32_t pid;
    char msg[96];
    if (!path || !label) return -1;
    if (make_target_tty_path(tty, sizeof(tty)) != 0) return -1;
    pid = spawn(path, tty);
    if (pid < 0) {
        msg[0] = '\0';
        (void)buf_append(msg, sizeof(msg), label);
        (void)buf_append(msg, sizeof(msg), " launch failed");
        set_toast(msg);
        return -1;
    }
    msg[0] = '\0';
    (void)buf_append(msg, sizeof(msg), label);
    (void)buf_append(msg, sizeof(msg), " started");
    set_toast(msg);
    return 0;
}

static int state_changed(void) {
    if (strcmp(g_state.mode, g_prev.mode) != 0) return 1;
    if (strcmp(g_state.ip, g_prev.ip) != 0) return 1;
    if (g_state.mx != g_prev.mx) return 1;
    if (g_state.my != g_prev.my) return 1;
    if (g_state.btn != g_prev.btn) return 1;
    return 0;
}

static void term_clear(void) {
    for (uint32_t y = 0; y < TERM_ROWS_MAX; y++) {
        memset(g_term_buf[y], ' ', TERM_COLS_MAX);
        g_term_buf[y][TERM_COLS_MAX] = '\0';
    }
    g_term_cur_x = 0u;
    g_term_cur_y = 0u;
}

static void term_scroll(void) {
    if (g_term_rows == 0u || g_term_cols == 0u) return;
    for (uint32_t y = 1; y < g_term_rows; y++) {
        memcpy(g_term_buf[y - 1u], g_term_buf[y], g_term_cols);
    }
    memset(g_term_buf[g_term_rows - 1u], ' ', g_term_cols);
    if (g_term_cur_y > 0u) g_term_cur_y--;
}

static void term_newline(void) {
    g_term_cur_x = 0u;
    g_term_cur_y++;
    if (g_term_cur_y >= g_term_rows) {
        g_term_cur_y = (g_term_rows > 0u) ? (g_term_rows - 1u) : 0u;
        term_scroll();
    }
}

static void term_putc(char ch) {
    if (g_term_cols == 0u || g_term_rows == 0u) return;
    if (ch == '\r') return;
    if (ch == '\n') {
        term_newline();
        return;
    }
    if (ch == '\b') {
        if (g_term_cur_x > 0u) {
            g_term_cur_x--;
            g_term_buf[g_term_cur_y][g_term_cur_x] = ' ';
        }
        return;
    }
    if ((uint8_t)ch < 32u || (uint8_t)ch > 126u) return;

    g_term_buf[g_term_cur_y][g_term_cur_x] = ch;
    g_term_cur_x++;
    if (g_term_cur_x >= g_term_cols) term_newline();
}

static void term_feed(const char *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) term_putc(buf[i]);
}

static void layout_buttons(void) {
    uint32_t top_h = 44u;
    uint32_t panel_w = 200u;
    uint32_t left = 14u;
    uint32_t y = top_h + 18u;
    uint32_t bw = panel_w - 28u;
    uint32_t bh = 42u;
    uint32_t gap = 10u;

    g_btn[0].x = left; g_btn[0].y = y; g_btn[0].w = bw; g_btn[0].h = bh; g_btn[0].label = "Terminal"; g_btn[0].id = BTN_TERMINAL; y += bh + gap;
    g_btn[1].x = left; g_btn[1].y = y; g_btn[1].w = bw; g_btn[1].h = bh; g_btn[1].label = "Settings"; g_btn[1].id = BTN_SETTINGS; y += bh + gap;
    g_btn[2].x = left; g_btn[2].y = y; g_btn[2].w = bw; g_btn[2].h = bh; g_btn[2].label = "TaskMgr"; g_btn[2].id = BTN_TASKMGR; y += bh + gap;
    g_btn[3].x = left; g_btn[3].y = y; g_btn[3].w = bw; g_btn[3].h = bh; g_btn[3].label = "Reboot"; g_btn[3].id = BTN_REBOOT; y += bh + gap;
    g_btn[4].x = left; g_btn[4].y = y; g_btn[4].w = bw; g_btn[4].h = bh; g_btn[4].label = "Power Off"; g_btn[4].id = BTN_POWEROFF;
}

static void detect_screen(void) {
    dev_fb_info_t info;
    int fd = open("/dev/vesa", 0);
    if (fd < 0) return;
    if (ioctl(fd, DEV_IOCTL_VESA_GET_INFO, &info) == 0) {
        if (info.width >= 640u) g_width = info.width;
        if (info.height >= 480u) g_height = info.height;
    }
    close(fd);
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

static int bind_input(void) {
    struct sockaddr_in a;
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return -1;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT_COMPOSD);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (const void*)&a, sizeof(a)) != 0) {
        close(s);
        return -1;
    }
    return s;
}

static int parse_state_packet(char *msg, wm_state_t *out) {
    char *p = msg;
    char *tok = next_tok(&p);
    if (!out || !tok || strcmp(tok, "STATE") != 0) return -1;
    while (1) {
        char *kv = next_tok(&p);
        char *eq;
        if (!kv) break;
        eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(kv, "mode") == 0) {
            strncpy(out->mode, eq + 1, sizeof(out->mode) - 1u);
            out->mode[sizeof(out->mode) - 1u] = '\0';
        } else if (strcmp(kv, "ip") == 0) {
            strncpy(out->ip, eq + 1, sizeof(out->ip) - 1u);
            out->ip[sizeof(out->ip) - 1u] = '\0';
        } else if (strcmp(kv, "mx") == 0) {
            (void)parse_u32(eq + 1, &out->mx);
        } else if (strcmp(kv, "my") == 0) {
            (void)parse_u32(eq + 1, &out->my);
        } else if (strcmp(kv, "btn") == 0) {
            (void)parse_u32(eq + 1, &out->btn);
        }
    }
    return 0;
}

static int parse_key_packet(char *msg, key_event_t *out) {
    char *p = msg;
    char *tok = next_tok(&p);
    if (!out || !tok || strcmp(tok, "KEY") != 0) return -1;
    memset(out, 0, sizeof(*out));
    while (1) {
        char *kv = next_tok(&p);
        char *eq;
        if (!kv) break;
        eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(kv, "a") == 0) {
            (void)parse_s32(eq + 1, &out->ascii);
        } else if (strcmp(kv, "s") == 0) {
            (void)parse_u32(eq + 1, &out->scancode);
        } else if (strcmp(kv, "p") == 0) {
            (void)parse_u32(eq + 1, &out->pressed);
        } else if (strcmp(kv, "sh") == 0) {
            (void)parse_u32(eq + 1, &out->shift);
        } else if (strcmp(kv, "ct") == 0) {
            (void)parse_u32(eq + 1, &out->ctrl);
        } else if (strcmp(kv, "al") == 0) {
            (void)parse_u32(eq + 1, &out->alt);
        }
    }
    return 0;
}

static void poll_packets(void) {
    char msg[256];
    struct sockaddr_in src;
    uint32_t slen;
    while (1) {
        slen = sizeof(src);
        int32_t n = recvfrom(g_sock_in, msg, sizeof(msg) - 1u, MSG_DONTWAIT, (void*)&src, &slen);
        ui_event_t ev;
        if (n <= 0) break;
        msg[n] = '\0';
        memset(&ev, 0, sizeof(ev));
        ev.st = g_state;
        if (parse_state_packet(msg, &ev.st) == 0) {
            ev.type = EVT_STATE;
            evt_push(&ev);
            continue;
        }
        if (parse_key_packet(msg, &ev.key) == 0) {
            ev.type = EVT_KEY;
            evt_push(&ev);
        }
    }
}

static int ensure_terminal_backend(void) {
    if (g_term_ready) return 0;

    if (g_fd_ptmx < 0) g_fd_ptmx = open("/dev/ptmx", 0);
    if (g_fd_ptmx < 0) return -1;

    memset(&g_pty, 0, sizeof(g_pty));
    if (ioctl(g_fd_ptmx, DEV_IOCTL_PTY_ALLOC, &g_pty) != 0) return -1;

    g_fd_ptm = open(g_pty.master_path, 0);
    if (g_fd_ptm < 0) return -1;
    (void)ioctl(g_fd_ptm, DEV_IOCTL_PTY_RESET, 0);

    g_term_pid = spawn("/bin/sh", g_pty.slave_path);
    if (g_term_pid < 0) return -1;

    g_term_ready = 1;
    term_clear();
    return 0;
}

static void show_terminal(void) {
    if (ensure_terminal_backend() != 0) {
        set_toast("Failed to initialize PTY terminal");
        return;
    }
    if (g_term_pid >= 0) {
        int32_t st = task_state(g_term_pid);
        if (st == 3 || st < 0) g_term_pid = spawn("/bin/sh", g_pty.slave_path);
    }
    g_term_visible = 1;
    g_term_focus = 1;
}

static void hide_terminal(void) {
    g_term_visible = 0;
    g_term_focus = 0;
}

static void send_key_to_terminal(const key_event_t *k) {
    char ch = 0;
    if (!k || !k->pressed || !g_term_ready || g_fd_ptm < 0) return;
    if (k->ctrl && (k->ascii == 'c' || k->ascii == 'C')) {
        ch = 3;
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->ctrl && (k->ascii == 'd' || k->ascii == 'D')) {
        ch = 4;
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->ctrl && (k->ascii == 'l' || k->ascii == 'L')) {
        ch = 12;
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->ascii > 0 && k->ascii < 127) {
        ch = (char)k->ascii;
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->scancode == 0x0Eu) {
        ch = '\b';
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->scancode == 0x1Cu) {
        ch = '\n';
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->scancode == 0x0Fu) {
        ch = '\t';
        (void)write(g_fd_ptm, &ch, 1u);
        return;
    }
    if (k->scancode == 0x48u) {
        (void)write(g_fd_ptm, "\x1B[A", 3u);
        return;
    }
    if (k->scancode == 0x50u) {
        (void)write(g_fd_ptm, "\x1B[B", 3u);
        return;
    }
    if (k->scancode == 0x4Du) {
        (void)write(g_fd_ptm, "\x1B[C", 3u);
        return;
    }
    if (k->scancode == 0x4Bu) {
        (void)write(g_fd_ptm, "\x1B[D", 3u);
    }
}

static int apply_key_shortcuts(const key_event_t *k) {
    if (!k || !k->pressed) return 0;
    if (k->alt && k->scancode >= 0x02u && k->scancode <= 0x09u) {
        uint32_t idx = k->scancode - 0x01u;
        int fd = open("/dev/tty/1", 0);
        if (fd >= 0) {
            (void)ioctl(fd, DEV_IOCTL_TTY_SET_ACTIVE, &idx);
            close(fd);
        }
    }
    if (k->ctrl && k->alt && (k->scancode == 0x14u || k->ascii == 't' || k->ascii == 'T')) {
        show_terminal();
        return 1;
    }
    if (k->scancode == 0x01u && g_term_visible) {
        hide_terminal();
        return 1;
    }
    return 0;
}

static int run_action(uint32_t id) {
    if (id == BTN_TERMINAL) {
        show_terminal();
        return 1;
    }
    if (id == BTN_SETTINGS) {
        (void)launch_app("/bin/settings", "Settings");
        return 1;
    }
    if (id == BTN_TASKMGR) {
        (void)launch_app("/bin/taskmgr", "TaskMgr");
        return 1;
    }
    if (id == BTN_REBOOT) {
        if (g_fd_power >= 0) (void)ioctl(g_fd_power, DEV_IOCTL_POWER_REBOOT, NULL);
        set_toast("Reboot requested");
        return 1;
    }
    if (id == BTN_POWEROFF) {
        if (g_fd_power >= 0) (void)ioctl(g_fd_power, DEV_IOCTL_POWER_POWEROFF, NULL);
        set_toast("Power off requested");
        return 1;
    }
    return 0;
}

static int handle_click(void) {
    uint32_t down = (g_state.btn & 1u) ? 1u : 0u;
    if (!down || g_prev_btn_down) {
        g_prev_btn_down = down;
        return 0;
    }
    g_prev_btn_down = down;

    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        if (point_in(g_state.mx, g_state.my, &g_btn[i])) {
            return run_action(g_btn[i].id);
        }
    }

    if (g_term_visible) {
        uint32_t top_h = 44u;
        uint32_t panel_w = 200u;
        uint32_t tx = panel_w + 20u;
        uint32_t ty = top_h + 20u;
        uint32_t tw = g_width - tx - 20u;
        uint32_t th = g_height - ty - 20u;

        if (g_state.mx >= tx + tw - 24u && g_state.mx < tx + tw - 4u &&
            g_state.my >= ty + 4u && g_state.my < ty + 22u) {
            hide_terminal();
            return 1;
        }

        if (g_state.mx >= tx && g_state.mx < tx + tw &&
            g_state.my >= ty && g_state.my < ty + th) {
            g_term_focus = 1;
            return 1;
        }
    }

    g_term_focus = 0;
    return 1;
}

static void update_terminal_geometry(void) {
    uint32_t top_h = 44u;
    uint32_t panel_w = 200u;
    uint32_t tx = panel_w + 20u;
    uint32_t ty = top_h + 20u;
    uint32_t tw = g_width - tx - 20u;
    uint32_t th = g_height - ty - 20u;
    uint32_t cols = (tw > 20u) ? ((tw - 20u) / 12u) : 0u;
    uint32_t rows = (th > 36u) ? ((th - 36u) / 16u) : 0u;

    if (cols > TERM_COLS_MAX) cols = TERM_COLS_MAX;
    if (rows > TERM_ROWS_MAX) rows = TERM_ROWS_MAX;
    if (cols < 20u) cols = 20u;
    if (rows < 6u) rows = 6u;

    if (cols != g_term_cols || rows != g_term_rows) {
        g_term_cols = cols;
        g_term_rows = rows;
        term_clear();
    }
}

static int pump_pty_output(void) {
    int dirty = 0;
    if (!g_term_ready || g_fd_ptm < 0) return 0;
    while (1) {
        uint32_t avail = 0u;
        char buf[256];
        int32_t n;
        if (ioctl(g_fd_ptm, DEV_IOCTL_PTY_GET_READABLE, &avail) != 0 || avail == 0u) break;
        if (avail > sizeof(buf)) avail = sizeof(buf);
        n = read(g_fd_ptm, buf, avail);
        if (n <= 0) break;
        term_feed(buf, (uint32_t)n);
        dirty = 1;
    }
    return dirty;
}

static void process_events(int active, int *need_redraw) {
    ui_event_t ev;
    while (evt_pop(&ev)) {
        if (ev.type == EVT_STATE) {
            g_state = ev.st;
            if (g_state.mx >= g_width) g_state.mx = (g_width > 0u) ? (g_width - 1u) : 0u;
            if (g_state.my >= g_height) g_state.my = (g_height > 0u) ? (g_height - 1u) : 0u;
            if (active && handle_click()) *need_redraw = 1;
            continue;
        }
        if (ev.type == EVT_KEY) {
            if (apply_key_shortcuts(&ev.key)) *need_redraw = 1;
            if (active && g_term_visible && g_term_focus) {
                send_key_to_terminal(&ev.key);
                *need_redraw = 1;
            }
        }
    }
}

static void draw_terminal(void) {
    uint32_t top_h = 44u;
    uint32_t panel_w = 200u;
    uint32_t tx = panel_w + 20u;
    uint32_t ty = top_h + 20u;
    uint32_t tw = g_width - tx - 20u;
    uint32_t th = g_height - ty - 20u;
    uint32_t cy;

    update_terminal_geometry();

    send_rect(tx, ty, tw, th, "0F151B");
    send_frame(tx, ty, tw, th, "5FA6D8");
    send_rect(tx, ty, tw, 26u, "17232E");
    send_text(tx + 8u, ty + 8u, 2u, "E8F3FB", "Terminal");
    send_text(tx + tw - 18u, ty + 8u, 2u, "F28A8A", "x");

    for (uint32_t y = 0; y < g_term_rows; y++) {
        uint32_t len = g_term_cols;
        while (len > 0u && g_term_buf[y][len - 1u] == ' ') len--;
        g_term_buf[y][len] = '\0';
        cy = ty + 30u + y * 16u;
        if (len > 0u) send_text(tx + 10u, cy, 2u, "CFE3F4", g_term_buf[y]);
        g_term_buf[y][len] = ' ';
    }

    if (g_term_focus && ((g_tick_ms / 500u) % 2u) == 0u) {
        uint32_t cx = tx + 10u + g_term_cur_x * 12u;
        uint32_t yy = ty + 30u + g_term_cur_y * 16u + 13u;
        if (cx + 10u < tx + tw && yy + 2u < ty + th) send_rect(cx, yy, 10u, 2u, "E6F0F8");
    }
}

static void render_desktop(void) {
    uint32_t top_h = 44u;
    uint32_t panel_w = 200u;

    send_clear("0B141D");
    send_rect(0u, 0u, g_width, top_h, "1A2732");
    send_rect(0u, top_h - 2u, g_width, 2u, "4E8DBB");
    send_text(14u, 12u, 2u, "E8F4FD", "[]");
    send_text(44u, 12u, 2u, "E8F4FD", "HouseOS");

    send_rect(0u, top_h, panel_w, g_height - top_h, "14202A");
    send_rect(panel_w - 2u, top_h, 2u, g_height - top_h, "365367");

    for (uint32_t i = 0; i < BTN_COUNT; i++) {
        const ui_button_t *b = &g_btn[i];
        int hover = point_in(g_state.mx, g_state.my, b);
        send_rect(b->x, b->y, b->w, b->h, hover ? "355167" : "243847");
        send_rect(b->x, b->y, b->w, 2u, "7EB5DE");
        send_text(b->x + 10u, b->y + 13u, 2u, "F2FAFF", b->label);
    }

    if (g_term_visible) {
        draw_terminal();
    } else {
        uint32_t cx = panel_w + 36u;
        uint32_t cy = top_h + 34u;
        send_text(cx, cy, 2u, "CFE4F4", "Desktop compositor is running");
        send_text(cx, cy + 32u, 2u, "AFCADB", "Open Terminal from the left panel");
        send_text(cx, cy + 64u, 2u, "AFCADB", "Shortcut: Ctrl+Alt+T");
    }

    if (g_toast_ttl_ms > 0u && g_toast[0]) {
        uint32_t tw = g_width / 2u;
        uint32_t tx = (g_width - tw) / 2u;
        uint32_t ty = g_height - 72u;
        send_rect(tx, ty, tw, 40u, "24384A");
        send_rect(tx, ty, tw, 2u, "87C0EA");
        send_text(tx + 12u, ty + 14u, 2u, "ECF7FF", g_toast);
    }

    draw_cursor(g_state.mx, g_state.my);
    send_cmd("PRESENT");
}

int main(int argc, char **argv) {
    int need_redraw = 1;
    (void)argc;
    (void)argv;

    strcpy(g_state.mode, "unknown");
    strcpy(g_state.ip, "0.0.0.0");
    g_prev = g_state;
    term_clear();

    detect_screen();
    detect_target_tty();
    layout_buttons();

    g_fd_power = open("/dev/power", 0);
    g_sock_in = bind_input();
    if (g_sock_in < 0) return exec("/bin/sh");
    g_sock_out = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock_out < 0) return exec("/bin/sh");

    memset(&g_dst, 0, sizeof(g_dst));
    g_dst.sin_family = AF_INET;
    g_dst.sin_port = htons(PORT_GFXD);
    g_dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    hq_widget_init(&g_ui_root, 1u);
    hq_widget_set_geometry(&g_ui_root, (ui_rect_t){0, 0, (int)g_width, (int)g_height});
    hq_app_init(&g_ui_app, &g_ui_root);

    while (g_ui_app.running) {
        (void)hq_app_process_once(&g_ui_app);
        int active = is_target_tty_active();
        poll_packets();
        process_events(active, &need_redraw);

        if (pump_pty_output()) need_redraw = 1;
        if (state_changed()) need_redraw = 1;
        if ((g_tick_ms % 1000u) == 0u) need_redraw = 1;

        if (g_toast_ttl_ms > 0u) {
            if (g_toast_ttl_ms > 20u) g_toast_ttl_ms -= 20u;
            else g_toast_ttl_ms = 0u;
            need_redraw = 1;
        }

        if (active && need_redraw) {
            render_desktop();
            g_prev = g_state;
            need_redraw = 0;
        }

        sleep(active ? 20u : 40u);
        g_tick_ms += active ? 20u : 40u;
    }
    return 0;
}
