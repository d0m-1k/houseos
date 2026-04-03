#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <devctl.h>

#define FB_MAX_BYTES (3u * 1024u * 1024u)

typedef struct {
    int x;
    int y;
    int w;
    int h;
} rect_t;

typedef struct {
    uint8_t loaded;
    uint8_t glyph_h;
    const uint8_t *glyphs;
} psf_font_t;

static uint8_t g_fb[FB_MAX_BYTES];
static dev_fb_info_t g_info;
static int g_fd_fb = -1;
static int g_fd_mouse = -1;
static int g_fd_kbd = -1;
static int g_fd_power = -1;
static int g_mouse_x = 24;
static int g_mouse_y = 24;
static uint32_t g_mouse_btn = 0;
static uint32_t g_prev_mouse_btn = 0;
static psf_font_t g_font;

static uint8_t g_settings_tab = 0;
static uint8_t g_cfg_show_top_stats = 0;
static uint8_t g_cfg_cursor_compact = 1;
static uint8_t g_cfg_term_autofocus = 1;
static uint8_t g_cfg_disable_mouse_click = 0;
static uint8_t g_cfg_mouse_inv_x = 0;
static uint8_t g_cfg_mouse_inv_y = 0;
static uint8_t g_cfg_mouse_inv_btn = 0;
static uint8_t g_cfg_kbd_layout = 0;
static uint8_t g_cfg_kbd_switch_hotkey = 0;
static uint8_t g_cfg_net_use_dhcp = 1;
static uint8_t g_cfg_dns_profile = 0;
static uint8_t g_cfg_display_mode = 0;
static uint8_t g_cfg_boot_default = 0;
static uint8_t g_cfg_theme = 0;

static uint32_t C_BG_DESKTOP   = 0xDCE7F5;
static uint32_t C_BG_STRIPE    = 0xC7D7EC;
static uint32_t C_SURFACE      = 0xF7FAFF;
static uint32_t C_SURFACE_ALT  = 0xEAF0F8;
static uint32_t C_PANEL        = 0xD8E3F2;
static uint32_t C_PANEL_LINE   = 0xA9BCD3;
static uint32_t C_PRIMARY      = 0x3F74B9;
static uint32_t C_PRIMARY_HI   = 0x4D85CE;
static uint32_t C_PRIMARY_SOFT = 0xBFD3EC;
static uint32_t C_TEXT         = 0x1E2D42;
static uint32_t C_TEXT_SOFT    = 0x50657F;
static uint32_t C_BORDER       = 0xAFC0D6;
static uint32_t C_DANGER       = 0xCC5A5A;

static int inside(int x, int y, const rect_t *r) {
    return (x >= r->x && y >= r->y && x < (r->x + r->w) && y < (r->y + r->h));
}

static int inside_rect(int x, int y, rect_t r) {
    return inside(x, y, &r);
}

static void apply_theme(uint8_t theme) {
    if (theme == 1u) {
        C_BG_DESKTOP   = 0xEFE8DB;
        C_BG_STRIPE    = 0xD9CDB7;
        C_SURFACE      = 0xFFF8EC;
        C_SURFACE_ALT  = 0xF2E7D2;
        C_PANEL        = 0xE7DCC8;
        C_PANEL_LINE   = 0xBFAE8F;
        C_PRIMARY      = 0xA16A2A;
        C_PRIMARY_HI   = 0xBE7F36;
        C_PRIMARY_SOFT = 0xE4C8A0;
        C_TEXT         = 0x352515;
        C_TEXT_SOFT    = 0x6E543A;
        C_BORDER       = 0xC9B595;
        C_DANGER       = 0xB84646;
        return;
    }
    if (theme == 2u) {
        C_BG_DESKTOP   = 0xDDEFE3;
        C_BG_STRIPE    = 0xBFDCCB;
        C_SURFACE      = 0xF4FCF7;
        C_SURFACE_ALT  = 0xE6F5EC;
        C_PANEL        = 0xD5EBDE;
        C_PANEL_LINE   = 0x9FC8AF;
        C_PRIMARY      = 0x2F8A5A;
        C_PRIMARY_HI   = 0x3BA16B;
        C_PRIMARY_SOFT = 0xBDE3CC;
        C_TEXT         = 0x173526;
        C_TEXT_SOFT    = 0x3F6B54;
        C_BORDER       = 0xA6CFB8;
        C_DANGER       = 0xB84646;
        return;
    }
    C_BG_DESKTOP   = 0xDCE7F5;
    C_BG_STRIPE    = 0xC7D7EC;
    C_SURFACE      = 0xF7FAFF;
    C_SURFACE_ALT  = 0xEAF0F8;
    C_PANEL        = 0xD8E3F2;
    C_PANEL_LINE   = 0xA9BCD3;
    C_PRIMARY      = 0x3F74B9;
    C_PRIMARY_HI   = 0x4D85CE;
    C_PRIMARY_SOFT = 0xBFD3EC;
    C_TEXT         = 0x1E2D42;
    C_TEXT_SOFT    = 0x50657F;
    C_BORDER       = 0xAFC0D6;
    C_DANGER       = 0xCC5A5A;
}

