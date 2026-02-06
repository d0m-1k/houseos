#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <asm/idt.h>
#include <asm/mm.h>
#include <string.h>
#include <progs/shell.h>
#include <progs/snake.h>
#include <asm/processor.h>
#include <drivers/memfs.h>

void kmain(void) {
    vga_init();
    vga_color_set(vga_color_make(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_clear();

    idt_init();
    keyboard_init();
    sti();
    mm_init();
    kmalloc_init();

    heap_debug();

    shell_run();
    
    while (1) hlt();
}