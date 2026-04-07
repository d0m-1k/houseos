#include <drivers/tty.h>
#include <drivers/serial.h>
#include <drivers/vesa.h>
#include <drivers/vga.h>
#include <drivers/fonts/psf.h>
#include <drivers/fonts/font_renderer.h>
#include <drivers/keyboard.h>
#include <drivers/power.h>
#include <devctl.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <asm/task.h>
#include <drivers/serial.h>
#include <string.h>

#define VESA_TTY_COUNT 8
#define SERIAL_TTY_COUNT 1
#define TTY_HISTORY_MAX 32
#define TTY_HISTORY_LINE_MAX 1024

typedef enum {
    TTY_VESA = 0,
    TTY_SERIAL = 1,
    TTY_VGA = 2,
} tty_type_t;

typedef struct {
    tty_type_t type;
    uint32_t index;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cols;
    uint32_t rows;
    char *cells;
    char *history;
    uint32_t history_count;
    uint32_t history_next;
    uint8_t ansi_state;
    uint32_t ansi_param;
    uint8_t ansi_have_param;
    int32_t fg_pid;
} tty_device_t;

static tty_device_t g_tty_v[VESA_TTY_COUNT];
static tty_device_t g_tty_s[SERIAL_TTY_COUNT];
static uint16_t g_serial_ports[SERIAL_TTY_COUNT] = { 0x3F8 };
static uint32_t g_active_tty = 0;
static psf_font_t *g_font = NULL;
static uint32_t g_char_w = 8;
static uint32_t g_char_h = 16;
static uint32_t g_fg = 0x00D0D0D0;
static uint32_t g_bg = 0x00000000;
static uint8_t g_tty_ready = 0;

static inline void tty_spin_wait(void) {
    uint32_t flags;
    __asm__ __volatile__("pushf; pop %0" : "=r"(flags) :: "memory");
    sti();
    __asm__ __volatile__("hlt");
    if ((flags & (1u << 9)) == 0u) cli();
}

static void tty_render_full(tty_device_t *tty);
static void tty_draw_cell(uint32_t col, uint32_t row, char c);
static const uint8_t *tty_fallback_glyph(char c);
static void tty_draw_fallback_char(uint32_t px, uint32_t py, char c);
static void tty_redraw_row(tty_device_t *tty, uint32_t row);
static void tty_putc_vesa(tty_device_t *tty, char c);
static void tty_putc_vga(tty_device_t *tty, char c);
static void tty_cursor_left_raw(tty_device_t *tty);
static void tty_cursor_right_raw(tty_device_t *tty);
static int tty_consume_ansi(tty_device_t *tty, char c);
static void tty_erase_to_eol(tty_device_t *tty);
static char tty_event_ascii(const struct key_event *ev);
static void tty_input_cursor(tty_device_t *tty, int visible);

static char tty_take_input_char(const struct key_event *ev) {
    char c = 0;
    if (keyboard_available()) {
        c = keyboard_getchar();
        if (c == '\r') c = '\n';
        return c;
    }
    c = tty_event_ascii(ev);
    if (ev) {
        if (ev->scancode == KEY_ENTER) c = '\n';
        else if (ev->scancode == KEY_BACKSPACE) c = '\b';
    }
    if (c == '\r') c = '\n';
    return c;
}

static int tty_handle_ctrl_char(tty_device_t *tty, char c) {
    if (!tty) return 0;
    if (c == 0x04) {
        tty_input_cursor(tty, 0);
        return 1;
    }
    if (c == 0x03) {
        if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
        tty_input_cursor(tty, 0);
        if (tty->type == TTY_VGA) tty_putc_vga(tty, '^');
        else tty_putc_vesa(tty, '^');
        if (tty->type == TTY_VGA) tty_putc_vga(tty, 'C');
        else tty_putc_vesa(tty, 'C');
        if (tty->type == TTY_VGA) tty_putc_vga(tty, '\n');
        else tty_putc_vesa(tty, '\n');
        return 2;
    }
    return 0;
}

static int tty_is_alpha_scancode(uint8_t scancode) {
    return ((scancode >= 0x10 && scancode <= 0x19) ||
            (scancode >= 0x1E && scancode <= 0x26) ||
            (scancode >= 0x2C && scancode <= 0x32));
}

static void tty_input_cursor(tty_device_t *tty, int visible) {
    uint32_t px;
    uint32_t py;
    uint32_t y;
    uint32_t x;
    if (!tty || tty->type != TTY_VESA || tty->index != g_active_tty) return;
    if (tty->cursor_x >= tty->cols || tty->cursor_y >= tty->rows) return;

    x = tty->cursor_x;
    y = tty->cursor_y;
    px = x * g_char_w;
    py = y * g_char_h + ((g_char_h > 2) ? (g_char_h - 2) : (g_char_h - 1));

    if (visible) {
        vesa_fill_rect(px, py, g_char_w, 1, g_fg);
    } else {
        tty_draw_cell(x, y, tty->cells[y * tty->cols + x]);
    }
}

static void tty_cursor_left_raw(tty_device_t *tty) {
    if (!tty || tty->type != TTY_VESA) return;
    if (tty->cursor_x > 0) {
        tty->cursor_x--;
    } else if (tty->cursor_y > 0) {
        tty->cursor_y--;
        tty->cursor_x = (tty->cols > 0) ? (tty->cols - 1) : 0;
    }
}

static void tty_cursor_right_raw(tty_device_t *tty) {
    if (!tty || tty->type != TTY_VESA) return;
    if (tty->cursor_x + 1 < tty->cols) {
        tty->cursor_x++;
    } else {
        tty->cursor_x = 0;
        if (tty->cursor_y + 1 < tty->rows) tty->cursor_y++;
    }
}

static void tty_erase_to_eol(tty_device_t *tty) {
    uint32_t y;
    uint32_t x;
    if (!tty || !tty->cells) return;
    if (tty->cursor_y >= tty->rows || tty->cursor_x >= tty->cols) return;
    y = tty->cursor_y;
    for (x = tty->cursor_x; x < tty->cols; x++) {
        tty->cells[y * tty->cols + x] = ' ';
        if (tty->index == g_active_tty) {
            if (tty->type == TTY_VGA) tty_redraw_row(tty, y);
            else tty_draw_cell(x, y, ' ');
        }
    }
}

