// image_renderer.c - полностью без плавающей точки
#include <drivers/images/image.h>
#include <string.h>

static inline uint8_t fast_divide_255(uint32_t value) {
    return (value + 1 + (value >> 8)) >> 8;
}

static inline uint8_t alpha_blend(uint8_t src, uint8_t dst, uint8_t alpha) {
    uint32_t src_weight = alpha;
    uint32_t dst_weight = 255 - alpha;
    uint32_t result = (src * src_weight + dst * dst_weight);
    return fast_divide_255(result);
}

void image_draw(image_t* img, uint32_t x, uint32_t y) {
    if (!img || !img->data || !vesa_is_initialized()) return;
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t draw_width = img->width;
    uint32_t draw_height = img->height;
    
    if (x + draw_width > screen_width) draw_width = screen_width - x;
    if (y + draw_height > screen_height) draw_height = screen_height - y;
    
    for (uint32_t src_y = 0; src_y < draw_height; src_y++) {
        uint8_t* src_row = img->data + src_y * img->pitch;
        
        for (uint32_t src_x = 0; src_x < draw_width; src_x++) {
            uint8_t* src_pixel = src_row + src_x * (img->bpp / 8);
            
            uint32_t color;
            if (img->bpp == 24) {
                color = vesa_rgb(src_pixel[0], src_pixel[1], src_pixel[2]);
            } else if (img->bpp == 32) {
                color = vesa_argb(src_pixel[3], src_pixel[0], src_pixel[1], src_pixel[2]);
            } else {
                continue;
            }
            
            vesa_put_pixel(x + src_x, y + src_y, color);
        }
    }
}

void image_draw_transparent(image_t* img, uint32_t x, uint32_t y, uint32_t transparent_color) {
    if (!img || !img->data || !vesa_is_initialized()) return;
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t draw_width = img->width;
    uint32_t draw_height = img->height;
    
    if (x + draw_width > screen_width) draw_width = screen_width - x;
    if (y + draw_height > screen_height) draw_height = screen_height - y;
    
    for (uint32_t src_y = 0; src_y < draw_height; src_y++) {
        uint8_t* src_row = img->data + src_y * img->pitch;
        
        for (uint32_t src_x = 0; src_x < draw_width; src_x++) {
            uint8_t* src_pixel = src_row + src_x * (img->bpp / 8);
            
            uint32_t pixel_color;
            if (img->bpp == 24) {
                pixel_color = vesa_rgb(src_pixel[0], src_pixel[1], src_pixel[2]);
            } else if (img->bpp == 32) {
                pixel_color = vesa_argb(src_pixel[3], src_pixel[0], src_pixel[1], src_pixel[2]);
            } else {
                continue;
            }
            
            if (pixel_color != transparent_color) {
                vesa_put_pixel(x + src_x, y + src_y, pixel_color);
            }
        }
    }
}

void image_draw_alpha(image_t* img, uint32_t x, uint32_t y) {
    if (!img || !img->data || !vesa_is_initialized() || img->bpp != 32) {
        image_draw(img, x, y);
        return;
    }
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t draw_width = img->width;
    uint32_t draw_height = img->height;
    
    if (x + draw_width > screen_width) draw_width = screen_width - x;
    if (y + draw_height > screen_height) draw_height = screen_height - y;
    
    for (uint32_t src_y = 0; src_y < draw_height; src_y++) {
        uint8_t* src_row = img->data + src_y * img->pitch;
        
        for (uint32_t src_x = 0; src_x < draw_width; src_x++) {
            uint8_t* src_pixel = src_row + src_x * 4;
            
            uint8_t alpha = src_pixel[3];
            
            if (alpha == 0) {
                continue;
            } else if (alpha == 255) {
                uint32_t color = vesa_rgb(src_pixel[0], src_pixel[1], src_pixel[2]);
                vesa_put_pixel(x + src_x, y + src_y, color);
            } else {
                uint32_t bg_color = vesa_get_pixel(x + src_x, y + src_y);
                
                uint8_t bg_r, bg_g, bg_b, bg_a;
                vesa_extract_color(bg_color, &bg_r, &bg_g, &bg_b, &bg_a);
                
                // ТОЛЬКО ЦЕЛОЧИСЛЕННОЕ ВЫЧИСЛЕНИЕ - УБРАН float!
                uint8_t r = alpha_blend(src_pixel[0], bg_r, alpha);
                uint8_t g = alpha_blend(src_pixel[1], bg_g, alpha);
                uint8_t b = alpha_blend(src_pixel[2], bg_b, alpha);
                
                uint32_t blended_color = vesa_rgb(r, g, b);
                vesa_put_pixel(x + src_x, y + src_y, blended_color);
            }
        }
    }
}

void image_draw_scaled(image_t* img, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!img || !img->data || !vesa_is_initialized() || width == 0 || height == 0) return;
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t draw_width = width;
    uint32_t draw_height = height;
    
    if (x + draw_width > screen_width) draw_width = screen_width - x;
    if (y + draw_height > screen_height) draw_height = screen_height - y;
    
    for (uint32_t dy = 0; dy < draw_height; dy++) {
        uint32_t src_y = (dy * img->height) / height;
        if (src_y >= img->height) src_y = img->height - 1;
        
        uint8_t* src_row = img->data + src_y * img->pitch;
        
        for (uint32_t dx = 0; dx < draw_width; dx++) {
            uint32_t src_x = (dx * img->width) / width;
            if (src_x >= img->width) src_x = img->width - 1;
            
            uint8_t* src_pixel = src_row + src_x * (img->bpp / 8);
            
            uint32_t color;
            if (img->bpp == 24) {
                color = vesa_rgb(src_pixel[0], src_pixel[1], src_pixel[2]);
            } else if (img->bpp == 32) {
                color = vesa_argb(src_pixel[3], src_pixel[0], src_pixel[1], src_pixel[2]);
            } else {
                continue;
            }
            
            vesa_put_pixel(x + dx, y + dy, color);
        }
    }
}

void image_draw_part(image_t* img, uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h, uint32_t dst_x, uint32_t dst_y) {
    if (!img || !img->data || !vesa_is_initialized()) return;
    if (src_x >= img->width || src_y >= img->height) return;
    
    if (src_x + src_w > img->width) src_w = img->width - src_x;
    if (src_y + src_h > img->height) src_h = img->height - src_y;
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t draw_width = src_w;
    uint32_t draw_height = src_h;
    
    if (dst_x + draw_width > screen_width) draw_width = screen_width - dst_x;
    if (dst_y + draw_height > screen_height) draw_height = screen_height - dst_y;
    
    if (draw_width < src_w) src_w = draw_width;
    if (draw_height < src_h) src_h = draw_height;
    
    for (uint32_t y = 0; y < src_h; y++) {
        uint8_t* src_row = img->data + (src_y + y) * img->pitch;
        
        for (uint32_t x = 0; x < src_w; x++) {
            uint8_t* src_pixel = src_row + (src_x + x) * (img->bpp / 8);
            
            uint32_t color;
            if (img->bpp == 24) {
                color = vesa_rgb(src_pixel[0], src_pixel[1], src_pixel[2]);
            } else if (img->bpp == 32) {
                color = vesa_argb(src_pixel[3], src_pixel[0], src_pixel[1], src_pixel[2]);
            } else {
                continue;
            }
            
            vesa_put_pixel(dst_x + x, dst_y + y, color);
        }
    }
}