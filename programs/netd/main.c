#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct {
    char hostname[64];
    int dhcp;
    uint32_t static_ip;
    uint32_t static_mask;
    uint32_t static_gw;
    uint32_t dns1;
    uint32_t dns2;
    uint32_t lease_default;
    uint16_t dhcp_server_port;
    uint16_t dhcp_client_port;
    uint32_t renew_before;
} netd_conf_t;

typedef struct {
    uint32_t ip;
    uint32_t mask;
    uint32_t gw;
    uint32_t dns1;
    uint32_t dns2;
    uint32_t lease_seconds;
    int via_dhcp;
} netd_state_t;

static char g_buf[512];

static int buf_append(char *dst, uint32_t cap, const char *s) {
    uint32_t a;
    uint32_t b;
    if (!dst || !s || cap == 0) return -1;
    a = (uint32_t)strlen(dst);
    b = (uint32_t)strlen(s);
    if (a + b + 1 > cap) return -1;
    memcpy(dst + a, s, b + 1);
    return 0;
}

static void u32_to_dec(uint32_t v, char *out, uint32_t cap) {
    char t[16];
    uint32_t i = 0;
    uint32_t j = 0;
    if (!out || cap < 2) return;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v && i < sizeof(t)) {
        t[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (j < i && j + 1 < cap) {
        out[j] = t[i - 1u - j];
        j++;
    }
    out[j] = '\0';
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        e--;
        *e = '\0';
    }
    return s;
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

static int parse_ipv4(const char *s, uint32_t *out) {
    uint32_t parts[4];
    uint32_t i = 0;
    const char *p = s;
    if (!s || !out) return -1;
    while (i < 4) {
        uint32_t v = 0;
        uint32_t digs = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10u + (uint32_t)(*p - '0');
            if (v > 255u) return -1;
            p++;
            digs++;
        }
        if (digs == 0) return -1;
        parts[i++] = v;
        if (i == 4) break;
        if (*p != '.') return -1;
        p++;
    }
    if (*p != '\0') return -1;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

static void ipv4_to_str(uint32_t ip, char *out, uint32_t cap) {
    char p[4][4];
    if (!out || cap < 16) return;
    out[0] = '\0';
    u32_to_dec((ip >> 24) & 0xFFu, p[0], sizeof(p[0]));
    u32_to_dec((ip >> 16) & 0xFFu, p[1], sizeof(p[1]));
    u32_to_dec((ip >> 8) & 0xFFu, p[2], sizeof(p[2]));
    u32_to_dec(ip & 0xFFu, p[3], sizeof(p[3]));
    (void)buf_append(out, cap, p[0]);
    (void)buf_append(out, cap, ".");
    (void)buf_append(out, cap, p[1]);
    (void)buf_append(out, cap, ".");
    (void)buf_append(out, cap, p[2]);
    (void)buf_append(out, cap, ".");
    (void)buf_append(out, cap, p[3]);
}

static int read_text(const char *path, char *buf, uint32_t cap) {
    int fd;
    int32_t n;
    if (!path || !buf || cap < 2) return -1;
    fd = open(path, 0);
    if (fd < 0) return -1;
    n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) return -1;
    buf[(uint32_t)n] = '\0';
    return 0;
}

