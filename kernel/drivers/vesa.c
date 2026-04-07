#include <drivers/vesa.h>
#include <string.h>
#include <stddef.h>

static vesa_info_t* vesa_info = (vesa_info_t*)VESA_INFO_ADDR;
static vesa_mode_info_t* mode_info = (vesa_mode_info_t*)MODE_INFO_ADDR;
static uint8_t* framebuffer = NULL;
static bool initialized = false;
static uint32_t bytes_per_pixel = 0;
#ifndef CONFIG_VESA_ROTATION
#define CONFIG_VESA_ROTATION 0
#endif
static uint32_t rotation_deg = 0;

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define SWAP(a, b) { typeof(a) temp = a; a = b; b = temp; }
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

static inline uint32_t scale_component_to_mask(uint8_t value, uint8_t mask_bits) {
    uint32_t maxv;
    if (mask_bits == 0u) return 0u;
    if (mask_bits >= 8u) return (uint32_t)value;
    maxv = (1u << mask_bits) - 1u;
    return ((uint32_t)value * maxv + 127u) / 255u;
}

static inline uint8_t scale_component_from_mask(uint32_t value, uint8_t mask_bits) {
    uint32_t maxv;
    if (mask_bits == 0u) return 0u;
    if (mask_bits >= 8u) return (uint8_t)value;
    maxv = (1u << mask_bits) - 1u;
    return (uint8_t)((value * 255u + (maxv / 2u)) / maxv);
}

static inline uint32_t vesa_pack_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t packed = 0u;
    packed |= (scale_component_to_mask(r, mode_info->red_mask) & ((1u << mode_info->red_mask) - 1u))
              << mode_info->red_position;
    packed |= (scale_component_to_mask(g, mode_info->green_mask) & ((1u << mode_info->green_mask) - 1u))
              << mode_info->green_position;
    packed |= (scale_component_to_mask(b, mode_info->blue_mask) & ((1u << mode_info->blue_mask) - 1u))
              << mode_info->blue_position;
    if (mode_info->reserved_mask != 0u) {
        packed |= (scale_component_to_mask(a, mode_info->reserved_mask) & ((1u << mode_info->reserved_mask) - 1u))
                  << mode_info->reserved_position;
    }
    return packed;
}

static inline void vesa_unpack_color(uint32_t packed, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    uint32_t rm = (mode_info->red_mask != 0u) ? ((1u << mode_info->red_mask) - 1u) : 0u;
    uint32_t gm = (mode_info->green_mask != 0u) ? ((1u << mode_info->green_mask) - 1u) : 0u;
    uint32_t bm = (mode_info->blue_mask != 0u) ? ((1u << mode_info->blue_mask) - 1u) : 0u;
    uint32_t am = (mode_info->reserved_mask != 0u) ? ((1u << mode_info->reserved_mask) - 1u) : 0u;
    if (r) *r = scale_component_from_mask((packed >> mode_info->red_position) & rm, mode_info->red_mask);
    if (g) *g = scale_component_from_mask((packed >> mode_info->green_position) & gm, mode_info->green_mask);
    if (b) *b = scale_component_from_mask((packed >> mode_info->blue_position) & bm, mode_info->blue_mask);
    if (a) *a = scale_component_from_mask((packed >> mode_info->reserved_position) & am, mode_info->reserved_mask);
}

bool vesa_init(void) {
    uint32_t min_pitch;
    if (strncmp(vesa_info->signature, "VESA", 4) != 0) return false;
    if ((mode_info->attributes & 0x0001u) == 0u) return false; 
    if ((mode_info->attributes & 0x0010u) == 0u) return false; 
    if ((mode_info->attributes & 0x0080u) == 0u) return false; 
    if (mode_info->width == 0 || mode_info->height == 0) return false;
    if (mode_info->bpp < 8 || mode_info->bpp > 32) return false;
    if (mode_info->memory_model != 4u && mode_info->memory_model != 6u) return false;
    if (mode_info->framebuffer < 0x00100000u) return false;

    bytes_per_pixel = mode_info->bpp / 8;
    if (bytes_per_pixel == 0 || bytes_per_pixel > 4) return false;
    min_pitch = (uint32_t)mode_info->width * bytes_per_pixel;
    if (mode_info->pitch < min_pitch) return false;

    framebuffer = (uint8_t*)(uintptr_t)mode_info->framebuffer;
    initialized = true;
    return true;
}

bool vesa_is_initialized(void) {
    return initialized && framebuffer != NULL;
}

static uint32_t vesa_phys_width(void) {
    return mode_info->width;
}

static uint32_t vesa_phys_height(void) {
    return mode_info->height;
}

