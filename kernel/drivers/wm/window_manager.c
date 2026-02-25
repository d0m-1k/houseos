#include <drivers/wm/window_manager.h>
#include <drivers/vesa.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <string.h>

#define WM_MAX_WINDOWS 16
#define WINDOW_PADDING 6
#define CURSOR_W 8
#define CURSOR_H 12
#define CURSOR_MAX_W 16
#define CURSOR_MAX_H 16
#define TITLE_BTN_GAP 4
#define TITLE_BTN_MIN 10
#define TITLE_BTN_MAX 16
#define WM_HOT __attribute__((optimize("O3")))

volatile bool wm_in_frame = false;

static uint8_t *g_fb = NULL;
static uint8_t *g_fb_hw = NULL;
static uint8_t *g_backbuffer = NULL;
static uint32_t g_width = 0;
static uint32_t g_height = 0;
static uint32_t g_pitch = 0;
static uint32_t g_bpp_bytes = 0;
static uint32_t g_fb_size = 0;
static uint32_t g_theme_desktop_color = 0x00ffffff;      // #FFFFFF
static uint32_t g_theme_window_focus_color = 0x00000000; // #000000
static uint32_t g_theme_window_blur_color = 0x00000000;  // #000000
static uint32_t g_btn_close_pixels[CURSOR_MAX_W * CURSOR_MAX_H];
static uint8_t g_btn_close_w = 0;
static uint8_t g_btn_close_h = 0;
static bool g_btn_close_custom = false;
static uint32_t g_btn_full_pixels[CURSOR_MAX_W * CURSOR_MAX_H];
static uint8_t g_btn_full_w = 0;
static uint8_t g_btn_full_h = 0;
static bool g_btn_full_custom = false;

static uint32_t wm_title_height(const wm_t *wm);
static void wm_mark_damage(wm_t *wm, int32_t x, int32_t y, uint32_t w, uint32_t h);
static uint8_t wm_cursor_w(const wm_t *wm) { return (wm && wm->cursor_w) ? wm->cursor_w : CURSOR_W; }
static uint8_t wm_cursor_h(const wm_t *wm) { return (wm && wm->cursor_h) ? wm->cursor_h : CURSOR_H; }

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool parse_hex_rgb(const char *s, uint32_t *out) {
    if (!s || !out) return false;
    if (s[0] == '#') s++;
    int n[6];
    for (int i = 0; i < 6; i++) {
        int v = hex_nibble(s[i]);
        if (v < 0) return false;
        n[i] = v;
    }
    if (s[6] != '\0') return false;

    uint8_t r = (uint8_t)((n[0] << 4) | n[1]);
    uint8_t g = (uint8_t)((n[2] << 4) | n[3]);
    uint8_t b = (uint8_t)((n[4] << 4) | n[5]);
    *out = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    return true;
}

static void fb_present_rect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!g_fb_hw || !g_fb || g_fb_hw == g_fb) return;
    if (w == 0 || h == 0) return;

    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + (int32_t)w;
    int32_t y2 = y + (int32_t)h;
    if (x2 > (int32_t)g_width) x2 = (int32_t)g_width;
    if (y2 > (int32_t)g_height) y2 = (int32_t)g_height;
    if (x1 >= x2 || y1 >= y2) return;

    uint32_t row_bytes = (uint32_t)(x2 - x1) * g_bpp_bytes;
    for (int32_t cy = y1; cy < y2; cy++) {
        uint32_t off = (uint32_t)cy * g_pitch + (uint32_t)x1 * g_bpp_bytes;
        memcpy(g_fb_hw + off, g_fb + off, row_bytes);
    }
}

static void fb_present_full(void) {
    if (!g_fb_hw || !g_fb || g_fb_hw == g_fb) return;
    memcpy(g_fb_hw, g_fb, g_fb_size);
}

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static WM_HOT void fb_put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (!g_fb || x < 0 || y < 0 || (uint32_t)x >= g_width || (uint32_t)y >= g_height) return;

    uint8_t *p = g_fb + (uint32_t)y * g_pitch + (uint32_t)x * g_bpp_bytes;
    switch (g_bpp_bytes) {
        case 4:
            *(uint32_t*)p = color;
            break;
        case 3:
            p[0] = (uint8_t)((color >> 16) & 0xFF);
            p[1] = (uint8_t)((color >> 8) & 0xFF);
            p[2] = (uint8_t)(color & 0xFF);
            break;
        case 2:
            *(uint16_t*)p = (uint16_t)color;
            break;
        case 1:
            p[0] = (uint8_t)color;
            break;
    }
}

static WM_HOT uint32_t fb_get_pixel(int32_t x, int32_t y) {
    if (!g_fb || x < 0 || y < 0 || (uint32_t)x >= g_width || (uint32_t)y >= g_height) return 0;

    uint8_t *p = g_fb + (uint32_t)y * g_pitch + (uint32_t)x * g_bpp_bytes;
    switch (g_bpp_bytes) {
        case 4:
            return *(uint32_t*)p;
        case 3:
            return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        case 2:
            return *(uint16_t*)p;
        case 1:
            return p[0];
        default:
            return 0;
    }
}

