#include <drivers/mouse.h>
#include <drivers/vesa.h>
#include <asm/port.h>
#include <stdint.h>
#include <stdbool.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define PS2_STATUS_OUT_FULL 0x01
#define PS2_STATUS_IN_FULL  0x02
#define PS2_STATUS_AUX_DATA 0x20

#define MOUSE_QUEUE_SIZE 128
static mouse_packet_t g_queue[MOUSE_QUEUE_SIZE];
static volatile uint32_t g_head = 0;
static volatile uint32_t g_tail = 0;
static volatile uint32_t g_count = 0;

static volatile uint8_t g_packet[3];
static volatile uint8_t g_cycle = 0;
static volatile int16_t g_x = 0;
static volatile int16_t g_y = 0;
static volatile uint8_t g_buttons = 0;

static inline void io_wait_small(void) {
    outb(0x80, 0);
}

static bool ps2_wait_input_empty(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS) & PS2_STATUS_IN_FULL) == 0) return true;
    }
    return false;
}

static bool ps2_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & PS2_STATUS_OUT_FULL) return true;
    }
    return false;
}

static void ps2_write_cmd(uint8_t cmd) {
    if (!ps2_wait_input_empty()) return;
    outb(PS2_CMD, cmd);
    io_wait_small();
}

static void ps2_write_data(uint8_t data) {
    if (!ps2_wait_input_empty()) return;
    outb(PS2_DATA, data);
    io_wait_small();
}

static uint8_t ps2_read_data(void) {
    if (!ps2_wait_output_full()) return 0xFF;
    return inb(PS2_DATA);
}

static void mouse_write(uint8_t data) {
    ps2_write_cmd(0xD4);
    ps2_write_data(data);
}

static uint8_t mouse_read_ack(void) {
    return ps2_read_data();
}

static void queue_push(mouse_packet_t p) {
    if (g_count >= MOUSE_QUEUE_SIZE) {
        g_head = (g_head + 1) % MOUSE_QUEUE_SIZE;
        g_count--;
    }
    g_queue[g_tail] = p;
    g_tail = (g_tail + 1) % MOUSE_QUEUE_SIZE;
    g_count++;
}

bool mouse_available(void) {
    return g_count != 0;
}

bool mouse_try_get_packet(mouse_packet_t* out) {
    if (!out) return false;
    if (g_count == 0) return false;

    *out = g_queue[g_head];
    g_head = (g_head + 1) % MOUSE_QUEUE_SIZE;
    g_count--;
    return true;
}

mouse_packet_t mouse_get_packet(void) {
    mouse_packet_t p;
    while (!mouse_try_get_packet(&p)) {
        __asm__ __volatile__("hlt");
    }
    return p;
}

void mouse_init(void) {
    ps2_write_cmd(0xA8);

    ps2_write_cmd(0x20);
    uint8_t status = ps2_read_data();
    status |= 0x03;
    status &= (uint8_t)~0x30;
    ps2_write_cmd(0x60);
    ps2_write_data(status);

    mouse_write(0xF6);
    mouse_read_ack();

    mouse_write(0xF4);
    mouse_read_ack();

    g_cycle = 0;
    g_head = g_tail = g_count = 0;
    g_x = 0;
    g_y = 0;
    g_buttons = 0;
}

void mouse_handler(void) {
    uint8_t st = inb(PS2_STATUS);
    if ((st & PS2_STATUS_OUT_FULL) == 0) return;
    if ((st & PS2_STATUS_AUX_DATA) == 0) {
        (void)inb(PS2_DATA);
        return;
    }

    uint8_t data = inb(PS2_DATA);

    if (g_cycle == 0 && (data & 0x08) == 0) {
        return;
    }

    g_packet[g_cycle] = data;
    g_cycle = (uint8_t)((g_cycle + 1) % 3);

    if (g_cycle == 0) {
        uint8_t b0 = g_packet[0];
        uint8_t b1 = g_packet[1];
        uint8_t b2 = g_packet[2];

        if (b0 & 0xC0) return;

        mouse_packet_t p;
        p.buttons = b0 & 0x07;
        p.x_movement = (int8_t)b1;
        p.y_movement = (int8_t)b2;
        g_buttons = p.buttons;

        g_x += p.x_movement;
        g_y -= p.y_movement;

        if (vesa_is_initialized()) {
            int16_t max_x = (int16_t)(vesa_get_width() ? (vesa_get_width() - 1) : 0);
            int16_t max_y = (int16_t)(vesa_get_height() ? (vesa_get_height() - 1) : 0);
            if (g_x < 0) g_x = 0;
            if (g_y < 0) g_y = 0;
            if (g_x > max_x) g_x = max_x;
            if (g_y > max_y) g_y = max_y;
        }
        p.x = g_x;
        p.y = g_y;

        queue_push(p);
    }
}

int16_t mouse_get_x(void) {
    return g_x;
}

int16_t mouse_get_y(void) {
    return g_y;
}

uint8_t mouse_get_buttons(void) {
    return g_buttons;
}
