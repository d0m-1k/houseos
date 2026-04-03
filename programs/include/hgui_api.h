#pragma once

#include <ui_sdk.h>
#include <stdint.h>

typedef struct {
    void (*layout_begin)(ui_layout_t *lay, ui_rect_t bounds, int padding, int row_h, int gap);
    ui_rect_t (*layout_take_row)(ui_layout_t *lay, int h);
    ui_rect_t (*rect_inset)(ui_rect_t r, int pad);
    void (*split_h)(ui_rect_t src, int left_w, int gap, ui_rect_t *left, ui_rect_t *right);
    void (*split_v)(ui_rect_t src, int top_h, int gap, ui_rect_t *top, ui_rect_t *bottom);
} hgui_api_t;

#define HGUI_API_VERSION 1u
int hgui_get_api(uint32_t ver, hgui_api_t *out, uint32_t out_size);
