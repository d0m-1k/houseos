#include <drivers/inputdev.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <asm/processor.h>
#include <devctl.h>

static ssize_t keyboard_dev_read(void *ctx, void *buf, size_t size) {
    struct key_event ev;
    (void)ctx;
    if (!buf || size < sizeof(struct key_event)) return -1;
    while (!keyboard_try_get_event(&ev)) {
        sti();
        hlt();
        cli();
    }
    *(struct key_event*)buf = ev;
    return (ssize_t)sizeof(struct key_event);
}

static int keyboard_dev_ioctl(void *ctx, uint32_t request, void *arg) {
    (void)ctx;
    if (request == DEV_IOCTL_KBD_GET_INFO) {
        dev_keyboard_info_t *out = (dev_keyboard_info_t*)arg;
        if (!out) return -1;
        out->layout = (uint32_t)keyboard_get_layout();
        out->caps_lock = keyboard_caps_lock() ? 1U : 0U;
        out->num_lock = keyboard_num_lock() ? 1U : 0U;
        out->scroll_lock = keyboard_scroll_lock() ? 1U : 0U;
        return 0;
    }
    return -1;
}

static ssize_t mouse_dev_read(void *ctx, void *buf, size_t size) {
    mouse_packet_t p;
    (void)ctx;
    if (!buf || size < sizeof(mouse_packet_t)) return -1;
    while (!mouse_try_get_packet(&p)) {
        sti();
        hlt();
        cli();
    }
    *(mouse_packet_t*)buf = p;
    return (ssize_t)sizeof(mouse_packet_t);
}

static int mouse_dev_ioctl(void *ctx, uint32_t request, void *arg) {
    (void)ctx;
    if (request == DEV_IOCTL_MOUSE_GET_INFO) {
        dev_mouse_info_t *out = (dev_mouse_info_t*)arg;
        if (!out) return -1;
        out->x = mouse_get_x();
        out->y = mouse_get_y();
        out->buttons = mouse_get_buttons();
        return 0;
    }
    return -1;
}

void inputdev_init(memfs *root_fs) {
    if (!root_fs) return;
    memfs_create_dir(root_fs, "/devices");
    memfs_create_device_ops(root_fs, "/devices/keyboard", MEMFS_DEV_READ,
        keyboard_dev_read, 0, keyboard_dev_ioctl, 0);
    memfs_create_device_ops(root_fs, "/devices/mouse", MEMFS_DEV_READ,
        mouse_dev_read, 0, mouse_dev_ioctl, 0);
}
