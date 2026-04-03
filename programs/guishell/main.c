#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <devctl.h>
#include <ui_sdk.h>
#include <dylib.h>
#include <hgui_api.h>
#include <hgui/app.h>

#define FB_MAX_BYTES (3u * 1024u * 1024u)
#define TERM_BUF_LINES 256
#define TERM_BUF_COLS 512
#define MENU_ITEMS 5
#define TOP_PANEL_H 28
#define BOTTOM_PANEL_H 30
#define EVT_QUEUE_CAP 64

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
static uint32_t C_TERM_BG      = 0xF1F5FB;
static uint32_t C_TERM_TEXT    = 0x213147;

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

typedef enum {
    WIN_TERMINAL = 0,
    WIN_SETTINGS = 1,
    WIN_TASKMGR = 2,
    WIN_COUNT = 3,
} win_id_t;

typedef struct {
    uint8_t visible;
    uint8_t minimized;
    uint8_t active;
    rect_t rc;
    int min_w;
    int min_h;
    const char *title;
} gui_window_t;

typedef enum {
    ACT_OPEN_TERMINAL = 0,
    ACT_OPEN_SETTINGS = 1,
    ACT_OPEN_TASKMGR = 2,
    ACT_REBOOT = 3,
    ACT_POWEROFF = 4,
} action_t;

typedef struct {
    const char *label;
    action_t action;
} menu_item_t;

typedef enum {
    EVT_NONE = 0,
    EVT_MOUSE_MOVE = 1,
    EVT_MOUSE_DOWN = 2,
    EVT_MOUSE_UP = 3,
    EVT_KEY = 4,
} evt_type_t;

typedef struct {
    evt_type_t type;
    int x;
    int y;
    uint32_t buttons;
    dev_keyboard_event_t key;
} gui_event_t;

static const rect_t g_apps_btn = { 8, 4, 24, 20 };
static const rect_t g_menu_box = { 8, TOP_PANEL_H + 2, 216, 24 * MENU_ITEMS + 8 };

static const menu_item_t g_menu[MENU_ITEMS] = {
    { "Terminal", ACT_OPEN_TERMINAL },
    { "Settings", ACT_OPEN_SETTINGS },
    { "Task Manager", ACT_OPEN_TASKMGR },
    { "Reboot", ACT_REBOOT },
    { "Power Off", ACT_POWEROFF },
};

static uint8_t g_fb[FB_MAX_BYTES];
static dev_fb_info_t g_info;
static int g_fd_fb = -1;
static int g_fd_mouse = -1;
static int g_fd_power = -1;
static int g_fd_kbd = -1;
static int g_fd_ptmx = -1;
static int g_fd_pty_master = -1;
static int32_t g_pty_shell_pid = -1;
static dev_pty_alloc_t g_pty_alloc;
static uint8_t g_pty_alloc_valid = 0;
static int g_mouse_x = 20;
static int g_mouse_y = 20;
static uint32_t g_mouse_btn = 0;
static uint32_t g_prev_mouse_btn = 0;
static int g_prev_active = 0;
static uint32_t g_tty_index = 0;
static psf_font_t g_font;

static gui_window_t g_windows[WIN_COUNT] = {
    { 0, 0, 0, { 38, 72, 640, 340 }, 420, 240, "Terminal" },
    { 0, 0, 0, { 170, 94, 560, 320 }, 460, 300, "Settings" },
    { 0, 0, 0, { 220, 140, 440, 240 }, 380, 220, "Task Manager" },
};

static uint8_t g_zorder[WIN_COUNT] = { WIN_TERMINAL, WIN_SETTINGS, WIN_TASKMGR };
static uint32_t g_zcount = WIN_COUNT;

static int g_menu_open = 0;
static int g_hover_menu = -1;
static int g_hover_task = -1;

static char g_term_buf[TERM_BUF_LINES][TERM_BUF_COLS];
static uint32_t g_term_row = 0;
static uint32_t g_term_col = 0;

static char g_status_mode[16] = "unknown";
static char g_status_ip[32] = "0.0.0.0";
static uint32_t g_status_mx = 0;
static uint32_t g_status_my = 0;
static uint32_t g_status_btn = 0;
static uint32_t g_status_tick = 0;
static int g_drag_window = -1;
static int g_resize_window = -1;
static int g_drag_off_x = 0;
static int g_drag_off_y = 0;
static int g_resize_start_x = 0;
static int g_resize_start_y = 0;
static int g_resize_start_w = 0;
static int g_resize_start_h = 0;
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
static char g_term_input_line[128];
static uint32_t g_term_input_len = 0;
static int g_force_redraw = 0;
static int g_hgui_handle = -1;
static hgui_api_t g_hgui_api;
static uint8_t g_hgui_loaded = 0;
static uint32_t g_lang_switch_cooldown_until = 0;
static hq_application_t g_ui_app;
static hq_widget_t g_ui_root;

static rect_t g_clip = { 0, 0, 0, 0 };
static uint8_t g_clip_enabled = 0;
static rect_t g_clip_stack[8];
static uint8_t g_clip_stack_en[8];
static uint8_t g_clip_depth = 0;

static gui_event_t g_evt_q[EVT_QUEUE_CAP];
static uint32_t g_evt_head = 0;
static uint32_t g_evt_tail = 0;
static uint32_t g_evt_count = 0;

static void handle_click(void);
static void update_drag(void);
static void update_resize(void);

static void fallback_layout_begin(ui_layout_t *lay, ui_rect_t bounds, int padding, int row_h, int gap) {
    if (!lay) return;
    lay->outer = bounds;
    lay->cursor_x = bounds.x + padding;
    lay->cursor_y = bounds.y + padding;
    lay->gap = gap;
    lay->row_h = row_h;
}

static ui_rect_t fallback_layout_take_row(ui_layout_t *lay, int h) {
    ui_rect_t r = {0, 0, 0, 0};
    int row_h;
    if (!lay) return r;
    row_h = (h > 0) ? h : lay->row_h;
    r.x = lay->cursor_x;
    r.y = lay->cursor_y;
    r.w = lay->outer.w - (lay->cursor_x - lay->outer.x) * 2;
    r.h = row_h;
    lay->cursor_y += row_h + lay->gap;
    return r;
}

static void ui_begin(ui_layout_t *lay, ui_rect_t bounds, int padding, int row_h, int gap) {
    if (g_hgui_loaded && g_hgui_api.layout_begin) {
        g_hgui_api.layout_begin(lay, bounds, padding, row_h, gap);
        return;
    }
    fallback_layout_begin(lay, bounds, padding, row_h, gap);
}

static ui_rect_t ui_take_row(ui_layout_t *lay, int h) {
    if (g_hgui_loaded && g_hgui_api.layout_take_row) return g_hgui_api.layout_take_row(lay, h);
    return fallback_layout_take_row(lay, h);
}

static int inside(int x, int y, const rect_t *r) {
    return (x >= r->x && y >= r->y && x < (r->x + r->w) && y < (r->y + r->h));
}

static int inside_rect(int x, int y, rect_t r) {
    return inside(x, y, &r);
}

static void set_clip_rect(int x, int y, int w, int h) {
    g_clip.x = x;
    g_clip.y = y;
    g_clip.w = w;
    g_clip.h = h;
    g_clip_enabled = 1;
}

static void clear_clip_rect(void) {
    g_clip_enabled = 0;
}

static void clip_push_rect(int x, int y, int w, int h) {
    int nx = x;
    int ny = y;
    int nx2 = x + w;
    int ny2 = y + h;
    if (g_clip_depth < 8u) {
        g_clip_stack[g_clip_depth] = g_clip;
        g_clip_stack_en[g_clip_depth] = g_clip_enabled;
        g_clip_depth++;
    }
    if (g_clip_enabled) {
        int cx = g_clip.x;
        int cy = g_clip.y;
        int cx2 = g_clip.x + g_clip.w;
        int cy2 = g_clip.y + g_clip.h;
        if (nx < cx) nx = cx;
        if (ny < cy) ny = cy;
        if (nx2 > cx2) nx2 = cx2;
        if (ny2 > cy2) ny2 = cy2;
    }
    if (nx2 < nx) nx2 = nx;
    if (ny2 < ny) ny2 = ny;
    set_clip_rect(nx, ny, nx2 - nx, ny2 - ny);
}

static void clip_pop_rect(void) {
    if (g_clip_depth == 0u) return;
    g_clip_depth--;
    g_clip = g_clip_stack[g_clip_depth];
    g_clip_enabled = g_clip_stack_en[g_clip_depth];
}

static int buf_append(char *dst, uint32_t cap, const char *s) {
    uint32_t a;
    uint32_t b;
    if (!dst || !s || cap == 0u) return -1;
    a = (uint32_t)strlen(dst);
    b = (uint32_t)strlen(s);
    if (a + b + 1u > cap) return -1;
    memcpy(dst + a, s, b + 1u);
    return 0;
}

