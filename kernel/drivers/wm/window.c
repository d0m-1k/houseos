#include <drivers/wm/window.h>
#include <asm/mm.h>
#include <string.h>

static uint32_t g_window_id = 1;
static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t calc_canvas_width(uint32_t width) {
    if (width <= 2) return 1;
    return width - 2;
}

static uint32_t calc_canvas_height(uint32_t height) {
    if (height <= 2) return 1;
    return height - 2;
}

static bool canvas_alloc(window_t *win, uint32_t width, uint32_t height, bool preserve_content) {
    if (!win) return false;

    uint32_t *old_canvas = win->canvas;
    uint32_t old_cw = win->canvas_width;
    uint32_t old_ch = win->canvas_height;

    uint32_t cw = calc_canvas_width(width);
    uint32_t ch = calc_canvas_height(height);
    uint32_t count = cw * ch;

    if (count == 0) return false;

    if (!preserve_content && old_canvas) {
        // Live-resize fast path: drop old canvas first to avoid peak memory (old + new).
        kfree(old_canvas);
        old_canvas = NULL;
        win->canvas = NULL;
        win->canvas_width = 0;
        win->canvas_height = 0;
    }

    uint32_t *new_canvas = (uint32_t*)kmalloc(count * sizeof(uint32_t));
    if (!new_canvas) return false;

    memset(new_canvas, 0, count * sizeof(uint32_t));

    if (old_canvas && preserve_content) {
        uint32_t copy_w = min_u32(old_cw, cw);
        uint32_t copy_h = min_u32(old_ch, ch);
        for (uint32_t y = 0; y < copy_h; y++) {
            memcpy(new_canvas + y * cw, old_canvas + y * old_cw, copy_w * sizeof(uint32_t));
        }
    }
    if (old_canvas) kfree(old_canvas);

    win->canvas = new_canvas;
    win->canvas_width = cw;
    win->canvas_height = ch;
    win->canvas_dirty = true;
    return true;
}

window_t *window_create(int32_t x, int32_t y, uint32_t width, uint32_t height, const char *title) {
    if (width < 80 || height < 25) return NULL;

    window_t *win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;

    memset(win, 0, sizeof(window_t));

    win->id = g_window_id++;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->visible = true;
    win->draggable = true;
    win->focused = false;

    win->bg_color = 0x001d1f22;
    win->border_color = 0x00a8b0b8;
    win->title_bg_color = 0x002a2f36;
    win->title_fg_color = 0x00f2f4f8;
    win->body_fg_color = 0x00e4e9ef;
    win->can_close = true;
    win->can_maximize = true;
    win->resizable = false;
    win->title_enabled = true;
    win->maximized = false;
    win->restore_x = x;
    win->restore_y = y;
    win->restore_width = width;
    win->restore_height = height;

    window_set_title(win, title ? title : "window");
    win->canvas = NULL;
    win->canvas_width = 0;
    win->canvas_height = 0;
    win->canvas_dirty = true;
    win->caret_x = 0;
    win->caret_y = 0;
    win->font = NULL;

    if (!canvas_alloc(win, width, height, true)) {
        kfree(win);
        return NULL;
    }

    return win;
}

void window_destroy(window_t *win) {
    if (!win) return;
    if (win->canvas) kfree(win->canvas);
    kfree(win);
}

bool window_resize(window_t *win, uint32_t width, uint32_t height) {
    return window_resize_ex(win, width, height, true);
}

bool window_resize_ex(window_t *win, uint32_t width, uint32_t height, bool preserve_content) {
    if (!win || width < 80 || height < 25) return false;
    if (!canvas_alloc(win, width, height, preserve_content)) return false;

    win->width = width;
    win->height = height;
    return true;
}

