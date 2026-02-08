// images/image.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/vesa.h>

typedef enum {
    IMAGE_TYPE_BMP,
    IMAGE_TYPE_PNG,
    IMAGE_TYPE_JPEG,
    IMAGE_TYPE_UNKNOWN
} image_type_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint8_t* data;
    bool has_alpha;
    image_type_t type;
    uint32_t data_size;
} image_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} rgba_color_t;

image_t* image_load_from_memfs(memfs* fs, const char* path);
void image_free(image_t* img);
void image_draw(image_t* img, uint32_t x, uint32_t y);
void image_draw_scaled(image_t* img, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void image_draw_part(image_t* img, uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h, uint32_t dst_x, uint32_t dst_y);
void image_draw_transparent(image_t* img, uint32_t x, uint32_t y, uint32_t transparent_color);
void image_draw_alpha(image_t* img, uint32_t x, uint32_t y);

image_type_t image_detect_type(const uint8_t* data, size_t size);
uint32_t image_calculate_pitch(uint32_t width, uint32_t bpp);