static int write_text(const char *path, const char *text) {
    int fd;
    uint32_t len;
    if (!path || !text) return -1;
    (void)unlink(path);
    fd = open(path, 1);
    if (fd < 0) return -1;
    len = (uint32_t)strlen(text);
    if (write(fd, text, len) != (int32_t)len) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static void conf_defaults(netd_conf_t *c) {
    memset(c, 0, sizeof(*c));
    strcpy(c->hostname, "houseos");
    c->dhcp = 1;
    c->static_ip = (10u << 24) | (0u << 16) | (2u << 8) | 15u;
    c->static_mask = (255u << 24) | (255u << 16) | (255u << 8) | 0u;
    c->static_gw = (10u << 24) | (0u << 16) | (2u << 8) | 2u;
    c->lease_default = 300;
    c->dhcp_server_port = 67;
    c->dhcp_client_port = 68;
    c->renew_before = 30;
    c->dns1 = (1u << 24) | (1u << 16) | (1u << 8) | 1u;
}

static void parse_conf_line(netd_conf_t *c, char *line) {
    char *eq;
    char *k;
    char *v;
    uint32_t u;
    eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    k = trim(line);
    v = trim(eq + 1);
    if (strcmp(k, "hostname") == 0) {
        strncpy(c->hostname, v, sizeof(c->hostname) - 1);
        c->hostname[sizeof(c->hostname) - 1] = '\0';
    } else if (strcmp(k, "dhcp") == 0) {
        c->dhcp = (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "yes") == 0) ? 1 : 0;
    } else if (strcmp(k, "static_ip") == 0) {
        (void)parse_ipv4(v, &c->static_ip);
    } else if (strcmp(k, "static_mask") == 0) {
        (void)parse_ipv4(v, &c->static_mask);
    } else if (strcmp(k, "static_gw") == 0) {
        (void)parse_ipv4(v, &c->static_gw);
    } else if (strcmp(k, "dns1") == 0) {
        (void)parse_ipv4(v, &c->dns1);
    } else if (strcmp(k, "dns2") == 0) {
        (void)parse_ipv4(v, &c->dns2);
    } else if (strcmp(k, "lease_seconds") == 0) {
        if (parse_u32(v, &u) == 0) c->lease_default = u;
    } else if (strcmp(k, "dhcp_server_port") == 0) {
        if (parse_u32(v, &u) == 0 && u <= 65535u) c->dhcp_server_port = (uint16_t)u;
    } else if (strcmp(k, "dhcp_client_port") == 0) {
        if (parse_u32(v, &u) == 0 && u <= 65535u) c->dhcp_client_port = (uint16_t)u;
    } else if (strcmp(k, "renew_before") == 0) {
        if (parse_u32(v, &u) == 0) c->renew_before = u;
    }
}

static void load_conf(netd_conf_t *c) {
    char *line;
    char *nl;
    conf_defaults(c);
    if (read_text("/etc/netd.conf", g_buf, sizeof(g_buf)) != 0) return;
    line = g_buf;
    while (*line) {
        char *cur = line;
        char *hash;
        nl = strchr(cur, '\n');
        if (nl) *nl = '\0';
        hash = strchr(cur, '#');
        if (hash) *hash = '\0';
        parse_conf_line(c, trim(cur));
        if (!nl) break;
        line = nl + 1;
    }
}

static int parse_offer(const char *msg, netd_state_t *st) {
    char tmp[256];
    char *p;
    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, msg, sizeof(tmp) - 1);
    if (strncmp(tmp, "OFFER ", 6) != 0 && strncmp(tmp, "ACK ", 4) != 0) return -1;
    p = tmp;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p) {
        char *tok = p;
        char *eq = strchr(tok, '=');
        while (*p && *p != ' ') p++;
        if (*p == ' ') {
            *p = '\0';
            p++;
            while (*p == ' ') p++;
        }
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(tok, "ip") == 0) (void)parse_ipv4(eq + 1, &st->ip);
        else if (strcmp(tok, "mask") == 0) (void)parse_ipv4(eq + 1, &st->mask);
        else if (strcmp(tok, "gw") == 0) (void)parse_ipv4(eq + 1, &st->gw);
        else if (strcmp(tok, "dns1") == 0) (void)parse_ipv4(eq + 1, &st->dns1);
        else if (strcmp(tok, "dns2") == 0) (void)parse_ipv4(eq + 1, &st->dns2);
        else if (strcmp(tok, "lease") == 0) (void)parse_u32(eq + 1, &st->lease_seconds);
    }
    return (st->ip != 0) ? 0 : -1;
}

static int dhcp_request(const netd_conf_t *c, netd_state_t *st) {
    int s;
    struct sockaddr_in cli;
    struct sockaddr_in srv;
    struct sockaddr_in src;
    uint32_t src_len = sizeof(src);
    uint32_t waited = 0;
    int32_t n;
    char msg[256];

    memset(st, 0, sizeof(*st));
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return -1;

    memset(&cli, 0, sizeof(cli));
    cli.sin_family = AF_INET;
    cli.sin_port = htons(c->dhcp_client_port);
    cli.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (const void*)&cli, sizeof(cli)) != 0) {
        close(s);
        return -1;
    }

    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(c->dhcp_server_port);
    srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    msg[0] = '\0';
    (void)buf_append(msg, sizeof(msg), "DISCOVER hostname=");
    (void)buf_append(msg, sizeof(msg), c->hostname);
    if (sendto(s, msg, (uint32_t)strlen(msg), 0, (const void*)&srv, sizeof(srv)) < 0) {
        close(s);
        return -1;
    }

    while (waited < 3000u) {
        n = recvfrom(s, msg, sizeof(msg) - 1, MSG_DONTWAIT, (void*)&src, &src_len);
        if (n > 0) {
            msg[n] = '\0';
            if (parse_offer(msg, st) == 0) {
                st->via_dhcp = 1;
                if (st->lease_seconds == 0) st->lease_seconds = c->lease_default;
                close(s);
                return 0;
            }
        }
        sleep(100);
        waited += 100;
    }

    close(s);
    return -1;
}