static int tty_consume_ansi(tty_device_t *tty, char c) {
    if (!tty) return 0;
    if (tty->ansi_state == 0u) {
        if ((uint8_t)c == 27u) {
            tty->ansi_state = 1u;
            tty->ansi_param = 0u;
            tty->ansi_have_param = 0u;
            return 1;
        }
        return 0;
    }

    if (tty->ansi_state == 1u) {
        if (c == '[') {
            tty->ansi_state = 2u;
            return 1;
        }
        tty->ansi_state = 0u;
        return 1;
    }

    if (tty->ansi_state == 2u) {
        if (c >= '0' && c <= '9') {
            tty->ansi_have_param = 1u;
            tty->ansi_param = tty->ansi_param * 10u + (uint32_t)(c - '0');
            return 1;
        }
        if (c == ';') return 1;
        if (c >= 0x40 && c <= 0x7E) {
            uint32_t n = tty->ansi_have_param ? tty->ansi_param : 1u;
            if (n == 0u) n = 1u;
            if (c == 'D') {
                while (n--) tty_cursor_left_raw(tty);
            } else if (c == 'C') {
                while (n--) tty_cursor_right_raw(tty);
            } else if (c == 'G') {
                uint32_t col = tty->ansi_have_param ? (tty->ansi_param - 1u) : 0u;
                if (col >= tty->cols) col = tty->cols ? (tty->cols - 1u) : 0u;
                tty->cursor_x = col;
            } else if (c == 'H' || c == 'f') {
                tty->cursor_x = 0;
                tty->cursor_y = 0;
            } else if (c == 'K' || c == 'X' || c == 'J') {
                tty_erase_to_eol(tty);
            }
            tty->ansi_state = 0u;
            tty->ansi_param = 0u;
            tty->ansi_have_param = 0u;
            return 1;
        }
        tty->ansi_state = 0u;
        return 1;
    }

    tty->ansi_state = 0u;
    return 1;
}

static char tty_event_ascii(const struct key_event *ev) {
    size_t layout;
    uint8_t shift_idx;
    char c;
    if (!ev) return 0;
    if (ev->ascii) return ev->ascii;
    if (ev->scancode == KEY_ENTER) return '\n';
    if (ev->scancode == KEY_BACKSPACE) return '\b';
    if (ev->scancode == KEY_TAB) return '\t';
    if (ev->ctrl || ev->alt) return 0;
    if (ev->scancode >= 255u) return 0;
    layout = keyboard_get_layout();
    if (layout >= LAYOUT_COUNT) layout = 0;
    shift_idx = (ev->shift ^ (ev->caps && tty_is_alpha_scancode(ev->scancode))) ? 1u : 0u;
    c = keyboard_map[layout][shift_idx][ev->scancode];
    return c;
}

static void tty_history_store(tty_device_t *tty, const char *line, size_t len) {
    uint32_t i;
    uint32_t last_idx;
    uint32_t copy_len;
    if (!tty || !line || len == 0) return;
    if (!tty->history) return;
    if (len >= TTY_HISTORY_LINE_MAX) len = TTY_HISTORY_LINE_MAX - 1;

    if (tty->history_count > 0) {
        const char *last_line;
        last_idx = (tty->history_next + TTY_HISTORY_MAX - 1) % TTY_HISTORY_MAX;
        last_line = tty->history + (last_idx * TTY_HISTORY_LINE_MAX);
        i = 0;
        while (i < len && last_line[i] == line[i]) i++;
        if (i == len && last_line[i] == '\0') return;
    }

    copy_len = (uint32_t)len;
    memcpy(tty->history + (tty->history_next * TTY_HISTORY_LINE_MAX), line, copy_len);
    tty->history[tty->history_next * TTY_HISTORY_LINE_MAX + copy_len] = '\0';
    tty->history_next = (tty->history_next + 1) % TTY_HISTORY_MAX;
    if (tty->history_count < TTY_HISTORY_MAX) tty->history_count++;
}

static const char *tty_history_get(tty_device_t *tty, uint32_t from_latest) {
    uint32_t idx;
    if (!tty || from_latest >= tty->history_count) return NULL;
    if (!tty->history) return NULL;
    idx = (tty->history_next + TTY_HISTORY_MAX - 1 - from_latest) % TTY_HISTORY_MAX;
    return tty->history + (idx * TTY_HISTORY_LINE_MAX);
}

static void tty_input_clear_line(tty_device_t *tty, const char *line, size_t len, size_t cursor) {
    size_t old_len = len;
    while (cursor < len) {
        tty_putc_vesa(tty, line[cursor]);
        cursor++;
    }
    while (len > 0) {
        tty_cursor_left_raw(tty);
        len--;
    }
    for (size_t i = 0; i < old_len; i++) tty_putc_vesa(tty, ' ');
    for (size_t i = 0; i < old_len; i++) tty_cursor_left_raw(tty);
}

static void tty_input_draw_line(tty_device_t *tty, const char *line, size_t len, size_t cursor) {
    for (size_t i = 0; i < len; i++) tty_putc_vesa(tty, line[i]);
    while (cursor < len) {
        tty_cursor_left_raw(tty);
        len--;
    }
}

static void tty_set_active(uint32_t idx) {
    if (idx >= VESA_TTY_COUNT) return;
    keyboard_clear_buffers();
    g_active_tty = idx;
    if (g_tty_v[g_active_tty].type == TTY_VESA && vesa_is_initialized()) vesa_clear(g_bg);
    if (g_tty_v[g_active_tty].type == TTY_VGA) vga_clear();
    tty_render_full(&g_tty_v[g_active_tty]);
}

static void tty_hotkey_handler(uint8_t keycode, bool pressed, bool shift, bool ctrl, bool alt) {
    if (!pressed) return;

    if (ctrl && alt && keycode == KEY_DEL) {
        if (power_get_ctrl_alt_del_mode() == POWER_CAD_REBOOT) {
            tty_klog("tty: Ctrl+Alt+Del -> reboot\n");
            power_reboot();
        } else {
            tty_klog("tty: Ctrl+Alt+Del ignored by CAD mode\n");
        }
        return;
    }

    if ((alt || (ctrl && shift)) && keycode >= KEY_F1 && keycode <= KEY_F8) {
        uint32_t idx = (uint32_t)(keycode - KEY_F1);
        tty_set_active(idx);
        return;
    }
}

static void tty_draw_cell(uint32_t col, uint32_t row, char c) {
    uint32_t px = col * g_char_w;
    uint32_t py = row * g_char_h;
    vesa_fill_rect(px, py, g_char_w, g_char_h, g_bg);
    if (c == ' ') return;
    if (g_font) {
        font_draw_char(g_font, px, py, c, g_fg);
        return;
    }
    tty_draw_fallback_char(px, py, c);
}