static WM_HOT void fb_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!g_fb || w == 0 || h == 0) return;

    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + (int32_t)w;
    int32_t y2 = y + (int32_t)h;

    if (x2 > (int32_t)g_width) x2 = (int32_t)g_width;
    if (y2 > (int32_t)g_height) y2 = (int32_t)g_height;
    if (x1 >= x2 || y1 >= y2) return;

    if (g_bpp_bytes == 3) {
        uint8_t r = (uint8_t)((color >> 16) & 0xFF);
        uint8_t g = (uint8_t)((color >> 8) & 0xFF);
        uint8_t b = (uint8_t)(color & 0xFF);

        for (int32_t cy = y1; cy < y2; cy++) {
            uint8_t *line = g_fb + (uint32_t)cy * g_pitch + (uint32_t)x1 * 3;
            for (int32_t cx = x1; cx < x2; cx++) {
                uint32_t off = (uint32_t)(cx - x1) * 3;
                line[off + 0] = r;
                line[off + 1] = g;
                line[off + 2] = b;
            }
        }
        return;
    }

    for (int32_t cy = y1; cy < y2; cy++) {
        for (int32_t cx = x1; cx < x2; cx++) {
            fb_put_pixel(cx, cy, color);
        }
    }
}

static void rect_union(bool *has, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h,
                       int32_t nx, int32_t ny, uint32_t nw, uint32_t nh) {
    if (!has || !x || !y || !w || !h || nw == 0 || nh == 0) return;

    int32_t x1 = nx < 0 ? 0 : nx;
    int32_t y1 = ny < 0 ? 0 : ny;
    int32_t x2 = nx + (int32_t)nw;
    int32_t y2 = ny + (int32_t)nh;
    if (x2 > (int32_t)g_width) x2 = (int32_t)g_width;
    if (y2 > (int32_t)g_height) y2 = (int32_t)g_height;
    if (x1 >= x2 || y1 >= y2) return;

    if (!*has) {
        *x = x1; *y = y1; *w = (uint32_t)(x2 - x1); *h = (uint32_t)(y2 - y1); *has = true;
        return;
    }

    int32_t ux1 = (*x < x1) ? *x : x1;
    int32_t uy1 = (*y < y1) ? *y : y1;
    int32_t ux2 = ((*x + (int32_t)(*w)) > x2) ? (*x + (int32_t)(*w)) : x2;
    int32_t uy2 = ((*y + (int32_t)(*h)) > y2) ? (*y + (int32_t)(*h)) : y2;

    *x = ux1; *y = uy1; *w = (uint32_t)(ux2 - ux1); *h = (uint32_t)(uy2 - uy1);
}

static void fb_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t t) {
    if (w == 0 || h == 0 || t == 0) return;

    fb_fill_rect(x, y, w, t, color);
    fb_fill_rect(x, y + (int32_t)h - (int32_t)t, w, t, color);
    fb_fill_rect(x, y, t, h, color);
    fb_fill_rect(x + (int32_t)w - (int32_t)t, y, t, h, color);
}

static void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, g_width, g_height, color);
}

static uint32_t wm_title_button_size(uint32_t title_h) {
    uint32_t s = title_h > 6 ? title_h - 6 : TITLE_BTN_MIN;
    if (s < TITLE_BTN_MIN) s = TITLE_BTN_MIN;
    if (s > TITLE_BTN_MAX) s = TITLE_BTN_MAX;
    return s;
}

static void wm_get_title_button_rect(const window_t *win, uint32_t title_h, bool close_button, int32_t *x, int32_t *y, uint32_t *w, uint32_t *h) {
    if (!win || !x || !y || !w || !h) return;

    uint32_t bs = wm_title_button_size(title_h);
    int32_t by = win->y + ((int32_t)title_h - (int32_t)bs) / 2;
    int32_t right = win->x + (int32_t)win->width - TITLE_BTN_GAP;
    int32_t bx_close = right - (int32_t)bs;
    int32_t bx_full = bx_close - TITLE_BTN_GAP - (int32_t)bs;

    *x = close_button ? bx_close : bx_full;
    *y = by;
    *w = bs;
    *h = bs;
}

static bool wm_point_in_rect(int32_t px, int32_t py, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return false;
    return px >= x && py >= y && px < x + (int32_t)w && py < y + (int32_t)h;
}

static void wm_draw_title_buttons(window_t *win, uint32_t title_h) {
    if (!win || !win->title_enabled) return;

    int32_t bx, by;
    uint32_t bw, bh;

    if (win->can_maximize) {
        wm_get_title_button_rect(win, title_h, false, &bx, &by, &bw, &bh);
        fb_fill_rect(bx, by, bw, bh, 0x003a8f5a);
        fb_draw_rect(bx, by, bw, bh, 0x00d2e9d9, 1);
        if (g_btn_full_custom && g_btn_full_w > 0 && g_btn_full_h > 0) {
            int32_t ix = bx + ((int32_t)bw - (int32_t)g_btn_full_w) / 2;
            int32_t iy = by + ((int32_t)bh - (int32_t)g_btn_full_h) / 2;
            for (uint8_t y = 0; y < g_btn_full_h; y++) {
                for (uint8_t x = 0; x < g_btn_full_w; x++) {
                    uint32_t c = g_btn_full_pixels[(uint32_t)y * CURSOR_MAX_W + x];
                    if (c == 0xFFFFFFFFu) continue;
                    fb_put_pixel(ix + x, iy + y, c);
                }
            }
        } else {
            fb_draw_rect(bx + 3, by + 3, bw > 6 ? bw - 6 : 1, bh > 6 ? bh - 6 : 1, 0x00eef7f1, 1);
        }
    }

    if (win->can_close) {
        wm_get_title_button_rect(win, title_h, true, &bx, &by, &bw, &bh);
        fb_fill_rect(bx, by, bw, bh, 0x00b53c4a);
        fb_draw_rect(bx, by, bw, bh, 0x00f4d9dd, 1);
        if (g_btn_close_custom && g_btn_close_w > 0 && g_btn_close_h > 0) {
            int32_t ix = bx + ((int32_t)bw - (int32_t)g_btn_close_w) / 2;
            int32_t iy = by + ((int32_t)bh - (int32_t)g_btn_close_h) / 2;
            for (uint8_t y = 0; y < g_btn_close_h; y++) {
                for (uint8_t x = 0; x < g_btn_close_w; x++) {
                    uint32_t c = g_btn_close_pixels[(uint32_t)y * CURSOR_MAX_W + x];
                    if (c == 0xFFFFFFFFu) continue;
                    fb_put_pixel(ix + x, iy + y, c);
                }
            }
        } else {
            for (uint32_t i = 2; i + 2 < bw && i + 2 < bh; i++) {
                fb_put_pixel(bx + (int32_t)i, by + (int32_t)i, 0x00fff1f2);
                fb_put_pixel(bx + (int32_t)(bw - 1 - i), by + (int32_t)i, 0x00fff1f2);
            }
        }
    }
}

