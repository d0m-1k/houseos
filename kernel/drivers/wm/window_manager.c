#include <drivers/wm/window_manager.h>
#include <drivers/wm/window.h>
#include <drivers/vesa.h>
#include <drivers/mouse.h>
#include <drivers/keyboard.h>
#include <drivers/fonts/font_renderer.h>
#include <asm/task.h>
#include <asm/processor.h>
#include <asm/mm.h>
#include <string.h>

wm_t *wm_create() {
    wm_t *wm = kmalloc(sizeof(wm_t));
    if (!wm) return NULL;

    wm->buffer = kmalloc(vesa_get_width() * vesa_get_height() * vesa_get_bpp());
    if (!wm->buffer) {
        kfree(wm);
        return NULL;
    }

    wm->windows = NULL;
}

void wm_add_window(wm_t *wm, window_t *win) {
    if (!wm) return;
    if (!wm->windows) {
        wm->windows = win;
        return;
    }
    window_t *w = wm->windows;
    while (w->next != NULL) w = w->next;
    w->next = win;

    if (win->task) task_create(win->task, NULL);
}

void wm_draw_all(wm_t *wm) {
    if (!wm) return;
    if (!wm->windows);
    window_t *win = wm->windows;
    while (win->next != NULL) {
        window_draw(win, wm->buffer);
        win = win->next;
    }
    uint8_t *buffer = (uint8_t *) vesa_get_framebuffer();
    memcpy(buffer, win->buffer, vesa_get_width() * vesa_get_height() * vesa_get_bpp());
}