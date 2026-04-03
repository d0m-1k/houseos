#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <devctl.h>
#include <hgui/app.h>

#define FB_MAX_BYTES (3u * 1024u * 1024u)
#define PORT_GFXD 7711

static uint8_t g_fb[FB_MAX_BYTES];
static dev_fb_info_t g_info;
static int g_fd_fb = -1;
static int g_sock = -1;
static uint32_t g_tty_index = 0;
static uint32_t g_active_tty = 0xFFFFFFFFu;
static int g_dirty = 1;
static uint8_t g_font_blob[8192];
static const uint8_t *g_psf_glyphs = NULL;
static uint32_t g_psf_glyph_h = 16;
static hq_application_t g_ui_app;
static hq_widget_t g_ui_root;

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

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int parse_hex24(const char *s, uint32_t *out) {
    int i;
    uint32_t v = 0;
    if (!s || !out) return -1;
    for (i = 0; i < 6; i++) {
        int h = hex_val(s[i]);
        if (h < 0) return -1;
        v = (v << 4) | (uint32_t)h;
    }
    if (s[6] != '\0') return -1;
    *out = v;
    return 0;
}

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

static void put_px(int x, int y, uint32_t c) {
    uint32_t bpp;
    uint32_t off;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= g_info.width || (uint32_t)y >= g_info.height) return;
    bpp = g_info.bpp / 8u;
    if (bpp < 3u || bpp > 4u) return;
    off = (uint32_t)y * g_info.pitch + (uint32_t)x * bpp;
    if (off + bpp > sizeof(g_fb)) return;
    g_fb[off + 0] = (uint8_t)((c >> 16) & 0xFFu);
    g_fb[off + 1] = (uint8_t)((c >> 8) & 0xFFu);
    g_fb[off + 2] = (uint8_t)(c & 0xFFu);
    if (bpp == 4u) g_fb[off + 3] = 0;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    int ix;
    int iy;
    if (w <= 0 || h <= 0) return;
    for (iy = 0; iy < h; iy++) {
        for (ix = 0; ix < w; ix++) put_px(x + ix, y + iy, c);
    }
    g_dirty = 1;
}

static void stroke_rect(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (h == 1) {
        fill_rect(x, y, w, 1, c);
        return;
    }
    if (w == 1) {
        fill_rect(x, y, 1, h, c);
        return;
    }
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y + 1, 1, h - 2, c);
    fill_rect(x + w - 1, y + 1, 1, h - 2, c);
}

