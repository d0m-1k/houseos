#include <drivers/keyboard.h>
#include <drivers/vga.h>
#include <asm/port.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static size_t buffer_head = 0;
static size_t buffer_tail = 0;
static size_t buffer_count = 0;

// Очередь событий
#define EVENT_BUFFER_SIZE 64
static struct key_event event_buffer[EVENT_BUFFER_SIZE];
static size_t event_head = 0;
static size_t event_tail = 0;
static size_t event_count = 0;

// Состояние клавиш-модификаторов
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool caps_lock = false;
static bool num_lock = false;
static bool scroll_lock = false;
static bool echo_mode = false;

// Для обработки расширенных скан-кодов
static bool ext_scancode = false;
static bool pause_break = false;
static uint8_t last_scancode = 0;
static size_t current_layout = 0;

// Позиция в буфере для двухбайтовых скан-кодов (Pause/Break)
static uint8_t pause_buffer[6];
static int pause_index = 0;

// Буфер для скэнкодов (отдельно от ASCII)
static uint8_t scancode_buffer[KEYBOARD_BUFFER_SIZE];
static size_t scancode_head = 0;
static size_t scancode_tail = 0;
static size_t scancode_count = 0;

// Приватные функции
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

// Функции для работы с контроллером клавиатуры
static void keyboard_send_command(uint8_t cmd) {
    // Ждем, пока контроллер будет готов принять команду
    int timeout = 1000;
    while ((inb(0x64) & 0x02) && timeout-- > 0);
    outb(0x60, cmd);
}

static uint8_t keyboard_read_response(void) {
    // Ждем, пока данные станут доступны
    int timeout = 1000;
    while (!(inb(0x64) & 0x01) && timeout-- > 0);
    if (timeout <= 0) return 0xFF; // Таймаут
    return inb(0x60);
}

static uint8_t keyboard_read_status(void) {
    return inb(0x64);
}

// Преобразование скан-кода в ASCII
static char get_ascii(uint8_t scancode, bool shift) {
    if (scancode > SC_MAX) return 0;
    return keyboard_map[current_layout][shift ? 1 : 0][scancode];
}

// Проверка, является ли скан-код расширенным
static bool is_ext_scancode(uint8_t scancode) {
    for (int i = 0; ext_scancodes[i] != 0; i++) {
        if (ext_scancodes[i] == scancode) return true;
    }
    return false;
}

// Обработка специальных клавиш
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
                // Выводим состояние Caps Lock при эхо-режиме
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

// Обработка расширенных скан-кодов
static void handle_extended_scancode(uint8_t scancode, bool pressed) {
    // Обрабатываем только нажатия (не отпускания)
    if (!pressed) return;
    
    // Создаем событие для расширенной клавиши
    struct key_event event = {
        .scancode = scancode,
        .ascii = 0,
        .pressed = pressed,
        .shift = shift_pressed,
        .ctrl = ctrl_pressed,
        .alt = alt_pressed,
        .caps = caps_lock
    };
    
    // Для некоторых расширенных клавиш можем определить ASCII
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
            // Для остальных расширенных клавиш ASCII = 0
            event.ascii = 0;
            break;
    }
    
    // Добавляем событие в очередь
    event_buffer_put(event);
    
    // Также добавляем в буфер ASCII (если есть)
    if (event.ascii != 0) {
        buffer_put(event.ascii);
    }
}

// Обработка комбинаций Ctrl+Alt+...
static void handle_ctrl_alt_combo(uint8_t scancode, bool pressed) {
    if (!pressed) return;
    
    // Ctrl+Alt+Del - перезагрузка
    if (ctrl_pressed && alt_pressed && scancode == 0x53) { // Delete
        // Отправляем команду сброса
        outb(0x64, 0xFE);
        return;
    }
}

// Установка режима эха
void keyboard_set_echo_mode(bool value) {
    echo_mode = value;
}

// Получение режима эха
bool keyboard_get_echo_mode(void) {
    return echo_mode;
}

// Получение текущего скан-кода (без ожидания)
uint8_t keyboard_get_scancode(void) {
    if (scancode_count == 0) return 0;
    return scancode_buffer_get();
}

// Проверка доступности скан-кода
bool keyboard_scancode_available(void) {
    return scancode_count > 0;
}

// Преобразование скан-кода в ASCII
char keyboard_scancode_to_ascii(uint8_t scancode) {
    // Проверяем нажатие shift
    bool shift = shift_pressed;
    
    // Для букв учитываем Caps Lock
    if (scancode >= 0x10 && scancode <= 0x19) { // A-Z
        shift = shift_pressed ^ caps_lock;
    }
    
    return get_ascii(scancode, shift);
}

// Инициализация клавиатуры
void keyboard_init(void) {
    // Сбрасываем состояние
    buffer_head = buffer_tail = buffer_count = 0;
    event_head = event_tail = event_count = 0;
    scancode_head = scancode_tail = scancode_count = 0;
    
    shift_pressed = ctrl_pressed = alt_pressed = false;
    caps_lock = num_lock = scroll_lock = false;
    echo_mode = true;
    ext_scancode = false;
    pause_break = false;
    pause_index = 0;
    
    // Отключаем клавиатуру
    outb(0x64, 0xAD);
    
    // Очищаем буфер
    while (inb(0x64) & 0x01) inb(0x60);
    
    // Включаем клавиатуру
    outb(0x64, 0xAE);
    
    // Устанавливаем скан-код set 2 (по умолчанию в большинстве систем)
    keyboard_send_command(0xF0);
    keyboard_read_response();
    keyboard_send_command(0x02); // set 2
    keyboard_read_response();
    
    // Включаем клавиатуру
    keyboard_send_command(0xF4);
    keyboard_read_response();
    
    // Устанавливаем LEDs
    keyboard_set_leds(false, false, false);
    
    // Включаем прерывания
    outb(0x21, inb(0x21) & 0xFD);
}

