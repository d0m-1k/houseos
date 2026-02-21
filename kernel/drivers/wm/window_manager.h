#pragma once

#include <drivers/fonts/psf.h>
#include <drivers/wm/window.h>

typedef struct _wm_t {
    window_t *windows;
    uint8_t *buffer;
} wm_t;

wm_t *wm_create();
void wm_add_window(wm_t *wm, window_t *win);
void wm_draw_all(wm_t *wm);