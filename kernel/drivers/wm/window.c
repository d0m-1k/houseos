#include <drivers/wm/window.h>
#include <drivers/wm/window_manager.h>
#include <drivers/fonts/font_renderer.h>
#include <asm/mm.h>
#include <string.h>

window_t *window_create(psf_font_t *font, char *title, size_t x, size_t y, size_t width, size_t height,
    void (*task)(void*), void (*event_handler)(void*)) {
    
    window_t *win = kmalloc(sizeof(window_t));
    if (!win) return NULL;

    win->buffer = kmalloc(width * height * vesa_get_bpp());
    if (!win->buffer) {
        kfree(win);
        return NULL;
    }

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->task = task;
    win->event_handler = event_handler;

    return win;
}

void window_draw(window_t *win, uint8_t *buffer) {
    if (!win) return;
    if (!buffer) return;
    for (int y = win->y; y < win->height; y++) {
        memcpy(buffer+(y*vesa_get_width()+win->x), win->buffer+(y*win->width), win->width);
    }
}
