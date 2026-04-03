#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    void *(*memcpy_fn)(void *dst, const void *src, size_t n);
    void *(*memset_fn)(void *dst, int c, size_t n);
    size_t (*strlen_fn)(const char *s);
    int (*strcmp_fn)(const char *a, const char *b);
    void (*utoa_fn)(unsigned int value, char *buf, unsigned int base);
    int32_t (*open_fn)(const char *path, uint32_t flags);
    int32_t (*close_fn)(int32_t fd);
    int32_t (*read_fn)(int32_t fd, void *buf, uint32_t size);
    int32_t (*write_fn)(int32_t fd, const void *buf, uint32_t size);
} hstd_api_t;

#define HSTD_API_VERSION 1u
int hstd_get_api(uint32_t ver, hstd_api_t *out, uint32_t out_size);

