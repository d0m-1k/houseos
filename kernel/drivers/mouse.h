#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t buttons;
    int16_t x_movement;
    int16_t y_movement;
    int16_t x;
    int16_t y;
} mouse_packet_t;

void mouse_init(void);
void mouse_handler(void);
bool mouse_available(void);
mouse_packet_t mouse_get_packet(void);
bool mouse_try_get_packet(mouse_packet_t* out);
