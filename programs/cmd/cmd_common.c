#include "cmd_common.h"

#include <syscall.h>
#include <stdio.h>
#include <string.h>

const mode_desc_t g_vga_mode_desc[] = {
    {0x00, 80, 25, 4}, {0x02, 80, 25, 4}, {0x03, 80, 25, 4},
};
const uint32_t g_vga_mode_desc_count = sizeof(g_vga_mode_desc) / sizeof(g_vga_mode_desc[0]);

int is_supported_vga_mode(uint32_t mode) {
    return (mode == 0 || mode == 2 || mode == 3) ? 1 : 0;
}

uint32_t gcd_u32(uint32_t a, uint32_t b) {
    while (b != 0) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

int parse_u32_dec(const char *s, uint32_t *out) {
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

int parse_u32(const char *s, uint32_t *out) {
    uint32_t v = 0;
    uint32_t i = 0;
    uint32_t base = 10;
    if (!s || !out || s[0] == '\0') return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
        if (!s[i]) return -1;
    }
    while (s[i]) {
        char c = s[i++];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return -1;
        if (d >= base) return -1;
        v = v * base + d;
    }
    *out = v;
    return 0;
}

int parse_i32(const char *s, int32_t *out) {
    uint32_t v;
    if (!s || !out || s[0] == '\0') return -1;
    if (s[0] == '-') {
        if (parse_u32_dec(s + 1, &v) != 0) return -1;
        *out = -(int32_t)v;
        return 0;
    }
    if (parse_u32_dec(s, &v) != 0) return -1;
    *out = (int32_t)v;
    return 0;
}

int normalize_path(const char *cwd, const char *in, char *out, uint32_t cap) {
    char tmp[256];
    uint32_t i = 0;
    uint32_t o = 0;
    uint32_t seg_starts[64];
    uint32_t seg_count = 0;

    if (!cwd || !in || !out || cap < 2) return -1;

    if (in[0] == '/') {
        strncpy(tmp, in, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
    } else if (strcmp(cwd, "/") == 0) {
        tmp[0] = '/';
        strncpy(tmp + 1, in, sizeof(tmp) - 2);
        tmp[sizeof(tmp) - 1] = '\0';
    } else {
        strncpy(tmp, cwd, sizeof(tmp) - 1);
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

        {
            uint32_t start = i;
            uint32_t len;
            while (tmp[i] && tmp[i] != '/') i++;
            len = i - start;

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
    }

    if (o == 0) out[o++] = '/';
    out[o] = '\0';
    return 0;
}

static const char *aspect_label(uint32_t arw, uint32_t arh) {
    if (arw == 4 && arh == 3) return "4:3";
    if (arw == 5 && arh == 4) return "5:4";
    if (arw == 16 && arh == 9) return "16:9";
    if (arw == 16 && arh == 10) return "16:10";
    if (arw == 8 && arh == 5) return "8:5";
    if (arw == 3 && arh == 2) return "3:2";
    if (arw == 1 && arh == 1) return "1:1";
    if (arw == 21 && arh == 9) return "21:9";
    if (arw == 64 && arh == 27) return "64:27";
    return NULL;
}

void print_mode_line(uint16_t id, uint16_t w, uint16_t h, uint8_t bpp) {
    uint32_t g = gcd_u32((uint32_t)w, (uint32_t)h);
    uint32_t arw = (uint32_t)w / g;
    uint32_t arh = (uint32_t)h / g;
    const char *ratio = aspect_label(arw, arh);
    if (ratio) fprintf(stdout, "%ux%ux%u %s - 0x%x (%u)\n", w, h, bpp, ratio, (uint32_t)id, (uint32_t)id);
    else fprintf(stdout, "%ux%ux%u - 0x%x (%u)\n", w, h, bpp, (uint32_t)id, (uint32_t)id);
}

static int parse_ratio(const char *s, uint32_t *arw, uint32_t *arh) {
    const char *sep;
    uint32_t w;
    uint32_t h;
    uint32_t g;
    if (!s || !arw || !arh) return -1;
    sep = strchr(s, ':');
    if (!sep || sep == s || sep[1] == '\0') return -1;
    {
        char left[16];
        char right[16];
        uint32_t lsz = (uint32_t)(sep - s);
        uint32_t rsz = (uint32_t)strlen(sep + 1);
        if (lsz >= sizeof(left) || rsz >= sizeof(right)) return -1;
        memcpy(left, s, lsz);
        left[lsz] = '\0';
        memcpy(right, sep + 1, rsz + 1);
        if (parse_u32_dec(left, &w) != 0 || parse_u32_dec(right, &h) != 0) return -1;
    }
    if (w == 0 || h == 0) return -1;
    g = gcd_u32(w, h);
    *arw = w / g;
    *arh = h / g;
    return 0;
}

int parse_modes_filters(int argc, char **argv, int start, mode_filter_t *f) {
    memset(f, 0, sizeof(*f));
    for (int i = start; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "w=", 2) == 0) {
            if (parse_u32_dec(a + 2, &f->w) != 0) return -1;
            f->has_w = 1;
        } else if (strncmp(a, "h=", 2) == 0) {
            if (parse_u32_dec(a + 2, &f->h) != 0) return -1;
            f->has_h = 1;
        } else if (strncmp(a, "b=", 2) == 0) {
            if (parse_u32_dec(a + 2, &f->b) != 0) return -1;
            f->has_b = 1;
        } else if (strncmp(a, "r=", 2) == 0) {
            if (parse_ratio(a + 2, &f->arw, &f->arh) != 0) return -1;
            f->has_r = 1;
        } else {
            return -1;
        }
    }
    return 0;
}

int mode_matches(const mode_filter_t *f, uint16_t w, uint16_t h, uint8_t bpp) {
    uint32_t g;
    uint32_t arw;
    uint32_t arh;
    if (f->has_w && f->has_h) {
        int direct = (f->w == (uint32_t)w && f->h == (uint32_t)h);
        int swapped = (f->w == (uint32_t)h && f->h == (uint32_t)w);
        if (!direct && !swapped) return 0;
    } else if (f->has_w) {
        if (f->w != (uint32_t)w && f->w != (uint32_t)h) return 0;
    } else if (f->has_h) {
        if (f->h != (uint32_t)h && f->h != (uint32_t)w) return 0;
    }
    if (f->has_b && f->b != (uint32_t)bpp) return 0;
    if (!f->has_r) return 1;
    if (w == 0 || h == 0) return 0;
    g = gcd_u32((uint32_t)w, (uint32_t)h);
    arw = (uint32_t)w / g;
    arh = (uint32_t)h / g;
    return (arw == f->arw && arh == f->arh) ? 1 : 0;
}

static int str_contains(const char *hay, const char *needle) {
    uint32_t hlen = (uint32_t)strlen(hay);
    uint32_t nlen = (uint32_t)strlen(needle);
    if (nlen == 0) return 1;
    if (hlen < nlen) return 0;
    for (uint32_t i = 0; i + nlen <= hlen; i++) {
        if (strncmp(hay + i, needle, nlen) == 0) return 1;
    }
    return 0;
}

int grep_stream(int fd, const char *needle) {
    char in[CMD_BUF_SZ];
    char line[CMD_BUF_SZ * 2];
    uint32_t line_len = 0;
    uint32_t needle_len = (uint32_t)strlen(needle);
    for (;;) {
        int32_t n = read(fd, in, sizeof(in));
        if (n <= 0) break;
        for (int32_t i = 0; i < n; i++) {
            char c = in[i];
            if (line_len + 1 < sizeof(line)) line[line_len++] = c;
            if (c == '\n') {
                line[line_len] = '\0';
                if (needle_len == 0 || str_contains(line, needle)) write(fileno(stdout), line, line_len);
                line_len = 0;
            }
        }
        if (n < (int32_t)sizeof(in)) break;
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        if (needle_len == 0 || str_contains(line, needle)) write(fileno(stdout), line, line_len);
    }
    return 0;
}

int less_stream(int fd) {
    char in[CMD_BUF_SZ];
    uint32_t rows = 24;
    uint32_t lines_left;
    dev_tty_info_t ti;
    int interactive = (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) == 0 && ti.rows > 1);
    if (interactive) rows = ti.rows;
    lines_left = rows - 1;

    for (;;) {
        int32_t n = read(fd, in, sizeof(in));
        if (n <= 0) break;
        for (int32_t i = 0; i < n; i++) {
            char c = in[i];
            write(fileno(stdout), &c, 1);
            if (c != '\n') continue;
            if (!interactive) continue;
            if (lines_left > 0) lines_left--;
            if (lines_left > 0) continue;
            {
                char ans[8];
                int32_t rn;
                const char *prompt = "--More-- (Enter/q)\n";
                write(fileno(stdout), prompt, (uint32_t)strlen(prompt));
                rn = read(fileno(stdin), ans, sizeof(ans));
                if (rn > 0 && (ans[0] == 'q' || ans[0] == 'Q')) return 0;
                lines_left = rows - 1;
            }
        }
        if (n < (int32_t)sizeof(in)) break;
    }
    return 0;
}

