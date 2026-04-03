#pragma once

#include <stdint.h>
#include <hgui/core/event.h>
#include <hgui/widgets/widget.h>

#define HQ_EVENT_QUEUE_CAP 64u

typedef struct {
    hq_widget_t *root;
    uint8_t running;
    uint8_t process_paint_on_tick;
    uint32_t tick_counter;
    hq_event_t queue[HQ_EVENT_QUEUE_CAP];
    uint32_t q_head;
    uint32_t q_tail;
} hq_application_t;

void hq_app_init(hq_application_t *app, hq_widget_t *root);
int hq_app_post_event(hq_application_t *app, const hq_event_t *ev);
int hq_app_process_once(hq_application_t *app);
int hq_app_exec(hq_application_t *app, uint32_t max_ticks);
void hq_app_quit(hq_application_t *app);
