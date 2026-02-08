#pragma once

#include <drivers/images/image.h>

typedef struct {
    uint16_t signature;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
} __attribute__((packed)) bmp_file_header_t;

typedef struct {
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
} __attribute__((packed)) bmp_info_header_t;

image_t* bmp_load(const uint8_t* data, size_t size);
bool bmp_verify(const uint8_t* data, size_t size);
void bmp_flip_vertical(uint8_t* data, uint32_t width, uint32_t height, uint32_t pitch);