static int load_psf_font(void) {
    static uint8_t font_blob[8192];
    int fd;
    int32_t n;
    const char *paths[] = {
        "/system/fonts/default8x16.psf"
    };
    uint32_t i;

    memset(&g_font, 0, sizeof(g_font));
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        fd = open(paths[i], 0);
        if (fd < 0) continue;
        n = read(fd, font_blob, sizeof(font_blob));
        close(fd);
        if (n < 4) continue;
        if (font_blob[0] != 0x36 || font_blob[1] != 0x04) continue;
        g_font.glyph_h = font_blob[3];
        if (g_font.glyph_h != 16u) continue;
        g_font.glyphs = font_blob + 4;
        g_font.loaded = 1;
        return 0;
    }
    return -1;
}

static void put_px(int x, int y, uint32_t c) {
    uint32_t bpp;
    uint32_t off;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= g_info.width || (uint32_t)y >= g_info.height) return;
    bpp = g_info.bpp / 8u;
    if (bpp < 3u || bpp > 4u) return;
    off = (uint32_t)y * g_info.pitch + (uint32_t)x * bpp;
    if (off + bpp > sizeof(g_fb)) return;
    g_fb[off + 0] = (uint8_t)((c >> 16) & 0xFFu);
    g_fb[off + 1] = (uint8_t)((c >> 8) & 0xFFu);
    g_fb[off + 2] = (uint8_t)(c & 0xFFu);
    if (bpp == 4u) g_fb[off + 3] = 0;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    uint32_t bpp;
    int y0;
    int x0;
    int y1;
    int x1;
    int iy;
    if (w <= 0 || h <= 0) return;
    if (x >= (int)g_info.width || y >= (int)g_info.height) return;
    if (x + w <= 0 || y + h <= 0) return;

    x0 = (x < 0) ? 0 : x;
    y0 = (y < 0) ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > (int)g_info.width) x1 = (int)g_info.width;
    if (y1 > (int)g_info.height) y1 = (int)g_info.height;
    if (x1 <= x0 || y1 <= y0) return;

    bpp = g_info.bpp / 8u;
    for (iy = y0; iy < y1; iy++) {
        uint8_t *p = &g_fb[(uint32_t)iy * g_info.pitch + (uint32_t)x0 * bpp];
        int ix;
        for (ix = x0; ix < x1; ix++) {
            p[0] = (uint8_t)((c >> 16) & 0xFFu);
            p[1] = (uint8_t)((c >> 8) & 0xFFu);
            p[2] = (uint8_t)(c & 0xFFu);
            if (bpp == 4u) p[3] = 0u;
            p += bpp;
        }
    }
}

static void stroke_rect(int x, int y, int w, int h, uint32_t c) {
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}

static void draw_char(int x, int y, char ch, uint32_t c) {
    const uint8_t *g;
    int row;
    int col;
    if (!g_font.loaded) return;
    g = g_font.glyphs + ((uint8_t)ch) * g_font.glyph_h;
    for (row = 0; row < g_font.glyph_h; row++) {
        uint8_t bits = g[row];
        for (col = 0; col < 8; col++) {
            if (bits & (0x80u >> col)) put_px(x + col, y + row, c);
        }
    }
}

static void draw_text(int x, int y, const char *s, uint32_t c) {
    int i = 0;
    while (s && s[i]) {
        draw_char(x + i * 8, y, s[i], c);
        i++;
    }
}

static void draw_text_fit(int x, int y, int max_px, const char *s, uint32_t c) {
    int i;
    int max_chars;
    if (!s || max_px <= 0) return;
    max_chars = max_px / 8;
    if (max_chars <= 0) return;
    for (i = 0; s[i] && i < max_chars; i++) draw_char(x + i * 8, y, s[i], c);
}

