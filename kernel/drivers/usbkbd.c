#include <drivers/usbkbd.h>
#include <drivers/xhci.h>
#include <drivers/keyboard.h>
#include <drivers/tty.h>
#include <asm/processor.h>
#include <devctl.h>
#include <string.h>

#define USBKBD_MAX_DEVS 4
#define USBKBD_EVENT_Q 256

typedef struct {
    uint8_t present;
    uint8_t hc;
    uint8_t port;
    uint8_t iface_num;
    uint8_t ep_in;
    uint16_t mps;
    uint8_t interval;
    uint8_t prev[8];
    uint8_t report[64];
    uint8_t fail_count;
} usbkbd_dev_t;

static usbkbd_dev_t g_kbd[USBKBD_MAX_DEVS];
static uint8_t g_kbd_count = 0;
static uint32_t g_probe_ticks = 0;
static dev_keyboard_event_t g_ev_q[USBKBD_EVENT_Q];
static uint32_t g_ev_head = 0;
static uint32_t g_ev_tail = 0;
static uint32_t g_ev_count = 0;
static uint8_t g_caps_state = 0;
static uint8_t g_input_seen = 0;

static int usbkbd_is_hid_key(uint8_t k) {
    return (k == 0u) || (k >= 0x04u && k <= 0xA4u);
}

static void usbkbd_decode_report(const uint8_t *raw, uint32_t got, uint8_t out[8]) {
    int best_base = -1;
    int best_score = -100000;
    if (!raw || !out || got == 0u) return;
    memset(out, 0, 8);
    if (got < 8u) {
        memcpy(out, raw, got);
        return;
    }

    for (int base = 0; base <= 1; base++) {
        int score = 0;
        if ((uint32_t)(base + 8) > got) continue;

        if (raw[base + 1] == 0u) score += 10;
        else score -= 6;

        if (base == 0) score += 3;
        if (base == 1 && raw[0] != 0u) score += 2;
        if (base == 1 && raw[0] == 0u) score -= 4;

        for (int i = 2; i < 8; i++) {
            uint8_t k = raw[base + i];
            if (usbkbd_is_hid_key(k)) score += 2;
            else score -= 10;
        }

        if (score > best_score) {
            best_score = score;
            best_base = base;
        }
    }

    if (best_base < 0) {
        memcpy(out, raw, 8);
    } else {
        memcpy(out, &raw[best_base], 8);
    }
}

static void usbkbd_event_push(const dev_keyboard_event_t *ev) {
    if (!ev) return;
    if (g_ev_count >= USBKBD_EVENT_Q) {
        g_ev_head = (g_ev_head + 1u) % USBKBD_EVENT_Q;
        g_ev_count--;
    }
    g_ev_q[g_ev_tail] = *ev;
    g_ev_tail = (g_ev_tail + 1u) % USBKBD_EVENT_Q;
    g_ev_count++;
}

static int usbkbd_event_pop(dev_keyboard_event_t *out) {
    if (!out || g_ev_count == 0u) return -1;
    *out = g_ev_q[g_ev_head];
    g_ev_head = (g_ev_head + 1u) % USBKBD_EVENT_Q;
    g_ev_count--;
    return 0;
}

static ssize_t usbkbd_dev_read(void *ctx, void *buf, size_t size) {
    dev_keyboard_event_t ev;
    (void)ctx;
    if (!buf || size < sizeof(ev)) return -1;
    while (usbkbd_event_pop(&ev) != 0) {
        sti();
        hlt();
        cli();
    }
    *(dev_keyboard_event_t*)buf = ev;
    return (ssize_t)sizeof(ev);
}

static void usbkbd_emit_event(uint8_t scancode, uint8_t pressed, uint8_t shift, uint8_t ctrl, uint8_t alt, uint8_t caps, int ascii) {
    dev_keyboard_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.scancode = scancode;
    ev.ascii = (int8_t)ascii;
    ev.pressed = pressed ? 1u : 0u;
    ev.shift = shift ? 1u : 0u;
    ev.ctrl = ctrl ? 1u : 0u;
    ev.alt = alt ? 1u : 0u;
    ev.caps = caps ? 1u : 0u;
    usbkbd_event_push(&ev);
}