static const uint8_t *glyph5x7(char c) {
    static const uint8_t sp[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t dsh[7] = {0, 0, 0, 31, 0, 0, 0};
    static const uint8_t us[7] = {0, 0, 0, 0, 0, 0, 31};
    static const uint8_t dot[7] = {0, 0, 0, 0, 0, 12, 12};
    static const uint8_t com[7] = {0, 0, 0, 0, 0, 12, 8};
    static const uint8_t col[7] = {0, 12, 12, 0, 12, 12, 0};
    static const uint8_t eq[7] = {0, 0, 31, 0, 31, 0, 0};
    static const uint8_t plus[7] = {0, 4, 4, 31, 4, 4, 0};
    static const uint8_t qst[7] = {14, 17, 1, 2, 4, 0, 4};
    static const uint8_t exc[7] = {4, 4, 4, 4, 4, 0, 4};
    static const uint8_t dol[7] = {4, 15, 20, 14, 5, 30, 4};
    static const uint8_t sl[7] = {1, 2, 4, 8, 16, 0, 0};
    static const uint8_t bsl[7] = {16, 8, 4, 2, 1, 0, 0};
    static const uint8_t lpa[7] = {2, 4, 8, 8, 8, 4, 2};
    static const uint8_t rpa[7] = {8, 4, 2, 2, 2, 4, 8};
    static const uint8_t lbr[7] = {14, 8, 8, 8, 8, 8, 14};
    static const uint8_t rbr[7] = {14, 2, 2, 2, 2, 2, 14};
    static const uint8_t lt[7] = {2, 4, 8, 16, 8, 4, 2};
    static const uint8_t gt[7] = {8, 4, 2, 1, 2, 4, 8};
    static const uint8_t n0[7] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t n1[7] = {4, 12, 4, 4, 4, 4, 14};
    static const uint8_t n2[7] = {14, 17, 1, 2, 4, 8, 31};
    static const uint8_t n3[7] = {30, 1, 1, 14, 1, 1, 30};
    static const uint8_t n4[7] = {2, 6, 10, 18, 31, 2, 2};
    static const uint8_t n5[7] = {31, 16, 16, 30, 1, 1, 30};
    static const uint8_t n6[7] = {14, 16, 16, 30, 17, 17, 14};
    static const uint8_t n7[7] = {31, 1, 2, 4, 8, 8, 8};
    static const uint8_t n8[7] = {14, 17, 17, 14, 17, 17, 14};
    static const uint8_t n9[7] = {14, 17, 17, 15, 1, 1, 14};
    static const uint8_t a[7] = {14, 17, 17, 31, 17, 17, 17};
    static const uint8_t b[7] = {30, 17, 17, 30, 17, 17, 30};
    static const uint8_t c_[7] = {14, 17, 16, 16, 16, 17, 14};
    static const uint8_t d[7] = {30, 17, 17, 17, 17, 17, 30};
    static const uint8_t e[7] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t f[7] = {31, 16, 16, 30, 16, 16, 16};
    static const uint8_t g[7] = {14, 17, 16, 23, 17, 17, 14};
    static const uint8_t h[7] = {17, 17, 17, 31, 17, 17, 17};
    static const uint8_t i[7] = {14, 4, 4, 4, 4, 4, 14};
    static const uint8_t j[7] = {7, 2, 2, 2, 2, 18, 12};
    static const uint8_t k[7] = {17, 18, 20, 24, 20, 18, 17};
    static const uint8_t l[7] = {16, 16, 16, 16, 16, 16, 31};
    static const uint8_t m[7] = {17, 27, 21, 21, 17, 17, 17};
    static const uint8_t n[7] = {17, 25, 21, 19, 17, 17, 17};
    static const uint8_t o[7] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t p[7] = {30, 17, 17, 30, 16, 16, 16};
    static const uint8_t q[7] = {14, 17, 17, 17, 21, 18, 13};
    static const uint8_t r[7] = {30, 17, 17, 30, 20, 18, 17};
    static const uint8_t s[7] = {15, 16, 16, 14, 1, 1, 30};
    static const uint8_t t[7] = {31, 4, 4, 4, 4, 4, 4};
    static const uint8_t u[7] = {17, 17, 17, 17, 17, 17, 14};
    static const uint8_t v[7] = {17, 17, 17, 17, 17, 10, 4};
    static const uint8_t w[7] = {17, 17, 17, 21, 21, 21, 10};
    static const uint8_t x[7] = {17, 17, 10, 4, 10, 17, 17};
    static const uint8_t y[7] = {17, 17, 10, 4, 4, 4, 4};
    static const uint8_t z[7] = {31, 1, 2, 4, 8, 16, 31};
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {
        case 'A': return a; case 'B': return b; case 'C': return c_; case 'D': return d;
        case 'E': return e; case 'F': return f; case 'G': return g; case 'H': return h;
        case 'I': return i; case 'J': return j; case 'K': return k; case 'L': return l;
        case 'M': return m; case 'N': return n; case 'O': return o; case 'P': return p;
        case 'Q': return q; case 'R': return r; case 'S': return s; case 'T': return t;
        case 'U': return u; case 'V': return v; case 'W': return w; case 'X': return x;
        case 'Y': return y; case 'Z': return z;
        case '0': return n0; case '1': return n1; case '2': return n2; case '3': return n3;
        case '4': return n4; case '5': return n5; case '6': return n6; case '7': return n7;
        case '8': return n8; case '9': return n9;
        case '-': return dsh; case '_': return us; case '.': return dot; case ',': return com;
        case ':': return col; case '=': return eq; case '+': return plus;
        case '?': return qst; case '!': return exc; case '$': return dol;
        case '/': return sl; case '\\': return bsl; case '(': return lpa; case ')': return rpa;
        case '[': return lbr; case ']': return rbr; case '<': return lt; case '>': return gt;
        default: return sp;
    }
}

static void draw_char(int x, int y, char ch, int scale, uint32_t c) {
    const uint8_t *g;
    int row;
    int col;
    int dx;
    int dy;
    if (g_psf_glyphs && g_psf_glyph_h > 0u) {
        g = g_psf_glyphs + ((uint8_t)ch) * g_psf_glyph_h;
        for (row = 0; row < (int)g_psf_glyph_h; row++) {
            uint8_t bits = g[row];
            for (col = 0; col < 8; col++) {
                if ((bits & (0x80u >> col)) == 0u) continue;
                for (dy = 0; dy < scale; dy++) {
                    for (dx = 0; dx < scale; dx++) put_px(x + col * scale + dx, y + row * scale + dy, c);
                }
            }
        }
    } else {
        g = glyph5x7(ch);
        for (row = 0; row < 7; row++) {
            for (col = 0; col < 5; col++) {
                if ((g[row] & (1u << (4 - col))) == 0) continue;
                for (dy = 0; dy < scale; dy++) {
                    for (dx = 0; dx < scale; dx++) put_px(x + col * scale + dx, y + row * scale + dy, c);
                }
            }
        }
    }
    g_dirty = 1;
}

static void draw_text(int x, int y, const char *s, int scale, uint32_t c) {
    int i = 0;
    int adv = (g_psf_glyphs && g_psf_glyph_h > 0u) ? (8 * scale) : (6 * scale);
    while (s && s[i]) {
        draw_char(x + i * adv, y, s[i], scale, c);
        i++;
    }
}

static int load_psf_font(void) {
    const char *paths[] = {
        "/system/fonts/default8x16.psf"
    };
    uint32_t i;

    g_psf_glyphs = NULL;
    g_psf_glyph_h = 16u;
    for (i = 0; i < (uint32_t)(sizeof(paths) / sizeof(paths[0])); i++) {
        int fd = open(paths[i], 0);
        int32_t n;
        if (fd < 0) continue;
        n = read(fd, g_font_blob, sizeof(g_font_blob));
        close(fd);
        if (n < 4) continue;
        if (g_font_blob[0] != 0x36 || g_font_blob[1] != 0x04) continue;
        if (g_font_blob[3] == 0u) continue;
        g_psf_glyph_h = (uint32_t)g_font_blob[3];
        g_psf_glyphs = g_font_blob + 4;
        return 0;
    }
    return -1;
}

static void detect_tty(void) {
    dev_tty_info_t ti;
    int fd = open("/dev/tty/2", 0);
    if (fd >= 0) {
        if (ioctl(fd, DEV_IOCTL_TTY_GET_INFO, &ti) == 0) {
            g_tty_index = ti.index;
            close(fd);
            return;
        }
        close(fd);
    }
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) == 0) {
        g_tty_index = ti.index;
        return;
    }
    g_tty_index = 2;
}