static rect_t settings_left_panel_width(void) {
    rect_t r;
    int inner_w = (int)g_info.width - 44;
    int left_w = inner_w / 3;
    if (left_w < 170) left_w = 170;
    if (left_w > 220) left_w = 220;
    if (left_w > inner_w - 140) left_w = inner_w - 140;
    if (left_w < 140) left_w = 140;
    r.x = 22;
    r.y = 54;
    r.w = left_w;
    r.h = (int)g_info.height - 76;
    return r;
}

static rect_t settings_tab_rect(int idx) {
    rect_t lp = settings_left_panel_width();
    rect_t r;
    r.x = lp.x + 10;
    r.y = lp.y + 24 + idx * 24;
    r.w = lp.w - 20;
    r.h = 20;
    return r;
}

static rect_t settings_body_rect(void) {
    rect_t lp = settings_left_panel_width();
    rect_t r;
    r.x = lp.x + lp.w + 10;
    r.y = lp.y;
    r.w = (int)g_info.width - r.x - 12;
    r.h = lp.h;
    return r;
}

static rect_t settings_content_rect(void) {
    rect_t b = settings_body_rect();
    rect_t r;
    r.x = b.x + 8;
    r.y = b.y + 30;
    r.w = b.w - 16;
    r.h = b.h - 38;
    if (r.w < 40) r.w = 40;
    if (r.h < 40) r.h = 40;
    return r;
}

static rect_t settings_check_rect(rect_t group, int xoff, int yoff) {
    rect_t r;
    r.x = group.x + xoff;
    r.y = group.y + yoff;
    r.w = 18;
    r.h = 18;
    return r;
}

static int settings_check_label_w(rect_t group, rect_t check) {
    int right = group.x + group.w - 8;
    int left = check.x + 26;
    int w = right - left;
    if (w < 20) w = 20;
    return w;
}

static void settings_check_draw(rect_t r, int on) {
    fill_rect(r.x, r.y, r.w, r.h, C_SURFACE);
    stroke_rect(r.x, r.y, r.w, r.h, C_BORDER);
    if (on) fill_rect(r.x + 4, r.y + 4, r.w - 8, r.h - 8, C_PRIMARY);
}

static rect_t settings_theme_opt_rect(rect_t group, int idx) {
    rect_t r;
    int pref_cols = 2;
    int min_cell_w = 150;
    int cols = (pref_cols > 1 && (group.w - 24) >= (min_cell_w * 2 + 12)) ? 2 : 1;
    int col_w = (group.w - 24 - (cols - 1) * 12) / cols;
    int col = idx % cols;
    int row = idx / cols;
    if (col_w < 70) col_w = 70;
    r.x = group.x + 12 + col * (col_w + 12);
    r.y = group.y + 30 + row * 24;
    r.w = 18;
    r.h = 18;
    return r;
}

static int settings_theme_cols(rect_t group) {
    int min_cell_w = 150;
    return ((group.w - 24) >= (min_cell_w * 2 + 12)) ? 2 : 1;
}

static rect_t ui_panel_content(rect_t r, const char *title) {
    rect_t inner = r;
    fill_rect(r.x, r.y, r.w, r.h, C_SURFACE);
    stroke_rect(r.x, r.y, r.w, r.h, C_BORDER);
    draw_text_fit(r.x + 8, r.y + 8, r.w - 16, title, C_TEXT);
    inner.x += 8;
    inner.y += 28;
    inner.w -= 16;
    inner.h -= 36;
    if (inner.w < 20) inner.w = 20;
    if (inner.h < 20) inner.h = 20;
    return inner;
}

static rect_t ui_panel_inner(rect_t r) {
    rect_t inner = r;
    inner.x += 8;
    inner.y += 28;
    inner.w -= 16;
    inner.h -= 36;
    if (inner.w < 20) inner.w = 20;
    if (inner.h < 20) inner.h = 20;
    return inner;
}

static rect_t g_btn_back(void) {
    rect_t r = { (int)g_info.width - 290, 12, 80, 22 };
    return r;
}

static rect_t g_btn_reboot(void) {
    rect_t r = { (int)g_info.width - 200, 12, 90, 22 };
    return r;
}