static uint8_t hid_to_set1(uint8_t hid, uint8_t *extended) {
    *extended = 0;
    switch (hid) {
        case 0x04: return 0x1E; /* a */
        case 0x05: return 0x30; /* b */
        case 0x06: return 0x2E; /* c */
        case 0x07: return 0x20; /* d */
        case 0x08: return 0x12; /* e */
        case 0x09: return 0x21; /* f */
        case 0x0A: return 0x22; /* g */
        case 0x0B: return 0x23; /* h */
        case 0x0C: return 0x17; /* i */
        case 0x0D: return 0x24; /* j */
        case 0x0E: return 0x25; /* k */
        case 0x0F: return 0x26; /* l */
        case 0x10: return 0x32; /* m */
        case 0x11: return 0x31; /* n */
        case 0x12: return 0x18; /* o */
        case 0x13: return 0x19; /* p */
        case 0x14: return 0x10; /* q */
        case 0x15: return 0x13; /* r */
        case 0x16: return 0x1F; /* s */
        case 0x17: return 0x14; /* t */
        case 0x18: return 0x16; /* u */
        case 0x19: return 0x2F; /* v */
        case 0x1A: return 0x11; /* w */
        case 0x1B: return 0x2D; /* x */
        case 0x1C: return 0x15; /* y */
        case 0x1D: return 0x2C; /* z */
        case 0x1E: return KEY_1;
        case 0x1F: return KEY_2;
        case 0x20: return KEY_3;
        case 0x21: return KEY_4;
        case 0x22: return KEY_5;
        case 0x23: return KEY_6;
        case 0x24: return KEY_7;
        case 0x25: return KEY_8;
        case 0x26: return 0x0A; /* 9 */
        case 0x27: return 0x0B; /* 0 */
        case 0x28: return KEY_ENTER;
        case 0x29: return KEY_ESC;
        case 0x2A: return KEY_BACKSPACE;
        case 0x2B: return KEY_TAB;
        case 0x2C: return 0x39; /* space */
        case 0x2D: return 0x0C; /* - */
        case 0x2E: return 0x0D; /* = */
        case 0x2F: return 0x1A; /* [ */
        case 0x30: return 0x1B; /* ] */
        case 0x31: return 0x2B; /* \ */
        case 0x33: return 0x27; /* ; */
        case 0x34: return 0x28; /* ' */
        case 0x35: return 0x29; /* ` */
        case 0x36: return 0x33; /* , */
        case 0x37: return 0x34; /* . */
        case 0x38: return 0x35; /* / */
        case 0x39: return KEY_CAPS;
        case 0x3A: return KEY_F1;
        case 0x3B: return KEY_F2;
        case 0x3C: return KEY_F3;
        case 0x3D: return KEY_F4;
        case 0x3E: return KEY_F5;
        case 0x3F: return KEY_F6;
        case 0x40: return KEY_F7;
        case 0x41: return KEY_F8;
        case 0x42: return KEY_F9;
        case 0x43: return KEY_F10;
        case 0x44: return KEY_F11;
        case 0x45: return KEY_F12;
        case 0x49: return KEY_INS;
        case 0x4A: return KEY_HOME;
        case 0x4B: return KEY_PGUP;
        case 0x4C: return KEY_DEL;
        case 0x4D: return KEY_END;
        case 0x4E: return KEY_PGDOWN;
        case 0x4F: return KEY_RIGHT;
        case 0x50: return KEY_LEFT;
        case 0x51: return KEY_DOWN;
        case 0x52: return KEY_UP;
        default: return 0;
    }
}

static int hid_in_report(const uint8_t report[8], uint8_t key) {
    for (uint32_t i = 2; i < 8; i++) {
        if (report[i] == key) return 1;
    }
    return 0;
}

