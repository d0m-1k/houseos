// #include <drivers/tty.h>
#include <drivers/vga.h>

void kmain(void) {
    for (volatile size_t i = 0; i < (volatile size_t) 100000000; i++);
    vga_init();
    for (volatile size_t i = 0; i < (volatile size_t) 100000000; i++);
    vga_print("Hello world!");
    for (volatile size_t i = 0; i < (volatile size_t) 100000000; i++);
    vga_put_char('@');
    
    while (1) asm volatile("hlt");
}