static int is_active(void) {
    uint32_t idx = 0xFFFFFFFFu;
    int fd;

    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_ACTIVE, &idx) == 0) {
        int match = (idx == g_tty_index) ? 1 : 0;
        if (idx != g_active_tty) {
            g_active_tty = idx;
            if (match) g_dirty = 1;
        }
        return match;
    }

    fd = open("/dev/tty/1", 0);
    if (fd >= 0) {
        idx = 0xFFFFFFFFu;
        if (ioctl(fd, DEV_IOCTL_TTY_GET_ACTIVE, &idx) == 0) {
            int match = (idx == g_tty_index) ? 1 : 0;
            if (idx != g_active_tty) {
                g_active_tty = idx;
                if (match) g_dirty = 1;
            }
            close(fd);
            return match;
        }
        close(fd);
    }

    return 0;
}

static void process_cmd(char *line) {
    char *p = line;
    char *cmd = next_tok(&p);
    if (!cmd) return;
    if (strcmp(cmd, "CLR") == 0) {
        char *hc = next_tok(&p);
        uint32_t c = 0;
        if (hc && parse_hex24(hc, &c) == 0) fill_rect(0, 0, (int)g_info.width, (int)g_info.height, c);
        return;
    }
    if (strcmp(cmd, "RECT") == 0) {
        char *sx = next_tok(&p);
        char *sy = next_tok(&p);
        char *sw = next_tok(&p);
        char *sh = next_tok(&p);
        char *hc = next_tok(&p);
        uint32_t x, y, w, h, c;
        if (!sx || !sy || !sw || !sh || !hc) return;
        if (parse_u32(sx, &x) != 0 || parse_u32(sy, &y) != 0 || parse_u32(sw, &w) != 0 || parse_u32(sh, &h) != 0) return;
        if (parse_hex24(hc, &c) != 0) return;
        fill_rect((int)x, (int)y, (int)w, (int)h, c);
        return;
    }
    if (strcmp(cmd, "FRAME") == 0) {
        char *sx = next_tok(&p);
        char *sy = next_tok(&p);
        char *sw = next_tok(&p);
        char *sh = next_tok(&p);
        char *hc = next_tok(&p);
        uint32_t x, y, w, h, c;
        if (!sx || !sy || !sw || !sh || !hc) return;
        if (parse_u32(sx, &x) != 0 || parse_u32(sy, &y) != 0 || parse_u32(sw, &w) != 0 || parse_u32(sh, &h) != 0) return;
        if (parse_hex24(hc, &c) != 0) return;
        stroke_rect((int)x, (int)y, (int)w, (int)h, c);
        return;
    }
    if (strcmp(cmd, "TEXT") == 0) {
        char *sx = next_tok(&p);
        char *sy = next_tok(&p);
        char *ss = next_tok(&p);
        char *hc = next_tok(&p);
        uint32_t x, y, sc, c;
        while (*p && is_space(*p)) p++;
        if (!sx || !sy || !ss || !hc || !*p) return;
        if (parse_u32(sx, &x) != 0 || parse_u32(sy, &y) != 0 || parse_u32(ss, &sc) != 0) return;
        if (parse_hex24(hc, &c) != 0) return;
        if (sc == 0 || sc > 4) sc = 1;
        draw_text((int)x, (int)y, p, (int)sc, c);
        return;
    }
    if (strcmp(cmd, "PRESENT") == 0) {
        g_dirty = 1;
        return;
    }
}