static const uint8_t *tty_fallback_glyph(char c) {
    static const uint8_t sp[7] = {0,0,0,0,0,0,0};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t dot[7] = {0,0,0,0,0,12,12};
    static const uint8_t colon[7] = {0,12,12,0,12,12,0};
    static const uint8_t slash[7] = {1,2,4,8,16,0,0};
    static const uint8_t n0[7] = {14,17,17,17,17,17,14};
    static const uint8_t n1[7] = {4,12,4,4,4,4,14};
    static const uint8_t n2[7] = {14,17,1,2,4,8,31};
    static const uint8_t n3[7] = {30,1,1,14,1,1,30};
    static const uint8_t n4[7] = {2,6,10,18,31,2,2};
    static const uint8_t n5[7] = {31,16,16,30,1,1,30};
    static const uint8_t n6[7] = {14,16,16,30,17,17,14};
    static const uint8_t n7[7] = {31,1,2,4,8,8,8};
    static const uint8_t n8[7] = {14,17,17,14,17,17,14};
    static const uint8_t n9[7] = {14,17,17,15,1,1,14};
    static const uint8_t a[7] = {14,17,17,31,17,17,17};
    static const uint8_t b[7] = {30,17,17,30,17,17,30};
    static const uint8_t c_[7] = {14,17,16,16,16,17,14};
    static const uint8_t d[7] = {30,17,17,17,17,17,30};
    static const uint8_t e[7] = {31,16,16,30,16,16,31};
    static const uint8_t f[7] = {31,16,16,30,16,16,16};
    static const uint8_t g[7] = {14,17,16,23,17,17,14};
    static const uint8_t h[7] = {17,17,17,31,17,17,17};
    static const uint8_t i[7] = {14,4,4,4,4,4,14};
    static const uint8_t j[7] = {7,2,2,2,2,18,12};
    static const uint8_t k[7] = {17,18,20,24,20,18,17};
    static const uint8_t l[7] = {16,16,16,16,16,16,31};
    static const uint8_t m[7] = {17,27,21,21,17,17,17};
    static const uint8_t n[7] = {17,25,21,19,17,17,17};
    static const uint8_t o[7] = {14,17,17,17,17,17,14};
    static const uint8_t p[7] = {30,17,17,30,16,16,16};
    static const uint8_t q[7] = {14,17,17,17,21,18,13};
    static const uint8_t r[7] = {30,17,17,30,20,18,17};
    static const uint8_t s[7] = {15,16,16,14,1,1,30};
    static const uint8_t t[7] = {31,4,4,4,4,4,4};
    static const uint8_t u[7] = {17,17,17,17,17,17,14};
    static const uint8_t v[7] = {17,17,17,17,17,10,4};
    static const uint8_t w[7] = {17,17,17,21,21,21,10};
    static const uint8_t x[7] = {17,17,10,4,10,17,17};
    static const uint8_t y[7] = {17,17,10,4,4,4,4};
    static const uint8_t z[7] = {31,1,2,4,8,16,31};
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {
        case 'A': return a; case 'B': return b; case 'C': return c_; case 'D': return d;
        case 'E': return e; case 'F': return f; case 'G': return g; case 'H': return h;
        case 'I': return i; case 'J': return j; case 'K': return k; case 'L': return l;
        case 'M': return m; case 'N': return n; case 'O': return o; case 'P': return p;
        case 'Q': return q; case 'R': return r; case 'S': return s; case 'T': return t;
        case 'U': return u; case 'V': return v; case 'W': return w; case 'X': return x;
        case 'Y': return y; case 'Z': return z;
        case '0': return n0; case '1': return n1; case '2': return n2; case '3': return n3;
        case '4': return n4; case '5': return n5; case '6': return n6; case '7': return n7;
        case '8': return n8; case '9': return n9;
        case '-': return dash; case '.': return dot; case ':': return colon; case '/': return slash;
        default: return sp;
    }
}

static void tty_draw_fallback_char(uint32_t px, uint32_t py, char c) {
    const uint8_t *g = tty_fallback_glyph(c);
    uint32_t sx = (g_char_w >= 5) ? (g_char_w / 5) : 1;
    uint32_t sy = (g_char_h >= 7) ? (g_char_h / 7) : 1;
    uint32_t s = (sx < sy) ? sx : sy;
    uint32_t gw;
    uint32_t gh;
    uint32_t ox;
    uint32_t oy;
    if (s == 0) s = 1;
    gw = 5 * s;
    gh = 7 * s;
    ox = (g_char_w > gw) ? ((g_char_w - gw) / 2) : 0;
    oy = (g_char_h > gh) ? ((g_char_h - gh) / 2) : 0;
    for (uint32_t row = 0; row < 7; row++) {
        for (uint32_t col = 0; col < 5; col++) {
            if ((g[row] & (1u << (4 - col))) == 0) continue;
            vesa_fill_rect(px + ox + col * s, py + oy + row * s, s, s, g_fg);
        }
    }
}

static void tty_render_full(tty_device_t *tty) {
    volatile uint16_t *vga_mem;
    uint16_t blank;
    uint8_t color;
    if (!tty || !tty->cells) return;
    if (tty->type == TTY_VESA) {
        for (uint32_t y = 0; y < tty->rows; y++) {
            for (uint32_t x = 0; x < tty->cols; x++) {
                tty_draw_cell(x, y, tty->cells[y * tty->cols + x]);
            }
        }
        return;
    }
    if (tty->type != TTY_VGA) return;

    color = vga_color_get();
    blank = (uint16_t)((color << 8) | ' ');
    vga_mem = (volatile uint16_t*)(uintptr_t)VGA_MEMORY_ADDRESS;
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga_mem[i] = blank;
    for (uint32_t y = 0; y < tty->rows; y++) {
        for (uint32_t x = 0; x < tty->cols; x++) {
            char c = tty->cells[y * tty->cols + x];
            vga_mem[y * VGA_WIDTH + x] = (uint16_t)((color << 8) | (uint8_t)c);
        }
    }
    vga_cursor_set(tty->cursor_x, tty->cursor_y);
}

static void tty_redraw_row(tty_device_t *tty, uint32_t row) {
    volatile uint16_t *vga_mem;
    uint8_t color;
    if (!tty || !tty->cells || row >= tty->rows) return;
    if (tty->type == TTY_VESA) {
        for (uint32_t x = 0; x < tty->cols; x++) {
            tty_draw_cell(x, row, tty->cells[row * tty->cols + x]);
        }
        return;
    }
    if (tty->type != TTY_VGA) return;
    color = vga_color_get();
    vga_mem = (volatile uint16_t*)(uintptr_t)VGA_MEMORY_ADDRESS;
    for (uint32_t x = 0; x < tty->cols; x++) {
        char c = tty->cells[row * tty->cols + x];
        vga_mem[row * VGA_WIDTH + x] = (uint16_t)((color << 8) | (uint8_t)c);
    }
}