static void apply_static_fallback(const netd_conf_t *c, netd_state_t *st) {
    memset(st, 0, sizeof(*st));
    st->ip = c->static_ip;
    st->mask = c->static_mask;
    st->gw = c->static_gw;
    st->dns1 = c->dns1;
    st->dns2 = c->dns2;
    st->lease_seconds = c->lease_default;
    st->via_dhcp = 0;
}

static void write_resolv(const netd_state_t *st) {
    FILE *f;
    char ip1[16];
    char ip2[16];
    mkdir("/etc");
    f = fopen("/etc/resolv.conf", "w");
    if (!f) return;
    if (st->dns1 != 0) {
        ipv4_to_str(st->dns1, ip1, sizeof(ip1));
        fprintf(f, "nameserver %s\n", ip1);
    }
    if (st->dns2 != 0) {
        ipv4_to_str(st->dns2, ip2, sizeof(ip2));
        fprintf(f, "nameserver %s\n", ip2);
    }
    if (st->dns1 == 0 && st->dns2 == 0) {
        fprintf(f, "# no DNS configured\n");
    }
    fclose(f);
}

static void write_state(const netd_state_t *st) {
    FILE *f;
    char ip[16];
    char mask[16];
    char gw[16];
    char dns1[16];
    char dns2[16];

    ipv4_to_str(st->ip, ip, sizeof(ip));
    ipv4_to_str(st->mask, mask, sizeof(mask));
    ipv4_to_str(st->gw, gw, sizeof(gw));
    ipv4_to_str(st->dns1, dns1, sizeof(dns1));
    ipv4_to_str(st->dns2, dns2, sizeof(dns2));
    mkdir("/run");
    f = fopen("/run/netd.state", "w");
    if (!f) return;
    fprintf(f, "mode=%s\n", st->via_dhcp ? "dhcp" : "static");
    fprintf(f, "ip=%s\n", ip);
    fprintf(f, "mask=%s\n", mask);
    fprintf(f, "gateway=%s\n", gw);
    fprintf(f, "dns1=%s\n", dns1);
    fprintf(f, "dns2=%s\n", dns2);
    fprintf(f, "lease_seconds=%u\n", (unsigned)st->lease_seconds);
    fclose(f);
}

static void log_line(const char *s) {
    write(1, s, (uint32_t)strlen(s));
}

static void do_one_cycle(const netd_conf_t *c, netd_state_t *st) {
    if (c->dhcp && dhcp_request(c, st) == 0) {
        log_line("netd: dhcp lease acquired\n");
    } else {
        apply_static_fallback(c, st);
        if (st->ip) log_line("netd: using static fallback\n");
        else log_line("netd: no lease and no static_ip configured\n");
    }
    write_resolv(st);
    write_state(st);
}

static int cmd_status(void) {
    if (read_text("/run/netd.state", g_buf, sizeof(g_buf)) != 0) {
        fprintf(stderr, "netd: no state\n");
        return 1;
    }
    fprintf(stdout, "%s", g_buf);
    return 0;
}

static int run_daemon(void) {
    netd_conf_t conf;
    netd_state_t st;
    uint32_t wait_ms;
    load_conf(&conf);
    log_line("netd: start\n");
    while (1) {
        do_one_cycle(&conf, &st);
        if (st.lease_seconds > conf.renew_before) {
            wait_ms = (st.lease_seconds - conf.renew_before) * 1000u;
        } else {
            wait_ms = 10000u;
        }
        sleep(wait_ms);
        load_conf(&conf);
    }
    return 0;
}

int main(int argc, char **argv) {
    netd_conf_t conf;
    netd_state_t st;

    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        return cmd_status();
    }
    if (argc >= 2 && strcmp(argv[1], "oneshot") == 0) {
        load_conf(&conf);
        do_one_cycle(&conf, &st);
        return 0;
    }
    return run_daemon();
}
