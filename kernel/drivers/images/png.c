// images/png.c
#include <drivers/images/png.h>
#include <string.h>

bool png_verify(const uint8_t* data, size_t size) {
    if (size < 8) return false;
    
    const uint8_t png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return memcmp(data, png_signature, 8) == 0;
}

image_t* png_load(const uint8_t* data, size_t size) {
    // TODO: Реализовать PNG парсер
    return NULL;
}