static void tty_scroll(tty_device_t *tty) {
    if (!tty || !tty->cells || tty->rows == 0) return;
    if (tty->type != TTY_VESA && tty->type != TTY_VGA) return;
    size_t line_sz = tty->cols;
    size_t total = tty->rows * tty->cols;
    memmove(tty->cells, tty->cells + line_sz, total - line_sz);
    memset(tty->cells + total - line_sz, ' ', line_sz);
    if (tty->index != g_active_tty) return;

    if (tty->type == TTY_VESA) {
        vesa_scroll(0, -(int32_t)g_char_h, g_bg);
        tty_redraw_row(tty, tty->rows - 1);
        return;
    }

    if (tty->type == TTY_VGA) {
        volatile uint16_t *vga_mem = (volatile uint16_t*)(uintptr_t)VGA_MEMORY_ADDRESS;
        memmove((void*)vga_mem, (const void*)(vga_mem + VGA_WIDTH), (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t));
        tty_redraw_row(tty, tty->rows - 1);
        return;
    }
}

static void tty_putc_vesa(tty_device_t *tty, char c) {
    if (!tty || tty->type != TTY_VESA || !tty->cells) return;
    if (tty_consume_ansi(tty, c)) return;

    if (c == '\r') {
        tty->cursor_x = 0;
        return;
    }
    if (c == '\n') {
        tty->cursor_x = 0;
        tty->cursor_y++;
        if (tty->cursor_y >= tty->rows) {
            tty->cursor_y = tty->rows - 1;
            tty_scroll(tty);
        }
        return;
    }
    if (c == '\b') {
        tty_cursor_left_raw(tty);
        tty->cells[tty->cursor_y * tty->cols + tty->cursor_x] = ' ';
        if (tty->index == g_active_tty) tty_draw_cell(tty->cursor_x, tty->cursor_y, ' ');
        return;
    }

    tty->cells[tty->cursor_y * tty->cols + tty->cursor_x] = c;
    if (tty->index == g_active_tty) tty_draw_cell(tty->cursor_x, tty->cursor_y, c);

    tty->cursor_x++;
    if (tty->cursor_x >= tty->cols) {
        tty->cursor_x = 0;
        tty->cursor_y++;
        if (tty->cursor_y >= tty->rows) {
            tty->cursor_y = tty->rows - 1;
            tty_scroll(tty);
        }
    }
}

static void tty_putc_vga(tty_device_t *tty, char c) {
    volatile uint16_t *vga_mem;
    uint8_t color;
    if (!tty || tty->type != TTY_VGA || !tty->cells) return;
    if (tty->cols == 0 || tty->rows == 0) return;
    if (tty_consume_ansi(tty, c)) return;

    if (c == '\r') {
        tty->cursor_x = 0;
        if (tty->index == g_active_tty) vga_cursor_set(tty->cursor_x, tty->cursor_y);
        return;
    }
    if (c == '\n') {
        tty->cursor_x = 0;
        tty->cursor_y++;
        if (tty->cursor_y >= tty->rows) {
            tty->cursor_y = tty->rows - 1;
            tty_scroll(tty);
        }
        if (tty->index == g_active_tty) vga_cursor_set(tty->cursor_x, tty->cursor_y);
        return;
    }
    if (c == '\b') {
        tty_cursor_left_raw(tty);
        tty->cells[tty->cursor_y * tty->cols + tty->cursor_x] = ' ';
        if (tty->index == g_active_tty) {
            color = vga_color_get();
            vga_mem = (volatile uint16_t*)(uintptr_t)VGA_MEMORY_ADDRESS;
            vga_mem[tty->cursor_y * VGA_WIDTH + tty->cursor_x] = (uint16_t)((color << 8) | ' ');
            vga_cursor_set(tty->cursor_x, tty->cursor_y);
        }
        return;
    }

    tty->cells[tty->cursor_y * tty->cols + tty->cursor_x] = c;
    if (tty->index == g_active_tty) {
        color = vga_color_get();
        vga_mem = (volatile uint16_t*)(uintptr_t)VGA_MEMORY_ADDRESS;
        vga_mem[tty->cursor_y * VGA_WIDTH + tty->cursor_x] = (uint16_t)((color << 8) | (uint8_t)c);
    }

    tty->cursor_x++;
    if (tty->cursor_x >= tty->cols) {
        tty->cursor_x = 0;
        tty->cursor_y++;
        if (tty->cursor_y >= tty->rows) {
            tty->cursor_y = tty->rows - 1;
            tty_scroll(tty);
        }
    }
    if (tty->index == g_active_tty) vga_cursor_set(tty->cursor_x, tty->cursor_y);
}