static void wm_toggle_fullscreen(wm_t *wm, window_t *win) {
    if (!wm || !win || !win->can_maximize) return;

    wm_mark_damage(wm, win->x, win->y, win->width, win->height);

    if (!win->maximized) {
        win->restore_x = win->x;
        win->restore_y = win->y;
        win->restore_width = win->width;
        win->restore_height = win->height;
        win->x = 0;
        win->y = 0;
        if (!window_resize(win, g_width, g_height)) {
            win->width = g_width;
            win->height = g_height;
            win->canvas_dirty = true;
        }
        win->maximized = true;
    } else {
        win->x = win->restore_x;
        win->y = win->restore_y;
        if (!window_resize(win, win->restore_width, win->restore_height)) {
            win->width = win->restore_width;
            win->height = win->restore_height;
            win->canvas_dirty = true;
        }
        win->maximized = false;
    }

    wm_mark_damage(wm, win->x, win->y, win->width, win->height);
}

static void wm_hit_title_buttons(wm_t *wm, window_t *win, int32_t mx, int32_t my, bool *hit_close, bool *hit_full) {
    if (hit_close) *hit_close = false;
    if (hit_full) *hit_full = false;
    if (!wm || !win || !win->title_enabled) return;

    uint32_t title_h = wm_title_height(wm);
    int32_t bx, by;
    uint32_t bw, bh;

    if (win->can_close) {
        wm_get_title_button_rect(win, title_h, true, &bx, &by, &bw, &bh);
        if (wm_point_in_rect(mx, my, bx, by, bw, bh)) {
            if (hit_close) *hit_close = true;
            return;
        }
    }

    if (win->can_maximize) {
        wm_get_title_button_rect(win, title_h, false, &bx, &by, &bw, &bh);
        if (wm_point_in_rect(mx, my, bx, by, bw, bh)) {
            if (hit_full) *hit_full = true;
        }
    }
}

static uint32_t wm_title_height(const wm_t *wm) {
    if (!wm || !wm->font) return 20;
    uint32_t h = wm->font->height + 6;
    if (h < 18) h = 18;
    return h;
}

static bool point_in_window(const window_t *win, int32_t x, int32_t y) {
    if (!win || !win->visible) return false;

    int32_t x2 = win->x + (int32_t)win->width;
    int32_t y2 = win->y + (int32_t)win->height;
    return x >= win->x && x < x2 && y >= win->y && y < y2;
}

static bool point_in_title(const wm_t *wm, const window_t *win, int32_t x, int32_t y) {
    if (!wm || !win) return false;
    if (!win->title_enabled) return false;
    uint32_t title_h = wm_title_height(wm);

    return point_in_window(win, x, y) && y < (win->y + (int32_t)title_h) && y >= win->y;
}

static window_t *wm_window_at(wm_t *wm, int32_t x, int32_t y) {
    if (!wm) return NULL;

    for (size_t i = wm->window_count; i > 0; i--) {
        window_t *win = wm->windows[i - 1];
        if (point_in_window(win, x, y)) return win;
    }
    return NULL;
}

static int wm_find_index(wm_t *wm, window_t *win) {
    if (!wm || !win) return -1;

    for (size_t i = 0; i < wm->window_count; i++) {
        if (wm->windows[i] == win) return (int)i;
    }
    return -1;
}

static void wm_mark_damage(wm_t *wm, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!wm || w == 0 || h == 0) return;

    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + (int32_t)w;
    int32_t y2 = y + (int32_t)h;

    if (x2 > (int32_t)g_width) x2 = (int32_t)g_width;
    if (y2 > (int32_t)g_height) y2 = (int32_t)g_height;
    if (x1 >= x2 || y1 >= y2) return;

    if (!wm->damage_dirty) {
        wm->damage_x = x1;
        wm->damage_y = y1;
        wm->damage_w = (uint32_t)(x2 - x1);
        wm->damage_h = (uint32_t)(y2 - y1);
        wm->damage_dirty = true;
        return;
    }

    int32_t ux1 = wm->damage_x < x1 ? wm->damage_x : x1;
    int32_t uy1 = wm->damage_y < y1 ? wm->damage_y : y1;
    int32_t ux2 = (wm->damage_x + (int32_t)wm->damage_w) > x2 ? (wm->damage_x + (int32_t)wm->damage_w) : x2;
    int32_t uy2 = (wm->damage_y + (int32_t)wm->damage_h) > y2 ? (wm->damage_y + (int32_t)wm->damage_h) : y2;

    wm->damage_x = ux1;
    wm->damage_y = uy1;
    wm->damage_w = (uint32_t)(ux2 - ux1);
    wm->damage_h = (uint32_t)(uy2 - uy1);
}

