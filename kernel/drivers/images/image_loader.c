#include <drivers/images/image.h>
#include <drivers/images/bmp.h>
#include <drivers/images/png.h>
#include <drivers/images/jpeg.h>
#include <drivers/images/txt.h>
#include <asm/mm.h>
#include <string.h>

uint32_t image_calculate_pitch(uint32_t width, uint32_t bpp) {
    uint32_t bytes = (bpp + 7) / 8;
    uint32_t pitch = width * bytes;
    while (pitch % 4 != 0) pitch++;
    return pitch;
}

image_type_t image_detect_type(const uint8_t* data, size_t size) {
    if (!data || size == 0) return IMAGE_TYPE_UNKNOWN;
    if (size >= 12) {
        if (bmp_verify(data, size)) return IMAGE_TYPE_BMP;
        if (png_verify(data, size)) return IMAGE_TYPE_PNG;
        if (jpeg_verify(data, size)) return IMAGE_TYPE_JPEG;
    }
    if (txt_verify(data, size)) return IMAGE_TYPE_TXT;
    return IMAGE_TYPE_UNKNOWN;
}

static bool image_path_has_txt_ext(const char* path) {
    if (!path) return false;

    const char* dot = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '.') dot = p;
    }
    if (!dot) return false;
    return strcmp(dot, ".txt") == 0 || strcmp(dot, ".TXT") == 0;
}

image_t* image_load_from_buffer(const uint8_t* data, size_t size) {
    if (!data || size == 0) return NULL;
    
    image_type_t type = image_detect_type(data, size);
    
    switch (type) {
        case IMAGE_TYPE_BMP: return bmp_load(data, size);
        case IMAGE_TYPE_PNG: return png_load(data, size);
        case IMAGE_TYPE_JPEG: return jpeg_load(data, size);
        case IMAGE_TYPE_TXT: return txt_load(data, size);
        default: return NULL;
    }
}

image_t* image_load_from_memfs(memfs* fs, const char* path) {
    if (!fs || !path) return NULL;
    
    memfs_inode info;
    if (memfs_get_info(fs, path, &info) != 0) return NULL;
    
    if (info.type != MEMFS_TYPE_FILE) return NULL;
    
    uint8_t* file_data = (uint8_t*)valloc(info.file.size);
    if (!file_data) return NULL;
    
    ssize_t bytes_read = memfs_read(fs, path, file_data, info.file.size);
    if (bytes_read != info.file.size) {
        vfree(file_data);
        return NULL;
    }
    
    image_t* img = NULL;
    if (image_path_has_txt_ext(path)) {
        img = txt_load(file_data, info.file.size);
    }
    if (!img) {
        img = image_load_from_buffer(file_data, info.file.size);
    }
    
    vfree(file_data);
    
    return img;
}

void image_free(image_t* img) {
    if (!img) return;
    
    if (img->data) vfree(img->data);
    
    vfree(img);
}