static ssize_t tty_vesa_read(void *ctx, void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    char *out = (char*)buf;
    size_t n = 0;
    size_t cursor = 0;
    size_t saved_len = 0;
    size_t saved_cursor = 0;
    int history_pos = -1;
    char saved_line[TTY_HISTORY_LINE_MAX];
    struct key_event ev;

    if (!tty || !buf) return -1;
    if (size == 0) return 0;
    if (size > TTY_HISTORY_LINE_MAX) size = TTY_HISTORY_LINE_MAX;

    if (size == 1) {
        while (1) {
            while (tty->index != g_active_tty) tty_spin_wait();
            while (!keyboard_event_available() && !keyboard_available()) tty_spin_wait();
            if (tty->index != g_active_tty) continue;

            if (!keyboard_event_available() && keyboard_available()) {
                char c = keyboard_getchar();
                if (c == '\r') c = '\n';
                if (c == 0x04) return 0;
                if (c == 0x03) {
                    if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
                    tty_putc_vesa(tty, '^');
                    tty_putc_vesa(tty, 'C');
                    tty_putc_vesa(tty, '\n');
                    continue;
                }
                out[0] = c;
                if (c == '\b') tty_putc_vesa(tty, '\b');
                else if (c == '\n' || ((uint8_t)c >= 32u && (uint8_t)c != 127u)) tty_putc_vesa(tty, c);
                return 1;
            }

            ev = keyboard_get_event();
            if (!ev.pressed) continue;
            if (ev.ascii != 0 && keyboard_available()) (void)keyboard_getchar();

            {
                char c = tty_take_input_char(&ev);
                if (c == 0) continue;
                if (c == 0x04) return 0;
                if (c == 0x03) {
                    if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
                    tty_putc_vesa(tty, '^');
                    tty_putc_vesa(tty, 'C');
                    tty_putc_vesa(tty, '\n');
                    continue;
                }
                out[0] = c;
                if (c == '\b') tty_putc_vesa(tty, '\b');
                else if (c == '\n' || ((uint8_t)c >= 32u && (uint8_t)c != 127u)) tty_putc_vesa(tty, c);
                return 1;
            }
        }
    }

    tty_input_cursor(tty, 1);

    while (n < (size - 1)) {
        while (tty->index != g_active_tty) {
            tty_spin_wait();
        }
        while (!keyboard_event_available() && !keyboard_available()) {
            tty_spin_wait();
        }

        if (tty->index != g_active_tty) continue;
        if (!keyboard_event_available() && keyboard_available()) {
            char c = keyboard_getchar();
            if (c == '\r') c = '\n';
            {
                int ctrl = tty_handle_ctrl_char(tty, c);
                if (ctrl == 1) return 0;
                if (ctrl == 2) return -1;
            }

            if (c == '\n') {
                tty_input_cursor(tty, 0);
                out[n++] = '\n';
                out[n] = '\0';
                if (n > 1) tty_history_store(tty, out, n - 1);
                tty_putc_vesa(tty, '\n');
                return (ssize_t)n;
            }

            if (c == '\b') {
                if (cursor > 0) {
                    tty_input_cursor(tty, 0);
                    memmove(out + cursor - 1, out + cursor, n - cursor);
                    n--;
                    cursor--;
                    tty_putc_vesa(tty, '\b');
                    for (size_t i = cursor; i < n; i++) tty_putc_vesa(tty, out[i]);
                    tty_putc_vesa(tty, ' ');
                    for (size_t i = cursor; i <= n; i++) tty_cursor_left_raw(tty);
                    tty_input_cursor(tty, 1);
                }
                continue;
            }

            if ((uint8_t)c >= 32u && (uint8_t)c != 127u && n < (size - 1)) {
                tty_input_cursor(tty, 0);
                memmove(out + cursor + 1, out + cursor, n - cursor);
                out[cursor] = c;
                n++;
                for (size_t i = cursor; i < n; i++) tty_putc_vesa(tty, out[i]);
                cursor++;
                for (size_t i = cursor; i < n; i++) tty_cursor_left_raw(tty);
                tty_input_cursor(tty, 1);
            }
            continue;
        }

        ev = keyboard_get_event();
        if (!ev.pressed) continue;

        if (ev.scancode == KEY_LEFT) {
            if (cursor > 0) {
                tty_input_cursor(tty, 0);
                tty_cursor_left_raw(tty);
                cursor--;
                tty_input_cursor(tty, 1);
            }
            continue;
        }
        if (ev.scancode == KEY_RIGHT) {
            if (cursor < n) {
                tty_input_cursor(tty, 0);
                tty_cursor_right_raw(tty);
                cursor++;
                tty_input_cursor(tty, 1);
            }
            continue;
        }
        if (ev.scancode == KEY_HOME) {
            if (cursor > 0) {
                tty_input_cursor(tty, 0);
                while (cursor > 0) {
                    tty_cursor_left_raw(tty);
                    cursor--;
                }
                tty_input_cursor(tty, 1);
            }
            continue;
        }
        if (ev.scancode == KEY_END) {
            if (cursor < n) {
                tty_input_cursor(tty, 0);
                while (cursor < n) {
                    tty_cursor_right_raw(tty);
                    cursor++;
                }
                tty_input_cursor(tty, 1);
            }
            continue;
        }
        if (ev.scancode == KEY_UP) {
            const char *hline;
            size_t hlen;
            if (tty->history_count == 0) continue;
            if (history_pos < (int)(tty->history_count - 1)) {
                if (history_pos < 0) {
                    memcpy(saved_line, out, n);
                    saved_len = n;
                    saved_cursor = cursor;
                }
                history_pos++;
            }
            hline = tty_history_get(tty, (uint32_t)history_pos);
            if (!hline) continue;

            tty_input_cursor(tty, 0);
            tty_input_clear_line(tty, out, n, cursor);

            hlen = strlen(hline);
            if (hlen >= size) hlen = size - 1;
            memcpy(out, hline, hlen);
            n = hlen;
            cursor = n;
            tty_input_draw_line(tty, out, n, cursor);
            tty_input_cursor(tty, 1);
            continue;
        }
        if (ev.scancode == KEY_DOWN) {
            if (history_pos < 0) continue;
            tty_input_cursor(tty, 0);
            tty_input_clear_line(tty, out, n, cursor);

            if (history_pos > 0) {
                const char *hline;
                size_t hlen;
                history_pos--;
                hline = tty_history_get(tty, (uint32_t)history_pos);
                if (!hline) {
                    tty_input_cursor(tty, 1);
                    continue;
                }
                hlen = strlen(hline);
                if (hlen >= size) hlen = size - 1;
                memcpy(out, hline, hlen);
                n = hlen;
                cursor = n;
                tty_input_draw_line(tty, out, n, cursor);
            } else if (history_pos == 0) {
                history_pos = -1;
                n = saved_len;
                if (n >= size) n = size - 1;
                memcpy(out, saved_line, n);
                cursor = (saved_cursor <= n) ? saved_cursor : n;
                tty_input_draw_line(tty, out, n, cursor);
            }

            tty_input_cursor(tty, 1);
            continue;
        }

        {
            char c = tty_take_input_char(&ev);
            int ctrl = tty_handle_ctrl_char(tty, c);
            if (ctrl == 1) return 0;
            if (ctrl == 2) return -1;

            if (c == '\n' || ev.scancode == KEY_ENTER) {
                tty_input_cursor(tty, 0);
                out[n++] = '\n';
                out[n] = '\0';
                if (n > 1) tty_history_store(tty, out, n - 1);
                tty_putc_vesa(tty, '\n');
                return (ssize_t)n;
            }

            if (c == '\b' || ev.scancode == KEY_BACKSPACE) {
                if (cursor > 0) {
                    tty_input_cursor(tty, 0);
                    memmove(out + cursor - 1, out + cursor, n - cursor);
                    n--;
                    cursor--;
                    tty_putc_vesa(tty, '\b');
                    for (size_t i = cursor; i < n; i++) tty_putc_vesa(tty, out[i]);
                    tty_putc_vesa(tty, ' ');
                    for (size_t i = cursor; i <= n; i++) tty_cursor_left_raw(tty);
                    tty_input_cursor(tty, 1);
                }
                continue;
            }

            if (c == 0 || c < 32 || c == 127) continue;

            if (n < (size - 1)) {
                tty_input_cursor(tty, 0);
                memmove(out + cursor + 1, out + cursor, n - cursor);
                out[cursor] = c;
                n++;
                for (size_t i = cursor; i < n; i++) tty_putc_vesa(tty, out[i]);
                cursor++;
                for (size_t i = cursor; i < n; i++) tty_cursor_left_raw(tty);
                tty_input_cursor(tty, 1);
            }
        }
    }

    out[n] = '\0';
    tty_input_cursor(tty, 0);
    return (ssize_t)n;
}

