#include <drivers/fonts/psf.h>
#include <drivers/filesystem/memfs.h>
#include <drivers/filesystem/initramfs.h>
#include <drivers/vesa.h>
#include <drivers/serial.h>
#include <string.h>
#include <asm/mm.h>

bool psf_verify_magic(const uint8_t* data) {
    if (data[0] == 0x36 && data[1] == 0x04) return true;
    if (data[0] == 0x72 && data[1] == 0xb5 && 
        data[2] == 0x4a && data[3] == 0x86) return true;
    return false;
}

static void psf_fill_from_psf1(psf_font_t *font, psf1_header_t *header, uint8_t *base) {
    font->version = 1;
    font->num_glyphs = (header->mode & 0x01) ? 512u : 256u;
    font->glyph_size = header->charsize;
    font->height = header->charsize;
    font->width = 8;
    font->data = base + sizeof(psf1_header_t);
}

static void psf_fill_from_psf2(psf_font_t *font, psf2_header_t *header, uint8_t *base) {
    font->version = 2;
    font->num_glyphs = header->numglyphs;
    font->glyph_size = header->bytesperglyph;
    font->height = header->height;
    font->width = header->width;
    font->data = base + header->headersize;
    font->has_unicode_table = (header->flags & 0x01) != 0;
    if (font->has_unicode_table) {
        uint8_t *table_start = font->data + (font->num_glyphs * font->glyph_size);
        font->unicode_table = (uint32_t*)table_start;
    }
}

static int psf_try_info(memfs *fs, const char *path, memfs_inode *info) {
    if (!path || path[0] != '/') return -1;
    if (memfs_get_info(fs, path, info) != 0) return -1;
    if (info->type != MEMFS_TYPE_FILE || info->file.size == 0) return -1;
    return 0;
}

static bool psf_payload_is_valid(const psf_font_t *font, size_t total_size) {
    size_t data_off;
    size_t glyph_bytes;
    if (!font || !font->data) return false;
    if (font->width == 0 || font->height == 0) return false;
    if (font->num_glyphs == 0 || font->glyph_size == 0) return false;

    data_off = (size_t)(font->data - font->raw_data);
    glyph_bytes = (size_t)font->num_glyphs * (size_t)font->glyph_size;
    if (glyph_bytes > total_size) return false;
    if (data_off > total_size - glyph_bytes) return false;
    if (font->glyph_size < (((size_t)font->width + 7u) / 8u) * (size_t)font->height) return false;
    return true;
}

psf_font_t* psf_parse_data(uint8_t* data, size_t size) {
    if (!data || size < 4) return NULL;
    if (!psf_verify_magic(data)) return NULL;
    
    psf_font_t* font = (psf_font_t*)valloc(sizeof(psf_font_t));
    if (!font) return NULL;
    
    memset(font, 0, sizeof(psf_font_t));
    
    font->raw_data = data;
    if (data[0] == 0x36 && data[1] == 0x04) {
        psf1_header_t* header = (psf1_header_t*)data;
        if (size < sizeof(psf1_header_t)) {
            vfree(font);
            return NULL;
        }
        psf_fill_from_psf1(font, header, data);
    } else {
        psf2_header_t* header = (psf2_header_t*)data;
        if (size < sizeof(psf2_header_t) || header->headersize > size) {
            vfree(font);
            return NULL;
        }
        psf_fill_from_psf2(font, header, data);
    }
    if (!psf_payload_is_valid(font, size)) {
        vfree(font);
        return NULL;
    }
    
    return font;
}

psf_font_t* psf_load_from_memfs(memfs* fs, const char* path) {
    const char *chosen_path = NULL;
    uint8_t *font_data = NULL;
    size_t font_size = 0;
    if (!fs || !path) {
        serial_write(SERIAL_COM1, "psf: bad args\n");
        return NULL;
    }
    
    memfs_inode info;
    if (psf_try_info(fs, path, &info) == 0) {
        chosen_path = path;
        serial_write(SERIAL_COM1, "psf: source=memfs path=");
        serial_write(SERIAL_COM1, chosen_path);
        serial_write(SERIAL_COM1, "\n");
    }

    if (!chosen_path) {
        serial_write(SERIAL_COM1, "psf: source=none path=");
        serial_write(SERIAL_COM1, path);
        serial_write(SERIAL_COM1, "\n");
        return NULL;
    }
    
    if (info.type != MEMFS_TYPE_FILE) {
        serial_write(SERIAL_COM1, "psf: not file\n");
        return NULL;
    }
    
    font_data = (uint8_t*)valloc(info.file.size);
    if (!font_data) {
        serial_write(SERIAL_COM1, "psf: alloc data fail\n");
        return NULL;
    }
    
    if (info.file.data) {
        memcpy(font_data, info.file.data, info.file.size);
    } else {
        ssize_t bytes_read;
        if (!chosen_path) {
            serial_write(SERIAL_COM1, "psf: read path missing\n");
            vfree(font_data);
            return NULL;
        }
        bytes_read = memfs_read(fs, chosen_path, font_data, info.file.size);
        if (bytes_read < 0 || (size_t)bytes_read != info.file.size) {
            serial_write(SERIAL_COM1, "psf: read fail\n");
            vfree(font_data);
            return NULL;
        }
    }
    font_size = info.file.size;

    if (font_size < 4 || !psf_verify_magic(font_data)) {
        serial_write(SERIAL_COM1, "psf: bad magic\n");
        vfree(font_data);
        return NULL;
    }
    
    psf_font_t* font = (psf_font_t*)valloc(sizeof(psf_font_t));
    if (!font) {
        serial_write(SERIAL_COM1, "psf: alloc font fail\n");
        vfree(font_data);
        return NULL;
    }
    
    memset(font, 0, sizeof(psf_font_t));
    
    if (font_data[0] == 0x36 && font_data[1] == 0x04) {
        psf1_header_t* header = (psf1_header_t*)font_data;
        if (font_size < sizeof(psf1_header_t)) {
            serial_write(SERIAL_COM1, "psf: short psf1\n");
            vfree(font_data);
            vfree(font);
            return NULL;
        }
        psf_fill_from_psf1(font, header, font_data);
    } else {
        psf2_header_t* header = (psf2_header_t*)font_data;
        if (font_size < sizeof(psf2_header_t) || header->headersize > font_size) {
            serial_write(SERIAL_COM1, "psf: short psf2\n");
            vfree(font_data);
            vfree(font);
            return NULL;
        }
        psf_fill_from_psf2(font, header, font_data);
    }
    font->raw_data = font_data;
    if (!psf_payload_is_valid(font, font_size)) {
        serial_write(SERIAL_COM1, "psf: bad payload\n");
        vfree(font_data);
        vfree(font);
        return NULL;
    }
    
    return font;
}

void psf_free_font(psf_font_t* font) {
    if (!font) return;
    
    if (font->raw_data) vfree(font->raw_data);
    
    vfree(font);
}