static bool wm_window_intersects_rect(window_t *win, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!win || !win->visible || w == 0 || h == 0) return false;

    int32_t ax1 = win->x;
    int32_t ay1 = win->y;
    int32_t ax2 = win->x + (int32_t)win->width;
    int32_t ay2 = win->y + (int32_t)win->height;

    int32_t bx1 = x;
    int32_t by1 = y;
    int32_t bx2 = x + (int32_t)w;
    int32_t by2 = y + (int32_t)h;

    if (ax1 >= bx2 || bx1 >= ax2) return false;
    if (ay1 >= by2 || by1 >= ay2) return false;
    return true;
}

static WM_HOT void wm_draw_char(wm_t *wm, int32_t x, int32_t y, char c, uint32_t color) {
    if (!wm || !wm->font || !wm->font->data) return;

    unsigned char uc = (unsigned char)c;
    if (uc >= wm->font->num_glyphs) uc = 0;

    const uint8_t *glyph = wm->font->data + ((uint32_t)uc * wm->font->glyph_size);
    uint32_t w = wm->font->width;
    uint32_t h = wm->font->height;
    uint32_t bpl = (w + 7) / 8;

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            uint32_t boff = row * bpl + (col / 8);
            uint8_t mask = (uint8_t)(0x80 >> (col % 8));
            if (glyph[boff] & mask) {
                fb_put_pixel(x + (int32_t)col, y + (int32_t)row, color);
            }
        }
    }
}

static WM_HOT void wm_draw_window(wm_t *wm, window_t *win) {
    if (!wm || !win || !win->visible) return;

    uint32_t title_h = wm_title_height(wm);
    uint32_t fill_color = win->focused ? g_theme_window_focus_color : g_theme_window_blur_color;
    uint32_t title_bg = fill_color;
    uint32_t inner_x = (uint32_t)win->x;
    uint32_t inner_y = (uint32_t)win->y;
    uint32_t inner_w = win->width;
    uint32_t inner_h = win->height;
    if (inner_w == 0 || inner_h == 0) return;

    fb_fill_rect((int32_t)inner_x, (int32_t)inner_y, inner_w, inner_h, fill_color);

    uint32_t draw_title_h = win->title_enabled ? (title_h < inner_h ? title_h : inner_h) : 0;
    if (draw_title_h > 0) {
        fb_fill_rect((int32_t)inner_x, (int32_t)inner_y, inner_w, draw_title_h, title_bg);
        wm_draw_title_buttons(win, draw_title_h);
    }

    if (inner_h > draw_title_h) {
        fb_fill_rect((int32_t)inner_x, (int32_t)(inner_y + draw_title_h), inner_w, inner_h - draw_title_h, fill_color);
    }

    if (win->canvas && win->canvas_width > 0 && win->canvas_height > 0) {
        uint32_t body_h = inner_h > draw_title_h ? (inner_h - draw_title_h) : 0;
        uint32_t copy_w = inner_w < win->canvas_width ? inner_w : win->canvas_width;
        uint32_t copy_h = body_h < win->canvas_height ? body_h : win->canvas_height;
        int32_t dst_x = (int32_t)inner_x;
        int32_t dst_y = (int32_t)(inner_y + draw_title_h);

        for (uint32_t y = 0; y < copy_h; y++) {
            uint32_t *src = win->canvas + y * win->canvas_width;
            for (uint32_t x = 0; x < copy_w; x++) {
                fb_put_pixel(dst_x + (int32_t)x, dst_y + (int32_t)y, src[x]);
            }
        }
    }

    if (wm->font && draw_title_h > 0) {
        int32_t tx = (int32_t)inner_x + WINDOW_PADDING;
        int32_t ty = (int32_t)inner_y + 3;
        int32_t title_right_limit = win->x + (int32_t)win->width - WINDOW_PADDING;
        if (win->can_close || win->can_maximize) {
            int32_t bx, by;
            uint32_t bw, bh;
            wm_get_title_button_rect(win, draw_title_h, win->can_maximize ? false : true, &bx, &by, &bw, &bh);
            title_right_limit = bx - TITLE_BTN_GAP;
        }
        for (const char *p = win->title; *p; p++) {
            wm_draw_char(wm, tx, ty, *p, 0x00ffffff);
            tx += (int32_t)wm->font->width;
            if (tx >= title_right_limit) break;
        }
    }
}

