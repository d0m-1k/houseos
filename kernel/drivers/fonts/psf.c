#include <drivers/fonts/psf.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/vesa.h>
#include <string.h>
#include <asm/mm.h>

bool psf_verify_magic(const uint8_t* data) {
    if (data[0] == 0x36 && data[1] == 0x04) return true;
    if (data[0] == 0x72 && data[1] == 0xb5 && 
        data[2] == 0x4a && data[3] == 0x86) return true;
    return false;
}

psf_font_t* psf_parse_data(uint8_t* data, size_t size) {
    if (!data || size < 4) return NULL;
    if (!psf_verify_magic(data)) return NULL;
    
    psf_font_t* font = (psf_font_t*)valloc(sizeof(psf_font_t));
    if (!font) return NULL;
    
    memset(font, 0, sizeof(psf_font_t));
    
    if (data[0] == 0x36 && data[1] == 0x04) {
        psf1_header_t* header = (psf1_header_t*)data;
        font->version = 1;
        font->num_glyphs = (header->mode == 0) ? 512 : 256;
        font->glyph_size = header->charsize;
        font->height = header->charsize * 8;
        font->width = 8;
        font->data = data + sizeof(psf1_header_t);
        font->has_unicode_table = false;
    } else {
        psf2_header_t* header = (psf2_header_t*)data;
        font->version = 2;
        font->num_glyphs = header->numglyphs;
        font->glyph_size = header->bytesperglyph;
        font->height = header->height;
        font->width = header->width;
        font->data = data + header->headersize;
        
        font->has_unicode_table = (header->flags & 0x01) != 0;
        
        if (font->has_unicode_table) {
            uint8_t* table_start = font->data + (font->num_glyphs * font->glyph_size);
            font->unicode_table = (uint32_t*)table_start;
        }
    }
    
    return font;
}

psf_font_t* psf_load_from_memfs(memfs* fs, const char* path) {
    if (!fs || !path) return NULL;
    
    memfs_inode info;
    if (memfs_get_info(fs, path, &info) != 0) return NULL;
    
    if (info.type != MEMFS_TYPE_FILE) return NULL;
    
    uint8_t* font_data = (uint8_t*)valloc(info.file.size);
    if (!font_data) return NULL;
    
    ssize_t bytes_read = memfs_read(fs, path, font_data, info.file.size);
    if (bytes_read != info.file.size) {
        vfree(font_data);
        return NULL;
    }
    
    if (!psf_verify_magic(font_data)) {
        vfree(font_data);
        return NULL;
    }
    
    psf_font_t* font = (psf_font_t*)valloc(sizeof(psf_font_t));
    if (!font) {
        vfree(font_data);
        return NULL;
    }
    
    memset(font, 0, sizeof(psf_font_t));
    
    if (font_data[0] == 0x36 && font_data[1] == 0x04) {
        psf1_header_t* header = (psf1_header_t*)font_data;
        font->version = 1;
        font->num_glyphs = (header->mode == 0) ? 512 : 256;
        font->glyph_size = header->charsize;
        font->height = 16;
        font->width = 8;
        font->data = font_data + sizeof(psf1_header_t);
    } else {
        psf2_header_t* header = (psf2_header_t*)font_data;
        font->version = 2;
        font->num_glyphs = header->numglyphs;
        font->glyph_size = header->bytesperglyph;
        font->height = header->height;
        font->width = header->width;
        font->data = font_data + header->headersize;
    }
    
    return font;
}

void psf_free_font(psf_font_t* font) {
    if (!font) return;
    
    if (font->data) {
        uint8_t* base_ptr;
        if (font->version == 1) base_ptr = font->data - sizeof(psf1_header_t);
        else base_ptr = font->data - ((psf2_header_t*)font->data - sizeof(psf2_header_t))->headersize;
        vfree(base_ptr);
    }
    
    vfree(font);
}