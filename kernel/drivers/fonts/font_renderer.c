#include <drivers/fonts/font_renderer.h>
#include <string.h>
#include <asm/mm.h>

const uint8_t* font_get_glyph(psf_font_t* font, char c) {
    if (!font || !font->data) return NULL;
    
    unsigned char uc = (unsigned char)c;
    
    if (uc >= font->num_glyphs) return font->data;
    
    return font->data + (uc * font->glyph_size);
}

void font_draw_char(psf_font_t* font, uint32_t x, uint32_t y, char c, uint32_t color) {
    if (!font || !vesa_is_initialized()) return;
    
    const uint8_t* glyph = font_get_glyph(font, c);
    if (!glyph) return;
    
    uint32_t width = font->width;
    uint32_t height = font->height;
    
    uint32_t bytes_per_line = (width + 7) / 8;
    
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            uint32_t byte_offset = row * bytes_per_line + (col / 8);
            uint8_t bit_mask = 0x80 >> (col % 8);
            
            uint8_t pixel_byte = glyph[byte_offset];
            bool pixel_on = (pixel_byte & bit_mask) != 0;
            
            if (pixel_on) vesa_put_pixel(x + col, y + row, color);
        }
    }
}

void font_draw_string(psf_font_t* font, uint32_t x, uint32_t y, const char* str, uint32_t fg_color) {
    if (!font || !str || !vesa_is_initialized()) return;
    
    uint32_t current_x = x;
    uint32_t current_y = y;
    uint32_t char_width = font->width;
    uint32_t char_height = font->height;
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    for (const char* ptr = str; *ptr != '\0'; ptr++) {
        char c = *ptr;
        
        switch (c) {
            case '\n':
                current_x = x;
                current_y += char_height;
                break;
                
            case '\r':
                current_x = x;
                break;
                
            case '\t':
                current_x = ((current_x - x) / (char_width * 4) + 1) * (char_width * 4) + x;
                break;
                
            case '\b':
                if (current_x > x + char_width) {
                    current_x -= char_width;
                    font_draw_char(font, current_x, current_y, ' ', fg_color);
                }
                break;
                
            default:
                if (current_x + char_width > screen_width) {
                    current_x = x;
                    current_y += char_height;
                }
                
                if (current_y + char_height > screen_height) return;
                
                font_draw_char(font, current_x, current_y, c, fg_color);
                current_x += char_width;
                break;
        }
    }
}