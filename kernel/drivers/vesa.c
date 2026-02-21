#include <drivers/vesa.h>
#include <string.h>
#include <stddef.h>

static vesa_info_t* vesa_info = (vesa_info_t*)VESA_INFO_ADDR;
static vesa_mode_info_t* mode_info = (vesa_mode_info_t*)MODE_INFO_ADDR;
static uint8_t* framebuffer = NULL;
static bool initialized = false;
static uint32_t bytes_per_pixel = 0;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define SWAP(a, b) { typeof(a) temp = a; a = b; b = temp; }
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

bool vesa_init(void) {
    if (strncmp(vesa_info->signature, "VESA", 4) != 0) return false;
    if (mode_info->framebuffer == 0) return false;
    
    framebuffer = (uint8_t*)(uintptr_t)mode_info->framebuffer;
    bytes_per_pixel = mode_info->bpp / 8;
    
    if (bytes_per_pixel != 1 && bytes_per_pixel != 2 && 
        bytes_per_pixel != 3 && bytes_per_pixel != 4) {
        return false;
    }
    
    initialized = true;
    return true;
}

bool vesa_is_initialized(void) {
    return initialized && framebuffer != NULL;
}

uint32_t vesa_calculate_pixel_offset(uint32_t x, uint32_t y) {
    return y * mode_info->pitch + x * bytes_per_pixel;
}

void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!vesa_is_initialized() || x >= mode_info->width || y >= mode_info->height) return;
    
    uint32_t offset = vesa_calculate_pixel_offset(x, y);
    uint8_t* pixel = framebuffer + offset;
    
    switch (bytes_per_pixel) {
        case 1:
            pixel[0] = (uint8_t)color;
            break;
        case 2:
            *(uint16_t*)pixel = (uint16_t)color;
            break;
        case 3:
            pixel[0] = (color >> 16) & 0xFF;
            pixel[1] = (color >> 8) & 0xFF;
            pixel[2] = color & 0xFF;
            break;
        case 4:
            *(uint32_t*)pixel = color;
            break;
    }
}

uint32_t vesa_get_pixel(uint32_t x, uint32_t y) {
    if (!vesa_is_initialized() || x >= mode_info->width || y >= mode_info->height) return 0;
    
    uint32_t offset = vesa_calculate_pixel_offset(x, y);
    uint8_t* pixel = framebuffer + offset;
    
    switch (bytes_per_pixel) {
        case 1: return pixel[0];
        case 2: return *(uint16_t*)pixel;
        case 3: return (pixel[2] << 16) | (pixel[1] << 8) | pixel[0];
        case 4: return *(uint32_t*)pixel;
        default: return 0;
    }
}

void vesa_clear(uint32_t color) {
    if (!vesa_is_initialized()) return;
    
    uint32_t screen_size = mode_info->pitch * mode_info->height;
    
    if (bytes_per_pixel == 4) {
        uint32_t* fb = (uint32_t*)framebuffer;
        uint32_t count = screen_size / 4;
        
        for (uint32_t i = 0; i < count; i++) fb[i] = color;
    } else {
        for (uint32_t y = 0; y < mode_info->height; y++) {
            for (uint32_t x = 0; x < mode_info->width; x++) {
                vesa_put_pixel(x, y, color);
            }
        }
    }
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!vesa_is_initialized()) return;
    
    uint32_t x_end = MIN(x + w, mode_info->width);
    uint32_t y_end = MIN(y + h, mode_info->height);
    
    if (x >= mode_info->width || y >= mode_info->height) return;
    
    if (bytes_per_pixel == 4) {
        for (uint32_t cy = y; cy < y_end; cy++) {
            uint32_t* line = (uint32_t*)(framebuffer + vesa_calculate_pixel_offset(x, cy));
            uint32_t width = x_end - x;
            
            for (uint32_t cx = 0; cx < width; cx++) {
                line[cx] = color;
            }
        }
    } else {
        for (uint32_t cy = y; cy < y_end; cy++) {
            for (uint32_t cx = x; cx < x_end; cx++) {
                vesa_put_pixel(cx, cy, color);
            }
        }
    }
}

void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness) {
    if (!vesa_is_initialized() || thickness == 0) return;
    
    vesa_fill_rect(x, y, w, thickness, color);
    vesa_fill_rect(x, y + h - thickness, w, thickness, color);
    vesa_fill_rect(x, y, thickness, h, color);
    vesa_fill_rect(x + w - thickness, y, thickness, h, color);
}

