#include <drivers/vga.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <asm/port.h>

static bool cursor_enabled;
static size_t cursor_x;
static size_t cursor_y;
static uint8_t color;
static uint16_t *buffer = (uint16_t *) 0xB8000;

void vga_init() {
    cursor_x = 0;
    cursor_y = 0;
    color = vga_color_make(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    vga_clear();
    vga_cursor_enable(14, 15);
}

void vga_clear() {
    cursor_x = 0;
    cursor_y = 0;
    vga_fill(' ');
    vga_cursor_update();
}

void vga_fill(char c) {
    uint16_t entry = (uint16_t)((color << 8) | (c & 0xFF));
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) buffer[i] = entry;
}

void vga_print(const char *str) {
    for (volatile int i = 0; str[i] != '\0'; i++) vga_put_char(str[i]);
}

void vga_put_char(char c) {
    switch (c) {
        case '\n':
            vga_newline();
            break;
            
        case '\r':
            cursor_x = 0;
            vga_cursor_update();
            break;
            
        case '\t':
            vga_tab();
            break;
            
        case '\b':
            vga_backspace();
            break;
            
        default:
            buffer[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)((color << 8) | c);
            cursor_x++;
            
            if (cursor_x >= VGA_WIDTH) vga_newline();
            break;
    }
    
    vga_cursor_update();
}

void vga_scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            buffer[(y - 1) * VGA_WIDTH + x] = buffer[y * VGA_WIDTH + x];
        }
    }
    
    uint16_t blank = (uint16_t)((color << 8) | ' ');
    for (size_t x = 0; x < VGA_WIDTH; x++) buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    
    cursor_y = VGA_HEIGHT - 1;
}

void vga_newline(void) {
    cursor_x = 0;
    cursor_y++;
    
    if (cursor_y >= VGA_HEIGHT) vga_scroll();
}

void vga_tab(void) {
    cursor_x = (cursor_x + 8) & ~7;
    if (cursor_x >= VGA_WIDTH) vga_newline();
}

void vga_backspace(void) {
    if (cursor_x > 0) {
        cursor_x--;
    } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    
    buffer[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)((color << 8) | ' ');
}

void vga_cursor_set(size_t x, size_t y) {
    cursor_x = (x < VGA_WIDTH) ? x : VGA_WIDTH - 1;
    cursor_y = (y < VGA_HEIGHT) ? y : VGA_HEIGHT - 1;
    vga_cursor_update();
}

void vga_cursor_set_x(size_t x) { 
    cursor_x = (x < VGA_WIDTH) ? x : VGA_WIDTH - 1;
    vga_cursor_update();
}

void vga_cursor_set_y(size_t y) { 
    cursor_y = (y < VGA_HEIGHT) ? y : VGA_HEIGHT - 1;
    vga_cursor_update();
}

size_t vga_cursor_get_x(void) { 
    return cursor_x; 
}

size_t vga_cursor_get_y(void) { 
    return cursor_y; 
}

bool vga_cursor_is_enabled(void) { 
    return cursor_enabled; 
}

void vga_cursor_update(void) {
    uint16_t pos = (uint16_t)(cursor_y * VGA_WIDTH + cursor_x);
    
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_cursor_enable(uint8_t cursor_start, uint8_t cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);
    
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
    
    cursor_enabled = true;
}

void vga_cursor_disable(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    cursor_enabled = false;
}

uint8_t vga_color_make(enum vga_color bg, enum vga_color fg) {
    return (uint8_t)((bg << 4) | fg);
}

uint8_t vga_color_get(void) { 
    return color; 
}

void vga_color_set(uint8_t c) { 
    color = c; 
}