#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <asm/idt.h>
#include <asm/mm.h>
#include <string.h>
#include <progs/shell.h>
#include <progs/gshell.h>
#include <progs/snake.h>
#include <asm/processor.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/vesa.h>
#include <drivers/filesystem/initramfs.h>

static memfs *fs = NULL;

void kmain(void) {
    vesa_init();

    // vga_init();
    // vga_color_set(vga_color_make(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    // vga_clear();

    idt_init();
    keyboard_init();
    sti();
    mm_init();
    kmalloc_init();

    fs = memfs_create(1034*1024*1024);
    if (fs == NULL) {
        vga_print("Failed to create MemFS\n");
    }

    initramfs_init(fs);

    gshell_run(fs);

    // shell_run(fs);
    
    while (1) hlt();
}