static void evt_push(const gui_event_t *ev) {
    if (!ev) return;
    if (g_evt_count >= EVT_QUEUE_CAP) {
        g_evt_head = (g_evt_head + 1u) % EVT_QUEUE_CAP;
        g_evt_count--;
    }
    g_evt_q[g_evt_tail] = *ev;
    g_evt_tail = (g_evt_tail + 1u) % EVT_QUEUE_CAP;
    g_evt_count++;
}

static int evt_pop(gui_event_t *out) {
    if (!out || g_evt_count == 0u) return -1;
    *out = g_evt_q[g_evt_head];
    g_evt_head = (g_evt_head + 1u) % EVT_QUEUE_CAP;
    g_evt_count--;
    return 0;
}

static void u32_to_dec(uint32_t v, char *out, uint32_t cap) {
    char t[16];
    uint32_t i = 0;
    uint32_t j = 0;
    if (!out || cap < 2u) return;
    if (v == 0u) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v && i < sizeof(t)) {
        t[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (j < i && j + 1u < cap) {
        out[j] = t[i - 1u - j];
        j++;
    }
    out[j] = '\0';
}

static void uptime_label(char *out, uint32_t cap) {
    uint32_t sec = get_ticks() / 1000u;
    uint32_t h = sec / 3600u;
    uint32_t m = (sec % 3600u) / 60u;
    char s1[16];
    char s2[16];
    if (!out || cap < 8u) return;
    u32_to_dec(h, s1, sizeof(s1));
    u32_to_dec(m, s2, sizeof(s2));
    strcpy(out, "up ");
    (void)buf_append(out, cap, s1);
    (void)buf_append(out, cap, "h ");
    (void)buf_append(out, cap, s2);
    (void)buf_append(out, cap, "m");
}

static uint32_t logical_cores_count(void) {
    uint32_t eax = 1;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    __asm__ __volatile__("cpuid"
        : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx)
        :
        : "memory");
    if (((ebx >> 16) & 0xFFu) == 0u) return 1u;
    return (ebx >> 16) & 0xFFu;
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
        C_TERM_BG      = 0xFFF6E7;
        C_TERM_TEXT    = 0x352515;
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
        C_TERM_BG      = 0xECF8F0;
        C_TERM_TEXT    = 0x173526;
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
    C_TERM_BG      = 0xF1F5FB;
    C_TERM_TEXT    = 0x213147;
}

static void apply_keyboard_layout(uint32_t idx) {
    if (g_fd_kbd < 0) return;
    (void)ioctl(g_fd_kbd, DEV_IOCTL_KBD_SET_LAYOUT, &idx);
}

static uint32_t get_keyboard_layout(void) {
    dev_keyboard_info_t ki;
    if (g_fd_kbd < 0) return 0u;
    if (ioctl(g_fd_kbd, DEV_IOCTL_KBD_GET_INFO, &ki) != 0) return 0u;
    return ki.layout;
}

static void toggle_keyboard_layout(void) {
    uint32_t now = get_ticks();
    uint32_t cur;
    uint32_t next;
    if (now < g_lang_switch_cooldown_until) return;
    g_lang_switch_cooldown_until = now + 180u;
    cur = get_keyboard_layout();
    next = (cur == 0u) ? 1u : 0u;
    apply_keyboard_layout(next);
    g_cfg_kbd_layout = (uint8_t)next;
    g_force_redraw = 1;
}

static int parse_state_value(const char *key, char *out, uint32_t cap) {
    char buf[256];
    char *line;
    char *nl;
    uint32_t klen;
    int fd = open("/run/netd.state", 0);
    int32_t n;
    if (!out || cap < 2u || !key) return -1;
    out[0] = '\0';
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1u);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    klen = (uint32_t)strlen(key);
    line = buf;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            strncpy(out, line + klen + 1u, cap - 1u);
            out[cap - 1u] = '\0';
            return 0;
        }
        if (!nl) break;
        line = nl + 1;
    }
    return -1;
}

static void refresh_status_cache(void) {
    uint32_t now;
    dev_mouse_info_t mi;
    now = get_ticks();
    if (now - g_status_tick >= 300u) {
        char mode[16];
        char ip[32];
        strcpy(mode, "unknown");
        strcpy(ip, "0.0.0.0");
        (void)parse_state_value("mode", mode, sizeof(mode));
        (void)parse_state_value("ip", ip, sizeof(ip));
        strncpy(g_status_mode, mode, sizeof(g_status_mode) - 1u);
        g_status_mode[sizeof(g_status_mode) - 1u] = '\0';
        strncpy(g_status_ip, ip, sizeof(g_status_ip) - 1u);
        g_status_ip[sizeof(g_status_ip) - 1u] = '\0';
        g_status_tick = now;
    }
    if (g_fd_mouse >= 0 && ioctl(g_fd_mouse, DEV_IOCTL_MOUSE_GET_INFO, &mi) == 0) {
        if (mi.x > 0) g_status_mx = (uint32_t)mi.x;
        else g_status_mx = 0u;
        if (mi.y > 0) g_status_my = (uint32_t)mi.y;
        else g_status_my = 0u;
        g_status_btn = mi.buttons;
    }
}

static void detect_current_tty(void) {
    dev_tty_info_t ti;
    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_INFO, &ti) == 0) {
        g_tty_index = ti.index;
        return;
    }
    g_tty_index = 2;
}

