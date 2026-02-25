#include <progs/panel.h>
#include <progs/applications.h>
#include <drivers/vesa.h>
#include <asm/mm.h>

#define APP_BTN_X 5
#define APP_BTN_Y 4
#define APP_BTN_W 17
#define APP_BTN_H 17
#define ICON_MAX_W 16
#define ICON_MAX_H 16

static bool panel_point_in_rect(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t rw, uint32_t rh) {
    if (rw == 0 || rh == 0) return false;
    return x >= rx && y >= ry && x < rx + (int32_t)rw && y < ry + (int32_t)rh;
}

static bool panel_load_icon_from_memfs(memfs *fs, const char *path, uint32_t *pixels, uint8_t *out_w, uint8_t *out_h) {
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

    for (uint32_t i = 0; i < ICON_MAX_W * ICON_MAX_H; i++) pixels[i] = 0xFFFFFFFFu;

    uint8_t w = 0;
    uint8_t h = 0;
    char *p = buf;
    while (*p && h < ICON_MAX_H) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        char saved = *p;
        *p = '\0';

        uint8_t line_w = 0;
        for (char *c = line; *c && line_w < ICON_MAX_W; c++) {
            uint32_t color = 0xFFFFFFFFu;
            if (*c == '#') color = 0x00000000;
            else if (*c == '*') color = 0x00ffffff;
            pixels[(uint32_t)h * ICON_MAX_W + line_w] = color;
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

static void panel_draw(gshell_state_t *st) {
    if (!st || !st->launcher_win) return;

    window_t *win = st->launcher_win;
    window_clear(win, 0x00f2f2f2);
    window_draw_rect(win, APP_BTN_X, APP_BTN_Y, APP_BTN_W, APP_BTN_H, 0x00000000, 1);

    if (st->launcher_icon_loaded && st->launcher_icon_w > 0 && st->launcher_icon_h > 0) {
        int32_t ix = APP_BTN_X + ((int32_t)APP_BTN_W - (int32_t)st->launcher_icon_w) / 2;
        int32_t iy = APP_BTN_Y + ((int32_t)APP_BTN_H - (int32_t)st->launcher_icon_h) / 2;
        for (uint8_t y = 0; y < st->launcher_icon_h; y++) {
            for (uint8_t x = 0; x < st->launcher_icon_w; x++) {
                uint32_t c = st->launcher_icon[(uint32_t)y * ICON_MAX_W + x];
                if (c == 0xFFFFFFFFu) continue;
                window_draw_pixel(win, ix + x, iy + y, c);
            }
        }
    } else {
        window_draw_string(win, APP_BTN_X + 4, APP_BTN_Y + 4, "...", 0x00000000);
    }

}

static void panel_on_draw(window_t *win, void *user_data) {
    (void)win;
    gshell_state_t *st = (gshell_state_t*)user_data;
    panel_draw(st);
}

static bool panel_on_mouse(window_t *win, int32_t local_x, int32_t local_y, uint8_t buttons, bool pressed, void *user_data) {
    (void)win;
    gshell_state_t *st = (gshell_state_t*)user_data;
    if (!st || !pressed || (buttons & 0x01) == 0) return false;

    if (panel_point_in_rect(local_x, local_y, APP_BTN_X, APP_BTN_Y, APP_BTN_W, APP_BTN_H)) {
        (void)applications_open(st);
        return true;
    }
    return false;
}

window_t *panel_create(gshell_state_t *st) {
    if (!st) return NULL;

    st->launcher_icon_loaded = panel_load_icon_from_memfs(st->fs, "/system/wm_btn_apps.txt", st->launcher_icon, &st->launcher_icon_w, &st->launcher_icon_h);

    window_t *panel = window_create(0, 0, vesa_get_width(), 25, "launcher");
    if (!panel) return NULL;

    panel->draggable = false;
    panel->resizable = false;
    panel->can_close = false;
    panel->can_maximize = false;
    window_disable_title(panel);
    window_set_handlers(panel, NULL, NULL, NULL, panel_on_mouse, panel_on_draw, st);
    st->launcher_win = panel;
    window_update(panel);

    if (!wm_add_window(st->wm, panel)) {
        window_destroy(panel);
        st->launcher_win = NULL;
        return NULL;
    }
    return panel;
}
