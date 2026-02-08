// bmp.c
#include <drivers/images/bmp.h>
#include <string.h>
#include <asm/mm.h>

static int32_t bmp_abs(int32_t v) { return v < 0 ? -v : v; }

static uint32_t read_u32(const uint8_t* d) { 
    return d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24); 
}

static uint16_t read_u16(const uint8_t* d) { 
    return d[0] | (d[1] << 8); 
}

bool bmp_verify(const uint8_t* data, size_t size) {
    if (size < 14) return false;
    return ((bmp_file_header_t*)data)->signature == 0x4D42;
}

image_t* bmp_load(const uint8_t* data, size_t size) {
    if (!data || size < 14) return NULL;
    if (!bmp_verify(data, size)) return NULL;
    
    bmp_file_header_t* fh = (bmp_file_header_t*)data;
    const uint8_t* dib = data + 14;
    
    uint32_t hdr_size = read_u32(dib);
    if (size < 14 + hdr_size) return NULL;
    
    int32_t width, height;
    uint16_t bpp;
    uint32_t comp = 0;
    
    if (hdr_size >= 40) {
        width = (int32_t)read_u32(dib + 4);
        height = (int32_t)read_u32(dib + 8);
        bpp = read_u16(dib + 14);
        comp = read_u32(dib + 16);
    } else return NULL;
    
    // Разрешаем BI_RGB (0) и BI_BITFIELDS (3)
    if (comp != 0 && comp != 3) return NULL;
    
    if (bpp != 24 && bpp != 32) return NULL;
    if (fh->data_offset >= size) return NULL;
    
    image_t* img = (image_t*)valloc(sizeof(image_t));
    if (!img) return NULL;
    memset(img, 0, sizeof(image_t));
    
    img->width = bmp_abs(width);
    img->height = bmp_abs(height);
    img->bpp = bpp;
    img->type = IMAGE_TYPE_BMP;
    img->has_alpha = (bpp == 32);
    img->pitch = image_calculate_pitch(img->width, img->bpp);
    
    uint32_t need_size = img->pitch * img->height;
    if (size < fh->data_offset + need_size) {
        vfree(img);
        return NULL;
    }
    
    img->data_size = need_size;
    img->data = (uint8_t*)valloc(img->data_size);
    if (!img->data) {
        vfree(img);
        return NULL;
    }
    
    const uint8_t* pixels = data + fh->data_offset;
    
    if (height > 0) {
        for (uint32_t y = 0; y < img->height; y++) {
            const uint8_t* src = pixels + (img->height - y - 1) * img->pitch;
            uint8_t* dst = img->data + y * img->pitch;
            memcpy(dst, src, img->pitch);
        }
    } else {
        memcpy(img->data, pixels, img->data_size);
    }
    
    if (img->bpp == 24) {
        for (uint32_t y = 0; y < img->height; y++) {
            uint8_t* row = img->data + y * img->pitch;
            for (uint32_t x = 0; x < img->width; x++) {
                uint8_t* p = row + x * 3;
                uint8_t t = p[0];
                p[0] = p[2];
                p[2] = t;
            }
        }
    } else if (img->bpp == 32) {
        // Читаем маски цветов для BI_BITFIELDS
        uint32_t r_mask = 0, g_mask = 0, b_mask = 0, a_mask = 0;
        
        if (comp == 3) {
            if (hdr_size >= 56) {
                r_mask = read_u32(dib + 0x24);
                g_mask = read_u32(dib + 0x28);
                b_mask = read_u32(dib + 0x2C);
                if (hdr_size >= 108) a_mask = read_u32(dib + 0x42);
            }
        }
        
        // Если маски не указаны, используем стандартные
        if (r_mask == 0 && g_mask == 0 && b_mask == 0) {
            r_mask = 0x00FF0000;
            g_mask = 0x0000FF00;
            b_mask = 0x000000FF;
            a_mask = 0xFF000000;
        }
        
        // Простая конвертация: предполагаем, что порядок в памяти BGRA
        // Для BI_BITFIELDS с нестандартными масками нужна дополнительная обработка
        for (uint32_t y = 0; y < img->height; y++) {
            uint8_t* row = img->data + y * img->pitch;
            for (uint32_t x = 0; x < img->width; x++) {
                uint8_t* p = row + x * 4;
                uint8_t t = p[0];
                p[0] = p[2];
                p[2] = t;
            }
        }
    }
    
    return img;
}

void bmp_flip_vertical(uint8_t* data, uint32_t w, uint32_t h, uint32_t p) {
    uint8_t* tmp = (uint8_t*)valloc(p);
    if (!tmp) return;
    
    for (uint32_t i = 0; i < h / 2; i++) {
        uint8_t* r1 = data + i * p;
        uint8_t* r2 = data + (h - i - 1) * p;
        memcpy(tmp, r1, p);
        memcpy(r1, r2, p);
        memcpy(r2, tmp, p);
    }
    
    vfree(tmp);
}