static int is_graphics_tty_active(void) {
    uint32_t idx = 0xFFFFFFFFu;
    int fd;

    if (ioctl(fileno(stdout), DEV_IOCTL_TTY_GET_ACTIVE, &idx) == 0) {
        return (idx == g_tty_index) ? 1 : 0;
    }

    idx = 0xFFFFFFFFu;
    fd = open("/dev/tty/1", 0);
    if (fd >= 0) {
        if (ioctl(fd, DEV_IOCTL_TTY_GET_ACTIVE, &idx) == 0) {
            close(fd);
            return (idx == g_tty_index) ? 1 : 0;
        }
        close(fd);
    }

    return 1;
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
    if (g_clip_enabled && !inside(x, y, &g_clip)) return;
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
    if (g_clip_enabled) {
        int cx0 = g_clip.x;
        int cy0 = g_clip.y;
        int cx1 = g_clip.x + g_clip.w;
        int cy1 = g_clip.y + g_clip.h;
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
    }
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

static void draw_cursor(int x, int y) {
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
    fill_rect(x + 2, y + h - 3, 2, 4, 0x101010);
    fill_rect(x + 2, y + h - 2, 3, 2, 0xFFFFFF);
}

static rect_t win_close_rect(const gui_window_t *w) {
    rect_t r;
    r.x = w->rc.x + w->rc.w - 22;
    r.y = w->rc.y + 4;
    r.w = 16;
    r.h = 16;
    return r;
}

static rect_t win_min_rect(const gui_window_t *w) {
    rect_t r;
    r.x = w->rc.x + w->rc.w - 42;
    r.y = w->rc.y + 4;
    r.w = 16;
    r.h = 16;
    return r;
}

static rect_t win_title_rect(const gui_window_t *w) {
    rect_t r;
    r.x = w->rc.x;
    r.y = w->rc.y;
    r.w = w->rc.w;
    r.h = 24;
    return r;
}

static rect_t win_resize_rect(const gui_window_t *w) {
    rect_t r;
    r.x = w->rc.x + w->rc.w - 14;
    r.y = w->rc.y + w->rc.h - 14;
    r.w = 12;
    r.h = 12;
    return r;
}

static int settings_left_panel_width(const gui_window_t *w) {
    int inner_w;
    int left_w;
    if (!w) return 170;
    inner_w = w->rc.w - 24;
    left_w = inner_w / 3;
    if (left_w < 170) left_w = 170;
    if (left_w > 220) left_w = 220;
    if (left_w > inner_w - 140) left_w = inner_w - 140;
    if (left_w < 140) left_w = 140;
    return left_w;
}

static rect_t settings_tab_rect(const gui_window_t *w, int idx) {
    rect_t r;
    int x = w->rc.x + 22;
    int left_w = settings_left_panel_width(w);
    int y = w->rc.y + 58 + idx * 24;
    r.x = x;
    r.y = y;
    r.w = left_w - 20;
    r.h = 20;
    return r;
}

static rect_t settings_body_rect(const gui_window_t *w) {
    rect_t r;
    int x = w->rc.x + 12;
    int y = w->rc.y + 34;
    int left_w = settings_left_panel_width(w);
    int right_x = x + left_w + 10;
    int right_w = w->rc.w - 24 - left_w - 10;
    int right_h = w->rc.h - 52;
    r.x = right_x;
    r.y = y;
    r.w = right_w;
    r.h = right_h;
    return r;
}

static rect_t settings_content_rect(const gui_window_t *w) {
    rect_t b = settings_body_rect(w);
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

typedef struct {
    rect_t area;
    int cursor;
    int gap;
    uint8_t vertical;
} ui_flex_t;

static void ui_flex_begin(ui_flex_t *f, rect_t area, int gap, uint8_t vertical) {
    if (!f) return;
    f->area = area;
    f->cursor = vertical ? area.y : area.x;
    f->gap = gap;
    f->vertical = vertical ? 1u : 0u;
}

static rect_t ui_flex_take(ui_flex_t *f, int span) {
    rect_t r = {0, 0, 0, 0};
    if (!f) return r;
    if (f->vertical) {
        r.x = f->area.x;
        r.y = f->cursor;
        r.w = f->area.w;
        r.h = span;
        f->cursor += span + f->gap;
    } else {
        r.x = f->cursor;
        r.y = f->area.y;
        r.w = span;
        r.h = f->area.h;
        f->cursor += span + f->gap;
    }
    return r;
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

static int collect_task_windows(win_id_t *out, int cap) {
    int n = 0;
    int i;
    if (!out || cap <= 0) return 0;
    for (i = 0; i < WIN_COUNT; i++) {
        if (!g_windows[i].visible) continue;
        if (n < cap) out[n++] = (win_id_t)i;
    }
    return n;
}

static void set_active_window(int idx) {
    int i;
    for (i = 0; i < WIN_COUNT; i++) g_windows[i].active = 0;
    if (idx >= 0 && idx < WIN_COUNT) g_windows[idx].active = 1;
}

static void bring_to_front(int idx) {
    uint32_t i;
    uint32_t pos = g_zcount;
    if (idx < 0 || idx >= WIN_COUNT) return;
    for (i = 0; i < g_zcount; i++) {
        if (g_zorder[i] == (uint8_t)idx) {
            pos = i;
            break;
        }
    }
    if (pos >= g_zcount) return;
    for (i = pos; i + 1u < g_zcount; i++) g_zorder[i] = g_zorder[i + 1u];
    g_zorder[g_zcount - 1u] = (uint8_t)idx;
}

static void open_window(win_id_t id) {
    if ((int)id < 0 || (int)id >= WIN_COUNT) return;
    g_windows[id].visible = 1;
    g_windows[id].minimized = 0;
    bring_to_front((int)id);
    set_active_window((int)id);
}

static void activate_top_visible(void) {
    int i;
    for (i = WIN_COUNT - 1; i >= 0; i--) {
        int wi = (int)g_zorder[i];
        if (g_windows[wi].visible && !g_windows[wi].minimized) {
            set_active_window(wi);
            return;
        }
    }
    set_active_window(-1);
}

static void close_window(win_id_t id) {
    if ((int)id < 0 || (int)id >= WIN_COUNT) return;
    if (g_drag_window == (int)id) g_drag_window = -1;
    g_windows[id].visible = 0;
    g_windows[id].minimized = 0;
    g_windows[id].active = 0;
    activate_top_visible();
}

static void minimize_window(win_id_t id) {
    if ((int)id < 0 || (int)id >= WIN_COUNT) return;
    if (!g_windows[id].visible) return;
    if (g_drag_window == (int)id) g_drag_window = -1;
    g_windows[id].minimized = 1;
    g_windows[id].active = 0;
    activate_top_visible();
}

static void toggle_window(win_id_t id) {
    if (!g_windows[id].visible) {
        open_window(id);
        return;
    }
    if (g_windows[id].minimized) {
        g_windows[id].minimized = 0;
        bring_to_front((int)id);
        set_active_window((int)id);
        return;
    }
    if (g_windows[id].active) {
        minimize_window(id);
    } else {
        g_windows[id].minimized = 0;
        bring_to_front((int)id);
        set_active_window((int)id);
    }
}

static int top_window_at(int x, int y) {
    int i;
    for (i = (int)g_zcount - 1; i >= 0; i--) {
        int wi = (int)g_zorder[i];
        if (!g_windows[wi].visible || g_windows[wi].minimized) continue;
        if (inside(x, y, &g_windows[wi].rc)) return wi;
    }
    return -1;
}

static void term_clear(void) {
    memset(g_term_buf, ' ', sizeof(g_term_buf));
    g_term_row = 0;
    g_term_col = 0;
}

static void term_scroll_up(void) {
    uint32_t i;
    for (i = 1; i < TERM_BUF_LINES; i++) {
        memcpy(g_term_buf[i - 1u], g_term_buf[i], TERM_BUF_COLS);
    }
    memset(g_term_buf[TERM_BUF_LINES - 1u], ' ', TERM_BUF_COLS);
    g_term_row = TERM_BUF_LINES - 1u;
    g_term_col = 0;
}

static void term_newline(void) {
    g_term_col = 0;
    if (g_term_row + 1u >= TERM_BUF_LINES) term_scroll_up();
    else g_term_row++;
}

static void term_putc(char ch) {
    if (ch == '\r') {
        g_term_col = 0;
        return;
    }
    if (ch == '\n') {
        term_newline();
        return;
    }
    if (ch == '\b') {
        if (g_term_col > 0u) {
            g_term_col--;
            g_term_buf[g_term_row][g_term_col] = ' ';
        }
        return;
    }
    if (ch == '\t') {
        uint32_t n = 4u - (g_term_col % 4u);
        while (n--) term_putc(' ');
        return;
    }
    if ((uint8_t)ch < 32u || (uint8_t)ch > 126u) return;
    g_term_buf[g_term_row][g_term_col] = ch;
    g_term_col++;
    if (g_term_col >= TERM_BUF_COLS) term_newline();
}

static void term_puts(const char *s) {
    uint32_t i = 0;
    if (!s) return;
    while (s[i]) term_putc(s[i++]);
}

static void load_hgui_runtime(void) {
    int h;
    memset(&g_hgui_api, 0, sizeof(g_hgui_api));
    g_hgui_loaded = 0;
    h = dl_open("/lib/libhgui.so");
    if (h < 0) return;
    {
        int (*get_api)(uint32_t, hgui_api_t*, uint32_t);
        get_api = (int (*)(uint32_t, hgui_api_t*, uint32_t))dl_sym(h, "hgui_get_api");
        if (!get_api) {
            dl_close(h);
            return;
        }
        if (get_api(HGUI_API_VERSION, &g_hgui_api, sizeof(g_hgui_api)) != 0) {
            dl_close(h);
            return;
        }
    }
    g_hgui_handle = h;
    g_hgui_loaded = 1;
}

static void pty_close_shell(void) {
    if (g_fd_pty_master >= 0) {
        close(g_fd_pty_master);
        g_fd_pty_master = -1;
    }
    if (g_fd_ptmx >= 0 && g_pty_alloc_valid) {
        uint32_t idx = g_pty_alloc.index;
        (void)ioctl(g_fd_ptmx, DEV_IOCTL_PTY_FREE, &idx);
    }
    if (g_fd_ptmx >= 0) {
        close(g_fd_ptmx);
        g_fd_ptmx = -1;
    }
    g_pty_shell_pid = -1;
    g_term_input_len = 0u;
    g_term_input_line[0] = '\0';
    g_pty_alloc_valid = 0;
    memset(&g_pty_alloc, 0, sizeof(g_pty_alloc));
}

static int pty_start_shell(void) {
    int32_t pid;
    g_fd_ptmx = open("/dev/ptmx", 0);
    if (g_fd_ptmx < 0) return -1;
    memset(&g_pty_alloc, 0, sizeof(g_pty_alloc));
    if (ioctl(g_fd_ptmx, DEV_IOCTL_PTY_ALLOC, &g_pty_alloc) != 0) {
        pty_close_shell();
        return -1;
    }
    g_pty_alloc_valid = 1;
    g_fd_pty_master = open(g_pty_alloc.master_path, 0);
    if (g_fd_pty_master < 0) {
        pty_close_shell();
        return -1;
    }
    pid = spawn("/bin/sh", g_pty_alloc.slave_path);
    if (pid < 0) {
        pty_close_shell();
        return -1;
    }
    g_pty_shell_pid = pid;
    return 0;
}

static int poll_pty_output(void) {
    uint32_t readable;
    uint8_t tmp[256];
    int32_t n;
    int changed = 0;
    if (g_fd_pty_master < 0) return 0;
    while (ioctl(g_fd_pty_master, DEV_IOCTL_PTY_GET_READABLE, &readable) == 0 && readable > 0u) {
        uint32_t ask = (readable > sizeof(tmp)) ? (uint32_t)sizeof(tmp) : readable;
        n = read(g_fd_pty_master, tmp, ask);
        if (n <= 0) break;
        for (int32_t i = 0; i < n; i++) term_putc((char)tmp[i]);
        changed = 1;
    }
    return changed;
}

static void term_input_backspace(void) {
    if (g_term_input_len == 0u) return;
    g_term_input_len--;
    g_term_input_line[g_term_input_len] = '\0';
    term_putc('\b');
    g_force_redraw = 1;
}

static void term_input_put(char ch) {
    if (g_term_input_len + 1u >= sizeof(g_term_input_line)) return;
    if ((uint8_t)ch < 32u || (uint8_t)ch > 126u) return;
    g_term_input_line[g_term_input_len++] = ch;
    g_term_input_line[g_term_input_len] = '\0';
    term_putc(ch);
    g_force_redraw = 1;
}

static void term_input_submit(void) {
    uint32_t i;
    char nl = '\n';
    for (i = 0; i < g_term_input_len; i++) {
        (void)write(g_fd_pty_master, &g_term_input_line[i], 1u);
    }
    (void)write(g_fd_pty_master, &nl, 1u);
    term_putc('\n');
    g_term_input_len = 0u;
    g_term_input_line[0] = '\0';
    g_force_redraw = 1;
}

static void poll_keyboard_events(void) {
    int i;
    if (g_fd_kbd < 0) return;
    for (i = 0; i < 24; i++) {
        dev_keyboard_event_t ev;
        if (ioctl(g_fd_kbd, DEV_IOCTL_KBD_GET_EVENT, &ev) != 0) break;
        if (ev.pressed) {
            gui_event_t e;
            memset(&e, 0, sizeof(e));
            e.type = EVT_KEY;
            e.key = ev;
            evt_push(&e);
        }
    }
}

static void process_key_event(const dev_keyboard_event_t *k) {
    int lang_combo = 0;
    if (!k) return;
    if (g_cfg_kbd_switch_hotkey == 0u) {
        if (k->alt && k->shift && (k->scancode == 0x2Au || k->scancode == 0x36u || k->scancode == 0x38u)) {
            lang_combo = 1;
        }
    } else {
        if (k->ctrl && k->shift && (k->scancode == 0x2Au || k->scancode == 0x36u || k->scancode == 0x1Du)) {
            lang_combo = 1;
        }
    }
    if (lang_combo) {
        toggle_keyboard_layout();
        return;
    }
    if (!g_windows[WIN_TERMINAL].visible || g_windows[WIN_TERMINAL].minimized || !g_windows[WIN_TERMINAL].active) {
        return;
    }
    if (g_fd_pty_master < 0) return;
    if (k->ascii == '\n' || k->ascii == '\r' || k->scancode == 0x1Cu) {
        term_input_submit();
        return;
    }
    if (k->ascii == '\b' || k->scancode == 0x0Eu) {
        term_input_backspace();
        return;
    }
    if (k->ascii >= 32 && k->ascii <= 126) term_input_put((char)k->ascii);
}

static void dispatch_events(int active) {
    while (1) {
        gui_event_t e;
        if (evt_pop(&e) != 0) break;
        if (!active) continue;
        if (e.type == EVT_MOUSE_DOWN) {
            handle_click();
        } else if (e.type == EVT_MOUSE_UP) {
            g_drag_window = -1;
            g_resize_window = -1;
        } else if (e.type == EVT_MOUSE_MOVE) {
            if (g_drag_window >= 0 && (g_mouse_btn & 1u)) update_drag();
            if (g_resize_window >= 0 && (g_mouse_btn & 1u)) update_resize();
        } else if (e.type == EVT_KEY) {
            process_key_event(&e.key);
        }
    }
}

static void render_terminal_window(const gui_window_t *w) {
    int tx = w->rc.x + 10;
    int ty = w->rc.y + 30;
    int tw = w->rc.w - 20;
    int th = w->rc.h - 40;
    uint32_t vis_cols;
    uint32_t vis_rows;
    uint32_t total_wrapped = 0;
    uint32_t start_wrapped = 0;
    uint32_t draw_row = 0;
    uint32_t src_row;

    fill_rect(tx, ty, tw, th, C_TERM_BG);
    vis_cols = (uint32_t)((tw - 12) / 8);
    vis_rows = (uint32_t)((th - 10) / 16);
    if (vis_cols == 0u || vis_rows == 0u) return;
    if (vis_cols > TERM_BUF_COLS) vis_cols = TERM_BUF_COLS;

    for (src_row = 0; src_row <= g_term_row && src_row < TERM_BUF_LINES; src_row++) {
        uint32_t len = TERM_BUF_COLS;
        uint32_t wraps;
        while (len > 0u && g_term_buf[src_row][len - 1u] == ' ') len--;
        if (len == 0u) len = 1u;
        wraps = (len + vis_cols - 1u) / vis_cols;
        total_wrapped += wraps;
    }
    if (total_wrapped > vis_rows) start_wrapped = total_wrapped - vis_rows;

    for (src_row = 0; src_row <= g_term_row && src_row < TERM_BUF_LINES; src_row++) {
        uint32_t len = TERM_BUF_COLS;
        uint32_t wraps;
        uint32_t seg;
        while (len > 0u && g_term_buf[src_row][len - 1u] == ' ') len--;
        if (len == 0u) len = 1u;
        wraps = (len + vis_cols - 1u) / vis_cols;
        for (seg = 0; seg < wraps; seg++) {
            uint32_t global_row = draw_row++;
            uint32_t off;
            uint32_t n;
            char line[TERM_BUF_COLS + 1];
            if (global_row < start_wrapped) continue;
            if (global_row - start_wrapped >= vis_rows) return;
            off = seg * vis_cols;
            if (off >= len) n = 0u;
            else {
                n = len - off;
                if (n > vis_cols) n = vis_cols;
            }
            if (n > 0u) memcpy(line, g_term_buf[src_row] + off, n);
            line[n] = '\0';
            draw_text(tx + 6, ty + 6 + (int)(global_row - start_wrapped) * 16, line, C_TERM_TEXT);
        }
    }
}

static void render_settings_window(const gui_window_t *w) {
    char line[160];
    rect_t left_panel;
    rect_t right_panel;
    rect_t body = settings_body_rect(w);
    rect_t area = settings_content_rect(w);
    rect_t g0, g1;
    ui_layout_t lay;
    ui_rect_t content;
    ui_rect_t row;
    static const char *tabs[] = {
        "Appearance", "Input Devices", "Network", "Display", "Bootloader", "Terminal", "About"
    };
    int i;

    left_panel.x = w->rc.x + 12;
    left_panel.y = w->rc.y + 34;
    left_panel.w = settings_left_panel_width(w);
    left_panel.h = w->rc.h - 52;
    right_panel = body;

    fill_rect(left_panel.x - 4, left_panel.y - 4, w->rc.w - 24, w->rc.h - 44, C_SURFACE);
    stroke_rect(left_panel.x - 4, left_panel.y - 4, w->rc.w - 24, w->rc.h - 44, C_BORDER);

    fill_rect(left_panel.x, left_panel.y, left_panel.w, left_panel.h, C_SURFACE_ALT);
    stroke_rect(left_panel.x, left_panel.y, left_panel.w, left_panel.h, C_BORDER);
    draw_text(left_panel.x + 10, left_panel.y + 8, "System Settings", C_TEXT);
    for (i = 0; i < 7; i++) {
        rect_t tr = settings_tab_rect(w, i);
        fill_rect(tr.x, tr.y, tr.w, tr.h, (g_settings_tab == (uint8_t)i) ? C_PRIMARY_SOFT : C_SURFACE_ALT);
        draw_text_fit(tr.x + 8, tr.y + 6, tr.w - 12, tabs[i], (g_settings_tab == (uint8_t)i) ? C_TEXT : C_TEXT_SOFT);
    }

    fill_rect(right_panel.x, right_panel.y, right_panel.w, right_panel.h, C_SURFACE_ALT);
    stroke_rect(right_panel.x, right_panel.y, right_panel.w, right_panel.h, C_BORDER);
    content.x = body.x;
    content.y = body.y;
    content.w = body.w;
    content.h = body.h;
    ui_begin(&lay, content, 10, 22, 8);

    if (g_settings_tab == 0u) {
        rect_t c0;
        rect_t c1;
        rect_t c2;
        rect_t c3;
        int cols;
        int col_w;
        rect_t content0;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Appearance", C_TEXT);
        g0 = area;
        content0 = ui_panel_content(g0, "Theme");
        draw_text_fit(content0.x + 4, content0.y + 2, content0.w - 8, "Pick palette:", C_TEXT_SOFT);
        cols = settings_theme_cols(g0);
        col_w = (g0.w - 24 - (cols - 1) * 12) / cols;
        if (col_w < 70) col_w = 70;
        c0 = settings_theme_opt_rect(g0, 0);
        c1 = settings_theme_opt_rect(g0, 1);
        c2 = settings_theme_opt_rect(g0, 2);
        c3 = settings_theme_opt_rect(g0, 3);
        clip_push_rect(g0.x + 1, g0.y + 1, g0.w - 2, g0.h - 2);
        settings_check_draw(c0, g_cfg_theme == 0u);
        settings_check_draw(c1, g_cfg_theme == 1u);
        settings_check_draw(c2, g_cfg_theme == 2u);
        settings_check_draw(c3, g_cfg_cursor_compact);
        draw_text_fit(c0.x + 26, c0.y + 3, col_w - 28, "Breeze Light", C_TEXT_SOFT);
        draw_text_fit(c1.x + 26, c1.y + 3, col_w - 28, "Warm Sand", C_TEXT_SOFT);
        draw_text_fit(c2.x + 26, c2.y + 3, col_w - 28, "Mint", C_TEXT_SOFT);
        draw_text_fit(c3.x + 26, c3.y + 3, col_w - 28, "Compact cursor", C_TEXT_SOFT);
        clip_pop_rect();
    } else if (g_settings_tab == 1u) {
        rect_t mk0;
        rect_t mk1;
        rect_t mk2;
        rect_t mk3;
        rect_t kb0;
        rect_t kb1;
        rect_t kb2;
        rect_t kb3;
        ui_flex_t vf;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Input Devices", C_TEXT);
        ui_flex_begin(&vf, area, 8, 1u);
        g0 = ui_flex_take(&vf, (area.h - 8) / 2);
        g1 = ui_flex_take(&vf, area.h - 8 - ((area.h - 8) / 2));
        g0 = ui_panel_content(g0, "Mouse");
        mk0 = settings_check_rect(g0, 12, 30);
        mk1 = settings_check_rect(g0, 12, 54);
        mk2 = settings_check_rect(g0, 12, 78);
        mk3 = settings_check_rect(g0, 12, 102);
        clip_push_rect(g0.x, g0.y, g0.w, g0.h);
        settings_check_draw(mk0, g_cfg_disable_mouse_click);
        settings_check_draw(mk1, g_cfg_mouse_inv_x);
        settings_check_draw(mk2, g_cfg_mouse_inv_y);
        settings_check_draw(mk3, g_cfg_mouse_inv_btn);
        draw_text_fit(mk0.x + 26, mk0.y + 3, settings_check_label_w(g0, mk0), "Disable mouse click", C_TEXT_SOFT);
        draw_text_fit(mk1.x + 26, mk1.y + 3, settings_check_label_w(g0, mk1), "Invert X", C_TEXT_SOFT);
        draw_text_fit(mk2.x + 26, mk2.y + 3, settings_check_label_w(g0, mk2), "Invert Y", C_TEXT_SOFT);
        draw_text_fit(mk3.x + 26, mk3.y + 3, settings_check_label_w(g0, mk3), "Invert clicks", C_TEXT_SOFT);
        u32_to_dec(g_status_mx, line, sizeof(line));
        draw_text_fit(g0.x + 12, g0.y + g0.h - 22, g0.w - 20, "Current cursor position is tracked live", C_TEXT_SOFT);
        clip_pop_rect();

        g1 = ui_panel_content(g1, "Keyboard");
        kb0 = settings_check_rect(g1, 12, 30);
        kb1 = settings_check_rect(g1, 12, 54);
        kb2 = settings_check_rect(g1, (g1.w > 220) ? (g1.w / 2) : 12, 78);
        kb3 = settings_check_rect(g1, (g1.w > 220) ? (g1.w / 2) : 12, (g1.w > 220) ? 102 : 102);
        clip_push_rect(g1.x, g1.y, g1.w, g1.h);
        settings_check_draw(kb0, g_cfg_kbd_layout == 0u);
        settings_check_draw(kb1, g_cfg_kbd_layout == 1u);
        settings_check_draw(kb2, g_cfg_kbd_switch_hotkey == 0u);
        settings_check_draw(kb3, g_cfg_kbd_switch_hotkey == 1u);
        draw_text_fit(kb0.x + 26, kb0.y + 3, settings_check_label_w(g1, kb0), "Layouts: EN + RU", C_TEXT_SOFT);
        draw_text_fit(kb1.x + 26, kb1.y + 3, settings_check_label_w(g1, kb1), "Layouts: EN only", C_TEXT_SOFT);
        draw_text_fit(kb2.x + 26, kb2.y + 3, settings_check_label_w(g1, kb2), "Alt+Shift", C_TEXT_SOFT);
        draw_text_fit(kb3.x + 26, kb3.y + 3, settings_check_label_w(g1, kb3), "Ctrl+Shift", C_TEXT_SOFT);
        clip_pop_rect();
    } else if (g_settings_tab == 2u) {
        rect_t n0;
        rect_t n1;
        rect_t n2;
        ui_flex_t vf;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Network", C_TEXT);
        ui_flex_begin(&vf, area, 8, 1u);
        g0 = ui_flex_take(&vf, (area.h - 8) / 2);
        g1 = ui_flex_take(&vf, area.h - 8 - ((area.h - 8) / 2));
        g0 = ui_panel_content(g0, "Devices");
        draw_text_fit(g0.x + 12, g0.y + 30, g0.w - 20, "eth0  (up)", C_TEXT_SOFT);
        draw_text_fit(g0.x + 12, g0.y + 50, g0.w - 20, "loop0 (up)", C_TEXT_SOFT);
        n0 = settings_check_rect(g0, 12, 70);
        clip_push_rect(g0.x, g0.y, g0.w, g0.h);
        settings_check_draw(n0, g_cfg_net_use_dhcp);
        draw_text_fit(n0.x + 26, n0.y + 3, settings_check_label_w(g0, n0), "Use DHCP", C_TEXT_SOFT);
        clip_pop_rect();

        g1 = ui_panel_content(g1, "DNS");
        n1 = settings_check_rect(g1, 12, 30);
        n2 = settings_check_rect(g1, 12, 54);
        clip_push_rect(g1.x, g1.y, g1.w, g1.h);
        settings_check_draw(n1, g_cfg_dns_profile == 0u);
        settings_check_draw(n2, g_cfg_dns_profile == 1u);
        draw_text_fit(n1.x + 26, n1.y + 3, settings_check_label_w(g1, n1), "System default", C_TEXT_SOFT);
        draw_text_fit(n2.x + 26, n2.y + 3, settings_check_label_w(g1, n2), "Cloudflare 1.1.1.1", C_TEXT_SOFT);
        clip_pop_rect();
    } else if (g_settings_tab == 3u) {
        rect_t d0;
        rect_t d1;
        rect_t d2;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Display", C_TEXT);
        g0 = ui_panel_content(area, "Mode");
        d0 = settings_check_rect(g0, 12, 30);
        d1 = settings_check_rect(g0, 12, 54);
        d2 = settings_check_rect(g0, (g0.w > 220) ? (g0.w / 2) : 12, 30);
        clip_push_rect(g0.x, g0.y, g0.w, g0.h);
        settings_check_draw(d0, g_cfg_display_mode == 0u);
        settings_check_draw(d1, g_cfg_display_mode == 1u);
        settings_check_draw(d2, g_cfg_display_mode == 2u);
        draw_text_fit(d0.x + 26, d0.y + 3, settings_check_label_w(g0, d0), "1024x768", C_TEXT_SOFT);
        draw_text_fit(d1.x + 26, d1.y + 3, settings_check_label_w(g0, d1), "800x600", C_TEXT_SOFT);
        draw_text_fit(d2.x + 26, d2.y + 3, settings_check_label_w(g0, d2), "640x480", C_TEXT_SOFT);
        clip_pop_rect();
    } else if (g_settings_tab == 4u) {
        rect_t b0;
        rect_t b1;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Bootloader", C_TEXT);
        g0 = ui_panel_content(area, "Default Boot Entry");
        b0 = settings_check_rect(g0, 12, 30);
        b1 = settings_check_rect(g0, 12, 54);
        clip_push_rect(g0.x, g0.y, g0.w, g0.h);
        settings_check_draw(b0, g_cfg_boot_default == 0u);
        settings_check_draw(b1, g_cfg_boot_default == 1u);
        draw_text_fit(b0.x + 26, b0.y + 3, settings_check_label_w(g0, b0), "Normal boot", C_TEXT_SOFT);
        draw_text_fit(b1.x + 26, b1.y + 3, settings_check_label_w(g0, b1), "Safe mode", C_TEXT_SOFT);
        clip_pop_rect();
    } else if (g_settings_tab == 5u) {
        rect_t t0;
        rect_t t1;
        rect_t t2;
        draw_text(right_panel.x + 10, right_panel.y + 10, "Terminal", C_TEXT);
        g0 = ui_panel_content(area, "Behavior");
        t0 = settings_check_rect(g0, 12, 30);
        t1 = settings_check_rect(g0, 12, 54);
        t2 = settings_check_rect(g0, 12, 78);
        clip_push_rect(g0.x, g0.y, g0.w, g0.h);
        settings_check_draw(t0, g_cfg_show_top_stats);
        settings_check_draw(t1, g_cfg_cursor_compact);
        settings_check_draw(t2, g_cfg_term_autofocus);
        draw_text_fit(t0.x + 26, t0.y + 3, settings_check_label_w(g0, t0), "Show uptime in top panel", C_TEXT_SOFT);
        draw_text_fit(t1.x + 26, t1.y + 3, settings_check_label_w(g0, t1), "Compact cursor style", C_TEXT_SOFT);
        draw_text_fit(t2.x + 26, t2.y + 3, settings_check_label_w(g0, t2), "Keep terminal focused", C_TEXT_SOFT);
        clip_pop_rect();
    } else {
        draw_text(right_panel.x + 10, right_panel.y + 10, "About", C_TEXT);
        draw_text(right_panel.x + 10, right_panel.y + 34, "HouseOS Desktop Shell", C_TEXT_SOFT);
        strcpy(line, "Network mode: ");
        (void)buf_append(line, sizeof(line), g_status_mode);
        draw_text(right_panel.x + 10, right_panel.y + 54, line, C_TEXT_SOFT);
        strcpy(line, "IP: ");
        (void)buf_append(line, sizeof(line), g_status_ip);
        draw_text(right_panel.x + 10, right_panel.y + 72, line, C_TEXT_SOFT);
        row = ui_take_row(&lay, 22);
        (void)row;
        strcpy(line, "Logical CPU cores: ");
        {
            char n[16];
            u32_to_dec(logical_cores_count(), n, sizeof(n));
            (void)buf_append(line, sizeof(line), n);
        }
        draw_text(right_panel.x + 10, right_panel.y + 92, line, C_TEXT_SOFT);
    }
}

static void render_taskmgr_window(const gui_window_t *w) {
    char line[96];
    char n1[16];
    char n2[16];
    char n3[16];
    char n4[16];
    int x = w->rc.x + 14;
    int y = w->rc.y + 36;
    int32_t st;

    fill_rect(x - 6, y - 6, w->rc.w - 28, w->rc.h - 52, C_SURFACE_ALT);
    stroke_rect(x - 6, y - 6, w->rc.w - 28, w->rc.h - 52, C_BORDER);
    draw_text(x, y, "Task Manager", C_TEXT);
    u32_to_dec(logical_cores_count(), n1, sizeof(n1));
    strcpy(line, "CPU logical cores: ");
    (void)buf_append(line, sizeof(line), n1);
    draw_text_fit(x, y + 20, w->rc.w - 36, line, C_TEXT_SOFT);

    u32_to_dec((uint32_t)g_pty_shell_pid, n2, sizeof(n2));
    st = (g_pty_shell_pid >= 0) ? task_state(g_pty_shell_pid) : -1;
    u32_to_dec((st < 0) ? 0u : (uint32_t)st, n3, sizeof(n3));
    strcpy(line, "PTY shell pid=");
    (void)buf_append(line, sizeof(line), n2);
    (void)buf_append(line, sizeof(line), " state=");
    (void)buf_append(line, sizeof(line), (st < 0) ? "n/a" : n3);
    draw_text_fit(x, y + 40, w->rc.w - 36, line, C_TEXT_SOFT);

    u32_to_dec((uint32_t)g_zcount, n4, sizeof(n4));
    strcpy(line, "Visible windows: ");
    (void)buf_append(line, sizeof(line), n4);
    draw_text_fit(x, y + 60, w->rc.w - 36, line, C_TEXT_SOFT);

    draw_text_fit(x, y + 90, w->rc.w - 36, "Processes list API is not available yet.", C_TEXT_SOFT);
    draw_text_fit(x, y + 108, w->rc.w - 36, "This panel shows runtime task status.", C_TEXT_SOFT);
}

static void draw_window(const gui_window_t *w) {
    rect_t c = win_close_rect(w);
    rect_t m = win_min_rect(w);
    rect_t rs = win_resize_rect(w);
    uint32_t top = w->active ? C_PRIMARY_HI : C_PRIMARY_SOFT;
    uint32_t body = C_SURFACE;
    uint32_t border = C_BORDER;

    fill_rect(w->rc.x, w->rc.y, w->rc.w, w->rc.h, body);
    fill_rect(w->rc.x, w->rc.y, w->rc.w, 24, top);
    fill_rect(w->rc.x, w->rc.y + 24, w->rc.w, 1, C_BORDER);
    stroke_rect(w->rc.x, w->rc.y, w->rc.w, w->rc.h, border);

    fill_rect(m.x, m.y, m.w, m.h, C_PRIMARY_SOFT);
    fill_rect(c.x, c.y, c.w, c.h, C_DANGER);
    draw_text_fit(w->rc.x + 8, w->rc.y + 5, w->rc.w - 56, w->title, C_TEXT);
    draw_text(m.x + 4, m.y + 4, "-", C_TEXT);
    draw_text(c.x + 4, c.y + 4, "X", C_SURFACE);
    fill_rect(rs.x, rs.y, rs.w, rs.h, C_PRIMARY_SOFT);
    fill_rect(rs.x + 3, rs.y + 7, 7, 1, C_TEXT_SOFT);
    fill_rect(rs.x + 5, rs.y + 5, 5, 1, C_TEXT_SOFT);
    fill_rect(rs.x + 7, rs.y + 3, 3, 1, C_TEXT_SOFT);
}

static rect_t task_button_rect(int slot, int count) {
    rect_t r;
    int w;
    int h = BOTTOM_PANEL_H - 8;
    if (count <= 0) count = 1;
    w = ((int)g_info.width - 20 - (count - 1) * 6) / count;
    if (w < 120) w = 120;
    if (w > 210) w = 210;
    r.x = 8 + slot * (w + 6);
    r.y = (int)g_info.height - BOTTOM_PANEL_H + 4;
    r.w = w;
    r.h = h;
    return r;
}

static void draw_top_panel(void) {
    fill_rect(0, 0, (int)g_info.width, TOP_PANEL_H, C_PANEL);
    fill_rect(0, TOP_PANEL_H - 1, (int)g_info.width, 1, C_PANEL_LINE);

    fill_rect(g_apps_btn.x, g_apps_btn.y, g_apps_btn.w, g_apps_btn.h, g_menu_open ? C_PRIMARY_HI : C_PRIMARY);
    fill_rect(g_apps_btn.x + 6, g_apps_btn.y + 5, 12, 2, C_SURFACE);
    fill_rect(g_apps_btn.x + 6, g_apps_btn.y + 9, 12, 2, C_SURFACE);
    fill_rect(g_apps_btn.x + 6, g_apps_btn.y + 13, 12, 2, C_SURFACE);

    draw_text((int)g_info.width / 2 - 52, 6, "HouseOS Desktop", C_TEXT);
    if (g_cfg_show_top_stats) {
        char uptime[32];
        uptime_label(uptime, sizeof(uptime));
        draw_text((int)g_info.width - 8 - (int)strlen(uptime) * 8, 6, uptime, C_TEXT_SOFT);
    }
}

static void draw_bottom_panel(void) {
    win_id_t wins[WIN_COUNT];
    int count;
    int i;
    fill_rect(0, (int)g_info.height - BOTTOM_PANEL_H, (int)g_info.width, BOTTOM_PANEL_H, C_PANEL);
    fill_rect(0, (int)g_info.height - BOTTOM_PANEL_H, (int)g_info.width, 1, C_PANEL_LINE);

    count = collect_task_windows(wins, WIN_COUNT);
    for (i = 0; i < count; i++) {
        int wi = (int)wins[i];
        rect_t tr = task_button_rect(i, count);
        gui_window_t *w = &g_windows[wi];
        uint32_t c;
        if (w->minimized) c = C_PRIMARY_SOFT;
        else if (w->active) c = C_PRIMARY_HI;
        else c = C_PRIMARY;
        if (g_hover_task == wi) c += 0x000A0A0A;
        fill_rect(tr.x, tr.y, tr.w, tr.h, c);
        stroke_rect(tr.x, tr.y, tr.w, tr.h, C_BORDER);
        draw_text_fit(tr.x + 8, tr.y + 4, tr.w - 12, g_windows[wi].title, C_SURFACE);
    }
}

static void draw_menu(void) {
    int i;
    if (!g_menu_open) return;
    fill_rect(g_menu_box.x, g_menu_box.y, g_menu_box.w, g_menu_box.h, C_SURFACE);
    stroke_rect(g_menu_box.x, g_menu_box.y, g_menu_box.w, g_menu_box.h, C_BORDER);
    for (i = 0; i < MENU_ITEMS; i++) {
        rect_t row = { g_menu_box.x + 4, g_menu_box.y + 4 + i * 24, g_menu_box.w - 8, 22 };
        fill_rect(row.x, row.y, row.w, row.h, (i == g_hover_menu) ? C_PRIMARY_SOFT : C_SURFACE);
        draw_text_fit(row.x + 8, row.y + 4, row.w - 12, g_menu[i].label, C_TEXT);
    }
}

static void draw_desktop_background(void) {
    int h = (int)g_info.height - TOP_PANEL_H - BOTTOM_PANEL_H;
    fill_rect(0, TOP_PANEL_H, (int)g_info.width, h, C_BG_DESKTOP);
    fill_rect(0, TOP_PANEL_H + 38, (int)g_info.width, 1, C_BG_STRIPE);
    fill_rect(0, TOP_PANEL_H + 160, (int)g_info.width, 1, C_BG_STRIPE);
}

static void render_windows(void) {
    int zi;
    for (zi = 0; zi < (int)g_zcount; zi++) {
        int i = (int)g_zorder[zi];
        gui_window_t *w = &g_windows[i];
        if (!w->visible || w->minimized) continue;
        draw_window(w);
        set_clip_rect(w->rc.x + 1, w->rc.y + 25, w->rc.w - 2, w->rc.h - 26);
        if (i == WIN_TERMINAL) render_terminal_window(w);
        else if (i == WIN_SETTINGS) render_settings_window(w);
        else if (i == WIN_TASKMGR) render_taskmgr_window(w);
        clear_clip_rect();
    }
}

static void render_ui(void) {
    uint32_t frame_bytes = g_info.pitch * g_info.height;
    if (frame_bytes > sizeof(g_fb)) return;

    refresh_status_cache();
    draw_desktop_background();
    draw_top_panel();
    render_windows();
    draw_bottom_panel();
    draw_menu();
    draw_cursor(g_mouse_x, g_mouse_y);
    (void)write(g_fd_fb, g_fb, frame_bytes);
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

static int menu_item_at(int x, int y) {
    int i;
    if (!inside(x, y, &g_menu_box)) return -1;
    for (i = 0; i < MENU_ITEMS; i++) {
        rect_t row = { g_menu_box.x + 4, g_menu_box.y + 4 + i * 24, g_menu_box.w - 8, 22 };
        if (inside(x, y, &row)) return i;
    }
    return -1;
}

static int task_button_at(int x, int y) {
    win_id_t wins[WIN_COUNT];
    int count;
    int i;
    count = collect_task_windows(wins, WIN_COUNT);
    for (i = 0; i < count; i++) {
        rect_t r = task_button_rect(i, count);
        if (inside(x, y, &r)) return (int)wins[i];
    }
    return -1;
}

static void execute_menu(int idx) {
    if (idx < 0 || idx >= MENU_ITEMS) return;
    if (g_menu[idx].action == ACT_OPEN_TERMINAL) {
        open_window(WIN_TERMINAL);
    } else if (g_menu[idx].action == ACT_OPEN_SETTINGS) {
        (void)exec("/bin/settings");
    } else if (g_menu[idx].action == ACT_OPEN_TASKMGR) {
        open_window(WIN_TASKMGR);
    } else if (g_menu[idx].action == ACT_REBOOT) {
        if (g_fd_power >= 0) (void)ioctl(g_fd_power, DEV_IOCTL_POWER_REBOOT, NULL);
    } else if (g_menu[idx].action == ACT_POWEROFF) {
        if (g_fd_power >= 0) (void)ioctl(g_fd_power, DEV_IOCTL_POWER_POWEROFF, NULL);
    }
}

static int in_title_drag_zone(const gui_window_t *w, int x, int y) {
    rect_t t;
    rect_t c;
    rect_t m;
    if (!w) return 0;
    t = win_title_rect(w);
    c = win_close_rect(w);
    m = win_min_rect(w);
    if (!inside(x, y, &t)) return 0;
    if (inside(x, y, &c) || inside(x, y, &m)) return 0;
    return 1;
}

static void snap_window(rect_t *rc, int self_idx, int resize_mode) {
    int i;
    const int snap = 10;
    int left = rc->x;
    int right = rc->x + rc->w;
    int top = rc->y;
    int bottom = rc->y + rc->h;
    if (!rc) return;
    if (left < snap) rc->x = 0;
    if (top - TOP_PANEL_H < snap) rc->y = TOP_PANEL_H;
    if ((int)g_info.width - right < snap) rc->x = (int)g_info.width - rc->w;
    if ((int)g_info.height - BOTTOM_PANEL_H - bottom < snap) rc->y = (int)g_info.height - BOTTOM_PANEL_H - rc->h;

    for (i = 0; i < WIN_COUNT; i++) {
        rect_t o;
        if (i == self_idx) continue;
        if (!g_windows[i].visible || g_windows[i].minimized) continue;
        o = g_windows[i].rc;
        if (resize_mode) {
            if (rc->x < o.x + o.w && rc->x + rc->w > o.x) {
                if ((o.y - bottom) < snap && (o.y - bottom) > -snap) rc->h += (o.y - bottom);
                if ((o.y + o.h - bottom) < snap && (o.y + o.h - bottom) > -snap) rc->h += (o.y + o.h - bottom);
            }
            if (rc->y < o.y + o.h && rc->y + rc->h > o.y) {
                if ((o.x - right) < snap && (o.x - right) > -snap) rc->w += (o.x - right);
                if ((o.x + o.w - right) < snap && (o.x + o.w - right) > -snap) rc->w += (o.x + o.w - right);
            }
        } else {
            if (rc->y < o.y + o.h && rc->y + rc->h > o.y) {
                if ((o.x - right) < snap && (o.x - right) > -snap) rc->x = o.x - rc->w;
                if ((o.x + o.w - left) < snap && (o.x + o.w - left) > -snap) rc->x = o.x + o.w;
            }
            if (rc->x < o.x + o.w && rc->x + rc->w > o.x) {
                if ((o.y - bottom) < snap && (o.y - bottom) > -snap) rc->y = o.y - rc->h;
                if ((o.y + o.h - top) < snap && (o.y + o.h - top) > -snap) rc->y = o.y + o.h;
            }
        }
    }
}

static void clamp_window(rect_t *rc, int min_w, int min_h) {
    int max_x;
    int max_y;
    if (!rc) return;
    if (rc->w < min_w) rc->w = min_w;
    if (rc->h < min_h) rc->h = min_h;
    if (rc->w > (int)g_info.width - 8) rc->w = (int)g_info.width - 8;
    if (rc->h > (int)g_info.height - TOP_PANEL_H - 8) rc->h = (int)g_info.height - TOP_PANEL_H - 8;
    max_x = (int)g_info.width - rc->w;
    max_y = (int)g_info.height - BOTTOM_PANEL_H - rc->h;
    if (rc->x < 0) rc->x = 0;
    if (rc->x > max_x) rc->x = max_x;
    if (rc->y < TOP_PANEL_H) rc->y = TOP_PANEL_H;
    if (rc->y > max_y) rc->y = max_y;
}

static void update_drag(void) {
    gui_window_t *w;
    if (g_drag_window < 0 || g_drag_window >= WIN_COUNT) return;
    w = &g_windows[g_drag_window];
    w->rc.x = g_mouse_x - g_drag_off_x;
    w->rc.y = g_mouse_y - g_drag_off_y;
    snap_window(&w->rc, g_drag_window, 0);
    clamp_window(&w->rc, w->min_w, w->min_h);
}

static void update_resize(void) {
    gui_window_t *w;
    if (g_resize_window < 0 || g_resize_window >= WIN_COUNT) return;
    w = &g_windows[g_resize_window];
    w->rc.w = g_resize_start_w + (g_mouse_x - g_resize_start_x);
    w->rc.h = g_resize_start_h + (g_mouse_y - g_resize_start_y);
    snap_window(&w->rc, g_resize_window, 1);
    clamp_window(&w->rc, w->min_w, w->min_h);
}

static int handle_window_click(int wi) {
    gui_window_t *w;
    rect_t close_btn;
    rect_t min_btn;
    rect_t resize_btn;

    if (wi < 0 || wi >= WIN_COUNT) return 0;
    w = &g_windows[wi];
    if (!w->visible || w->minimized) return 0;

    close_btn = win_close_rect(w);
    min_btn = win_min_rect(w);
    resize_btn = win_resize_rect(w);

    if (inside(g_mouse_x, g_mouse_y, &close_btn)) {
        close_window((win_id_t)wi);
        return 1;
    }
    if (inside(g_mouse_x, g_mouse_y, &min_btn)) {
        minimize_window((win_id_t)wi);
        return 1;
    }

    bring_to_front(wi);
    set_active_window(wi);
    if (inside(g_mouse_x, g_mouse_y, &resize_btn)) {
        g_resize_window = wi;
        g_resize_start_x = g_mouse_x;
        g_resize_start_y = g_mouse_y;
        g_resize_start_w = w->rc.w;
        g_resize_start_h = w->rc.h;
        return 1;
    }

    if (wi == WIN_SETTINGS) {
        rect_t area = settings_content_rect(w);
        rect_t g0 = area;
        rect_t g1 = area;
        int i;
        for (i = 0; i < 7; i++) {
            rect_t tr = settings_tab_rect(w, i);
            if (inside(g_mouse_x, g_mouse_y, &tr)) {
                g_settings_tab = (uint8_t)i;
                return 1;
            }
        }

        if (g_settings_tab == 1u || g_settings_tab == 2u) {
            ui_flex_t vf;
            rect_t p0;
            rect_t p1;
            ui_flex_begin(&vf, area, 8, 1u);
            p0 = ui_flex_take(&vf, (area.h - 8) / 2);
            p1 = ui_flex_take(&vf, area.h - 8 - ((area.h - 8) / 2));
            g0 = ui_panel_inner(p0);
            g1 = ui_panel_inner(p1);
        } else if (g_settings_tab == 3u || g_settings_tab == 4u || g_settings_tab == 5u) {
            g0 = ui_panel_inner(area);
        }

        if (g_settings_tab == 0u) {
            rect_t t0 = settings_theme_opt_rect(area, 0);
            rect_t t1 = settings_theme_opt_rect(area, 1);
            rect_t t2 = settings_theme_opt_rect(area, 2);
            rect_t t3 = settings_theme_opt_rect(area, 3);
            if (inside(g_mouse_x, g_mouse_y, &t0)) {
                g_cfg_theme = 0u;
                apply_theme(g_cfg_theme);
                return 1;
            }
            if (inside(g_mouse_x, g_mouse_y, &t1)) {
                g_cfg_theme = 1u;
                apply_theme(g_cfg_theme);
                return 1;
            }
            if (inside(g_mouse_x, g_mouse_y, &t2)) {
                g_cfg_theme = 2u;
                apply_theme(g_cfg_theme);
                return 1;
            }
            if (inside(g_mouse_x, g_mouse_y, &t3)) {
                g_cfg_cursor_compact = g_cfg_cursor_compact ? 0 : 1;
                return 1;
            }
        }

        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 30))) {
            g_cfg_disable_mouse_click = g_cfg_disable_mouse_click ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 54))) {
            g_cfg_mouse_inv_x = g_cfg_mouse_inv_x ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 78))) {
            g_cfg_mouse_inv_y = g_cfg_mouse_inv_y ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 102))) {
            g_cfg_mouse_inv_btn = g_cfg_mouse_inv_btn ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g1, 12, 30))) {
            g_cfg_kbd_layout = 0u;
            apply_keyboard_layout(0u);
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g1, 12, 54))) {
            g_cfg_kbd_layout = 1u;
            apply_keyboard_layout(1u);
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g1, (g1.w > 220) ? (g1.w / 2) : 12, 78))) {
            g_cfg_kbd_switch_hotkey = 0u;
            return 1;
        }
        if (g_settings_tab == 1u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g1, (g1.w > 220) ? (g1.w / 2) : 12, 102))) {
            g_cfg_kbd_switch_hotkey = 1u;
            return 1;
        }

        if (g_settings_tab == 2u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 70))) {
            g_cfg_net_use_dhcp = g_cfg_net_use_dhcp ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 2u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g1, 12, 30))) {
            g_cfg_dns_profile = 0u;
            return 1;
        }
        if (g_settings_tab == 2u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g1, 12, 54))) {
            g_cfg_dns_profile = 1u;
            return 1;
        }

        if (g_settings_tab == 3u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 30))) {
            g_cfg_display_mode = 0u;
            return 1;
        }
        if (g_settings_tab == 3u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 54))) {
            g_cfg_display_mode = 1u;
            return 1;
        }
        if (g_settings_tab == 3u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, (g0.w > 220) ? (g0.w / 2) : 12, 30))) {
            g_cfg_display_mode = 2u;
            return 1;
        }

        if (g_settings_tab == 4u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 30))) {
            g_cfg_boot_default = 0u;
            return 1;
        }
        if (g_settings_tab == 4u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 54))) {
            g_cfg_boot_default = 1u;
            return 1;
        }

        if (g_settings_tab == 5u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 30))) {
            g_cfg_show_top_stats = g_cfg_show_top_stats ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 5u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 54))) {
            g_cfg_cursor_compact = g_cfg_cursor_compact ? 0 : 1;
            return 1;
        }
        if (g_settings_tab == 5u && inside_rect(g_mouse_x, g_mouse_y, settings_check_rect(g0, 12, 78))) {
            g_cfg_term_autofocus = g_cfg_term_autofocus ? 0 : 1;
            return 1;
        }
    }

    if (in_title_drag_zone(w, g_mouse_x, g_mouse_y)) {
        g_drag_window = wi;
        g_drag_off_x = g_mouse_x - w->rc.x;
        g_drag_off_y = g_mouse_y - w->rc.y;
    }

    return 1;
}