static ssize_t tty_vesa_write(void *ctx, const void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    const char *s = (const char*)buf;
    if (!tty || !buf) return -1;
    for (size_t i = 0; i < size; i++) tty_putc_vesa(tty, s[i]);
    return (ssize_t)size;
}

static ssize_t tty_vga_read(void *ctx, void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    char *out = (char*)buf;
    size_t n = 0;
    struct key_event ev;

    if (!tty || !buf) return -1;
    if (tty->type != TTY_VGA) return -1;
    if (size == 0) return 0;

    if (size == 1) {
        while (1) {
            while (tty->index != g_active_tty) tty_spin_wait();
            while (!keyboard_event_available() && !keyboard_available()) tty_spin_wait();
            if (tty->index != g_active_tty) continue;

            if (!keyboard_event_available() && keyboard_available()) {
                char c = keyboard_getchar();
                if (c == '\r') c = '\n';
                if (c == 0x04) return 0;
                if (c == 0x03) {
                    if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
                    tty_putc_vga(tty, '^');
                    tty_putc_vga(tty, 'C');
                    tty_putc_vga(tty, '\n');
                    continue;
                }
                out[0] = c;
                if (c == '\b') tty_putc_vga(tty, '\b');
                else if (c == '\n' || ((uint8_t)c >= 32u && (uint8_t)c != 127u)) tty_putc_vga(tty, c);
                return 1;
            }

            ev = keyboard_get_event();
            if (!ev.pressed) continue;
            {
                char c = tty_take_input_char(&ev);
                if (c == 0) continue;
                if (c == 0x04) return 0;
                if (c == 0x03) {
                    if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
                    tty_putc_vga(tty, '^');
                    tty_putc_vga(tty, 'C');
                    tty_putc_vga(tty, '\n');
                    continue;
                }
                out[0] = c;
                if (c == '\b') tty_putc_vga(tty, '\b');
                else if (c == '\n' || ((uint8_t)c >= 32u && (uint8_t)c != 127u)) tty_putc_vga(tty, c);
                return 1;
            }
        }
    }

    while (n < (size - 1)) {
        while (tty->index != g_active_tty) {
            tty_spin_wait();
        }
        while (!keyboard_event_available() && !keyboard_available()) {
            tty_spin_wait();
        }
        if (tty->index != g_active_tty) continue;

        if (!keyboard_event_available() && keyboard_available()) {
            char c = keyboard_getchar();
            if (c == '\r') c = '\n';
            if (c == 0x04) return 0;
            if (c == 0x03) {
                if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
                tty_putc_vga(tty, '^');
                tty_putc_vga(tty, 'C');
                tty_putc_vga(tty, '\n');
                return -1;
            }
            if (c == '\n') {
                out[n++] = '\n';
                out[n] = '\0';
                tty_putc_vga(tty, '\n');
                return (ssize_t)n;
            }
            if (c == '\b') {
                if (n > 0) {
                    n--;
                    tty_putc_vga(tty, '\b');
                }
                continue;
            }
            if ((uint8_t)c < 32u || c == 127) continue;
            out[n++] = c;
            tty_putc_vga(tty, c);
            continue;
        }

        ev = keyboard_get_event();
        if (!ev.pressed) continue;
        {
            char c = tty_take_input_char(&ev);
        if (c == 0x04) return 0;
        if (c == 0x03) {
            if (tty->fg_pid > 0) (void)task_terminate_by_pid((uint32_t)tty->fg_pid, -1, 2u);
            tty_putc_vga(tty, '^');
            tty_putc_vga(tty, 'C');
            tty_putc_vga(tty, '\n');
            return -1;
        }

        if (ev.scancode == KEY_ENTER) {
            out[n++] = '\n';
            out[n] = '\0';
            tty_putc_vga(tty, '\n');
            return (ssize_t)n;
        }
        if (ev.scancode == KEY_BACKSPACE) {
            if (n > 0) {
                n--;
                tty_putc_vga(tty, '\b');
            }
            continue;
        }
        if (c == '\r') {
            out[n++] = '\n';
            out[n] = '\0';
            tty_putc_vga(tty, '\n');
            return (ssize_t)n;
        }
        if (c == 0 || (uint8_t)c < 32 || c == 127) continue;

        out[n++] = c;
        tty_putc_vga(tty, c);
        }
    }

    out[n] = '\0';
    return (ssize_t)n;
}

static ssize_t tty_vga_write(void *ctx, const void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    const char *s = (const char*)buf;
    if (!tty || !buf) return -1;
    if (tty->type != TTY_VGA) return -1;
    for (size_t i = 0; i < size; i++) tty_putc_vga(tty, s[i]);
    return (ssize_t)size;
}

static int tty_vesa_ioctl(void *ctx, uint32_t request, void *arg) {
    tty_device_t *tty = (tty_device_t*)ctx;
    if (!tty) return -1;

    if (request == DEV_IOCTL_TTY_GET_INFO) {
        dev_tty_info_t *out = (dev_tty_info_t*)arg;
        if (!out) return -1;
        out->kind = DEV_TTY_KIND_VESA;
        out->index = tty->index + 1;
        out->cols = tty->cols;
        out->rows = tty->rows;
        out->cursor_x = tty->cursor_x;
        out->cursor_y = tty->cursor_y;
        return 0;
    }

    if (request == DEV_IOCTL_TTY_SET_ACTIVE) {
        if (!arg) return -1;
        uint32_t raw = *(uint32_t*)arg;
        uint32_t idx;
        if (raw >= 1 && raw <= VESA_TTY_COUNT) idx = raw - 1;
        else if (raw < VESA_TTY_COUNT) idx = raw; 
        else return -1;
        tty_set_active(idx);
        return 0;
    }
    if (request == DEV_IOCTL_TTY_GET_ACTIVE) {
        if (!arg) return -1;
        *(uint32_t*)arg = g_active_tty + 1;
        return 0;
    }
    if (request == DEV_IOCTL_TTY_SET_FG_PID) {
        int32_t pid = arg ? *(int32_t*)arg : -1;
        if (pid > 0 && !task_find_by_pid((uint32_t)pid)) return -1;
        tty->fg_pid = pid;
        return 0;
    }
    if (request == DEV_IOCTL_TTY_GET_FG_PID) {
        if (!arg) return -1;
        *(int32_t*)arg = tty->fg_pid;
        return 0;
    }

    return -1;
}