static void print_u32_base(uint32_t v, uint32_t base, int upper) {
    char tmp[32];
    uint32_t n = 0;
    if (v == 0) {
        char z = '0';
        write(fileno(stdout), &z, 1);
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        uint32_t d = v % base;
        if (d < 10) tmp[n++] = (char)('0' + d);
        else tmp[n++] = (char)((upper ? 'A' : 'a') + (d - 10));
        v /= base;
    }
    while (n > 0) {
        n--;
        write(fileno(stdout), &tmp[n], 1);
    }
}

int cmd_printf_impl(int argc, char **argv, int arg0) {
    int argi = arg0 + 2;
    const char *fmt;
    if (arg0 + 1 >= argc) {
        fprintf(stderr, "usage: printf <format> [args...]\n");
        return 1;
    }
    fmt = argv[arg0 + 1];
    for (uint32_t i = 0; fmt[i] != '\0'; i++) {
        char c = fmt[i];
        if (c == '\\') {
            char e = fmt[++i];
            if (e == '\0') break;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == '\\') c = '\\';
            else c = e;
            write(fileno(stdout), &c, 1);
            continue;
        }
        if (c != '%') {
            write(fileno(stdout), &c, 1);
            continue;
        }
        c = fmt[++i];
        if (c == '\0') break;
        if (c == '%') {
            write(fileno(stdout), "%", 1);
            continue;
        }
        if (argi >= argc) return 1;
        if (c == 's') {
            write(fileno(stdout), argv[argi], (uint32_t)strlen(argv[argi]));
        } else if (c == 'c') {
            write(fileno(stdout), argv[argi], 1);
        } else if (c == 'd') {
            int32_t v;
            if (parse_i32(argv[argi], &v) != 0) return 1;
            if (v < 0) {
                uint32_t uv = 0u - (uint32_t)v;
                write(fileno(stdout), "-", 1);
                print_u32_base(uv, 10, 0);
            } else {
                print_u32_base((uint32_t)v, 10, 0);
            }
        } else if (c == 'u') {
            uint32_t v;
            if (parse_u32(argv[argi], &v) != 0) return 1;
            print_u32_base(v, 10, 0);
        } else if (c == 'x') {
            uint32_t v;
            if (parse_u32(argv[argi], &v) != 0) return 1;
            print_u32_base(v, 16, 0);
        } else {
            write(fileno(stdout), "%", 1);
            write(fileno(stdout), &c, 1);
            i--;
        }
        argi++;
    }
    return 0;
}