void vesa_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color) {
    if (!vesa_is_initialized()) return;
    
    int dx = ABS((int)x2 - (int)x1);
    int dy = ABS((int)y2 - (int)y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        vesa_put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void vesa_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color, bool filled) {
    if (!vesa_is_initialized() || radius == 0) return;
    
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        if (filled) {
            vesa_draw_line(cx - x, cy + y, cx + x, cy + y, color);
            vesa_draw_line(cx - x, cy - y, cx + x, cy - y, color);
            vesa_draw_line(cx - y, cy + x, cx + y, cy + x, color);
            vesa_draw_line(cx - y, cy - x, cx + y, cy - x, color);
        } else {
            vesa_put_pixel(cx + x, cy + y, color);
            vesa_put_pixel(cx + y, cy + x, color);
            vesa_put_pixel(cx - y, cy + x, color);
            vesa_put_pixel(cx - x, cy + y, color);
            vesa_put_pixel(cx - x, cy - y, color);
            vesa_put_pixel(cx - y, cy - x, color);
            vesa_put_pixel(cx + y, cy - x, color);
            vesa_put_pixel(cx + x, cy - y, color);
        }
        
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        } if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void vesa_draw_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, uint32_t color, bool filled) {
    if (!vesa_is_initialized()) return;
    
    if (filled) {
        if (y1 > y2) { SWAP(x1, x2); SWAP(y1, y2); }
        if (y1 > y3) { SWAP(x1, x3); SWAP(y1, y3); }
        if (y2 > y3) { SWAP(x2, x3); SWAP(y2, y3); }
        
        for (uint32_t y = y1; y <= y3; y++) {
            int xa, xb;
            
            if (y < y2) {
                xa = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
                xb = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
            } else {
                if (y2 == y3) xa = x2;
                else xa = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
                xb = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
            }
            
            if (xa > xb) SWAP(xa, xb);
            vesa_draw_line(xa, y, xb, y, color);
        }
    } else {
        vesa_draw_line(x1, y1, x2, y2, color);
        vesa_draw_line(x2, y2, x3, y3, color);
        vesa_draw_line(x3, y3, x1, y1, color);
    }
}

void vesa_copy_rect(uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h) {
    if (!vesa_is_initialized()) return;
    
    if (src_x >= mode_info->width || src_y >= mode_info->height ||
        dst_x >= mode_info->width || dst_y >= mode_info->height) return;
    
    uint32_t copy_w = MIN(w, mode_info->width - MAX(src_x, dst_x));
    uint32_t copy_h = MIN(h, mode_info->height - MAX(src_y, dst_y));
    
    if (copy_w == 0 || copy_h == 0) return;
    
    for (uint32_t y = 0; y < copy_h; y++) {
        uint32_t src_offset = vesa_calculate_pixel_offset(src_x, src_y + y);
        uint32_t dst_offset = vesa_calculate_pixel_offset(dst_x, dst_y + y);
        
        memcpy(framebuffer + dst_offset, framebuffer + src_offset, copy_w * bytes_per_pixel);
    }
}

void vesa_scroll(int32_t dx, int32_t dy, uint32_t fill_color) {
    if (!vesa_is_initialized() || (dx == 0 && dy == 0)) return;
    
    if (dx == 0) {
        uint32_t abs_dy = ABS(dy);
        if ((uint32_t)abs_dy >= mode_info->height) {
            vesa_clear(fill_color);
            return;
        }
        
        if (dy > 0) {
            for (uint32_t y = mode_info->height - 1; y >= (uint32_t)dy; y--) {
                memcpy(framebuffer + vesa_calculate_pixel_offset(0, y),
                       framebuffer + vesa_calculate_pixel_offset(0, y - dy),
                       mode_info->width * bytes_per_pixel);
            }
        } else {
            for (uint32_t y = 0; y < mode_info->height - abs_dy; y++) {
                memcpy(framebuffer + vesa_calculate_pixel_offset(0, y),
                       framebuffer + vesa_calculate_pixel_offset(0, y + abs_dy),
                       mode_info->width * bytes_per_pixel);
            }
        }
        
        if (dy > 0) vesa_fill_rect(0, 0, mode_info->width, dy, fill_color);
        else vesa_fill_rect(0, mode_info->height - abs_dy, mode_info->width, abs_dy, fill_color);
    }
}

uint32_t vesa_get_width(void) {
    return initialized ? mode_info->width : 0;
}

uint32_t vesa_get_height(void) {
    return initialized ? mode_info->height : 0;
}

uint32_t vesa_get_pitch(void) {
    return initialized ? mode_info->pitch : 0;
}

uint32_t vesa_get_bpp(void) {
    return initialized ? mode_info->bpp : 0;
}

uint32_t vesa_get_framebuffer(void) {
    return (uint32_t)(uintptr_t)framebuffer;
}

uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

uint32_t vesa_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void vesa_extract_color(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (a) *a = (color >> 24) & 0xFF;
    if (r) *r = (color >> 16) & 0xFF;
    if (g) *g = (color >> 8) & 0xFF;
    if (b) *b = color & 0xFF;
}