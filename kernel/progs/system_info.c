#include <progs/system_info.h>
#include <drivers/vesa.h>
#include <asm/mm.h>
#include <asm/timer.h>
#include <string.h>

typedef struct system_info_state {
    gshell_state_t *gs;
} system_info_state_t;

static void append_s(char *out, uint32_t *len, uint32_t max, const char *s) {
    if (!out || !len || !s || *len >= max - 1) return;
    while (*s && *len < max - 1) out[(*len)++] = *s++;
    out[*len] = '\0';
}

static void append_u(char *out, uint32_t *len, uint32_t max, uint32_t v) {
    char n[16];
    utoa(v, n, 10);
    append_s(out, len, max, n);
}

static void append_hex(char *out, uint32_t *len, uint32_t max, uint32_t v) {
    char n[16];
    utoa(v, n, 16);
    append_s(out, len, max, "0x");
    append_s(out, len, max, n);
}

static void system_info_on_draw(window_t *win, void *user_data) {
    system_info_state_t *st = (system_info_state_t*)user_data;
    if (!win || !st || !st->gs) return;

    wm_t *wm = st->gs->wm;
    memfs *fs = st->gs->fs;
    psf_font_t *font = st->gs->font;

    char left[768];
    char right[768];
    uint32_t llen = 0;
    uint32_t rlen = 0;
    left[0] = '\0';
    right[0] = '\0';

    uint32_t ticks = timer_get_ticks();
    uint32_t up_s = ticks / 100;
    uint32_t up_ms = (ticks % 100) * 10;

    append_s(left, &llen, sizeof(left), "System Info\n\n");
    append_s(left, &llen, sizeof(left), "VESA:\n");
    append_s(left, &llen, sizeof(left), "  initialized: ");
    append_s(left, &llen, sizeof(left), vesa_is_initialized() ? "yes\n" : "no\n");
    append_s(left, &llen, sizeof(left), "  width: "); append_u(left, &llen, sizeof(left), vesa_get_width()); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  height: "); append_u(left, &llen, sizeof(left), vesa_get_height()); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  bpp: "); append_u(left, &llen, sizeof(left), vesa_get_bpp()); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  pitch: "); append_u(left, &llen, sizeof(left), vesa_get_pitch()); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  framebuffer: "); append_hex(left, &llen, sizeof(left), vesa_get_framebuffer()); append_s(left, &llen, sizeof(left), "\n\n");

    append_s(left, &llen, sizeof(left), "WM:\n");
    append_s(left, &llen, sizeof(left), "  windows: "); append_u(left, &llen, sizeof(left), (uint32_t)wm->window_count); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  mouse_x: "); append_u(left, &llen, sizeof(left), (uint32_t)wm->mouse_x); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  mouse_y: "); append_u(left, &llen, sizeof(left), (uint32_t)wm->mouse_y); append_s(left, &llen, sizeof(left), "\n\n");

    append_s(left, &llen, sizeof(left), "Uptime:\n");
    append_s(left, &llen, sizeof(left), "  ticks: "); append_u(left, &llen, sizeof(left), ticks); append_s(left, &llen, sizeof(left), "\n");
    append_s(left, &llen, sizeof(left), "  time: "); append_u(left, &llen, sizeof(left), up_s); append_s(left, &llen, sizeof(left), "."); append_u(left, &llen, sizeof(left), up_ms); append_s(left, &llen, sizeof(left), " s\n");

    append_s(right, &rlen, sizeof(right), "Initramfs:\n");
    append_s(right, &rlen, sizeof(right), "  total: "); append_u(right, &rlen, sizeof(right), (uint32_t)fs->total_memory); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  used: "); append_u(right, &rlen, sizeof(right), (uint32_t)fs->used_memory); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  inodes: "); append_u(right, &rlen, sizeof(right), (uint32_t)fs->inode_count); append_s(right, &rlen, sizeof(right), "\n\n");

    append_s(right, &rlen, sizeof(right), "Font:\n");
    append_s(right, &rlen, sizeof(right), "  width: "); append_u(right, &rlen, sizeof(right), font->width); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  height: "); append_u(right, &rlen, sizeof(right), font->height); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  glyphs: "); append_u(right, &rlen, sizeof(right), font->num_glyphs); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  glyph_size: "); append_u(right, &rlen, sizeof(right), font->glyph_size); append_s(right, &rlen, sizeof(right), "\n\n");

    append_s(right, &rlen, sizeof(right), "MM:\n");
    append_s(right, &rlen, sizeof(right), "  heap_total: "); append_u(right, &rlen, sizeof(right), (uint32_t)get_total_heap()); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  heap_used: "); append_u(right, &rlen, sizeof(right), (uint32_t)get_used_heap()); append_s(right, &rlen, sizeof(right), "\n");
    append_s(right, &rlen, sizeof(right), "  heap_free: "); append_u(right, &rlen, sizeof(right), (uint32_t)get_free_heap()); append_s(right, &rlen, sizeof(right), "\n");

    window_clear(win, 0x00080f16);
    window_draw_string(win, 5, 5, left, 0x00dce9f7);
    window_draw_string(win, (int32_t)(win->canvas_width / 2) + 5, 5, right, 0x00dce9f7);
}

static bool system_info_on_close(window_t *win, void *user_data) {
    (void)win;
    system_info_state_t *st = (system_info_state_t*)user_data;
    if (st) kfree(st);
    return true;
}

window_t *system_info_open(gshell_state_t *st) {
    if (!st || !st->wm || !st->font || !st->fs) return NULL;

    window_t *win = window_create(220, 60, 512, 512, "system_info");
    if (!win) return NULL;
    win->font = st->font;

    system_info_state_t *si = (system_info_state_t*)kmalloc(sizeof(system_info_state_t));
    if (!si) {
        window_destroy(win);
        return NULL;
    }
    si->gs = st;

    window_set_handlers(win, system_info_on_close, NULL, NULL, NULL, system_info_on_draw, si);
    window_update(win);
    if (!wm_add_window(st->wm, win)) {
        system_info_on_close(win, si);
        window_destroy(win);
        return NULL;
    }
    return win;
}
