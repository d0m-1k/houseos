#include <progs/gshell.h>
#include <drivers/vesa.h>
#include <drivers/fonts/font_renderer.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <string.h>

void test_task(void *arg) {
    int x = vesa_get_width(), y = vesa_get_height();
    while (1) {
        vesa_put_pixel(x, y, 0xFFFF0000);
        x--;
        if (x <= 0) {
            x = vesa_get_width();
            y--;
            sleep(10);
        }
        if (y <= 0) break;
    }
    task_exit();
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
        vesa_put_pixel(x, y, 0x00FFFF00);
        x++;
        if (x >= vesa_get_width()) {
            x = 0;
            y++;
            sleep(50);
        }
        if (y >= vesa_get_height()) break;
    }
    
    task_exit();
    psf_free_font(font);
}