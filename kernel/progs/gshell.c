#include <progs/gshell.h>
#include <drivers/vesa.h>
#include <drivers/fonts/psf.h>
#include <drivers/fonts/font_renderer.h>
#include <drivers/images/image.h>
#include <string.h>
#include <asm/processor.h>

void gshell_run(memfs *fs) {
    if (!fs) return;
    
    if (!vesa_init()) return;
    
    uint32_t white = vesa_rgb(255, 255, 255);
    uint32_t black = vesa_rgb(0, 0, 0);
    uint32_t red = vesa_rgb(255, 0, 0);
    uint32_t blue = vesa_rgb(0, 0, 255);
    uint32_t green = vesa_rgb(0, 255, 0);
    
    char buffer[64];
    
    vesa_clear(white);
    
    int w = vesa_get_width();
    int h = vesa_get_height();
    
    const char *font_path = "/system/fonts/default8x16.psf";

    psf_font_t *font = psf_load_from_memfs(fs, font_path);
    
    psf_free_font(font);
    
    while (1) hlt();
}