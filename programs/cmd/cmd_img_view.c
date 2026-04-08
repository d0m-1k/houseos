#include "commands.h"
#include "cmd_common.h"

#include <devctl.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define IMG_MAX_W 320
#define IMG_MAX_H 200
#define IMG_MAX_PITCH 4096
#define IMG_PIXELS (IMG_MAX_W * IMG_MAX_H)
#define IMG_MAX_RAW (IMG_PIXELS * 3)
#define IMG_FILE_MAX (IMG_MAX_RAW + 4096)
#define IMG_MAX_FB_H 768
#define IMG_FB_MAX (IMG_MAX_FB_H * IMG_MAX_PITCH)

static uint8_t g_file[IMG_FILE_MAX];
static uint8_t g_fb[IMG_FB_MAX];

static int open_fb_device(const char **used_path) {
    int fd = open("/dev/vesa", 0);
    if (fd >= 0) {
        if (used_path) *used_path = "/dev/vesa";
        return fd;
    }
    fd = open("/dev/framebuffer/buffer", 0);
    if (fd >= 0) {
        if (used_path) *used_path = "/dev/framebuffer/buffer";
        return fd;
    }
    if (used_path) *used_path = "(none)";
    return -1;
}

static int is_space_ch(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

static int skip_ws_and_comments(const uint8_t *buf, uint32_t size, uint32_t *off) {
    while (*off < size) {
        if (is_space_ch((char)buf[*off])) {
            (*off)++;
            continue;
        }
        if (buf[*off] == '#') {
            while (*off < size && buf[*off] != '\n') (*off)++;
            continue;
        }
        return 0;
    }
    return -1;
}

static int read_token_u32(const uint8_t *buf, uint32_t size, uint32_t *off, uint32_t *out) {
    uint32_t v = 0;
    int have = 0;
    if (skip_ws_and_comments(buf, size, off) != 0) return -1;
    while (*off < size) {
        uint8_t c = buf[*off];
        if (c >= '0' && c <= '9') {
            v = v * 10u + (uint32_t)(c - '0');
            have = 1;
            (*off)++;
            continue;
        }
        break;
    }
    if (!have) return -1;
    *out = v;
    return 0;
}

static int parse_ppm_p6(const uint8_t *buf, uint32_t size, uint32_t *w, uint32_t *h, const uint8_t **data, uint32_t *data_sz) {
    uint32_t off = 0;
    uint32_t maxv = 0;
    if (size < 3) return -1;
    if (buf[0] != 'P' || buf[1] != '6') return -1;
    off = 2;
    if (read_token_u32(buf, size, &off, w) != 0) return -1;
    if (read_token_u32(buf, size, &off, h) != 0) return -1;
    if (read_token_u32(buf, size, &off, &maxv) != 0) return -1;
    if (maxv != 255u) return -1;
    if (skip_ws_and_comments(buf, size, &off) != 0) return -1;
    *data = &buf[off];
    *data_sz = size - off;
    return 0;
}

int cmd_img_view(int argc, char **argv, int arg0, const char *cwd) {
    char path[256];
    int fd;
    int32_t n;
    uint32_t sz = 0;
    uint32_t w, h;
    const uint8_t *pixels;
    uint32_t px_sz;
    int fb_fd;
    const char *fb_path = NULL;
    dev_fb_info_t info;
    int bpp_bytes;
    uint32_t dst_w, dst_h;
    uint32_t pitch;
    uint32_t out_sz;

    (void)cwd;
    if (arg0 + 1 >= argc || arg0 + 2 < argc) {
        fprintf(stderr, "usage: img_view <file.ppm>\n");
        return 1;
    }

    if (normalize_path(cwd, argv[arg0 + 1], path, sizeof(path)) != 0) {
        fprintf(stderr, "img_view: bad path: %s\n", argv[arg0 + 1]);
        return 1;
    }

    fd = open(path, 0);
    if (fd < 0) {
        fprintf(stderr, "img_view: open failed: %s\n", path);
        return 1;
    }

    while (sz < IMG_FILE_MAX) {
        n = read(fd, g_file + sz, IMG_FILE_MAX - sz);
        if (n < 0) {
            close(fd);
            fprintf(stderr, "img_view: read failed\n");
            return 1;
        }
        if (n == 0) break;
        sz += (uint32_t)n;
    }
    close(fd);

    if (sz >= IMG_FILE_MAX) {
        fprintf(stderr, "img_view: file too large (limit %u bytes)\n", (uint32_t)IMG_FILE_MAX);
        return 1;
    }

    if (parse_ppm_p6(g_file, sz, &w, &h, &pixels, &px_sz) != 0) {
        fprintf(stderr, "img_view: only PPM P6 (max 255) is supported\n");
        return 1;
    }

    if (w == 0 || h == 0 || w > IMG_MAX_W || h > IMG_MAX_H) {
        fprintf(stderr, "img_view: image too big, max %ux%u\n", (uint32_t)IMG_MAX_W, (uint32_t)IMG_MAX_H);
        return 1;
    }
    if (px_sz < w * h * 3u) {
        fprintf(stderr, "img_view: truncated pixel data\n");
        return 1;
    }

    fb_fd = open_fb_device(&fb_path);
    if (fb_fd < 0) {
        fprintf(stderr, "img_view: framebuffer not available (tried /dev/vesa and /dev/framebuffer/buffer)\n");
        return 1;
    }
    if (ioctl(fb_fd, DEV_IOCTL_VESA_GET_INFO, &info) != 0) {
        close(fb_fd);
        fprintf(stderr, "img_view: framebuffer ioctl failed\n");
        return 1;
    }

    bpp_bytes = (int)(info.bpp / 8u);
    if (bpp_bytes != 3 && bpp_bytes != 4) {
        close(fb_fd);
        fprintf(stderr, "img_view: unsupported bpp=%u\n", info.bpp);
        return 1;
    }
    pitch = info.pitch;
    if (pitch == 0 || pitch > IMG_MAX_PITCH) {
        close(fb_fd);
        fprintf(stderr, "img_view: unsupported pitch=%u\n", pitch);
        return 1;
    }

    dst_w = info.width;
    dst_h = info.height;
    if (dst_w == 0 || dst_h == 0) {
        close(fb_fd);
        fprintf(stderr, "img_view: invalid framebuffer size\n");
        return 1;
    }
    if (dst_h > IMG_MAX_FB_H) {
        close(fb_fd);
        fprintf(stderr, "img_view: framebuffer too tall=%u (max %u)\n", dst_h, (uint32_t)IMG_MAX_FB_H);
        return 1;
    }

    memset(g_fb, 0, sizeof(g_fb));
    for (uint32_t y = 0; y < dst_h; y++) {
        uint32_t sy = (y * h) / dst_h;
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx = (x * w) / dst_w;
            uint32_t si = (sy * w + sx) * 3u;
            uint32_t di = y * pitch + x * (uint32_t)bpp_bytes;
            g_fb[di + 0] = pixels[si + 2];
            g_fb[di + 1] = pixels[si + 1];
            g_fb[di + 2] = pixels[si + 0];
            if (bpp_bytes == 4) g_fb[di + 3] = 0;
        }
    }

    out_sz = dst_h * pitch;
    if (write(fb_fd, g_fb, out_sz) < 0) {
        close(fb_fd);
        fprintf(stderr, "img_view: framebuffer write failed\n");
        return 1;
    }
    close(fb_fd);

    fprintf(stdout, "img_view: %ux%u -> fullscreen %ux%u via %s\n", w, h, dst_w, dst_h, fb_path);
    return 0;
}
