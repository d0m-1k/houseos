#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <drivers/fonts/psf.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/wm/window.h>

typedef struct _wm_t {
    window_t *windows[16];
    size_t window_count;
    psf_font_t *font;

    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;

    bool dragging;
    window_t *drag_window;
    int32_t drag_off_x;
    int32_t drag_off_y;

    uint32_t desktop_color;
    bool scene_dirty;
    bool cursor_dirty;
    bool cursor_drawn;
    int32_t prev_cursor_x;
    int32_t prev_cursor_y;
    uint32_t cursor_saved[16 * 16];
    uint32_t cursor_pixels[16 * 16];
    uint8_t cursor_w;
    uint8_t cursor_h;
    bool cursor_custom;

    window_t *content_dirty_window;

    bool damage_dirty;
    int32_t damage_x;
    int32_t damage_y;
    uint32_t damage_w;
    uint32_t damage_h;
} wm_t;

extern volatile bool wm_in_frame;

wm_t *wm_create(psf_font_t *font);
void wm_destroy(wm_t *wm);
bool wm_add_window(wm_t *wm, window_t *win);
void wm_close_window(wm_t *wm, window_t *win);
void wm_focus_window(wm_t *wm, window_t *win);
void wm_handle_mouse_packet(wm_t *wm, const mouse_packet_t *packet);
void wm_handle_key_event(wm_t *wm, const struct key_event *event);
void wm_request_redraw(wm_t *wm);
void wm_render(wm_t *wm);
window_t *wm_get_focused_window(wm_t *wm);
bool wm_load_cursor_from_memfs(wm_t *wm, memfs *fs, const char *path);
bool wm_load_title_buttons_from_memfs(wm_t *wm, memfs *fs, const char *close_path, const char *full_path);
bool wm_load_theme_from_memfs(wm_t *wm, memfs *fs, const char *path);
