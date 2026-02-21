#include <progs/gshell.h>
#include <drivers/wm/window.h>
#include <drivers/wm/window_manager.h>
#include <drivers/fonts/psf.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <string.h>

void testwin_task(void *arg) {
    window_task_t *args = (struct window_task_t*)arg;
    if (!args) return;
    wm_t *wm = args->wm;
    window_t *win = args->win;

    while (true) {
        memset(win->buffer, 0xFF, win->width * win->height * vesa_get_bpp());
        sleep(50);
    }
    
    task_exit();
}

void testwin_event_handler(void *arg) {
    window_event_handler_t *args = (struct window_event_handler_t*)arg;
    if (!args) return;
    
    task_exit();
}

void gshell_run(void *arg) {
    struct gshell_args *args = (struct gshell_args*)arg;
    if (!args || !args->fs) return;

    psf_font_t *font = psf_load_from_memfs(args->fs, "/system/fonts/default8x16.psf");
    if (!font) return;

    wm_t *wm = wm_create();
    
    window_t *win = window_create(font, "Test", 15, 15, 250, 600, testwin_task, testwin_event_handler);
    if (win) {
        wm_add_window(wm, win);
        wm_draw_all(wm);
    }

    while (true) {
        wm_draw_all(wm);
        sleep(1);
    }

    psf_free_font(font);
    task_exit();
}