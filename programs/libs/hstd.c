#include <hstd_api.h>

static inline uint32_t sc0(uint32_t n) {
    uint32_t r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n) : "memory");
    return r;
}

static inline uint32_t sc1(uint32_t n, uint32_t a1) {
    uint32_t r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a1) : "memory");
    return r;
}

static inline uint32_t sc2(uint32_t n, uint32_t a1, uint32_t a2) {
    uint32_t r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return r;
}

static inline uint32_t sc3(uint32_t n, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return r;
}

enum {
    SYS_READ = 4,
    SYS_WRITE = 5,
    SYS_OPEN = 8,
    SYS_CLOSE = 9,
};

static void *api_memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    size_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *api_memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

static size_t api_strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static int api_strcmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void api_utoa(unsigned int value, char *buf, unsigned int base) {
    char tmp[32];
    unsigned int i = 0;
    unsigned int j;
    if (!buf || base < 2 || base > 16) {
        if (buf) buf[0] = '\0';
        return;
    }
    if (value == 0u) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (value > 0u) {
        unsigned int d = value % base;
        tmp[i++] = (d < 10u) ? (char)('0' + d) : (char)('a' + (d - 10u));
        value /= base;
    }
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1u - j];
    buf[i] = '\0';
}

static int32_t api_open(const char *path, uint32_t flags) {
    return (int32_t)sc2(SYS_OPEN, (uint32_t)path, flags);
}

static int32_t api_close(int32_t fd) {
    return (int32_t)sc1(SYS_CLOSE, (uint32_t)fd);
}

static int32_t api_read(int32_t fd, void *buf, uint32_t size) {
    return (int32_t)sc3(SYS_READ, (uint32_t)fd, (uint32_t)buf, size);
}

static int32_t api_write(int32_t fd, const void *buf, uint32_t size) {
    return (int32_t)sc3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, size);
}

int hstd_get_api(uint32_t ver, hstd_api_t *out, uint32_t out_size) {
    if (ver != HSTD_API_VERSION || !out || out_size < sizeof(hstd_api_t)) return -1;
    out->memcpy_fn = api_memcpy;
    out->memset_fn = api_memset;
    out->strlen_fn = api_strlen;
    out->strcmp_fn = api_strcmp;
    out->utoa_fn = api_utoa;
    out->open_fn = api_open;
    out->close_fn = api_close;
    out->read_fn = api_read;
    out->write_fn = api_write;
    return 0;
}
