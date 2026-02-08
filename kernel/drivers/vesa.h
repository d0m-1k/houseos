#pragma once

#include <stdint.h>
#include <stdbool.h>

#define VESA_INFO_ADDR 0x8000
#define MODE_INFO_ADDR 0x8100

typedef struct {
    char signature[4];
    uint16_t version;
    uint32_t oem_string;
    uint32_t capabilities;
    uint32_t video_modes;
    uint16_t video_memory;
    uint16_t software_rev;
    uint32_t vendor;
    uint32_t product_name;
    uint32_t product_rev;
    uint8_t reserved[222];
    uint8_t oem_data[256];
} __attribute__((packed)) vesa_info_t;

typedef struct {
    uint16_t attributes;
    uint8_t window_a;
    uint8_t window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;
    
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__((packed)) vesa_mode_info_t;

bool vesa_init(void);
bool vesa_is_initialized(void);

void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t vesa_get_pixel(uint32_t x, uint32_t y);
void vesa_clear(uint32_t color);
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness);
void vesa_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);

void vesa_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color, bool filled);
void vesa_draw_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, uint32_t color, bool filled);
void vesa_copy_rect(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h);
void vesa_scroll(int32_t dx, int32_t dy, uint32_t fill_color);

uint32_t vesa_get_width(void);
uint32_t vesa_get_height(void);
uint32_t vesa_get_pitch(void);
uint32_t vesa_get_bpp(void);
uint32_t vesa_get_framebuffer(void);
uint32_t vesa_calculate_pixel_offset(uint32_t x, uint32_t y);

uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t vesa_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
void vesa_extract_color(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);