static void handle_click(void) {
    int mi;
    int ti;
    int wi;

    if (inside(g_mouse_x, g_mouse_y, &g_apps_btn)) {
        g_menu_open = g_menu_open ? 0 : 1;
        return;
    }

    if (g_cfg_disable_mouse_click) {
        gui_window_t *sw = &g_windows[WIN_SETTINGS];
        rect_t area = settings_content_rect(sw);
        ui_flex_t vf;
        rect_t p0;
        rect_t allow;
        ui_flex_begin(&vf, area, 8, 1u);
        p0 = ui_flex_take(&vf, (area.h - 8) / 2);
        allow = settings_check_rect(ui_panel_inner(p0), 12, 30);
        if (!(sw->visible && !sw->minimized && sw->active && g_settings_tab == 1u &&
              inside(g_mouse_x, g_mouse_y, &allow))) {
            return;
        }
    }

    if (g_menu_open) {
        mi = menu_item_at(g_mouse_x, g_mouse_y);
        if (mi >= 0) {
            execute_menu(mi);
            g_menu_open = 0;
            return;
        }
        if (!inside(g_mouse_x, g_mouse_y, &g_menu_box)) g_menu_open = 0;
    }

    ti = task_button_at(g_mouse_x, g_mouse_y);
    if (ti >= 0) {
        toggle_window((win_id_t)ti);
        return;
    }

    wi = top_window_at(g_mouse_x, g_mouse_y);
    if (wi >= 0) {
        (void)handle_window_click(wi);
        return;
    }

    set_active_window(-1);
}

