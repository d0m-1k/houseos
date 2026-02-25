#pragma once

#include <drivers/filesystem/memfs.h>
#include <drivers/wm/window.h>
#include <drivers/wm/window_manager.h>
#include <drivers/fonts/psf.h>
#include <stdint.h>
#include <stdbool.h>

struct gshell_args {
    memfs *fs;
};

typedef struct gshell_state {
    wm_t *wm;
    psf_font_t *font;
    memfs *fs;
    window_t *launcher_win;
    window_t *apps_win;
    uint32_t launcher_icon[16 * 16];
    uint8_t launcher_icon_w;
    uint8_t launcher_icon_h;
    bool launcher_icon_loaded;
} gshell_state_t;

void gshell_run(void *arg);
