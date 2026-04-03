#pragma once

#include <stdint.h>
#include <devctl.h>

#define CMD_BUF_SZ 512

typedef struct {
    uint16_t id;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} mode_desc_t;

typedef struct {
    uint8_t has_w;
    uint8_t has_h;
    uint8_t has_b;
    uint8_t has_r;
    uint32_t w;
    uint32_t h;
    uint32_t b;
    uint32_t arw;
    uint32_t arh;
} mode_filter_t;

extern const mode_desc_t g_vga_mode_desc[];
extern const uint32_t g_vga_mode_desc_count;

int is_supported_vga_mode(uint32_t mode);
uint32_t gcd_u32(uint32_t a, uint32_t b);
int parse_u32_dec(const char *s, uint32_t *out);
int parse_u32(const char *s, uint32_t *out);
int parse_i32(const char *s, int32_t *out);
int normalize_path(const char *cwd, const char *in, char *out, uint32_t cap);
int parse_modes_filters(int argc, char **argv, int start, mode_filter_t *f);
int mode_matches(const mode_filter_t *f, uint16_t w, uint16_t h, uint8_t bpp);
void print_mode_line(uint16_t id, uint16_t w, uint16_t h, uint8_t bpp);
int grep_stream(int fd, const char *needle);
int less_stream(int fd);
int cmd_printf_impl(int argc, char **argv, int arg0);
int cmd_hexdump_impl(int argc, char **argv, int arg0, const char *cwd);