static ssize_t tty_serial_read(void *ctx, void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    char *out = (char*)buf;
    size_t n = 0;
    size_t cursor = 0;
    size_t saved_len = 0;
    int history_pos = -1;
    char saved_line[TTY_HISTORY_LINE_MAX];
    uint16_t port;
    uint8_t esc_state = 0;
    char esc_param = 0;

    if (!tty || !buf || size == 0 || tty->index >= SERIAL_TTY_COUNT) return -1;
    if (size > TTY_HISTORY_LINE_MAX) size = TTY_HISTORY_LINE_MAX;
    port = g_serial_ports[tty->index];

    while (n < size) {
        while (!serial_received(port)) tty_spin_wait();
        char c = serial_read_char(port);
        if (esc_state == 0 && c == 27) {
            esc_state = 1;
            continue;
        }
        if (esc_state == 1) {
            if (c == '[') {
                esc_state = 2;
                continue;
            }
            esc_state = 0;
        }
        if (esc_state == 3) {
            esc_state = 0;
            if (c == '~') {
                if (esc_param == '1' || esc_param == '7') {
                    while (cursor > 0) {
                        serial_write_char(port, 27);
                        serial_write_char(port, '[');
                        serial_write_char(port, 'D');
                        cursor--;
                    }
                } else if (esc_param == '4' || esc_param == '8') {
                    while (cursor < n) {
                        serial_write_char(port, 27);
                        serial_write_char(port, '[');
                        serial_write_char(port, 'C');
                        cursor++;
                    }
                }
                continue;
            }
        }
        if (esc_state == 2) {
            esc_state = 0;
            if (c == '1' || c == '4' || c == '7' || c == '8') {
                esc_param = c;
                esc_state = 3;
                continue;
            }
            if (c == 'H') {
                while (cursor > 0) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                    cursor--;
                }
                continue;
            }
            if (c == 'F') {
                while (cursor < n) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'C');
                    cursor++;
                }
                continue;
            }
            if (c == 'D') {
                if (cursor > 0) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                    cursor--;
                }
                continue;
            }
            if (c == 'C') {
                if (cursor < n) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'C');
                    cursor++;
                }
                continue;
            }
            if (c == 'A') {
                const char *hline;
                size_t hlen;
                if (!tty->history || tty->history_count == 0) continue;
                if (history_pos < 0) {
                    memcpy(saved_line, out, n);
                    saved_len = n;
                }
                if (history_pos < (int)(tty->history_count - 1)) history_pos++;
                hline = tty_history_get(tty, (uint32_t)history_pos);
                if (!hline) continue;
                hlen = strlen(hline);
                if (hlen >= size) hlen = size - 1;

                while (cursor > 0) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                    cursor--;
                }
                for (size_t i = 0; i < n; i++) serial_write_char(port, ' ');
                for (size_t i = 0; i < n; i++) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                }

                memcpy(out, hline, hlen);
                n = hlen;
                cursor = n;
                for (size_t i = 0; i < n; i++) serial_write_char(port, out[i]);
                continue;
            }
            if (c == 'B') {
                size_t hlen = 0;
                if (history_pos < 0) continue;

                while (cursor > 0) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                    cursor--;
                }
                for (size_t i = 0; i < n; i++) serial_write_char(port, ' ');
                for (size_t i = 0; i < n; i++) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                }

                if (history_pos > 0) {
                    const char *hline;
                    history_pos--;
                    hline = tty_history_get(tty, (uint32_t)history_pos);
                    if (hline) {
                        hlen = strlen(hline);
                        if (hlen >= size) hlen = size - 1;
                        memcpy(out, hline, hlen);
                    }
                } else {
                    history_pos = -1;
                    hlen = saved_len;
                    if (hlen >= size) hlen = size - 1;
                    memcpy(out, saved_line, hlen);
                }

                n = hlen;
                cursor = n;
                for (size_t i = 0; i < n; i++) serial_write_char(port, out[i]);
                continue;
            }
            continue;
        }
        if (c == '\r') c = '\n';

        if (c == '\b' || c == 0x7F) {
            if (cursor > 0) {
                size_t tail = n - cursor;
                memmove(out + cursor - 1, out + cursor, tail);
                n--;
                cursor--;
                serial_write_char(port, '\b');
                for (size_t i = 0; i < tail; i++) serial_write_char(port, out[cursor + i]);
                serial_write_char(port, ' ');
                for (size_t i = 0; i <= tail; i++) {
                    serial_write_char(port, 27);
                    serial_write_char(port, '[');
                    serial_write_char(port, 'D');
                }
            }
            continue;
        }

        if (c == '\n') {
            out[n++] = '\n';
            if (n > 1) tty_history_store(tty, out, n - 1);
            serial_write_char(port, '\n');
            break;
        }

        if ((uint8_t)c < 32 || c == 127) continue;
        if (n >= size - 1) continue;

        memmove(out + cursor + 1, out + cursor, n - cursor);
        out[cursor] = c;
        n++;
        cursor++;
        for (size_t i = cursor - 1; i < n; i++) serial_write_char(port, out[i]);
        for (size_t i = cursor; i < n; i++) {
            serial_write_char(port, 27);
            serial_write_char(port, '[');
            serial_write_char(port, 'D');
        }
    }

    return (ssize_t)n;
}

static ssize_t tty_serial_write(void *ctx, const void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    const char *s = (const char*)buf;
    uint16_t port;
    if (!tty || !buf || tty->index >= SERIAL_TTY_COUNT) return -1;
    port = g_serial_ports[tty->index];
    for (size_t i = 0; i < size; i++) serial_write_char(port, s[i]);
    return (ssize_t)size;
}

static int tty_serial_ioctl(void *ctx, uint32_t request, void *arg) {
    tty_device_t *tty = (tty_device_t*)ctx;
    if (!tty) return -1;

    if (request == DEV_IOCTL_TTY_GET_INFO) {
        dev_tty_info_t *out = (dev_tty_info_t*)arg;
        if (!out) return -1;
        out->kind = DEV_TTY_KIND_SERIAL;
        out->index = tty->index;
        out->cols = 0;
        out->rows = 0;
        out->cursor_x = 0;
        out->cursor_y = 0;
        return 0;
    }
    if (request == DEV_IOCTL_TTY_GET_ACTIVE) {
        if (!arg) return -1;
        *(uint32_t*)arg = g_active_tty + 1;
        return 0;
    }
    if (request == DEV_IOCTL_TTY_SET_FG_PID) {
        int32_t pid = arg ? *(int32_t*)arg : -1;
        if (pid > 0 && !task_find_by_pid((uint32_t)pid)) return -1;
        tty->fg_pid = pid;
        return 0;
    }
    if (request == DEV_IOCTL_TTY_GET_FG_PID) {
        if (!arg) return -1;
        *(int32_t*)arg = tty->fg_pid;
        return 0;
    }
    return -1;
}