static void wm_draw_cursor(const wm_t *wm) {
    if (!wm) return;

    int32_t x = wm->mouse_x;
    int32_t y = wm->mouse_y;
    uint8_t cw = wm_cursor_w(wm);
    uint8_t ch = wm_cursor_h(wm);

    if (wm->cursor_custom) {
        for (uint8_t dy = 0; dy < ch; dy++) {
            for (uint8_t dx = 0; dx < cw; dx++) {
                uint32_t color = wm->cursor_pixels[(uint32_t)dy * CURSOR_MAX_W + dx];
                if (color == 0xFFFFFFFFu) continue;
                fb_put_pixel(x + dx, y + dy, color);
            }
        }
    } else {
        for (int32_t dy = 0; dy < ch; dy++) {
            for (int32_t dx = 0; dx <= dy / 2; dx++) {
                int32_t px = x + dx;
                int32_t py = y + dy;
                if (dx == 0 || dy == 0 || dx == dy / 2) fb_put_pixel(px, py, 0x00000000);
                else fb_put_pixel(px, py, 0x00f4f7fb);
            }
        }
    }
}

static void wm_cursor_save(wm_t *wm, int32_t x, int32_t y) {
    if (!wm) return;
    uint8_t cw = wm_cursor_w(wm);
    uint8_t ch = wm_cursor_h(wm);

    for (uint8_t dy = 0; dy < ch; dy++) {
        for (uint8_t dx = 0; dx < cw; dx++) {
            size_t idx = (size_t)dy * CURSOR_MAX_W + (size_t)dx;
            wm->cursor_saved[idx] = fb_get_pixel(x + dx, y + dy);
        }
    }
}

static void wm_cursor_restore(wm_t *wm) {
    if (!wm || !wm->cursor_drawn) return;

    int32_t x = wm->prev_cursor_x;
    int32_t y = wm->prev_cursor_y;
    uint8_t cw = wm_cursor_w(wm);
    uint8_t ch = wm_cursor_h(wm);

    for (uint8_t dy = 0; dy < ch; dy++) {
        for (uint8_t dx = 0; dx < cw; dx++) {
            size_t idx = (size_t)dy * CURSOR_MAX_W + (size_t)dx;
            fb_put_pixel(x + dx, y + dy, wm->cursor_saved[idx]);
        }
    }

    wm->cursor_drawn = false;
}

static void wm_cursor_draw_with_save(wm_t *wm) {
    if (!wm) return;
    wm_cursor_save(wm, wm->mouse_x, wm->mouse_y);
    wm_draw_cursor(wm);
    wm->prev_cursor_x = wm->mouse_x;
    wm->prev_cursor_y = wm->mouse_y;
    wm->cursor_drawn = true;
}

wm_t *wm_create(psf_font_t *font) {
    if (!vesa_is_initialized()) return NULL;

    g_fb_hw = (uint8_t*)(uintptr_t)vesa_get_framebuffer();
    g_fb = g_fb_hw;
    g_width = vesa_get_width();
    g_height = vesa_get_height();
    g_pitch = vesa_get_pitch();
    g_bpp_bytes = vesa_get_bpp() / 8;

    g_fb_size = g_pitch * g_height;

    if (!g_fb_hw || g_width == 0 || g_height == 0 || g_pitch == 0 || g_fb_size == 0) return NULL;
    if (g_bpp_bytes != 3 && g_bpp_bytes != 4 && g_bpp_bytes != 2 && g_bpp_bytes != 1) return NULL;

    g_backbuffer = (uint8_t*)kmalloc(g_fb_size);
    if (g_backbuffer) {
        memcpy(g_backbuffer, g_fb_hw, g_fb_size);
        g_fb = g_backbuffer;
    }

    wm_t *wm = (wm_t*)kmalloc(sizeof(wm_t));
    if (!wm) return NULL;

    memset(wm, 0, sizeof(wm_t));
    wm->font = font;
    wm->desktop_color = g_theme_desktop_color;
    wm->mouse_x = (int32_t)(g_width / 2);
    wm->mouse_y = (int32_t)(g_height / 2);
    wm->scene_dirty = true;
    wm->cursor_dirty = true;
    wm->cursor_w = CURSOR_W;
    wm->cursor_h = CURSOR_H;
    wm->cursor_custom = false;

    return wm;
}

void wm_destroy(wm_t *wm) {
    if (!wm) return;

    for (size_t i = 0; i < wm->window_count; i++) {
        window_destroy(wm->windows[i]);
        wm->windows[i] = NULL;
    }

    kfree(wm);
    if (g_backbuffer) {
        kfree(g_backbuffer);
        g_backbuffer = NULL;
    }
    g_fb = g_fb_hw;
}

bool wm_add_window(wm_t *wm, window_t *win) {
    if (!wm || !win) return false;
    if (wm->window_count >= WM_MAX_WINDOWS) return false;
    if (wm_find_index(wm, win) >= 0) return false;

    wm->windows[wm->window_count++] = win;
    win->font = wm->font;
    wm_focus_window(wm, win);
    wm->scene_dirty = true;
    return true;
}

void wm_close_window(wm_t *wm, window_t *win) {
    if (!wm || !win) return;
    if (!win->can_close) return;

    if (win->on_close && !win->on_close(win, win->user_data)) return;

    int idx = wm_find_index(wm, win);
    if (idx < 0) return;

    if (wm->drag_window == win) {
        wm->drag_window = NULL;
        wm->dragging = false;
    }
    wm_mark_damage(wm, win->x, win->y, win->width, win->height);
    window_destroy(win);

    for (size_t i = (size_t)idx; i + 1 < wm->window_count; i++) {
        wm->windows[i] = wm->windows[i + 1];
    }

    wm->window_count--;
    wm->windows[wm->window_count] = NULL;

    for (size_t i = 0; i < wm->window_count; i++) wm->windows[i]->focused = false;
    if (wm->window_count > 0) wm->windows[wm->window_count - 1]->focused = true;
}