static rect_t g_btn_power(void) {
    rect_t r = { (int)g_info.width - 100, 12, 90, 22 };
    return r;
}

static void draw_topbar(void) {
    rect_t back = g_btn_back();
    rect_t reb = g_btn_reboot();
    rect_t pwr = g_btn_power();

    fill_rect(0, 0, (int)g_info.width, 44, C_PANEL);
    fill_rect(0, 43, (int)g_info.width, 1, C_PANEL_LINE);
    draw_text(12, 14, "HouseOS Settings", C_TEXT);

    fill_rect(back.x, back.y, back.w, back.h, C_PRIMARY_SOFT);
    stroke_rect(back.x, back.y, back.w, back.h, C_BORDER);
    draw_text(back.x + 18, back.y + 4, "Back", C_TEXT);

    fill_rect(reb.x, reb.y, reb.w, reb.h, C_PRIMARY);
    stroke_rect(reb.x, reb.y, reb.w, reb.h, C_BORDER);
    draw_text(reb.x + 16, reb.y + 4, "Reboot", C_SURFACE);

    fill_rect(pwr.x, pwr.y, pwr.w, pwr.h, C_DANGER);
    stroke_rect(pwr.x, pwr.y, pwr.w, pwr.h, C_BORDER);
    draw_text(pwr.x + 10, pwr.y + 4, "PowerOff", C_SURFACE);
}

static void draw_cursor(void) {
    int x = g_mouse_x;
    int y = g_mouse_y;
    int h = g_cfg_cursor_compact ? 14 : 20;
    int w = g_cfg_cursor_compact ? 10 : 14;
    int row;
    for (row = 0; row < h; row++) {
        int span = (row < w) ? row : w - 1;
        int col;
        for (col = 0; col <= span; col++) {
            uint32_t c = 0xFFFFFF;
            if (col == 0 || col == span || row == 0 || row == h - 1) c = 0x101010;
            put_px(x + col, y + row, c);
        }
    }
}

