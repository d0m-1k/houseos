#pragma once

#include <stdint.h>
#include <ui_sdk.h>
#include <hgui/core/object.h>

struct hq_widget;
typedef struct hq_widget hq_widget_t;

typedef void (*hq_widget_paint_fn)(hq_widget_t *self, const hq_event_t *ev);
typedef int (*hq_widget_event_fn)(hq_widget_t *self, const hq_event_t *ev);

typedef struct {
    uint16_t item_height;
    uint16_t spacing;
    uint16_t padding;
} hq_vbox_layout_t;

struct hq_widget {
    hq_object_t obj;
    ui_rect_t rect;
    uint8_t visible;
    uint8_t enabled;
    hq_widget_paint_fn on_paint;
    hq_widget_event_fn on_widget_event;
    hq_vbox_layout_t vbox;
    uint8_t use_vbox;
};

void hq_widget_init(hq_widget_t *w, uint32_t id);
void hq_widget_add_child(hq_widget_t *parent, hq_widget_t *child);
void hq_widget_set_geometry(hq_widget_t *w, ui_rect_t rect);
void hq_widget_set_visible(hq_widget_t *w, int visible);
void hq_widget_set_enabled(hq_widget_t *w, int enabled);
void hq_widget_enable_vbox(hq_widget_t *w, uint16_t item_height, uint16_t spacing, uint16_t padding);
void hq_widget_apply_layout(hq_widget_t *w);
int hq_widget_dispatch(hq_widget_t *w, const hq_event_t *ev);
