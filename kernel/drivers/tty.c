#include <drivers/tty.h>
#include <drivers/serial.h>
#include <drivers/vesa.h>
#include <drivers/fonts/psf.h>
#include <drivers/fonts/font_renderer.h>
#include <drivers/keyboard.h>
#include <devctl.h>
#include <asm/mm.h>
#include <asm/processor.h>
#include <string.h>

#define VESA_TTY_COUNT 8
#define SERIAL_TTY_COUNT 2
#define TTY_HISTORY_MAX 32
#define TTY_HISTORY_LINE_MAX 1024

typedef enum {
    TTY_VESA = 0,
    TTY_SERIAL = 1,
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
} tty_device_t;

static tty_device_t g_tty_v[VESA_TTY_COUNT];
static tty_device_t g_tty_s[SERIAL_TTY_COUNT];
static uint16_t g_serial_ports[SERIAL_TTY_COUNT] = { 0x3F8, 0x2F8 };
static uint32_t g_active_tty = 0;
static psf_font_t *g_font = NULL;
static uint32_t g_char_w = 8;
static uint32_t g_char_h = 16;
static uint32_t g_fg = 0x00D0D0D0;
static uint32_t g_bg = 0x00000000;
static uint8_t g_tty_ready = 0;

static void tty_render_full(tty_device_t *tty);
static void tty_draw_cell(uint32_t col, uint32_t row, char c);
static void tty_putc_vesa(tty_device_t *tty, char c);

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
        tty_putc_vesa(tty, '\b');
        len--;
    }
    for (size_t i = 0; i < old_len; i++) tty_putc_vesa(tty, ' ');
    for (size_t i = 0; i < old_len; i++) tty_putc_vesa(tty, '\b');
}

static void tty_input_draw_line(tty_device_t *tty, const char *line, size_t len, size_t cursor) {
    for (size_t i = 0; i < len; i++) tty_putc_vesa(tty, line[i]);
    while (cursor < len) {
        tty_putc_vesa(tty, '\b');
        len--;
    }
}

static void tty_set_active(uint32_t idx) {
    if (idx >= VESA_TTY_COUNT) return;
    g_active_tty = idx;
    tty_render_full(&g_tty_v[g_active_tty]);
}

static void tty_hotkey_handler(uint8_t keycode, bool pressed, bool shift, bool ctrl, bool alt) {
    (void)shift;
    (void)ctrl;
    if (!pressed || !alt) return;
    if (keycode >= KEY_1 && keycode <= KEY_8) tty_set_active((uint32_t)(keycode - KEY_1));
}

static void tty_draw_cell(uint32_t col, uint32_t row, char c) {
    uint32_t px = col * g_char_w;
    uint32_t py = row * g_char_h;
    vesa_fill_rect(px, py, g_char_w, g_char_h, g_bg);
    if (c != ' ' && g_font) font_draw_char(g_font, px, py, c, g_fg);
}

static void tty_render_full(tty_device_t *tty) {
    if (!tty || tty->type != TTY_VESA || !tty->cells) return;
    for (uint32_t y = 0; y < tty->rows; y++) {
        for (uint32_t x = 0; x < tty->cols; x++) {
            tty_draw_cell(x, y, tty->cells[y * tty->cols + x]);
        }
    }
}

static void tty_scroll(tty_device_t *tty) {
    if (!tty || tty->type != TTY_VESA || !tty->cells || tty->rows == 0) return;
    size_t line_sz = tty->cols;
    size_t total = tty->rows * tty->cols;
    memmove(tty->cells, tty->cells + line_sz, total - line_sz);
    memset(tty->cells + total - line_sz, ' ', line_sz);
    if (tty->index == g_active_tty) tty_render_full(tty);
}

