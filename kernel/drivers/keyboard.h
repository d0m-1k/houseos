#pragma once

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_BUFFER_SIZE 256
#define SC_MAX 0x57
#define LAYOUT_COUNT 1

// Структура события клавиатуры
struct key_event {
    uint8_t scancode;
    char ascii;
    bool pressed;
    bool shift;
    bool ctrl;
    bool alt;
    bool caps;
};

// Специальные клавиши
enum key_spec {
    KEY_ESC        = 0x01,
    KEY_BACKSPACE  = 0x0E,
    KEY_TAB        = 0x0F,
    KEY_ENTER      = 0x1C,
    KEY_LCTRL      = 0x1D,
    KEY_LSHIFT     = 0x2A,
    KEY_RSHIFT     = 0x36,
    KEY_LALT       = 0x38,
    KEY_CAPS       = 0x3A,
    KEY_F1         = 0x3B,
    KEY_F2         = 0x3C,
    KEY_F3         = 0x3D,
    KEY_F4         = 0x3E,
    KEY_F5         = 0x3F,
    KEY_F6         = 0x40,
    KEY_F7         = 0x41,
    KEY_F8         = 0x42,
    KEY_F9         = 0x43,
    KEY_F10        = 0x44,
    KEY_F11        = 0x57,
    KEY_F12        = 0x58,
    KEY_NUMLOCK    = 0x45,
    KEY_SCROLLLOCK = 0x46,
    KEY_HOME       = 0x47,
    KEY_UP         = 0x48,
    KEY_PGUP       = 0x49,
    KEY_LEFT       = 0x4B,
    KEY_RIGHT      = 0x4D,
    KEY_END        = 0x4F,
    KEY_DOWN       = 0x50,
    KEY_PGDOWN     = 0x51,
    KEY_INS        = 0x52,
    KEY_DEL        = 0x53,
    KEY_PAUSE      = 0x61  // Специальный код для Pause/Break
};

// Раскладка клавиатуры (QWERTY)
static const char keyboard_map[LAYOUT_COUNT][2][255] = {
    { // English (QWERTY)
        { // without shift
            0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
            '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
            0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 
            0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
            '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        }, { // with shift
            0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
            '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
            0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
            0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
            '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        }
    }
};

// Расширенные скан-коды
static const uint8_t ext_scancodes[] = {
    0x1C,  // Enter (numpad)
    0x35,  // / (numpad)
    0x37,  // Print Screen
    0x38,  // Right Alt
    0x47,  // Home
    0x48,  // Up
    0x49,  // Page Up
    0x4B,  // Left
    0x4D,  // Right
    0x4F,  // End
    0x50,  // Down
    0x51,  // Page Down
    0x52,  // Insert
    0x53,  // Delete
    0x5B,  // Left Super
    0x5C,  // Right Super
    0x5D,  // Menu
    0x5E,  // Power
    0x5F,  // Sleep
    0x63,  // Wake
    0
};

// Прототипы функций
void keyboard_init(void);
void keyboard_handler(void);
char keyboard_getchar(void);
bool keyboard_available(void);
uint8_t keyboard_get_scancode(void);
bool keyboard_scancode_available(void);
char keyboard_scancode_to_ascii(uint8_t scancode);
struct key_event keyboard_get_event(void);
bool keyboard_event_available(void);
void keyboard_set_echo_mode(bool value);
bool keyboard_get_echo_mode(void);
void keyboard_set_leds(bool scroll, bool num, bool caps);
bool keyboard_caps_lock(void);
bool keyboard_num_lock(void);
bool keyboard_scroll_lock(void);
void keyboard_clear_buffers(void);
uint8_t keyboard_get_last_scancode(void);
void keyboard_set_layout(size_t layout_index);
size_t keyboard_get_layout(void);