void tty_serial_print(const char *text) {
    serial_write(SERIAL_COM1, text);
}

void tty_klog(const char *text) {
    tty_device_t *primary;
    tty_device_t *active;
    if (!text) return;

    serial_write(SERIAL_COM1, text);
    if (!g_tty_ready) return;

    primary = &g_tty_v[0];
    if (primary->cells) {
        const char *p = text;
        while (*p) {
            if (primary->type == TTY_VGA) tty_putc_vga(primary, *p++);
            else tty_putc_vesa(primary, *p++);
        }
    }

    if (g_active_tty >= VESA_TTY_COUNT || g_active_tty == 0) return;
    active = &g_tty_v[g_active_tty];
    if (!active->cells) return;
    while (*text) {
        if (active->type == TTY_VGA) tty_putc_vga(active, *text++);
        else tty_putc_vesa(active, *text++);
    }
}

void tty_init(memfs *root_fs, devfs_t *devfs) {
    if (!root_fs || !devfs) return;
    serial_write(SERIAL_COM1, "tty_init: enter\n");
    devfs_create_dir(devfs, "/tty");

    for (uint32_t i = 0; i < SERIAL_TTY_COUNT; i++) {
        char path[32];
        g_tty_s[i].type = TTY_SERIAL;
        g_tty_s[i].index = i;
        g_tty_s[i].history = (char*)kmalloc(TTY_HISTORY_MAX * TTY_HISTORY_LINE_MAX);
        if (g_tty_s[i].history) memset(g_tty_s[i].history, 0, TTY_HISTORY_MAX * TTY_HISTORY_LINE_MAX);
        g_tty_s[i].history_count = 0;
        g_tty_s[i].history_next = 0;
        g_tty_s[i].fg_pid = -1;
        serial_init(g_serial_ports[i]);
        strcpy(path, "/tty/S");
        path[6] = (char)('0' + i);
        path[7] = '\0';
        devfs_create_device_ops(devfs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            tty_serial_read, tty_serial_write, tty_serial_ioctl, &g_tty_s[i]);
    }
    serial_write(SERIAL_COM1, "tty_init: serial ready\n");

    g_font = psf_load_from_memfs(root_fs, "/system/fonts/default8x16.psf");
    if (g_font && g_font->width > 0 && g_font->height > 0) {
        g_char_w = g_font->width;
        g_char_h = g_font->height;
        tty_klog("tty: font renderer=psf\n");
    } else {
        tty_klog("tty: font renderer=builtin\n");
    }
    serial_write(SERIAL_COM1, "tty_init: font selected\n");

    if (vesa_is_initialized()) {
        serial_write(SERIAL_COM1, "tty_init: vesa branch\n");
        uint32_t cols = vesa_get_width() / g_char_w;
        uint32_t rows = vesa_get_height() / g_char_h;
        if (cols == 0 || rows == 0) return;

        for (uint32_t i = 0; i < VESA_TTY_COUNT; i++) {
            char path[32];
            g_tty_v[i].type = TTY_VESA;
            g_tty_v[i].index = i;
            g_tty_v[i].cursor_x = 0;
            g_tty_v[i].cursor_y = 0;
            g_tty_v[i].cols = cols;
            g_tty_v[i].rows = rows;
            g_tty_v[i].cells = (char*)valloc(cols * rows);
            if (g_tty_v[i].cells) memset(g_tty_v[i].cells, ' ', cols * rows);
            g_tty_v[i].history = (char*)kmalloc(TTY_HISTORY_MAX * TTY_HISTORY_LINE_MAX);
            if (g_tty_v[i].history) memset(g_tty_v[i].history, 0, TTY_HISTORY_MAX * TTY_HISTORY_LINE_MAX);
            g_tty_v[i].history_count = 0;
            g_tty_v[i].history_next = 0;
            g_tty_v[i].fg_pid = -1;

            strcpy(path, "/tty/");
            path[5] = (char)('1' + i);
            path[6] = '\0';
            devfs_create_device_ops(devfs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
                tty_vesa_read, tty_vesa_write, tty_vesa_ioctl, &g_tty_v[i]);
            if (i == 0) {
                devfs_create_device_ops(devfs, "/tty/0", MEMFS_DEV_READ | MEMFS_DEV_WRITE,
                    tty_vesa_read, tty_vesa_write, tty_vesa_ioctl, &g_tty_v[i]);
            }
        }
        serial_write(SERIAL_COM1, "tty_init: vesa ttys ready\n");
    }
    else {
        serial_write(SERIAL_COM1, "tty_init: vga branch\n");
        uint32_t cols = VGA_WIDTH;
        uint32_t rows = VGA_HEIGHT;
        vga_init();
        for (uint32_t i = 0; i < VESA_TTY_COUNT; i++) {
            char path[32];
            g_tty_v[i].type = TTY_VGA;
            g_tty_v[i].index = i;
            g_tty_v[i].cursor_x = 0;
            g_tty_v[i].cursor_y = 0;
            g_tty_v[i].cols = cols;
            g_tty_v[i].rows = rows;
            g_tty_v[i].cells = (char*)valloc(cols * rows);
            if (g_tty_v[i].cells) memset(g_tty_v[i].cells, ' ', cols * rows);
            g_tty_v[i].history = NULL;
            g_tty_v[i].history_count = 0;
            g_tty_v[i].history_next = 0;
            g_tty_v[i].fg_pid = -1;

            strcpy(path, "/tty/");
            path[5] = (char)('1' + i);
            path[6] = '\0';
            devfs_create_device_ops(devfs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
                tty_vga_read, tty_vga_write, tty_vesa_ioctl, &g_tty_v[i]);
            if (i == 0) {
                devfs_create_device_ops(devfs, "/tty/0", MEMFS_DEV_READ | MEMFS_DEV_WRITE,
                    tty_vga_read, tty_vga_write, tty_vesa_ioctl, &g_tty_v[i]);
            }
        }
    }

    g_active_tty = 0;
    serial_write(SERIAL_COM1, "tty_init: before first render\n");
    if (vesa_is_initialized()) vesa_clear(g_bg);
    tty_render_full(&g_tty_v[g_active_tty]);
    serial_write(SERIAL_COM1, "tty_init: after first render\n");
    keyboard_set_hotkey_handler(tty_hotkey_handler);
    serial_write(SERIAL_COM1, "tty_init: hotkey set\n");
    g_tty_ready = 1;
    serial_write(SERIAL_COM1, "tty_init: done\n");
}
