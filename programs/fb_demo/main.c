#include <syscall.h>
#include <stdint.h>
#include <devctl.h>
#include <hgui/app.h>

static uint8_t fb[320 * 8 * 4];
static hq_application_t g_app;
static hq_widget_t g_root;
static int g_fd = -1;
static int g_size = 0;
static int g_bpp_bytes = 0;

static int root_event(hq_widget_t *self, const hq_event_t *ev) {
    int i;
    (void)self;
    if (!ev) return 0;
    if (ev->type != HQ_EVENT_PAINT) return 0;
    for (i = 0; i < g_size; i += g_bpp_bytes) {
        fb[i + 0] = 0xFF;
        fb[i + 1] = 0x00;
        fb[i + 2] = 0x00;
        if (g_bpp_bytes == 4) fb[i + 3] = 0;
    }
    (void)write(g_fd, fb, (uint32_t)g_size);
    hq_app_quit(&g_app);
    return 1;
}

int main(void) {
    dev_fb_info_t info;
    hq_event_t paint_ev;
    g_fd = open("/dev/framebuffer/buffer", 0);
    if (g_fd < 0) return 1;
    if (ioctl(g_fd, DEV_IOCTL_VESA_GET_INFO, &info) != 0) return 1;

    g_bpp_bytes = (int)(info.bpp / 8);
    if (info.pitch == 0 || (g_bpp_bytes != 3 && g_bpp_bytes != 4)) return 1;
    g_size = 320 * 8 * g_bpp_bytes;

    hq_widget_init(&g_root, 1u);
    g_root.on_widget_event = root_event;
    hq_widget_set_geometry(&g_root, (ui_rect_t){0, 0, (int)info.width, (int)info.height});
    hq_app_init(&g_app, &g_root);

    paint_ev.type = HQ_EVENT_PAINT;
    paint_ev.target_id = 1u;
    paint_ev.arg0 = 0u;
    paint_ev.arg1 = 0u;
    paint_ev.arg2 = 0u;
    paint_ev.arg3 = 0u;
    (void)hq_app_post_event(&g_app, &paint_ev);
    (void)hq_app_exec(&g_app, 4u);

    (void)close(g_fd);
    return 0;
}