static void render_settings(void) {
    static const char *tabs[] = {
        "Appearance", "Input Devices", "Network", "Display", "Bootloader", "Terminal", "About"
    };
    rect_t left_panel = settings_left_panel_width();
    rect_t right_panel = settings_body_rect();
    rect_t area = settings_content_rect();
    rect_t g0;
    rect_t g1;
    int i;

    fill_rect(0, 44, (int)g_info.width, (int)g_info.height - 44, C_BG_DESKTOP);
    fill_rect(0, 82, (int)g_info.width, 1, C_BG_STRIPE);
    fill_rect(0, 200, (int)g_info.width, 1, C_BG_STRIPE);

    fill_rect(left_panel.x - 4, left_panel.y - 4, (int)g_info.width - 24, (int)g_info.height - 58, C_SURFACE);
    stroke_rect(left_panel.x - 4, left_panel.y - 4, (int)g_info.width - 24, (int)g_info.height - 58, C_BORDER);

    fill_rect(left_panel.x, left_panel.y, left_panel.w, left_panel.h, C_SURFACE_ALT);
    stroke_rect(left_panel.x, left_panel.y, left_panel.w, left_panel.h, C_BORDER);
    draw_text(left_panel.x + 10, left_panel.y + 8, "System Settings", C_TEXT);
    for (i = 0; i < 7; i++) {
        rect_t tr = settings_tab_rect(i);
        fill_rect(tr.x, tr.y, tr.w, tr.h, (g_settings_tab == (uint8_t)i) ? C_PRIMARY_SOFT : C_SURFACE_ALT);
        draw_text_fit(tr.x + 8, tr.y + 6, tr.w - 12, tabs[i], (g_settings_tab == (uint8_t)i) ? C_TEXT : C_TEXT_SOFT);
    }

    fill_rect(right_panel.x, right_panel.y, right_panel.w, right_panel.h, C_SURFACE_ALT);
    stroke_rect(right_panel.x, right_panel.y, right_panel.w, right_panel.h, C_BORDER);

    if (g_settings_tab == 0u) {
        rect_t c0;
        rect_t c1;
        rect_t c2;
        rect_t c3;
        int cols;
        int col_w;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Appearance", C_TEXT);
        g0 = ui_panel_content(area, "Theme");
        draw_text_fit(g0.x + 4, g0.y + 2, g0.w - 8, "Pick palette:", C_TEXT_SOFT);
        cols = settings_theme_cols(area);
        col_w = (area.w - 24 - (cols - 1) * 12) / cols;
        if (col_w < 70) col_w = 70;
        c0 = settings_theme_opt_rect(area, 0);
        c1 = settings_theme_opt_rect(area, 1);
        c2 = settings_theme_opt_rect(area, 2);
        c3 = settings_theme_opt_rect(area, 3);
        settings_check_draw(c0, g_cfg_theme == 0u);
        settings_check_draw(c1, g_cfg_theme == 1u);
        settings_check_draw(c2, g_cfg_theme == 2u);
        settings_check_draw(c3, g_cfg_cursor_compact);
        draw_text_fit(c0.x + 26, c0.y + 3, col_w - 28, "Breeze Light", C_TEXT_SOFT);
        draw_text_fit(c1.x + 26, c1.y + 3, col_w - 28, "Warm Sand", C_TEXT_SOFT);
        draw_text_fit(c2.x + 26, c2.y + 3, col_w - 28, "Mint", C_TEXT_SOFT);
        draw_text_fit(c3.x + 26, c3.y + 3, col_w - 28, "Compact cursor", C_TEXT_SOFT);
    } else if (g_settings_tab == 1u) {
        rect_t p0 = area;
        rect_t p1 = area;
        rect_t mk0;
        rect_t mk1;
        rect_t mk2;
        rect_t mk3;
        rect_t kb0;
        rect_t kb1;
        rect_t kb2;
        rect_t kb3;
        int h0 = (area.h - 8) / 2;
        p0.h = h0;
        p1.y = area.y + h0 + 8;
        p1.h = area.h - 8 - h0;

        draw_text(right_panel.x + 10, right_panel.y + 10, "Input Devices", C_TEXT);

        g0 = ui_panel_content(p0, "Mouse");
        mk0 = settings_check_rect(g0, 12, 30);
        mk1 = settings_check_rect(g0, 12, 54);
        mk2 = settings_check_rect(g0, 12, 78);
        mk3 = settings_check_rect(g0, 12, 102);
        settings_check_draw(mk0, g_cfg_disable_mouse_click);
        settings_check_draw(mk1, g_cfg_mouse_inv_x);
        settings_check_draw(mk2, g_cfg_mouse_inv_y);
        settings_check_draw(mk3, g_cfg_mouse_inv_btn);
        draw_text_fit(mk0.x + 26, mk0.y + 3, settings_check_label_w(g0, mk0), "Disable mouse click", C_TEXT_SOFT);
        draw_text_fit(mk1.x + 26, mk1.y + 3, settings_check_label_w(g0, mk1), "Invert X", C_TEXT_SOFT);
        draw_text_fit(mk2.x + 26, mk2.y + 3, settings_check_label_w(g0, mk2), "Invert Y", C_TEXT_SOFT);
        draw_text_fit(mk3.x + 26, mk3.y + 3, settings_check_label_w(g0, mk3), "Invert clicks", C_TEXT_SOFT);

        g1 = ui_panel_content(p1, "Keyboard");
        kb0 = settings_check_rect(g1, 12, 30);
        kb1 = settings_check_rect(g1, 12, 54);
        kb2 = settings_check_rect(g1, (g1.w > 220) ? (g1.w / 2) : 12, 78);
        kb3 = settings_check_rect(g1, (g1.w > 220) ? (g1.w / 2) : 12, 102);
        settings_check_draw(kb0, g_cfg_kbd_layout == 0u);
        settings_check_draw(kb1, g_cfg_kbd_layout == 1u);
        settings_check_draw(kb2, g_cfg_kbd_switch_hotkey == 0u);
        settings_check_draw(kb3, g_cfg_kbd_switch_hotkey == 1u);
        draw_text_fit(kb0.x + 26, kb0.y + 3, settings_check_label_w(g1, kb0), "Layouts: EN + RU", C_TEXT_SOFT);
        draw_text_fit(kb1.x + 26, kb1.y + 3, settings_check_label_w(g1, kb1), "Layouts: EN only", C_TEXT_SOFT);
        draw_text_fit(kb2.x + 26, kb2.y + 3, settings_check_label_w(g1, kb2), "Alt+Shift", C_TEXT_SOFT);
        draw_text_fit(kb3.x + 26, kb3.y + 3, settings_check_label_w(g1, kb3), "Ctrl+Shift", C_TEXT_SOFT);
    } else if (g_settings_tab == 2u) {
        rect_t p0 = area;
        rect_t p1 = area;
        rect_t n0;
        rect_t n1;
        rect_t n2;
        int h0 = (area.h - 8) / 2;
        p0.h = h0;
        p1.y = area.y + h0 + 8;
        p1.h = area.h - 8 - h0;

        draw_text(right_panel.x + 10, right_panel.y + 10, "Network", C_TEXT);
        g0 = ui_panel_content(p0, "Devices");
        draw_text_fit(g0.x + 12, g0.y + 30, g0.w - 20, "eth0  (up)", C_TEXT_SOFT);
        draw_text_fit(g0.x + 12, g0.y + 50, g0.w - 20, "loop0 (up)", C_TEXT_SOFT);
        n0 = settings_check_rect(g0, 12, 70);
        settings_check_draw(n0, g_cfg_net_use_dhcp);
        draw_text_fit(n0.x + 26, n0.y + 3, settings_check_label_w(g0, n0), "Use DHCP", C_TEXT_SOFT);

        g1 = ui_panel_content(p1, "DNS");
        n1 = settings_check_rect(g1, 12, 30);
        n2 = settings_check_rect(g1, 12, 54);
        settings_check_draw(n1, g_cfg_dns_profile == 0u);
        settings_check_draw(n2, g_cfg_dns_profile == 1u);
        draw_text_fit(n1.x + 26, n1.y + 3, settings_check_label_w(g1, n1), "System default", C_TEXT_SOFT);
        draw_text_fit(n2.x + 26, n2.y + 3, settings_check_label_w(g1, n2), "Cloudflare 1.1.1.1", C_TEXT_SOFT);
    } else if (g_settings_tab == 3u) {
        rect_t d0;
        rect_t d1;
        rect_t d2;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Display", C_TEXT);
        g0 = ui_panel_content(area, "Mode");
        d0 = settings_check_rect(g0, 12, 30);
        d1 = settings_check_rect(g0, 12, 54);
        d2 = settings_check_rect(g0, (g0.w > 220) ? (g0.w / 2) : 12, 30);
        settings_check_draw(d0, g_cfg_display_mode == 0u);
        settings_check_draw(d1, g_cfg_display_mode == 1u);
        settings_check_draw(d2, g_cfg_display_mode == 2u);
        draw_text_fit(d0.x + 26, d0.y + 3, settings_check_label_w(g0, d0), "1024x768", C_TEXT_SOFT);
        draw_text_fit(d1.x + 26, d1.y + 3, settings_check_label_w(g0, d1), "800x600", C_TEXT_SOFT);
        draw_text_fit(d2.x + 26, d2.y + 3, settings_check_label_w(g0, d2), "640x480", C_TEXT_SOFT);
    } else if (g_settings_tab == 4u) {
        rect_t b0;
        rect_t b1;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Bootloader", C_TEXT);
        g0 = ui_panel_content(area, "Default Boot Entry");
        b0 = settings_check_rect(g0, 12, 30);
        b1 = settings_check_rect(g0, 12, 54);
        settings_check_draw(b0, g_cfg_boot_default == 0u);
        settings_check_draw(b1, g_cfg_boot_default == 1u);
        draw_text_fit(b0.x + 26, b0.y + 3, settings_check_label_w(g0, b0), "Normal boot", C_TEXT_SOFT);
        draw_text_fit(b1.x + 26, b1.y + 3, settings_check_label_w(g0, b1), "Safe mode", C_TEXT_SOFT);
    } else if (g_settings_tab == 5u) {
        rect_t t0;
        rect_t t1;
        rect_t t2;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Terminal", C_TEXT);
        g0 = ui_panel_content(area, "Behavior");
        t0 = settings_check_rect(g0, 12, 30);
        t1 = settings_check_rect(g0, 12, 54);
        t2 = settings_check_rect(g0, 12, 78);
        settings_check_draw(t0, g_cfg_show_top_stats);
        settings_check_draw(t1, g_cfg_cursor_compact);
        settings_check_draw(t2, g_cfg_term_autofocus);
        draw_text_fit(t0.x + 26, t0.y + 3, settings_check_label_w(g0, t0), "Show uptime in top panel", C_TEXT_SOFT);
        draw_text_fit(t1.x + 26, t1.y + 3, settings_check_label_w(g0, t1), "Compact cursor style", C_TEXT_SOFT);
        draw_text_fit(t2.x + 26, t2.y + 3, settings_check_label_w(g0, t2), "Keep terminal focused", C_TEXT_SOFT);
    } else {
        draw_text(right_panel.x + 10, right_panel.y + 10, "About", C_TEXT);
        g0 = ui_panel_content(area, "HouseOS Settings");
        draw_text_fit(g0.x + 8, g0.y + 8, g0.w - 16, "Standalone GUI settings app", C_TEXT_SOFT);
        draw_text_fit(g0.x + 8, g0.y + 28, g0.w - 16, "Launched as separate program from guishell", C_TEXT_SOFT);
        draw_text_fit(g0.x + 8, g0.y + 48, g0.w - 16, "Press Esc or Back to return", C_TEXT_SOFT);
    }
}

