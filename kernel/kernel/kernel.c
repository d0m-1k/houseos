#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <asm/idt.h>
#include <asm/mm.h>
#include <string.h>
#include <progs/shell.h>
#include <progs/snake.h>
#include <asm/processor.h>

void kmain(void) {
    vga_init();
    vga_color_set(vga_color_make(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));
    vga_clear();
    vga_update();

    idt_init();
    keyboard_init();
    sti();
    mm_init();
    kmalloc_init();

    heap_debug();
    vga_update();

    shell_run();
    // struct shell_args args = {2, {"snake", "500000000"}};
    // snake_run(args);
    
    while (1) hlt();
}