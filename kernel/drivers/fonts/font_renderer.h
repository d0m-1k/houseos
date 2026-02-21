#pragma once

#include <drivers/fonts/psf.h>
#include <drivers/vesa.h>
#include <stdint.h>

const uint8_t* font_get_glyph(psf_font_t* font, char c);
void font_draw_char(psf_font_t* font, uint32_t x, uint32_t y, char c, uint32_t color);
void font_draw_string(psf_font_t* font, uint32_t x, uint32_t y, const char* str, uint32_t color);