static void do_back(void) {
    (void)exec("/bin/guishell");
}

static void handle_click(void) {
    rect_t back = g_btn_back();
    rect_t reb = g_btn_reboot();
    rect_t pwr = g_btn_power();
    rect_t area = settings_content_rect();

    if (inside(g_mouse_x, g_mouse_y, &back)) {
        do_back();
        return;
    }
    if (inside(g_mouse_x, g_mouse_y, &reb)) {
        if (g_fd_power >= 0) (void)ioctl(g_fd_power, DEV_IOCTL_POWER_REBOOT, 0);
        return;
    }
    if (inside(g_mouse_x, g_mouse_y, &pwr)) {
        if (g_fd_power >= 0) (void)ioctl(g_fd_power, DEV_IOCTL_POWER_POWEROFF, 0);
        return;
    }

    for (int i = 0; i < 7; i++) {
        rect_t tr = settings_tab_rect(i);
        if (inside(g_mouse_x, g_mouse_y, &tr)) {
            g_settings_tab = (uint8_t)i;
            return;
        }
    }

    if (g_settings_tab == 0u) {
        rect_t t0 = settings_theme_opt_rect(area, 0);
        rect_t t1 = settings_theme_opt_rect(area, 1);
        rect_t t2 = settings_theme_opt_rect(area, 2);
        rect_t t3 = settings_theme_opt_rect(area, 3);
        if (inside(g_mouse_x, g_mouse_y, &t0)) {
            g_cfg_theme = 0u;
            apply_theme(g_cfg_theme);
            return;
        }
        if (inside(g_mouse_x, g_mouse_y, &t1)) {
            g_cfg_theme = 1u;
            apply_theme(g_cfg_theme);
            return;
        }
        if (inside(g_mouse_x, g_mouse_y, &t2)) {
            g_cfg_theme = 2u;
            apply_theme(g_cfg_theme);
            return;
        }
        if (inside(g_mouse_x, g_mouse_y, &t3)) {
            g_cfg_cursor_compact = g_cfg_cursor_compact ? 0 : 1;
            return;
        }
    }

    if (g_settings_tab == 1u || g_settings_tab == 2u) {
        rect_t p0 = area;
        rect_t p1 = area;
        int h0 = (area.h - 8) / 2;
        p0.h = h0;
        p1.y = area.y + h0 + 8;
        p1.h = area.h - 8 - h0;
        p0 = ui_panel_inner(p0);
        p1 = ui_panel_inner(p1);

        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p0, 12, 30))) {
            g_cfg_disable_mouse_click = g_cfg_disable_mouse_click ? 0 : 1;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p0, 12, 54))) {
            g_cfg_mouse_inv_x = g_cfg_mouse_inv_x ? 0 : 1;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p0, 12, 78))) {
            g_cfg_mouse_inv_y = g_cfg_mouse_inv_y ? 0 : 1;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p0, 12, 102))) {
            g_cfg_mouse_inv_btn = g_cfg_mouse_inv_btn ? 0 : 1;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p1, 12, 30))) {
            g_cfg_kbd_layout = 0u;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p1, 12, 54))) {
            g_cfg_kbd_layout = 1u;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p1, (p1.w > 220) ? (p1.w / 2) : 12, 78))) {
            g_cfg_kbd_switch_hotkey = 0u;
            return;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p1, (p1.w > 220) ? (p1.w / 2) : 12, 102))) {
            g_cfg_kbd_switch_hotkey = 1u;
            return;
        }

        if (g_settings_tab == 2u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p0, 12, 70))) {
            g_cfg_net_use_dhcp = g_cfg_net_use_dhcp ? 0 : 1;
            return;
        }
        if (g_settings_tab == 2u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p1, 12, 30))) {
            g_cfg_dns_profile = 0u;
            return;
        }
        if (g_settings_tab == 2u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p1, 12, 54))) {
            g_cfg_dns_profile = 1u;
            return;
        }
    }

    if (g_settings_tab == 3u || g_settings_tab == 4u || g_settings_tab == 5u) {
        rect_t p = ui_panel_inner(area);
        if (g_settings_tab == 3u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 30))) {
            g_cfg_display_mode = 0u;
            return;
        }
        if (g_settings_tab == 3u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 54))) {
            g_cfg_display_mode = 1u;
            return;
        }
        if (g_settings_tab == 3u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, (p.w > 220) ? (p.w / 2) : 12, 30))) {
            g_cfg_display_mode = 2u;
            return;
        }
        if (g_settings_tab == 4u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 30))) {
            g_cfg_boot_default = 0u;
            return;
        }
        if (g_settings_tab == 4u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 54))) {
            g_cfg_boot_default = 1u;
            return;
        }
        if (g_settings_tab == 5u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 30))) {
            g_cfg_show_top_stats = g_cfg_show_top_stats ? 0 : 1;
            return;
        }
        if (g_settings_tab == 5u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 54))) {
            g_cfg_cursor_compact = g_cfg_cursor_compact ? 0 : 1;
            return;
        }
        if (g_settings_tab == 5u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(p, 12, 78))) {
            g_cfg_term_autofocus = g_cfg_term_autofocus ? 0 : 1;
            return;
        }
    }
}

