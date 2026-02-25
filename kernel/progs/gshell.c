#include <progs/gshell.h>
#include <progs/panel.h>
#include <progs/applications.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <asm/task.h>
#include <asm/timer.h>
#include <string.h>

void gshell_run(void *arg) {
    struct gshell_args *args = (struct gshell_args*)arg;
    if (!args || !args->fs) {
        task_exit();
        return;
    }

    psf_font_t *font = psf_load_from_memfs(args->fs, "/system/fonts/default8x16.psf");
    if (!font) {
        task_exit();
        return;
    }

    keyboard_set_echo_mode(false);

    wm_t *wm = wm_create(font);
    if (!wm) {
        keyboard_set_echo_mode(true);
        psf_free_font(font);
        task_exit();
        return;
    }

    (void)wm_load_theme_from_memfs(wm, args->fs, "/system/wm_theme.conf");
    (void)wm_load_cursor_from_memfs(wm, args->fs, "/system/cursor16.txt");
    (void)wm_load_title_buttons_from_memfs(wm, args->fs, "/system/wm_btn_close.txt", "/system/wm_btn_full.txt");

    gshell_state_t st;
    memset(&st, 0, sizeof(st));
    st.wm = wm;
    st.font = font;
    st.fs = args->fs;

    (void)panel_create(&st);
    wm_render(wm);

    while (1) {
        mouse_packet_t packet;
        while (mouse_try_get_packet(&packet)) {
            wm_handle_mouse_packet(wm, &packet);
        }

        while (keyboard_event_available()) {
            struct key_event ev = keyboard_get_event();

            if (ev.pressed && ev.ctrl && ev.scancode == KEY_ESC) {
                wm_destroy(wm);
                keyboard_set_echo_mode(true);
                psf_free_font(font);
                task_exit();
                return;
            }

            if (ev.pressed && ev.scancode == KEY_ESC && st.apps_win) {
                window_t *focused = wm_get_focused_window(wm);
                if (focused == st.apps_win) {
                    wm_close_window(wm, st.apps_win);
                    st.apps_win = NULL;
                    continue;
                }
            }

            wm_handle_key_event(wm, &ev);
        }

        wm_render(wm);
        sleep(10);
    }
}
