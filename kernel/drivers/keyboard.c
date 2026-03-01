#include <drivers/keyboard.h>
#include <kernel/kernel.h>
#ifdef ENABLE_VGA
#include <drivers/vga.h>
#endif
#include <asm/port.h>
#include <asm/processor.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifndef ENABLE_VGA
#define vga_print(...) ((void)0)
#define vga_put_char(...) ((void)0)
#define vga_cursor_update(...) ((void)0)
#define vga_backspace(...) ((void)0)
#define vga_color_set(...) ((void)0)
#define vga_color_make(...) ((uint8_t)0)
#define VGA_COLOR_LIGHT_RED 0
#define VGA_COLOR_BLACK 0
#define VGA_COLOR_LIGHT_GREY 0
#endif

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static size_t buffer_head = 0;
static size_t buffer_tail = 0;
static size_t buffer_count = 0;

#define EVENT_BUFFER_SIZE 512
static struct key_event event_buffer[EVENT_BUFFER_SIZE];
static size_t event_head = 0;
static size_t event_tail = 0;
static size_t event_count = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool num_lock = false;
static bool scroll_lock = false;
static bool echo_mode = false;

static bool ext_scancode = false;
static bool pause_break = false;
static uint8_t last_scancode = 0;
static size_t current_layout = 0;
static keyboard_hotkey_handler_t hotkey_handler = NULL;

static uint8_t pause_buffer[6];
static int pause_index = 0;

static uint8_t scancode_buffer[KEYBOARD_BUFFER_SIZE];
static size_t scancode_head = 0;
static size_t scancode_tail = 0;
static size_t scancode_count = 0;

static void buffer_put(char c) {
    if (buffer_count < KEYBOARD_BUFFER_SIZE) {
        keyboard_buffer[buffer_tail] = c;
        buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
        buffer_count++;
    }
}

static char buffer_get(void) {
    char c = 0;
    if (buffer_count > 0) {
        c = keyboard_buffer[buffer_head];
        buffer_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
        buffer_count--;
    }
    return c;
}

static void scancode_buffer_put(uint8_t scancode) {
    if (scancode_count < KEYBOARD_BUFFER_SIZE) {
        scancode_buffer[scancode_tail] = scancode;
        scancode_tail = (scancode_tail + 1) % KEYBOARD_BUFFER_SIZE;
        scancode_count++;
    }
}

static uint8_t scancode_buffer_get(void) {
    uint8_t scancode = 0;
    if (scancode_count > 0) {
        scancode = scancode_buffer[scancode_head];
        scancode_head = (scancode_head + 1) % KEYBOARD_BUFFER_SIZE;
        scancode_count--;
    }
    return scancode;
}

static void event_buffer_put(struct key_event event) {
    if (event_count < EVENT_BUFFER_SIZE) {
        event_buffer[event_tail] = event;
        event_tail = (event_tail + 1) % EVENT_BUFFER_SIZE;
        event_count++;
    }
}

static struct key_event event_buffer_get(void) {
    struct key_event event = {0};
    if (event_count > 0) {
        event = event_buffer[event_head];
        event_head = (event_head + 1) % EVENT_BUFFER_SIZE;
        event_count--;
    }
    return event;
}

static void keyboard_send_command(uint8_t cmd) {
    int timeout = 1000;
    while ((inb(0x64) & 0x02) && timeout-- > 0);
    outb(0x60, cmd);
}

static uint8_t keyboard_read_response(void) {
    int timeout = 1000;
    while (!(inb(0x64) & 0x01) && timeout-- > 0);
    if (timeout <= 0) return 0xFF;
    return inb(0x60);
}

static uint8_t keyboard_read_status(void) {
    return inb(0x64);
}

static char get_ascii(uint8_t scancode, bool shift) {
    if (scancode > SC_MAX) return 0;
    return keyboard_map[current_layout][shift ? 1 : 0][scancode];
}

static bool is_ext_scancode(uint8_t scancode) {
    for (int i = 0; ext_scancodes[i] != 0; i++) {
        if (ext_scancodes[i] == scancode) return true;
    }
    return false;
}

