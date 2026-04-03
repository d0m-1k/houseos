#pragma once

#include <stdint.h>
#include <hgui/widgets/widget.h>

typedef struct {
    uint16_t item_height;
    uint16_t spacing;
    uint16_t padding;
} hq_vbox_spec_t;

void hq_layout_vbox_set(hq_widget_t *parent, hq_vbox_spec_t spec);
void hq_layout_apply(hq_widget_t *parent);
