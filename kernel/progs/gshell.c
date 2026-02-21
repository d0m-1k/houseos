#include <progs/gshell.h>
#include <drivers/vesa.h>
#include <drivers/fonts/font_renderer.h>
#include <asm/task.h>

void test_task(void *arg) {
    int x = vesa_get_width(), y = vesa_get_height();
    while (1) {
        vesa_put_pixel(x, y, 0xFFFF0000);
        x--;
        if (x <= 0) {
            x = vesa_get_width();
            y--;
            task_yield();
        }
        for (volatile int i = 0; i < 1000; i++) if (i % 500 == 0) task_yield();
    }
}

void gshell_run(void *arg) {
    struct gshell_args *args = (struct gshell_args*)arg;
    memfs *fs = args->fs;

    if (!fs) return;

    psf_font_t *font = psf_load_from_memfs(fs, "/system/fonts/default8x16.psf");
    if (!font) return;

    task_create(test_task, NULL);

    int x = 0, y = 0;
    while (1) {
        vesa_put_pixel(x, y, 0xFF00FF00);
        x++;
        if (x >= vesa_get_width()) {
            x = 0;
            y++;
            task_yield();
        }
        for (volatile int i = 0; i < 1000; i++) if (i % 500 == 0) task_yield();
    }

    psf_free_font(font);
}