static void handle_modifiers(usbkbd_dev_t *d, const uint8_t *r) {
    uint8_t old = d->prev[0];
    uint8_t now = r[0];
    uint8_t shift = ((now & 0x02u) || (now & 0x20u)) ? 1u : 0u;
    uint8_t ctrl = ((now & 0x01u) || (now & 0x10u)) ? 1u : 0u;
    uint8_t alt = ((now & 0x04u) || (now & 0x40u)) ? 1u : 0u;
    if ((old ^ now) == 0) return;
    if ((old & 0x01u) != (now & 0x01u)) {
        uint8_t pressed = (now & 0x01u) ? 1u : 0u;
        keyboard_inject_event(KEY_LCTRL, 0, pressed, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(KEY_LCTRL, pressed, shift, ctrl, alt, g_caps_state, 0);
    }
    if ((old & 0x02u) != (now & 0x02u)) {
        uint8_t pressed = (now & 0x02u) ? 1u : 0u;
        keyboard_inject_event(KEY_LSHIFT, 0, pressed, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(KEY_LSHIFT, pressed, shift, ctrl, alt, g_caps_state, 0);
    }
    if ((old & 0x04u) != (now & 0x04u)) {
        uint8_t pressed = (now & 0x04u) ? 1u : 0u;
        keyboard_inject_event(KEY_LALT, 0, pressed, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(KEY_LALT, pressed, shift, ctrl, alt, g_caps_state, 0);
    }
    if ((old & 0x10u) != (now & 0x10u)) {
        uint8_t pressed = (now & 0x10u) ? 1u : 0u;
        keyboard_inject_event(KEY_LCTRL, 0, pressed, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(KEY_LCTRL, pressed, shift, ctrl, alt, g_caps_state, 0);
    }
    if ((old & 0x20u) != (now & 0x20u)) {
        uint8_t pressed = (now & 0x20u) ? 1u : 0u;
        keyboard_inject_event(KEY_RSHIFT, 0, pressed, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(KEY_RSHIFT, pressed, shift, ctrl, alt, g_caps_state, 0);
    }
    if ((old & 0x40u) != (now & 0x40u)) {
        uint8_t pressed = (now & 0x40u) ? 1u : 0u;
        keyboard_inject_event(KEY_LALT, 0, pressed, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(KEY_LALT, pressed, shift, ctrl, alt, g_caps_state, 0);
    }
}

static void handle_keys(usbkbd_dev_t *d, const uint8_t *r) {
    uint8_t ext = 0;
    uint8_t shift = ((r[0] & 0x02u) || (r[0] & 0x20u)) ? 1u : 0u;
    uint8_t ctrl = ((r[0] & 0x01u) || (r[0] & 0x10u)) ? 1u : 0u;
    uint8_t alt = ((r[0] & 0x04u) || (r[0] & 0x40u)) ? 1u : 0u;
    for (uint32_t i = 2; i < 8; i++) {
        uint8_t key = d->prev[i];
        uint8_t sc;
        if (!key || hid_in_report(r, key)) continue;
        sc = hid_to_set1(key, &ext);
        (void)ext;
        keyboard_inject_event(sc, 0, 0, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(sc, 0u, shift, ctrl, alt, g_caps_state, 0);
    }
    for (uint32_t i = 2; i < 8; i++) {
        uint8_t key = r[i];
        uint8_t sc;
        int ascii = 0;
        if (!key || hid_in_report(d->prev, key)) continue;
        sc = hid_to_set1(key, &ext);
        if (key == 0x39u) g_caps_state ^= 1u;
        if (sc < 255u) {
            uint8_t eff_shift = (uint8_t)(shift ^ g_caps_state);
            ascii = (int)keyboard_map[0][eff_shift ? 1 : 0][sc];
        }
        keyboard_inject_event(sc, (char)ascii, 1, shift, ctrl, alt, g_caps_state);
        usbkbd_emit_event(sc, 1u, shift, ctrl, alt, g_caps_state, ascii);
    }
}

static void usbkbd_probe(int log_result) {
    xhci_rescan_all();
    g_kbd_count = 0;
    memset(g_kbd, 0, sizeof(g_kbd));
    for (uint32_t hc = 0; hc < xhci_count() && g_kbd_count < USBKBD_MAX_DEVS; hc++) {
        const xhci_info_t *hci = xhci_get(hc);
        uint32_t max_ports = hci ? hci->max_ports : 0;
        if (max_ports > 32u) max_ports = 32u;
        for (uint32_t p = 1; p <= max_ports && g_kbd_count < USBKBD_MAX_DEVS; p++) {
            dev_usb_dev_info_t st;
            xhci_iface_info_t iface;
            usbkbd_dev_t *d;
            if (xhci_get_dev_state(hc, p, &st) != 0 || !st.present || st.slot_id == 0) continue;
            memset(&iface, 0, sizeof(iface));
            (void)xhci_get_iface_info(hc, p, &iface);
            if (!(iface.valid &&
                  iface.iface_class == 0x03u &&
                  iface.iface_subclass == 0x01u &&
                  iface.iface_proto == 0x01u) &&
                !(st.dev_class == 0x03u && st.dev_subclass == 0x01u && st.dev_proto == 0x01u) &&
                !(st.dev_class == 0x00u && st.dev_subclass == 0x00u && st.dev_proto == 0x00u)) {
                continue;
            }
            if (!iface.valid) {
                iface.iface_num = 0;
                iface.intr_in_ep = 1u;
                iface.intr_mps = 8u;
                iface.intr_interval = 10u;
            } else if (iface.intr_in_ep == 0) {
                iface.intr_in_ep = 1u;
                iface.intr_mps = iface.intr_mps ? iface.intr_mps : 8u;
                iface.intr_interval = iface.intr_interval ? iface.intr_interval : 10u;
            }

            (void)xhci_control_out0(hc, p, 0x00u, 0x09u, 1u, 0u); /* SET_CONFIGURATION(1) */
            if (xhci_configure_interrupt_in_endpoint(hc, p, iface.intr_in_ep, iface.intr_mps, iface.intr_interval) != 0) {
                if (log_result) tty_klog("usbkbd: cfg ep failed\n");
                continue;
            }
            (void)xhci_control_out0(hc, p, 0x21u, 0x0Bu, 0u, iface.iface_num); /* SET_PROTOCOL boot */
            (void)xhci_control_out0(hc, p, 0x21u, 0x0Au, 0u, iface.iface_num); /* SET_IDLE */
            d = &g_kbd[g_kbd_count++];
            d->present = 1;
            d->hc = (uint8_t)hc;
            d->port = (uint8_t)p;
            d->iface_num = iface.iface_num;
            d->ep_in = iface.intr_in_ep;
            d->mps = iface.intr_mps;
            d->interval = iface.intr_interval;
            memset(d->prev, 0, sizeof(d->prev));
            memset(d->report, 0, sizeof(d->report));
            d->fail_count = 0;
            if (log_result) {
                tty_klog("usbkbd: found on xhci");
                {
                    char tmp[16];
                    utoa(hc, tmp, 10); tty_klog(tmp);
                    tty_klog("p"); utoa(p, tmp, 10); tty_klog(tmp);
                    tty_klog(" ep="); utoa((uint32_t)d->ep_in, tmp, 10); tty_klog(tmp);
                    tty_klog("\n");
                }
            }
        }
    }
    if (log_result && g_kbd_count == 0) tty_klog("usbkbd: no keyboard\n");
}

void usbkbd_init(devfs_t *devfs) {
    g_probe_ticks = 0;
    g_kbd_count = 0;
    g_caps_state = 0;
    g_ev_head = 0;
    g_ev_tail = 0;
    g_ev_count = 0;
    g_input_seen = 0;
    memset(g_kbd, 0, sizeof(g_kbd));
    if (devfs) {
        (void)devfs_create_device_ops(devfs, "/usbkbd0", MEMFS_DEV_READ, usbkbd_dev_read, 0, 0, 0);
    }
    usbkbd_probe(1);
}

void usbkbd_poll(void) {
    uint32_t got = 0;
    uint8_t rpt[8];
    if (g_kbd_count == 0) {
        g_probe_ticks++;
        if ((g_probe_ticks % 128u) == 0u) {
            int first = (g_probe_ticks == 128u) ? 1 : 0;
            usbkbd_probe(first);
            if (g_kbd_count != 0) tty_klog("usbkbd: late attach\n");
        }
        return;
    }
    for (uint32_t i = 0; i < g_kbd_count; i++) {
        usbkbd_dev_t *d = &g_kbd[i];
        if (!d->present) continue;
        memset(d->report, 0, sizeof(d->report));
        if (xhci_interrupt_in(d->hc, d->port, d->ep_in, d->report, sizeof(d->report), &got) != 0) {
            d->fail_count++;
            if (d->fail_count >= 32u) {
                d->fail_count = 0;
                (void)xhci_configure_interrupt_in_endpoint(d->hc, d->port, d->ep_in, d->mps, d->interval);
            }
            continue;
        }
        d->fail_count = 0;
        if (got < 8u) continue;
        usbkbd_decode_report(d->report, got, rpt);
        if (memcmp(rpt, d->prev, 8) == 0) continue;
        if (!g_input_seen) {
            g_input_seen = 1;
            tty_klog("usbkbd: input ok\n");
        }
        handle_modifiers(d, rpt);
        handle_keys(d, rpt);
        memcpy(d->prev, rpt, 8);
    }
}