static int bind_socket(void) {
    struct sockaddr_in a;
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return -1;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT_GFXD);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (const void*)&a, sizeof(a)) != 0) {
        close(s);
        return -1;
    }
    return s;
}

int main(int argc, char **argv) {
    char msg[512];
    struct sockaddr_in src;
    uint32_t slen = sizeof(src);
    uint32_t frame_bytes;
    (void)argc;
    (void)argv;

    g_fd_fb = open("/dev/vesa", 0);
    if (g_fd_fb < 0) return exec("/bin/sh");
    if (ioctl(g_fd_fb, DEV_IOCTL_VESA_GET_INFO, &g_info) != 0) return exec("/bin/sh");
    frame_bytes = g_info.pitch * g_info.height;
    if (frame_bytes == 0 || frame_bytes > sizeof(g_fb)) return exec("/bin/sh");
    if (g_info.bpp != 24 && g_info.bpp != 32) return exec("/bin/sh");
    g_sock = bind_socket();
    if (g_sock < 0) return exec("/bin/sh");
    detect_tty();
    (void)load_psf_font();
    fill_rect(0, 0, (int)g_info.width, (int)g_info.height, 0x101A22);
    draw_text(24, 24, "GFXD READY", 2, 0xE0F0FF);

    hq_widget_init(&g_ui_root, 1u);
    hq_widget_set_geometry(&g_ui_root, (ui_rect_t){0, 0, (int)g_info.width, (int)g_info.height});
    hq_app_init(&g_ui_app, &g_ui_root);

    while (g_ui_app.running) {
        (void)hq_app_process_once(&g_ui_app);
        while (1) {
            slen = sizeof(src);
            int32_t n = recvfrom(g_sock, msg, sizeof(msg) - 1, MSG_DONTWAIT, (void*)&src, &slen);
            if (n <= 0) break;
            msg[n] = '\0';
            process_cmd(msg);
        }
        if (is_active() && g_dirty) {
            (void)write(g_fd_fb, g_fb, frame_bytes);
            g_dirty = 0;
        }
        sleep(10);
    }
    return 0;
}