static uint32_t vesa_logical_width(void) {
    if (!initialized) return 0;
    if (rotation_deg == 90u || rotation_deg == 270u) return vesa_phys_height();
    return vesa_phys_width();
}

static uint32_t vesa_logical_height(void) {
    if (!initialized) return 0;
    if (rotation_deg == 90u || rotation_deg == 270u) return vesa_phys_width();
    return vesa_phys_height();
}

static void vesa_map_xy(uint32_t x, uint32_t y, uint32_t *px, uint32_t *py) {
    uint32_t w = vesa_phys_width();
    uint32_t h = vesa_phys_height();
    switch (rotation_deg) {
        case 90u:
            *px = y;
            *py = (h - 1u) - x;
            break;
        case 180u:
            *px = (w - 1u) - x;
            *py = (h - 1u) - y;
            break;
        case 270u:
            *px = (w - 1u) - y;
            *py = x;
            break;
        default:
            *px = x;
            *py = y;
            break;
    }
}

bool vesa_set_rotation(uint32_t degrees) {
#if CONFIG_VESA_ROTATION
    if (degrees == 0u || degrees == 90u || degrees == 180u || degrees == 270u) {
        rotation_deg = degrees;
        return true;
    }
    return false;
#else
    if (degrees == 0u) return true;
    return false;
#endif
}

uint32_t vesa_get_rotation(void) {
    return rotation_deg;
}

uint32_t vesa_calculate_pixel_offset(uint32_t x, uint32_t y) {
    uint32_t px = x;
    uint32_t py = y;
    vesa_map_xy(x, y, &px, &py);
    return py * mode_info->pitch + px * bytes_per_pixel;
}

void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t px;
    uint32_t py;
    uint32_t packed;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    if (!vesa_is_initialized() || x >= vesa_logical_width() || y >= vesa_logical_height()) return;
    vesa_map_xy(x, y, &px, &py);
    
    uint32_t offset = py * mode_info->pitch + px * bytes_per_pixel;
    uint8_t* pixel = framebuffer + offset;
    r = (uint8_t)((color >> 16) & 0xFFu);
    g = (uint8_t)((color >> 8) & 0xFFu);
    b = (uint8_t)(color & 0xFFu);
    a = (uint8_t)((color >> 24) & 0xFFu);
    packed = vesa_pack_color(r, g, b, a);
    
    switch (bytes_per_pixel) {
        case 1:
            pixel[0] = (uint8_t)packed;
            break;
        case 2:
            *(uint16_t*)pixel = (uint16_t)packed;
            break;
        case 3:
            pixel[0] = (uint8_t)(packed & 0xFFu);
            pixel[1] = (uint8_t)((packed >> 8) & 0xFFu);
            pixel[2] = (uint8_t)((packed >> 16) & 0xFFu);
            break;
        case 4:
            *(uint32_t*)pixel = packed;
            break;
    }
}