void wm_focus_window(wm_t *wm, window_t *win) {
    if (!wm || !win) return;

    int idx = wm_find_index(wm, win);
    if (idx < 0) return;

    window_t *old_top = wm->window_count ? wm->windows[wm->window_count - 1] : NULL;

    for (size_t i = 0; i < wm->window_count; i++) wm->windows[i]->focused = false;

    if ((size_t)idx != wm->window_count - 1) {
        for (size_t i = (size_t)idx; i + 1 < wm->window_count; i++) {
            wm->windows[i] = wm->windows[i + 1];
        }
        wm->windows[wm->window_count - 1] = win;
    }

    win->focused = true;
    wm_mark_damage(wm, win->x, win->y, win->width, win->height);
    if (old_top && old_top != win) wm_mark_damage(wm, old_top->x, old_top->y, old_top->width, old_top->height);

    if (old_top && old_top != win && old_top->on_focus) old_top->on_focus(old_top, false, old_top->user_data);
    if (win->on_focus) win->on_focus(win, true, win->user_data);
}

void wm_handle_mouse_packet(wm_t *wm, const mouse_packet_t *packet) {
    if (!wm || !packet) return;

    int32_t old_x = wm->mouse_x;
    int32_t old_y = wm->mouse_y;
    uint8_t old_buttons = wm->mouse_buttons;

    int32_t max_x = (int32_t)g_width - 1;
    int32_t max_y = (int32_t)g_height - 1;

    wm->mouse_x = clamp_i32(wm->mouse_x + packet->x_movement, 0, max_x);
    wm->mouse_y = clamp_i32(wm->mouse_y - packet->y_movement, 0, max_y);

    bool left_now = (packet->buttons & 0x01) != 0;
    bool left_prev = (old_buttons & 0x01) != 0;

    if (left_now && !left_prev) {
        window_t *hit = wm_window_at(wm, wm->mouse_x, wm->mouse_y);
        if (hit) {
            wm_focus_window(wm, hit);
            int32_t local_x = wm->mouse_x - hit->x;
            int32_t local_y = wm->mouse_y - hit->y;
            if (hit->on_mouse) (void)hit->on_mouse(hit, local_x, local_y, packet->buttons, true, hit->user_data);

            bool hit_close = false;
            bool hit_full = false;
            if (point_in_title(wm, hit, wm->mouse_x, wm->mouse_y)) {
                wm_hit_title_buttons(wm, hit, wm->mouse_x, wm->mouse_y, &hit_close, &hit_full);
            }
            if (hit_close) {
                wm_close_window(wm, hit);
                wm->cursor_dirty = true;
                wm->mouse_buttons = packet->buttons;
                return;
            }
            if (hit_full) {
                wm_toggle_fullscreen(wm, hit);
                wm->cursor_dirty = true;
                wm->mouse_buttons = packet->buttons;
                return;
            }

            if (hit->draggable && point_in_title(wm, hit, wm->mouse_x, wm->mouse_y)) {
                wm->dragging = true;
                wm->drag_window = hit;
                wm->drag_off_x = wm->mouse_x - hit->x;
                wm->drag_off_y = wm->mouse_y - hit->y;
            }
        }
    }

    if (left_now && wm->dragging && wm->drag_window) {
        int32_t old_wx = wm->drag_window->x;
        int32_t old_wy = wm->drag_window->y;

        int32_t nx = wm->mouse_x - wm->drag_off_x;
        int32_t ny = wm->mouse_y - wm->drag_off_y;

        int32_t lim_x = (int32_t)g_width - (int32_t)wm->drag_window->width;
        int32_t lim_y = (int32_t)g_height - (int32_t)wm->drag_window->height;
        if (lim_x < 0) lim_x = 0;
        if (lim_y < 0) lim_y = 0;

        nx = clamp_i32(nx, 0, lim_x);
        ny = clamp_i32(ny, 0, lim_y);

        if (old_wx != nx || old_wy != ny) {
            wm_mark_damage(wm, old_wx, old_wy, wm->drag_window->width, wm->drag_window->height);
            wm->drag_window->x = nx;
            wm->drag_window->y = ny;
            wm_mark_damage(wm, nx, ny, wm->drag_window->width, wm->drag_window->height);
        }
    }

    if (!left_now && left_prev) {
        if (wm->drag_window && wm->drag_window->on_mouse) {
            int32_t local_x = wm->mouse_x - wm->drag_window->x;
            int32_t local_y = wm->mouse_y - wm->drag_window->y;
            (void)wm->drag_window->on_mouse(wm->drag_window, local_x, local_y, packet->buttons, false, wm->drag_window->user_data);
        }
        wm->dragging = false;
        wm->drag_window = NULL;
    }

    wm->mouse_buttons = packet->buttons;

    if (wm->mouse_x != old_x || wm->mouse_y != old_y || wm->mouse_buttons != old_buttons) {
        wm->cursor_dirty = true;
    }
}

void wm_handle_key_event(wm_t *wm, const struct key_event *event) {
    if (!wm || !event || !event->pressed) return;

    window_t *focused = wm_get_focused_window(wm);
    if (!focused) return;

    if (focused->on_key) (void)focused->on_key(focused, event, focused->user_data);
}

void wm_request_redraw(wm_t *wm) {
    if (!wm) return;
    wm->scene_dirty = true;
}

