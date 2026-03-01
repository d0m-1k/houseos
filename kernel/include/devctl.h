#pragma once

#include <stdint.h>

enum {
    DEV_IOCTL_FB_GET_INFO = 0x1000,
    DEV_IOCTL_TTY_GET_INFO = 0x1100,
    DEV_IOCTL_TTY_SET_ACTIVE = 0x1101,
    DEV_IOCTL_KBD_GET_INFO = 0x1200,
    DEV_IOCTL_MOUSE_GET_INFO = 0x1300,
};

enum {
    DEV_TTY_KIND_VESA = 0,
    DEV_TTY_KIND_SERIAL = 1,
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t address;
    uint32_t size;
} dev_fb_info_t;

typedef struct {
    uint32_t kind;
    uint32_t index;
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_x;
    uint32_t cursor_y;
} dev_tty_info_t;

typedef struct {
    uint32_t layout;
    uint32_t caps_lock;
    uint32_t num_lock;
    uint32_t scroll_lock;
} dev_keyboard_info_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t buttons;
} dev_mouse_info_t;
