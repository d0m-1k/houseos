#include <drivers/images/txt.h>
#include <asm/mm.h>
#include <string.h>

static void txt_map_char(uint8_t ch, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    *r = 0;
    *g = 0;
    *b = 0;
    *a = 0;

    switch (ch) {
        case '#':
        case '-':
            *a = 255;
            return;
        case '*':
            *r = 255; *g = 255; *b = 255; *a = 255;
            return;
        case '+':
            *r = 208; *g = 232; *b = 255; *a = 255;
            return;
        case 'R':
        case 'r':
            *r = 255; *g = 64; *b = 64; *a = 255;
            return;
        case 'G':
        case 'g':
            *r = 58; *g = 223; *b = 122; *a = 255;
            return;
        case 'B':
        case 'b':
            *r = 56; *g = 152; *b = 255; *a = 255;
            return;
        default:
            return;
    }
}

bool txt_verify(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;
    if (size > 16384) return false;

    bool has_newline = false;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == '\n' || c == '\r') {
            has_newline = true;
            continue;
        }
        if (c == '\t') continue;
        if (c < 32 || c > 126) return false;
    }

    return has_newline || size > 0;
}

image_t* txt_load(const uint8_t* data, size_t size) {
    if (!txt_verify(data, size)) return NULL;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t line_len = 0;

    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (line_len > width) width = line_len;
            height++;
            line_len = 0;
            continue;
        }
        line_len++;
    }

    if (line_len > 0) {
        if (line_len > width) width = line_len;
        height++;
    }

    if (width == 0 || height == 0) return NULL;

    image_t* img = (image_t*)valloc(sizeof(image_t));
    if (!img) return NULL;
    memset(img, 0, sizeof(image_t));

    img->width = width;
    img->height = height;
    img->bpp = 32;
    img->pitch = image_calculate_pitch(width, img->bpp);
    img->data_size = img->pitch * img->height;
    img->type = IMAGE_TYPE_TXT;
    img->has_alpha = true;

    img->data = (uint8_t*)valloc(img->data_size);
    if (!img->data) {
        vfree(img);
        return NULL;
    }
    memset(img->data, 0, img->data_size);

    uint32_t x = 0;
    uint32_t y = 0;
    for (size_t i = 0; i < size && y < height; i++) {
        uint8_t c = data[i];
        if (c == '\r') continue;
        if (c == '\n') {
            y++;
            x = 0;
            continue;
        }

        if (x < width) {
            uint8_t r, g, b, a;
            txt_map_char(c, &r, &g, &b, &a);
            uint8_t* p = img->data + y * img->pitch + x * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = a;
        }
        x++;
    }

    return img;
}