static void tty_putc_vesa(tty_device_t *tty, char c) {
    if (!tty || tty->type != TTY_VESA || !tty->cells) return;

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
        if (tty->cursor_x > 0) tty->cursor_x--;
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

    tty_input_cursor(tty, 1);

    while (n < (size - 1)) {
        while (tty->index != g_active_tty) {
            sti();
            hlt();
            cli();
        }
        while (!keyboard_event_available()) {
            sti();
            hlt();
            cli();
        }

        if (tty->index != g_active_tty) continue;
        ev = keyboard_get_event();
        if (!ev.pressed) continue;

        if (ev.ascii != 0 && keyboard_available()) (void)keyboard_getchar();

        if (ev.scancode == KEY_LEFT) {
            if (cursor > 0) {
                tty_input_cursor(tty, 0);
                tty_putc_vesa(tty, '\b');
                cursor--;
                tty_input_cursor(tty, 1);
            }
            continue;
        }
        if (ev.scancode == KEY_RIGHT) {
            if (cursor < n) {
                tty_input_cursor(tty, 0);
                tty_putc_vesa(tty, out[cursor]);
                cursor++;
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

        char c = ev.ascii;
        if (c == '\r') c = '\n';

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
                for (size_t i = cursor; i <= n; i++) tty_putc_vesa(tty, '\b');
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
            for (size_t i = cursor; i < n; i++) tty_putc_vesa(tty, '\b');
            tty_input_cursor(tty, 1);
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

static int tty_vesa_ioctl(void *ctx, uint32_t request, void *arg) {
    tty_device_t *tty = (tty_device_t*)ctx;
    if (!tty) return -1;

    if (request == DEV_IOCTL_TTY_GET_INFO) {
        dev_tty_info_t *out = (dev_tty_info_t*)arg;
        if (!out) return -1;
        out->kind = DEV_TTY_KIND_VESA;
        out->index = tty->index;
        out->cols = tty->cols;
        out->rows = tty->rows;
        out->cursor_x = tty->cursor_x;
        out->cursor_y = tty->cursor_y;
        return 0;
    }

    if (request == DEV_IOCTL_TTY_SET_ACTIVE) {
        if (!arg) return -1;
        uint32_t idx = *(uint32_t*)arg;
        if (idx >= VESA_TTY_COUNT) return -1;
        tty_set_active(idx);
        return 0;
    }

    return -1;
}

static ssize_t tty_serial_read(void *ctx, void *buf, size_t size) {
    tty_device_t *tty = (tty_device_t*)ctx;
    char *out = (char*)buf;
    size_t n = 0;
    uint16_t port;

    if (!tty || !buf || size == 0 || tty->index >= SERIAL_TTY_COUNT) return -1;
    port = g_serial_ports[tty->index];

    while (n < size) {
        while (!serial_received(port)) {
            sti();
            hlt();
            cli();
        }
        char c = serial_read_char(port);
        if (c == '\r') c = '\n';

        if (c == '\b' || c == 0x7F) {
            if (n > 0) {
                n--;
                serial_write_char(port, '\b');
                serial_write_char(port, ' ');
                serial_write_char(port, '\b');
            }
            continue;
        }

        out[n++] = c;
        serial_write_char(port, c);
        if (c == '\n') break;
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
    return -1;
}

void tty_serial_print(const char *text) {
    serial_write(SERIAL_COM1, text);
}

void tty_klog(const char *text) {
    if (!text) return;

    serial_write(SERIAL_COM1, text);
    if (!g_tty_ready || !g_tty_v[0].cells) return;
    while (*text) tty_putc_vesa(&g_tty_v[0], *text++);
}

void tty_init(memfs *root_fs) {
    if (!root_fs) return;

    memfs_create_dir(root_fs, "/devices");

    for (uint32_t i = 0; i < SERIAL_TTY_COUNT; i++) {
        char path[32];
        g_tty_s[i].type = TTY_SERIAL;
        g_tty_s[i].index = i;
        g_tty_s[i].history = NULL;
        g_tty_s[i].history_count = 0;
        g_tty_s[i].history_next = 0;
        serial_init(g_serial_ports[i]);
        strcpy(path, "/devices/ttyS");
        path[13] = (char)('0' + i);
        path[14] = '\0';
        memfs_create_device_ops(root_fs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
            tty_serial_read, tty_serial_write, tty_serial_ioctl, &g_tty_s[i]);
    }

    g_font = psf_load_from_memfs(root_fs, "/system/fonts/default8x16.psf");
    if (g_font && g_font->width > 0 && g_font->height > 0) {
        g_char_w = g_font->width;
        g_char_h = g_font->height;
    }

    {
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

            strcpy(path, "/devices/tty");
            path[12] = (char)('0' + i);
            path[13] = '\0';
            memfs_create_device_ops(root_fs, path, MEMFS_DEV_READ | MEMFS_DEV_WRITE,
                tty_vesa_read, tty_vesa_write, tty_vesa_ioctl, &g_tty_v[i]);
        }
    }

    g_active_tty = 0;
    vesa_clear(g_bg);
    tty_render_full(&g_tty_v[g_active_tty]);
    keyboard_set_hotkey_handler(tty_hotkey_handler);
    g_tty_ready = 1;
}