WM_HOT void wm_render(wm_t *wm) {
    if (!wm) return;

    for (size_t i = 0; i < wm->window_count; i++) {
        window_t *win = wm->windows[i];
        if (!win) continue;
        if (win->canvas_dirty) {
            if (win->on_draw) win->on_draw(win, win->user_data);
            wm_mark_damage(wm, win->x, win->y, win->width, win->height);
            win->canvas_dirty = false;
        }
    }

    if (!wm->scene_dirty && !wm->cursor_dirty && wm->content_dirty_window == NULL && !wm->damage_dirty) return;

    bool present_full = false;
    bool present_has = false;
    int32_t present_x = 0;
    int32_t present_y = 0;
    uint32_t present_w = 0;
    uint32_t present_h = 0;

    cli();
    wm_in_frame = true;

    if (wm->scene_dirty) {
        fb_clear(wm->desktop_color);
        for (size_t i = 0; i < wm->window_count; i++) {
            wm_draw_window(wm, wm->windows[i]);
        }
        wm->scene_dirty = false;
        wm->damage_dirty = false;
        wm->content_dirty_window = NULL;
        wm->cursor_dirty = true;
        wm->cursor_drawn = false;
        present_full = true;
    } else if (wm->content_dirty_window != NULL) {
        bool had_cursor = wm->cursor_drawn;
        int32_t old_cx = wm->prev_cursor_x;
        int32_t old_cy = wm->prev_cursor_y;
        wm_cursor_restore(wm);
        if (had_cursor) rect_union(&present_has, &present_x, &present_y, &present_w, &present_h, old_cx, old_cy, wm_cursor_w(wm), wm_cursor_h(wm));
        wm_draw_window(wm, wm->content_dirty_window);
        rect_union(&present_has, &present_x, &present_y, &present_w, &present_h,
                   wm->content_dirty_window->x, wm->content_dirty_window->y,
                   wm->content_dirty_window->width, wm->content_dirty_window->height);
        wm->content_dirty_window = NULL;
        wm->cursor_dirty = true;
    } else if (wm->damage_dirty) {
        bool had_cursor = wm->cursor_drawn;
        int32_t old_cx = wm->prev_cursor_x;
        int32_t old_cy = wm->prev_cursor_y;
        wm_cursor_restore(wm);
        if (had_cursor) rect_union(&present_has, &present_x, &present_y, &present_w, &present_h, old_cx, old_cy, wm_cursor_w(wm), wm_cursor_h(wm));
        fb_fill_rect(wm->damage_x, wm->damage_y, wm->damage_w, wm->damage_h, wm->desktop_color);
        for (size_t i = 0; i < wm->window_count; i++) {
            window_t *win = wm->windows[i];
            if (wm_window_intersects_rect(win, wm->damage_x, wm->damage_y, wm->damage_w, wm->damage_h)) {
                wm_draw_window(wm, win);
            }
        }
        rect_union(&present_has, &present_x, &present_y, &present_w, &present_h,
                   wm->damage_x, wm->damage_y, wm->damage_w, wm->damage_h);
        wm->damage_dirty = false;
        wm->cursor_dirty = true;
    } else if (wm->cursor_dirty) {
        bool had_cursor = wm->cursor_drawn;
        int32_t old_cx = wm->prev_cursor_x;
        int32_t old_cy = wm->prev_cursor_y;
        if (had_cursor) rect_union(&present_has, &present_x, &present_y, &present_w, &present_h, old_cx, old_cy, wm_cursor_w(wm), wm_cursor_h(wm));
        wm_cursor_restore(wm);
    }

    wm_cursor_draw_with_save(wm);
    rect_union(&present_has, &present_x, &present_y, &present_w, &present_h, wm->mouse_x, wm->mouse_y, wm_cursor_w(wm), wm_cursor_h(wm));
    wm->cursor_dirty = false;
    wm_in_frame = false;
    sti();

    if (present_full) fb_present_full();
    else if (present_has) fb_present_rect(present_x, present_y, present_w, present_h);
}

static bool wm_load_icon_from_memfs(memfs *fs, const char *path, uint32_t *pixels, uint8_t *out_w, uint8_t *out_h) {
    if (!fs || !path || !pixels || !out_w || !out_h) return false;

    memfs_inode info;
    if (memfs_get_info(fs, path, &info) != 0) return false;
    if (info.type != MEMFS_TYPE_FILE || info.file.size == 0 || info.file.size > 4096) return false;

    char *buf = (char*)kmalloc(info.file.size + 1);
    if (!buf) return false;

    ssize_t n = memfs_read(fs, path, buf, info.file.size);
    if (n != (ssize_t)info.file.size) {
        kfree(buf);
        return false;
    }
    buf[info.file.size] = '\0';

    for (uint32_t i = 0; i < CURSOR_MAX_W * CURSOR_MAX_H; i++) pixels[i] = 0xFFFFFFFFu;

    uint8_t w = 0;
    uint8_t h = 0;
    char *p = buf;
    while (*p && h < CURSOR_MAX_H) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        char saved = *p;
        *p = '\0';

        uint8_t line_w = 0;
        for (char *c = line; *c && line_w < CURSOR_MAX_W; c++) {
            uint32_t color = 0xFFFFFFFFu;
            if (*c == '#') color = 0x00000000;
            else if (*c == '*') color = 0x00ffffff;
            else if (*c == 'R') color = 0x00ff4040;
            else if (*c == 'G') color = 0x003adf7a;
            else if (*c == 'B') color = 0x003898ff;
            pixels[(uint32_t)h * CURSOR_MAX_W + line_w] = color;
            line_w++;
        }
        if (line_w > w) w = line_w;
        h++;

        *p = saved;
        while (*p == '\r' || *p == '\n') p++;
    }

    kfree(buf);

    if (w == 0 || h == 0) return false;
    *out_w = w;
    *out_h = h;
    return true;
}