static void handle_special_key(uint8_t scancode, bool pressed) {
    switch (scancode) {
        case KEY_LSHIFT: case KEY_RSHIFT:
            shift_pressed = pressed;
            break;
        case KEY_LCTRL:
            ctrl_pressed = pressed;
            break;
        case KEY_LALT:
            alt_pressed = pressed;
            break;
        case KEY_CAPS:
            if (pressed) {
                caps_lock = !caps_lock;
                keyboard_set_leds(scroll_lock, num_lock, caps_lock);
                if (echo_mode) {
                    vga_print("\n[Caps Lock: ");
                    vga_print(caps_lock ? "ON]" : "OFF]");
                }
            }
            break;
        case KEY_NUMLOCK:
            if (pressed) {
                num_lock = !num_lock;
                keyboard_set_leds(scroll_lock, num_lock, caps_lock);
                if (echo_mode) {
                    vga_print("\n[Num Lock: ");
                    vga_print(num_lock ? "ON]" : "OFF]");
                }
            }
            break;
        case KEY_SCROLLLOCK:
            if (pressed) {
                scroll_lock = !scroll_lock;
                keyboard_set_leds(scroll_lock, num_lock, caps_lock);
                if (echo_mode) {
                    vga_print("\n[Scroll Lock: ");
                    vga_print(scroll_lock ? "ON]" : "OFF]");
                }
            }
            break;
    }
}

static void handle_extended_scancode(uint8_t scancode, bool pressed) {
    if (!pressed) return;
    
    struct key_event event = {
        .scancode = scancode,
        .ascii = 0,
        .pressed = pressed,
        .shift = shift_pressed,
        .ctrl = ctrl_pressed,
        .alt = alt_pressed,
        .caps = caps_lock
    };
    
    switch (scancode) {
        case KEY_ENTER:
            event.ascii = '\n';
            if (echo_mode && !ctrl_pressed && !alt_pressed) {
                vga_put_char('\n');
                vga_cursor_update();
            }
            break;
        case KEY_BACKSPACE:
            event.ascii = '\b';
            if (echo_mode && !ctrl_pressed && !alt_pressed) {
                vga_backspace();
                vga_cursor_update();
            }
            break;
        case KEY_TAB:
            event.ascii = '\t';
            if (echo_mode && !ctrl_pressed && !alt_pressed) {
                vga_put_char('\t');
                vga_cursor_update();
            }
            break;
        default:
            event.ascii = 0;
            break;
    }
    
    event_buffer_put(event);
    
    if (event.ascii != 0) {
        buffer_put(event.ascii);
    }
}

static void handle_ctrl_alt_combo(uint8_t scancode, bool pressed) {
    if (!pressed) return;
    
    if (ctrl_pressed && alt_pressed && scancode == 0x53) {
        outb(0x64, 0xFE);
        return;
    }
}

void keyboard_set_echo_mode(bool value) {
    echo_mode = value;
}

bool keyboard_get_echo_mode(void) {
    return echo_mode;
}

uint8_t keyboard_get_scancode(void) {
    if (scancode_count == 0) return 0;
    return scancode_buffer_get();
}

bool keyboard_scancode_available(void) {
    return scancode_count > 0;
}

char keyboard_scancode_to_ascii(uint8_t scancode) {
    bool shift = shift_pressed;
    
    if (scancode >= 0x10 && scancode <= 0x19)
        shift = shift_pressed ^ caps_lock;
    
    return get_ascii(scancode, shift);
}

void keyboard_init(void) {
    buffer_head = buffer_tail = buffer_count = 0;
    event_head = event_tail = event_count = 0;
    scancode_head = scancode_tail = scancode_count = 0;
    
    shift_pressed = ctrl_pressed = alt_pressed = false;
    caps_lock = num_lock = scroll_lock = false;
    echo_mode = true;
    ext_scancode = false;
    pause_break = false;
    pause_index = 0;
    
    outb(0x64, 0xAD);
    
    while (inb(0x64) & 0x01) inb(0x60);
    
    outb(0x64, 0xAE);
    
    keyboard_send_command(0xF4);
    keyboard_read_response();
    
    keyboard_set_leds(false, false, false);
    
    outb(0x21, inb(0x21) & 0xFD);
}