// Обработчик прерываний клавиатуры
void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);
    last_scancode = scancode;
    
    // Сохраняем скан-код в буфер
    scancode_buffer_put(scancode);
    
    // Обработка специальной последовательности Pause/Break
    if (scancode == 0xE1) {
        pause_break = true;
        pause_index = 0;
        return;
    }
    
    if (pause_break) {
        pause_buffer[pause_index++] = scancode;
        if (pause_index >= 6) {
            pause_break = false;
            // Pause/Break нажат
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
    
    // Обработка расширенных скан-кодов
    if (scancode == 0xE0) {
        ext_scancode = true;
        return;
    }
    
    // Определяем нажата или отпущена клавиша
    bool pressed = !(scancode & 0x80);
    uint8_t keycode = scancode & 0x7F;
    
    // Обработка комбинаций Ctrl+Alt
    handle_ctrl_alt_combo(keycode, pressed);
    
    if (ext_scancode) {
        // Обработка расширенного скан-кода
        handle_extended_scancode(keycode, pressed);
        ext_scancode = false;
        return;
    }
    
    // Обработка обычных клавиш
    handle_special_key(keycode, pressed);
    
    if (pressed) {
        // Получаем ASCII символ
        bool effective_shift = shift_pressed ^ caps_lock;
        char ascii = get_ascii(keycode, effective_shift);
        
        // Создаем событие
        struct key_event event = {
            .scancode = keycode,
            .ascii = ascii,
            .pressed = pressed,
            .shift = shift_pressed,
            .ctrl = ctrl_pressed,
            .alt = alt_pressed,
            .caps = caps_lock
        };
        
        // Добавляем в очередь событий
        event_buffer_put(event);
        
        // Обработка ASCII символов
        if (ascii != 0) {
            // Обработка комбинаций с Ctrl
            if (ctrl_pressed) {
                switch(ascii) {
                    case 'c': case 'C':
                        buffer_put(0x03); // Ctrl+C
                        break;
                    case 'd': case 'D':
                        buffer_put(0x04); // Ctrl+D (EOF)
                        break;
                    case 'z': case 'Z':
                        buffer_put(0x1A); // Ctrl+Z (Suspend)
                        break;
                    case 'l': case 'L':
                        buffer_put(0x0C); // Ctrl+L (Clear screen)
                        break;
                    default:
                        // Ctrl+буква (1-26)
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
                // Alt+символ - игнорируем для буфера ASCII
                if (echo_mode) {
                    vga_color_set(vga_color_make(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    vga_print("[Alt+");
                    vga_put_char(ascii);
                    vga_put_char(']');
                    vga_color_set(vga_color_make(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                }
            }
            else {
                // Обычный символ
                buffer_put(ascii);
                
                // Вывод эха
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

// Получение символа с ожиданием
char keyboard_getchar(void) {
    while (buffer_count == 0) {
        asm volatile("hlt"); // Ожидание прерывания
    }
    return buffer_get();
}

// Проверка наличия символов
bool keyboard_available(void) {
    return buffer_count > 0;
}

// Получение события клавиатуры
struct key_event keyboard_get_event(void) {
    while (event_count == 0) {
        asm volatile("hlt"); // Ожидание прерывания
    }
    return event_buffer_get();
}

// Проверка наличия событий
bool keyboard_event_available(void) {
    return event_count > 0;
}

// Установка LED индикаторов
void keyboard_set_leds(bool scroll, bool num, bool caps) {
    scroll_lock = scroll;
    num_lock = num;
    caps_lock = caps;
    
    // Ждем готовности контроллера
    int timeout = 1000;
    while ((inb(0x64) & 0x02) && timeout-- > 0);
    
    // Отправляем команду установки LED
    outb(0x60, 0xED);
    
    // Ждем ответа
    timeout = 1000;
    while (!(inb(0x64) & 0x01) && timeout-- > 0);
    if (timeout <= 0) return;
    
    // Читаем ответ (должен быть 0xFA)
    uint8_t response = inb(0x60);
    if (response != 0xFA) return;
    
    // Отправляем битовую маску LED
    uint8_t leds = 0;
    if (scroll) leds |= 0x01;
    if (num)    leds |= 0x02;
    if (caps)   leds |= 0x04;
    
    outb(0x60, leds);
    
    // Ждем подтверждения
    timeout = 1000;
    while (!(inb(0x64) & 0x01) && timeout-- > 0);
    if (timeout <= 0) return;
    inb(0x60); // Читаем подтверждение
}

// Получение состояния Caps Lock
bool keyboard_caps_lock(void) {
    return caps_lock;
}

// Получение состояния Num Lock
bool keyboard_num_lock(void) {
    return num_lock;
}

// Получение состояния Scroll Lock
bool keyboard_scroll_lock(void) {
    return scroll_lock;
}

// Очистка буферов клавиатуры
void keyboard_clear_buffers(void) {
    buffer_head = buffer_tail = buffer_count = 0;
    event_head = event_tail = event_count = 0;
    scancode_head = scancode_tail = scancode_count = 0;
}

// Получение последнего скан-кода
uint8_t keyboard_get_last_scancode(void) {
    return last_scancode;
}

// Установка раскладки
void keyboard_set_layout(size_t layout_index) {
    if (layout_index < LAYOUT_COUNT) {
        current_layout = layout_index;
    }
}

// Получение текущей раскладки
size_t keyboard_get_layout(void) {
    return current_layout;
}