static char hex_nibble(uint8_t v) {
    v &= 0x0F;
    return (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10));
}

static void write_hex_u32(uint32_t v) {
    char s[8];
    for (int i = 0; i < 8; i++) {
        uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0x0F);
        s[i] = hex_nibble(n);
    }
    write(fileno(stdout), s, 8);
}

static void write_hex_byte(uint8_t b) {
    char s[2];
    s[0] = hex_nibble((uint8_t)(b >> 4));
    s[1] = hex_nibble(b);
    write(fileno(stdout), s, 2);
}

int cmd_hexdump_impl(int argc, char **argv, int arg0, const char *cwd) {
    uint8_t buf[16];
    uint32_t off = 0;
    int fd = fileno(stdin);
    if (arg0 + 1 < argc) {
        char path[256];
        if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) return 1;
        fd = open(path, 0);
        if (fd < 0) {
            fprintf(stderr, "hexdump: open failed\n");
            return 1;
        }
    }
    for (;;) {
        int32_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        write_hex_u32(off);
        write(fileno(stdout), "  ", 2);
        for (int32_t i = 0; i < 16; i++) {
            if (i < n) write_hex_byte(buf[i]);
            else write(fileno(stdout), "  ", 2);
            if (i != 15) write(fileno(stdout), " ", 1);
        }
        write(fileno(stdout), "  |", 3);
        for (int32_t i = 0; i < n; i++) {
            char c = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
            write(fileno(stdout), &c, 1);
        }
        write(fileno(stdout), "|\n", 2);

        off += (uint32_t)n;
        if (n < (int32_t)sizeof(buf)) break;
    }
    if (fd != fileno(stdin)) close(fd);
    return 0;
}
