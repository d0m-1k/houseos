#include <drivers/inputdev.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/power.h>
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
    if (request == DEV_IOCTL_KBD_GET_EVENT) {
        struct key_event ev;
        dev_keyboard_event_t *out = (dev_keyboard_event_t*)arg;
        if (!out) return -1;
        if (!keyboard_try_get_event(&ev)) return -1;
        out->scancode = ev.scancode;
        out->ascii = ev.ascii;
        out->pressed = ev.pressed ? 1U : 0U;
        out->shift = ev.shift ? 1U : 0U;
        out->ctrl = ev.ctrl ? 1U : 0U;
        out->alt = ev.alt ? 1U : 0U;
        out->caps = ev.caps ? 1U : 0U;
        out->reserved = 0U;
        return 0;
    }
    if (request == DEV_IOCTL_KBD_SET_LAYOUT) {
        uint32_t *idx = (uint32_t*)arg;
        if (!idx) return -1;
        keyboard_set_layout((size_t)(*idx));
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

static int power_dev_ioctl(void *ctx, uint32_t request, void *arg) {
    (void)ctx;
    switch (request) {
        case DEV_IOCTL_POWER_REBOOT:
            power_reboot();
            return 0;
        case DEV_IOCTL_POWER_POWEROFF:
            power_poweroff();
            return 0;
        case DEV_IOCTL_POWER_GET_CAD_MODE:
            if (!arg) return -1;
            *(uint32_t*)arg = power_get_ctrl_alt_del_mode();
            return 0;
        case DEV_IOCTL_POWER_SET_CAD_MODE:
            if (!arg) return -1;
            power_set_ctrl_alt_del_mode(*(uint32_t*)arg);
            return 0;
        default:
            return -1;
    }
}

void inputdev_init(devfs_t *devfs) {
    if (!devfs) return;
    devfs_create_device_ops(devfs, "/keyboard", MEMFS_DEV_READ,
        keyboard_dev_read, 0, keyboard_dev_ioctl, 0);
    devfs_create_device_ops(devfs, "/mouse", MEMFS_DEV_READ,
        mouse_dev_read, 0, mouse_dev_ioctl, 0);
    devfs_create_device_ops(devfs, "/power", 0,
        0, 0, power_dev_ioctl, 0);
}
