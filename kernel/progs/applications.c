#include <progs/applications.h>
#include <progs/system_info.h>

static bool apps_point_in_rect(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t rw, uint32_t rh) {
    if (rw == 0 || rh == 0) return false;
    return x >= rx && y >= ry && x < rx + (int32_t)rw && y < ry + (int32_t)rh;
}

static bool apps_on_close(window_t *win, void *user_data) {
    (void)win;
    gshell_state_t *st = (gshell_state_t*)user_data;
    if (st) st->apps_win = NULL;
    return true;
}

static bool apps_on_mouse(window_t *win, int32_t local_x, int32_t local_y, uint8_t buttons, bool pressed, void *user_data) {
    (void)win;
    gshell_state_t *st = (gshell_state_t*)user_data;
    if (!st || !pressed || (buttons & 0x01) == 0) return false;

    if (apps_point_in_rect(local_x, local_y, 10, 36, 180, 18)) {
        (void)system_info_open(st);
        if (st->apps_win) {
            wm_close_window(st->wm, st->apps_win);
            st->apps_win = NULL;
        }
        return true;
    }
    return false;
}

static void apps_on_draw(window_t *win, void *user_data) {
    (void)user_data;
    if (!win) return;

    window_clear(win, 0x00131a22);
    window_draw_string(win, 10, 10, "Applications:", 0x00e8eff7);
    window_draw_rect(win, 8, 34, 190, 20, 0x003a4e63, 1);
    window_draw_string(win, 12, 36, "System Info", 0x00e8eff7);
}

window_t *applications_open(gshell_state_t *st) {
    if (!st) return NULL;
    if (st->apps_win) {
        wm_focus_window(st->wm, st->apps_win);
        return st->apps_win;
    }

    window_t *apps = window_create(110, 32, 280, 200, "applications");
    if (!apps) return NULL;
    apps->font = st->font;
    window_disable_title(apps);

    window_set_handlers(apps, apps_on_close, NULL, NULL, apps_on_mouse, apps_on_draw, st);
    window_update(apps);

    if (!wm_add_window(st->wm, apps)) {
        window_destroy(apps);
        return NULL;
    }

    st->apps_win = apps;
    return apps;
}