int main(void) {
    uint32_t frame_bytes;
    int need_redraw = 1;

    g_fd_fb = open("/dev/vesa", 0);
    if (g_fd_fb < 0) return exec("/bin/sh");
    if (ioctl(g_fd_fb, DEV_IOCTL_VESA_GET_INFO, &g_info) != 0) return exec("/bin/sh");
    frame_bytes = g_info.pitch * g_info.height;
    if (frame_bytes == 0 || frame_bytes > sizeof(g_fb)) return exec("/bin/sh");
    if (g_info.bpp != 24 && g_info.bpp != 32) return exec("/bin/sh");
    if (load_psf_font() != 0) return exec("/bin/sh");

    g_fd_mouse = open("/dev/mouse", 0);
    g_fd_power = open("/dev/power", 0);
    g_fd_kbd = open("/dev/keyboard", 0);
    apply_keyboard_layout(g_cfg_kbd_layout);
    detect_current_tty();
    apply_theme(g_cfg_theme);
    load_hgui_runtime();

    term_clear();
    if (pty_start_shell() != 0) {
        term_puts("PTY start failed. Terminal unavailable.\n");
    } else {
        term_puts("PTY shell started on ");
        term_puts(g_pty_alloc.slave_path);
        term_puts("\n");
    }
    if (g_hgui_loaded) term_puts("sdk: /lib/libhgui.so loaded\n");
    else term_puts("sdk: fallback layout (libhgui unavailable)\n");

    hq_widget_init(&g_ui_root, 1u);
    hq_widget_set_geometry(&g_ui_root, (ui_rect_t){0, 0, (int)g_info.width, (int)g_info.height});
    hq_app_init(&g_ui_app, &g_ui_root);

    while (g_ui_app.running) {
        (void)hq_app_process_once(&g_ui_app);
        int old_x = g_mouse_x;
        int old_y = g_mouse_y;
        int old_hover = g_hover_menu;
        int old_task = g_hover_task;
        uint32_t old_btn = g_mouse_btn;
        int active = is_graphics_tty_active();
        int became_active = (active && !g_prev_active) ? 1 : 0;

        poll_mouse();
        if (active) poll_keyboard_events();
        if (poll_pty_output()) need_redraw = 1;
        if (g_pty_shell_pid >= 0 && task_state(g_pty_shell_pid) == 3) {
            term_puts("\n[pty] shell exited, restarting...\n");
            pty_close_shell();
            if (pty_start_shell() != 0) term_puts("[pty] restart failed\n");
            else {
                term_puts("[pty] restarted on ");
                term_puts(g_pty_alloc.slave_path);
                term_puts("\n");
            }
            need_redraw = 1;
        }

        if (old_x != g_mouse_x || old_y != g_mouse_y) {
            gui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = EVT_MOUSE_MOVE;
            ev.x = g_mouse_x;
            ev.y = g_mouse_y;
            ev.buttons = g_mouse_btn;
            evt_push(&ev);
        }
        if ((g_mouse_btn & 1u) && !(old_btn & 1u)) {
            gui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = EVT_MOUSE_DOWN;
            ev.x = g_mouse_x;
            ev.y = g_mouse_y;
            ev.buttons = g_mouse_btn;
            evt_push(&ev);
        } else if (!(g_mouse_btn & 1u) && (old_btn & 1u)) {
            gui_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = EVT_MOUSE_UP;
            ev.x = g_mouse_x;
            ev.y = g_mouse_y;
            ev.buttons = g_mouse_btn;
            evt_push(&ev);
        }

        dispatch_events(active);

        g_hover_menu = g_menu_open ? menu_item_at(g_mouse_x, g_mouse_y) : -1;
        g_hover_task = task_button_at(g_mouse_x, g_mouse_y);
        g_prev_mouse_btn = g_mouse_btn;

        if (old_x != g_mouse_x || old_y != g_mouse_y || old_hover != g_hover_menu ||
            old_task != g_hover_task || old_btn != g_mouse_btn) {
            need_redraw = 1;
        }
        if (g_force_redraw) {
            need_redraw = 1;
            g_force_redraw = 0;
        }
        if (became_active) need_redraw = 1;
        if ((get_ticks() % 800u) < 8u) need_redraw = 1;

        if (need_redraw) {
            render_ui();
            need_redraw = 0;
        }

        g_prev_active = active;
        sleep(10);
    }

    return 0;
}