uint32_t vesa_get_pixel(uint32_t x, uint32_t y) {
    uint32_t px;
    uint32_t py;
    uint32_t packed = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    if (!vesa_is_initialized() || x >= vesa_logical_width() || y >= vesa_logical_height()) return 0;
    vesa_map_xy(x, y, &px, &py);
    
    uint32_t offset = py * mode_info->pitch + px * bytes_per_pixel;
    uint8_t* pixel = framebuffer + offset;
    
    switch (bytes_per_pixel) {
        case 1:
            packed = pixel[0];
            break;
        case 2:
            packed = *(uint16_t*)pixel;
            break;
        case 3:
            packed = (uint32_t)pixel[0] | ((uint32_t)pixel[1] << 8) | ((uint32_t)pixel[2] << 16);
            break;
        case 4:
            packed = *(uint32_t*)pixel;
            break;
        default: return 0;
    }
    vesa_unpack_color(packed, &r, &g, &b, &a);
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void vesa_clear(uint32_t color) {
    uint32_t packed;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    if (!vesa_is_initialized()) return;
    r = (uint8_t)((color >> 16) & 0xFFu);
    g = (uint8_t)((color >> 8) & 0xFFu);
    b = (uint8_t)(color & 0xFFu);
    a = (uint8_t)((color >> 24) & 0xFFu);
    packed = vesa_pack_color(r, g, b, a);
    
    uint32_t screen_size = mode_info->pitch * mode_info->height;
    if (rotation_deg == 0u && bytes_per_pixel == 4) {
        uint32_t* fb = (uint32_t*)framebuffer;
        uint32_t count = screen_size / 4;
        
        for (uint32_t i = 0; i < count; i++) fb[i] = packed;
    } else {
        for (uint32_t y = 0; y < vesa_logical_height(); y++) {
            for (uint32_t x = 0; x < vesa_logical_width(); x++) {
                vesa_put_pixel(x, y, color);
            }
        }
    }
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t lw;
    uint32_t lh;
    uint32_t x_end;
    uint32_t y_end;
    uint32_t packed;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    if (!vesa_is_initialized()) return;
    r = (uint8_t)((color >> 16) & 0xFFu);
    g = (uint8_t)((color >> 8) & 0xFFu);
    b = (uint8_t)(color & 0xFFu);
    a = (uint8_t)((color >> 24) & 0xFFu);
    packed = vesa_pack_color(r, g, b, a);
    lw = vesa_logical_width();
    lh = vesa_logical_height();
    x_end = MIN(x + w, lw);
    y_end = MIN(y + h, lh);
    if (x >= lw || y >= lh) return;
    if (rotation_deg == 0u && bytes_per_pixel == 4) {
        for (uint32_t cy = y; cy < y_end; cy++) {
            uint32_t* line = (uint32_t*)(framebuffer + vesa_calculate_pixel_offset(x, cy));
            uint32_t width = x_end - x;
            
            for (uint32_t cx = 0; cx < width; cx++) {
                line[cx] = packed;
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
    uint32_t lw;
    uint32_t lh;
    uint32_t copy_w;
    uint32_t copy_h;
    if (!vesa_is_initialized()) return;
    lw = vesa_logical_width();
    lh = vesa_logical_height();
    if (src_x >= lw || src_y >= lh || dst_x >= lw || dst_y >= lh) return;
    copy_w = MIN(w, lw - MAX(src_x, dst_x));
    copy_h = MIN(h, lh - MAX(src_y, dst_y));
    if (copy_w == 0 || copy_h == 0) return;
    if (rotation_deg == 0u) {
        for (uint32_t y = 0; y < copy_h; y++) {
            uint32_t src_offset = vesa_calculate_pixel_offset(src_x, src_y + y);
            uint32_t dst_offset = vesa_calculate_pixel_offset(dst_x, dst_y + y);
            memcpy(framebuffer + dst_offset, framebuffer + src_offset, copy_w * bytes_per_pixel);
        }
    } else {
        for (uint32_t y = 0; y < copy_h; y++) {
            for (uint32_t x = 0; x < copy_w; x++) {
                uint32_t c = vesa_get_pixel(src_x + x, src_y + y);
                vesa_put_pixel(dst_x + x, dst_y + y, c);
            }
        }
    }
}

void vesa_scroll(int32_t dx, int32_t dy, uint32_t fill_color) {
    uint32_t lw;
    uint32_t lh;
    if (!vesa_is_initialized() || (dx == 0 && dy == 0)) return;
    lw = vesa_logical_width();
    lh = vesa_logical_height();
    
    if (dx == 0) {
        uint32_t abs_dy = ABS(dy);
        if ((uint32_t)abs_dy >= lh) {
            vesa_clear(fill_color);
            return;
        }
        if (rotation_deg == 0u) {
            if (dy > 0) {
                for (uint32_t y = lh - 1; y >= (uint32_t)dy; y--) {
                    memcpy(framebuffer + vesa_calculate_pixel_offset(0, y),
                           framebuffer + vesa_calculate_pixel_offset(0, y - dy),
                           lw * bytes_per_pixel);
                }
            } else {
                for (uint32_t y = 0; y < lh - abs_dy; y++) {
                    memcpy(framebuffer + vesa_calculate_pixel_offset(0, y),
                           framebuffer + vesa_calculate_pixel_offset(0, y + abs_dy),
                           lw * bytes_per_pixel);
                }
            }
        } else {
            if (dy > 0) {
                for (uint32_t y = lh - 1; y >= (uint32_t)dy; y--) {
                    for (uint32_t x = 0; x < lw; x++) {
                        vesa_put_pixel(x, y, vesa_get_pixel(x, y - dy));
                    }
                }
            } else {
                for (uint32_t y = 0; y < lh - abs_dy; y++) {
                    for (uint32_t x = 0; x < lw; x++) {
                        vesa_put_pixel(x, y, vesa_get_pixel(x, y + abs_dy));
                    }
                }
            }
        }
        
        if (dy > 0) vesa_fill_rect(0, 0, lw, dy, fill_color);
        else vesa_fill_rect(0, lh - abs_dy, lw, abs_dy, fill_color);
    }
}

uint32_t vesa_get_width(void) {
    return vesa_logical_width();
}

uint32_t vesa_get_height(void) {
    return vesa_logical_height();
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
