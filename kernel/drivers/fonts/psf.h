#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <drivers/filesystem/memfs.h>

typedef struct {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize;
} __attribute__((packed)) psf1_header_t;

typedef struct {
    uint8_t magic[4];
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t numglyphs;
    uint32_t bytesperglyph;
    uint32_t height;
    uint32_t width;
} __attribute__((packed)) psf2_header_t;

typedef struct {
    uint8_t version;
    uint32_t num_glyphs;
    uint32_t glyph_size;
    uint32_t height;
    uint32_t width;
    uint8_t* data;
    bool has_unicode_table;
    uint32_t* unicode_table;
} psf_font_t;

psf_font_t* psf_load_from_memfs(memfs *fs, const char* path);
void psf_free_font(psf_font_t* font);
bool psf_verify_magic(const uint8_t* data);
psf_font_t* psf_parse_data(uint8_t* data, size_t size);