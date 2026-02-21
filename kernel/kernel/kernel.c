#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <asm/idt.h>
#include <asm/task.h>
#include <asm/mm.h>
#include <asm/timer.h>
#include <string.h>
#include <progs/gshell.h>
#include <asm/processor.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/vesa.h>
#include <drivers/filesystem/initramfs.h>
#include <drivers/fonts/font_renderer.h>

static memfs *fs = NULL;

void kmain(void) {
    vesa_init();
    idt_init();
    keyboard_init();
    mouse_init();
    mm_init();
    kmalloc_init();
    timer_init();
    sti();

    fs = memfs_create(10 * 1024 * 1024);
    initramfs_init(fs);

    task_init(NULL);
    
    struct gshell_args args = {fs};
    task_create(gshell_run, &args);

    while (1) hlt();
}