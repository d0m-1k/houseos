#pragma once

#include <stdint.h>

typedef enum {
    HQ_EVENT_NONE = 0,
    HQ_EVENT_TICK = 1,
    HQ_EVENT_PAINT = 2,
    HQ_EVENT_KEY = 3,
    HQ_EVENT_MOUSE_MOVE = 4,
    HQ_EVENT_MOUSE_BUTTON = 5,
    HQ_EVENT_QUIT = 6
} hq_event_type_t;

typedef struct {
    hq_event_type_t type;
    uint32_t target_id;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
} hq_event_t;
