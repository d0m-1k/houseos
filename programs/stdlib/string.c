#include <string.h>

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src) {
    char *o = dst;
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return o;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char*)s;
        s++;
    }
    return (ch == '\0') ? (char*)s : 0;
}