bool wm_load_cursor_from_memfs(wm_t *wm, memfs *fs, const char *path) {
    if (!wm || !fs || !path) return false;

    memfs_inode info;
    if (memfs_get_info(fs, path, &info) != 0) return false;
    if (info.type != MEMFS_TYPE_FILE || info.file.size == 0 || info.file.size > 4096) return false;

    char *buf = (char*)kmalloc(info.file.size + 1);
    if (!buf) return false;

    ssize_t n = memfs_read(fs, path, buf, info.file.size);
    if (n != (ssize_t)info.file.size) {
        kfree(buf);
        return false;
    }
    buf[info.file.size] = '\0';

    uint8_t w = 0;
    uint8_t h = 0;
    uint32_t pixels[CURSOR_MAX_W * CURSOR_MAX_H];
    for (uint32_t i = 0; i < CURSOR_MAX_W * CURSOR_MAX_H; i++) pixels[i] = 0xFFFFFFFFu;

    char *p = buf;
    while (*p && h < CURSOR_MAX_H) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        char saved = *p;
        *p = '\0';

        uint8_t line_w = 0;
        for (char *c = line; *c && line_w < CURSOR_MAX_W; c++) {
            uint32_t color = 0xFFFFFFFFu;
            if (*c == '#') color = 0x00000000;
            else if (*c == '*') color = 0x00ffffff;
            else if (*c == '+') color = 0x00d0e8ff;
            pixels[(uint32_t)h * CURSOR_MAX_W + line_w] = color;
            line_w++;
        }
        if (line_w > w) w = line_w;
        h++;

        *p = saved;
        while (*p == '\r' || *p == '\n') p++;
    }

    kfree(buf);

    if (w == 0 || h == 0) return false;

    wm->cursor_w = w;
    wm->cursor_h = h;
    wm->cursor_custom = true;
    for (uint32_t i = 0; i < CURSOR_MAX_W * CURSOR_MAX_H; i++) wm->cursor_pixels[i] = pixels[i];
    wm->cursor_dirty = true;
    return true;
}

bool wm_load_title_buttons_from_memfs(wm_t *wm, memfs *fs, const char *close_path, const char *full_path) {
    if (!wm || !fs || !close_path || !full_path) return false;

    uint8_t cw = 0, ch = 0;
    uint8_t fw = 0, fh = 0;
    bool ok_close = wm_load_icon_from_memfs(fs, close_path, g_btn_close_pixels, &cw, &ch);
    bool ok_full = wm_load_icon_from_memfs(fs, full_path, g_btn_full_pixels, &fw, &fh);

    if (ok_close) {
        g_btn_close_w = cw;
        g_btn_close_h = ch;
        g_btn_close_custom = true;
    }
    if (ok_full) {
        g_btn_full_w = fw;
        g_btn_full_h = fh;
        g_btn_full_custom = true;
    }

    wm->scene_dirty = true;
    return ok_close && ok_full;
}

bool wm_load_theme_from_memfs(wm_t *wm, memfs *fs, const char *path) {
    if (!wm || !fs || !path) return false;

    memfs_inode info;
    if (memfs_get_info(fs, path, &info) != 0) return false;
    if (info.type != MEMFS_TYPE_FILE || info.file.size == 0 || info.file.size > 2048) return false;

    char *buf = (char*)kmalloc(info.file.size + 1);
    if (!buf) return false;

    ssize_t n = memfs_read(fs, path, buf, info.file.size);
    if (n != (ssize_t)info.file.size) {
        kfree(buf);
        return false;
    }
    buf[info.file.size] = '\0';

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        char saved = *p;
        *p = '\0';

        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq == '=') {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;

            while (*key == ' ' || *key == '\t') key++;
            while (*val == ' ' || *val == '\t') val++;
            char *vend = val + strlen(val);
            while (vend > val && (vend[-1] == ' ' || vend[-1] == '\t')) vend--;
            *vend = '\0';

            uint32_t parsed_color = 0;
            if (strcmp(key, "DESKTOP_BG") == 0) {
                if (parse_hex_rgb(val, &parsed_color)) g_theme_desktop_color = parsed_color;
            } else if (strcmp(key, "WINDOW_BG_FOCUSED") == 0) {
                if (parse_hex_rgb(val, &parsed_color)) g_theme_window_focus_color = parsed_color;
            } else if (strcmp(key, "WINDOW_BG_UNFOCUSED") == 0) {
                if (parse_hex_rgb(val, &parsed_color)) g_theme_window_blur_color = parsed_color;
            }
        }

        *p = saved;
        while (*p == '\r' || *p == '\n') p++;
    }

    kfree(buf);
    wm->desktop_color = g_theme_desktop_color;
    wm->scene_dirty = true;
    return true;
}

window_t *wm_get_focused_window(wm_t *wm) {
    if (!wm || wm->window_count == 0) return NULL;
    return wm->windows[wm->window_count - 1];
}
