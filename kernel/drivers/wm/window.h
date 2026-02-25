#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <drivers/fonts/psf.h>

typedef struct _wm_t wm_t;
typedef struct _window_t window_t;
struct key_event;

typedef bool (*window_on_close_t)(window_t *win, void *user_data);
typedef void (*window_on_focus_t)(window_t *win, bool focused, void *user_data);
typedef bool (*window_on_key_t)(window_t *win, const struct key_event *event, void *user_data);
typedef bool (*window_on_mouse_t)(window_t *win, int32_t local_x, int32_t local_y, uint8_t buttons, bool pressed, void *user_data);
typedef void (*window_on_draw_t)(window_t *win, void *user_data);

typedef struct _window_t {
    uint32_t id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    bool visible;
    bool draggable;
    bool focused;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t title_bg_color;
    uint32_t title_fg_color;
    uint32_t body_fg_color;
    bool can_close;
    bool can_maximize;
    bool resizable;
    bool title_enabled;
    bool maximized;
    int32_t restore_x;
    int32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    uint32_t *canvas;
    uint32_t canvas_width;
    uint32_t canvas_height;
    bool canvas_dirty;
    int32_t caret_x;
    int32_t caret_y;
    psf_font_t *font;
    char title[64];
    window_on_close_t on_close;
    window_on_focus_t on_focus;
    window_on_key_t on_key;
    window_on_mouse_t on_mouse;
    window_on_draw_t on_draw;
    void *user_data;
} window_t;

window_t *window_create(int32_t x, int32_t y, uint32_t width, uint32_t height, const char *title);
void window_destroy(window_t *win);
bool window_resize(window_t *win, uint32_t width, uint32_t height);
bool window_resize_ex(window_t *win, uint32_t width, uint32_t height, bool preserve_content);
void window_set_title(window_t *win, const char *title);
void window_enable_title(window_t *win);
void window_disable_title(window_t *win);
void window_diable_title(window_t *win);
void window_set_handlers(window_t *win, window_on_close_t on_close, window_on_focus_t on_focus, window_on_key_t on_key, window_on_mouse_t on_mouse, window_on_draw_t on_draw, void *user_data);
void window_update(window_t *win);
void window_draw_pixel(window_t *win, int32_t x, int32_t y, uint32_t color);
void window_draw_line(window_t *win, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
void window_draw_rect(window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness);
void window_fill_rect(window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void window_clear(window_t *win, uint32_t color);
void window_draw_char(window_t *win, int32_t x, int32_t y, char c, uint32_t color);
void window_draw_string(window_t *win, int32_t x, int32_t y, const char *text, uint32_t color);
