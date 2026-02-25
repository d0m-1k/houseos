#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <kernel/kernel.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY_ADDRESS 0xB8000

enum vga_color {
    VGA_COLOR_BLACK = 0x0,
    VGA_COLOR_BLUE = 0x1,
    VGA_COLOR_GREEN = 0x2,
    VGA_COLOR_CYAN = 0x3,
    VGA_COLOR_RED = 0x4,
    VGA_COLOR_MAGENTA = 0x5,
    VGA_COLOR_BROWN = 0x6,
    VGA_COLOR_LIGHT_GREY = 0x7,
    VGA_COLOR_DARK_GREY = 0x8,
    VGA_COLOR_LIGHT_BLUE = 0x9,
    VGA_COLOR_LIGHT_GREEN = 0xA,
    VGA_COLOR_LIGHT_CYAN = 0xB,
    VGA_COLOR_LIGHT_RED = 0xC,
    VGA_COLOR_LIGHT_MAGENTA = 0xD,
    VGA_COLOR_LIGHT_BROWN = 0xE,
    VGA_COLOR_WHITE = 0xF
};

void vga_init(void);
void vga_clear(void);
void vga_fill(char c);
void vga_print(const char *str);
void vga_put_char(char c);
void vga_scroll(void);
void vga_newline(void);
void vga_tab(void);
void vga_backspace(void);
void vga_cursor_set(size_t x, size_t y);
void vga_cursor_set_x(size_t x);
void vga_cursor_set_y(size_t y);
size_t vga_cursor_get_x(void);
size_t vga_cursor_get_y(void);
bool vga_cursor_is_enabled(void);
void vga_cursor_update(void);
void vga_cursor_enable(uint8_t cursor_start, uint8_t cursor_end);
void vga_cursor_disable(void);
uint8_t vga_color_make(enum vga_color fg, enum vga_color bg);
uint8_t vga_color_get(void);
void vga_color_set(uint8_t c);