void window_set_title(window_t *win, const char *title) {
    if (!win) return;

    if (!title) {
        win->title[0] = '\0';
        return;
    }

    strncpy(win->title, title, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = '\0';
}

void window_enable_title(window_t *win) {
    if (!win) return;
    win->title_enabled = true;
    win->canvas_dirty = true;
}

void window_disable_title(window_t *win) {
    if (!win) return;
    win->title_enabled = false;
    win->canvas_dirty = true;
}

void window_diable_title(window_t *win) {
    window_disable_title(win);
}

void window_set_handlers(window_t *win, window_on_close_t on_close, window_on_focus_t on_focus, window_on_key_t on_key, window_on_mouse_t on_mouse, window_on_draw_t on_draw, void *user_data) {
    if (!win) return;
    win->on_close = on_close;
    win->on_focus = on_focus;
    win->on_key = on_key;
    win->on_mouse = on_mouse;
    win->on_draw = on_draw;
    win->user_data = user_data;
}

void window_update(window_t *win) {
    if (!win) return;
    win->canvas_dirty = true;
}

void window_draw_pixel(window_t *win, int32_t x, int32_t y, uint32_t color) {
    if (!win || !win->canvas) return;
    if (x < 0 || y < 0 || (uint32_t)x >= win->canvas_width || (uint32_t)y >= win->canvas_height) return;

    win->canvas[(uint32_t)y * win->canvas_width + (uint32_t)x] = color;
}

void window_fill_rect(window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win || !win->canvas || w == 0 || h == 0) return;

    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + (int32_t)w;
    int32_t y2 = y + (int32_t)h;

    if (x2 > (int32_t)win->canvas_width) x2 = (int32_t)win->canvas_width;
    if (y2 > (int32_t)win->canvas_height) y2 = (int32_t)win->canvas_height;
    if (x1 >= x2 || y1 >= y2) return;

    for (int32_t cy = y1; cy < y2; cy++) {
        uint32_t *line = win->canvas + (uint32_t)cy * win->canvas_width;
        for (int32_t cx = x1; cx < x2; cx++) line[(uint32_t)cx] = color;
    }
}

void window_draw_rect(window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness) {
    if (!win || thickness == 0) return;
    window_fill_rect(win, x, y, w, thickness, color);
    window_fill_rect(win, x, y + (int32_t)h - (int32_t)thickness, w, thickness, color);
    window_fill_rect(win, x, y, thickness, h, color);
    window_fill_rect(win, x + (int32_t)w - (int32_t)thickness, y, thickness, h, color);
}

void window_draw_line(window_t *win, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
    if (!win) return;
    int32_t dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int32_t dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int32_t sx = x1 < x2 ? 1 : -1;
    int32_t sy = y1 < y2 ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        window_draw_pixel(win, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int32_t e2 = err * 2;
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

void window_clear(window_t *win, uint32_t color) {
    if (!win || !win->canvas) return;
    window_fill_rect(win, 0, 0, win->canvas_width, win->canvas_height, color);
}

void window_draw_char(window_t *win, int32_t x, int32_t y, char c, uint32_t color) {
    if (!win || !win->font || !win->font->data) return;

    unsigned char uc = (unsigned char)c;
    if (uc >= win->font->num_glyphs) uc = 0;

    const uint8_t *glyph = win->font->data + ((uint32_t)uc * win->font->glyph_size);
    uint32_t glyph_w = win->font->width;
    uint32_t glyph_h = win->font->height;
    uint32_t bpl = (glyph_w + 7) / 8;

    for (uint32_t row = 0; row < glyph_h; row++) {
        for (uint32_t col = 0; col < glyph_w; col++) {
            uint32_t boff = row * bpl + (col / 8);
            uint8_t mask = (uint8_t)(0x80 >> (col % 8));
            if (glyph[boff] & mask) {
                window_draw_pixel(win, x + (int32_t)col, y + (int32_t)row, color);
            }
        }
    }
}

void window_draw_string(window_t *win, int32_t x, int32_t y, const char *text, uint32_t color) {
    if (!win || !text || !win->font) return;

    int32_t cx = x;
    int32_t cy = y;
    int32_t cw = (int32_t)(win->font->width ? win->font->width : 8);
    int32_t ch = (int32_t)(win->font->height ? win->font->height : 16);

    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '\r') continue;
        if (c == '\n') {
            cx = x;
            cy += ch + 1;
            continue;
        }
        if (c == '\t') {
            cx += cw * 4;
            continue;
        }
        window_draw_char(win, cx, cy, c, color);
        cx += cw;
    }
}