void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);
    last_scancode = scancode;
    
    scancode_buffer_put(scancode);
    
    if (scancode == 0xE1) {
        pause_break = true;
        pause_index = 0;
        return;
    }
    
    if (pause_break) {
        pause_buffer[pause_index++] = scancode;
        if (pause_index >= 6) {
            pause_break = false;
            struct key_event event = {
                .scancode = KEY_PAUSE,
                .ascii = 0,
                .pressed = true,
                .shift = shift_pressed,
                .ctrl = ctrl_pressed,
                .alt = alt_pressed,
                .caps = caps_lock
            };
            event_buffer_put(event);
        }
        return;
    }
    
    if (scancode == 0xE0) {
        ext_scancode = true;
        return;
    }
    
    bool pressed = !(scancode & 0x80);
    uint8_t keycode = scancode & 0x7F;
    
    handle_ctrl_alt_combo(keycode, pressed);
    
    if (ext_scancode) {
        handle_extended_scancode(keycode, pressed);
        ext_scancode = false;
        return;
    }
    
    handle_special_key(keycode, pressed);

    if (pressed && alt_pressed && keycode >= KEY_1 && keycode <= KEY_8) {
        if (hotkey_handler) hotkey_handler(keycode, true, shift_pressed, ctrl_pressed, alt_pressed);
        return;
    }
    
    if (pressed) {
        bool effective_shift = shift_pressed ^ caps_lock;
        char ascii = get_ascii(keycode, effective_shift);
        
        struct key_event event = {
            .scancode = keycode,
            .ascii = ascii,
            .pressed = pressed,
            .shift = shift_pressed,
            .ctrl = ctrl_pressed,
            .alt = alt_pressed,
            .caps = caps_lock
        };
        
        event_buffer_put(event);
        
        if (ascii != 0) {
            if (ctrl_pressed) {
                switch(ascii) {
                    case 'c': case 'C':
                        buffer_put(0x03);
                        break;
                    case 'd': case 'D':
                        buffer_put(0x04);
                        break;
                    case 'z': case 'Z':
                        buffer_put(0x1A);
                        break;
                    case 'l': case 'L':
                        buffer_put(0x0C);
                        break;
                    default:
                        if (ascii >= 'a' && ascii <= 'z') {
                            buffer_put(ascii - 'a' + 1);
                        }
                        else if (ascii >= 'A' && ascii <= 'Z') {
                            buffer_put(ascii - 'A' + 1);
                        }
                        else {
                            buffer_put(ascii);
                        }
                }
            } 
            else if (alt_pressed) {
                if (echo_mode) {
                    vga_color_set(vga_color_make(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    vga_print("[Alt+");
                    vga_put_char(ascii);
                    vga_put_char(']');
                    vga_color_set(vga_color_make(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                }
            }
            else {
                buffer_put(ascii);
                
                if (echo_mode) {
                    if (ascii == '\n') {
                        vga_put_char('\n');
                        vga_cursor_update();
                    }
                    else if (ascii == '\b') {
                        vga_backspace();
                        vga_cursor_update();
                    }
                    else if (ascii == '\t') {
                        vga_put_char('\t');
                        vga_cursor_update();
                    }
                    else {
                        vga_put_char(ascii);
                        vga_cursor_update();
                    }
                }
            }
        }
    }
}

char keyboard_getchar(void) {
    while (buffer_count == 0) hlt();
    return buffer_get();
}

bool keyboard_available(void) {
    return buffer_count > 0;
}

struct key_event keyboard_get_event(void) {
    while (event_count == 0) hlt();
    return event_buffer_get();
}

bool keyboard_event_available(void) {
    return event_count > 0;
}

bool keyboard_try_get_event(struct key_event *out) {
    if (!out || event_count == 0) return false;
    *out = event_buffer_get();
    return true;
}

void keyboard_set_leds(bool scroll, bool num, bool caps) {
    scroll_lock = scroll;
    num_lock = num;
    caps_lock = caps;
    
    int timeout = 1000;
    while ((inb(0x64) & 0x02) && timeout-- > 0);
    
    outb(0x60, 0xED);
    
    timeout = 1000;
    while (!(inb(0x64) & 0x01) && timeout-- > 0);
    if (timeout <= 0) return;
    
    uint8_t response = inb(0x60);
    if (response != 0xFA) return;
    
    uint8_t leds = 0;
    if (scroll) leds |= 0x01;
    if (num)    leds |= 0x02;
    if (caps)   leds |= 0x04;
    
    outb(0x60, leds);
    
    timeout = 1000;
    while (!(inb(0x64) & 0x01) && timeout-- > 0);
    if (timeout <= 0) return;
    inb(0x60);
}

bool keyboard_caps_lock(void) {
    return caps_lock;
}

bool keyboard_num_lock(void) {
    return num_lock;
}

bool keyboard_scroll_lock(void) {
    return scroll_lock;
}

void keyboard_clear_buffers(void) {
    buffer_head = buffer_tail = buffer_count = 0;
    event_head = event_tail = event_count = 0;
    scancode_head = scancode_tail = scancode_count = 0;
}

uint8_t keyboard_get_last_scancode(void) {
    return last_scancode;
}

void keyboard_set_layout(size_t layout_index) {
    if (layout_index < LAYOUT_COUNT)
        current_layout = layout_index;
}

size_t keyboard_get_layout(void) {
    return current_layout;
}

void keyboard_set_hotkey_handler(keyboard_hotkey_handler_t handler) {
    hotkey_handler = handler;
}
