#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <drivers/fonts/psf.h>
#include <asm/task.h>
#include <drivers/wm/window_manager.h>

typedef struct _window_t {
    char *title;
    size_t x, y;
    size_t width, height;
    uint8_t *buffer;
    void (*task)(void*);
    void (*event_handler)(void*);
    struct _window_t *next;
} window_t;

typedef enum {
    WM_EVENT_NONE,
    WM_EVENT_MOUSE_MOVE,
    WM_EVENT_MOUSE_CLICK,
    WM_EVENT_MOUSE_RELEASE,
    WM_EVENT_KEY_PRESS,
    WM_EVENT_KEY_RELEASE,
    WM_EVENT_CLOSE,
    WM_EVENT_FOCUS_GAIN,
    WM_EVENT_FOCUS_LOSS,
    WM_EVENT_RESIZE,
    WM_EVENT_MOVE
} wm_event_type_t;

typedef struct _window_event_t {
    wm_event_type_t type;
    union {
        struct { size_t x, y; } mouse;
        struct { uint8_t scancode; char ascii; } key;
        struct { size_t width, height; } resize;
        struct { size_t x, y; } move;
    };
} window_event_t;

typedef struct _window_task_t {
    wm_t *wm;
    window_t *win;
} window_task_t;

typedef struct _window_event_handler_t {
    wm_t *wm;
    window_t *win;
    window_event_t *ev;
} window_event_handler_t;

window_t *window_create(psf_font_t *font, char *title, size_t x, size_t y, size_t width, size_t height,
    void (*task)(void*), void (*event_handler)(void*));
void window_draw(window_t *win, uint8_t *buffer);