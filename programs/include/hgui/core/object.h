#pragma once

#include <stdint.h>
#include <hgui/core/event.h>

struct hq_object;
typedef struct hq_object hq_object_t;

typedef int (*hq_object_event_fn)(hq_object_t *self, const hq_event_t *ev);

struct hq_object {
    uint32_t id;
    uint32_t kind;
    hq_object_t *parent;
    hq_object_t *first_child;
    hq_object_t *next_sibling;
    hq_object_event_fn on_event;
    void *userdata;
};

void hq_object_init(hq_object_t *obj, uint32_t id, uint32_t kind);
void hq_object_attach(hq_object_t *parent, hq_object_t *child);
void hq_object_detach(hq_object_t *child);
hq_object_t *hq_object_first_child(hq_object_t *obj);
hq_object_t *hq_object_next_sibling(hq_object_t *obj);
