
#include <drivers/images/jpeg.h>
#include <string.h>


bool jpeg_verify(const uint8_t* data, size_t size) {
    if (size < 2) return false;
    
    return (data[0] == 0xFF && data[1] == 0xD8);
}

image_t* jpeg_load(const uint8_t* data, size_t size) {
    
    return NULL;
}