static void poll_mouse(void) {
    dev_mouse_info_t mi;
    uint32_t b;
    if (g_fd_mouse < 0) return;
    if (ioctl(g_fd_mouse, DEV_IOCTL_MOUSE_GET_INFO, &mi) != 0) return;
    g_mouse_x = g_cfg_mouse_inv_x ? ((int)g_info.width - 1 - mi.x) : mi.x;
    g_mouse_y = g_cfg_mouse_inv_y ? ((int)g_info.height - 1 - mi.y) : mi.y;
    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= (int)g_info.width) g_mouse_x = (int)g_info.width - 1;
    if (g_mouse_y >= (int)g_info.height) g_mouse_y = (int)g_info.height - 1;
    b = mi.buttons;
    if (g_cfg_mouse_inv_btn) {
        uint32_t l = (b & 1u);
        uint32_t r = (b & 2u);
        b &= ~3u;
        b |= (l ? 2u : 0u) | (r ? 1u : 0u);
    }
    g_mouse_btn = b;
}

static int poll_escape_or_backspace(void) {
    int i;
    if (g_fd_kbd < 0) return 0;
    for (i = 0; i < 16; i++) {
        dev_keyboard_event_t ev;
        if (ioctl(g_fd_kbd, DEV_IOCTL_KBD_GET_EVENT, &ev) != 0) break;
        if (!ev.pressed) continue;
        if (ev.scancode == 0x01u || ev.ascii == 27 || ev.scancode == 0x0Eu) return 1;
    }
    return 0;
}

int main(void) {
    uint32_t frame_bytes;

    g_fd_fb = open("/dev/vesa", 0);
    if (g_fd_fb < 0) return 1;
    if (ioctl(g_fd_fb, DEV_IOCTL_VESA_GET_INFO, &g_info) != 0) return 1;
    frame_bytes = g_info.pitch * g_info.height;
    if (frame_bytes == 0 || frame_bytes > sizeof(g_fb)) return 1;
    if (g_info.bpp != 24 && g_info.bpp != 32) return 1;
    if (load_psf_font() != 0) return 1;

    g_fd_mouse = open("/dev/mouse", 0);
    g_fd_kbd = open("/dev/keyboard", 0);
    g_fd_power = open("/dev/power", 0);
    apply_theme(g_cfg_theme);

    while (1) {
        poll_mouse();
        if (poll_escape_or_backspace()) {
            do_back();
            return 0;
        }

        if ((g_mouse_btn & 1u) && !(g_prev_mouse_btn & 1u)) {
            handle_click();
        }
        g_prev_mouse_btn = g_mouse_btn;

        draw_topbar();
        render_settings();
        draw_cursor();
        (void)write(g_fd_fb, g_fb, frame_bytes);
        sleep(16);
    }

    return 0;
}
