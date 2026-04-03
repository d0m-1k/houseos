#pragma once

#include <stdint.h>

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;

typedef struct {
    ui_rect_t outer;
    int cursor_x;
    int cursor_y;
    int gap;
    int row_h;
} ui_layout_t;

void ui_layout_begin(ui_layout_t *lay, ui_rect_t bounds, int padding, int row_h, int gap);
ui_rect_t ui_layout_take_row(ui_layout_t *lay, int h);
ui_rect_t ui_rect_inset(ui_rect_t r, int pad);
void ui_split_h(ui_rect_t src, int left_w, int gap, ui_rect_t *left, ui_rect_t *right);
void ui_split_v(ui_rect_t src, int top_h, int gap, ui_rect_t *top, ui_rect_t *bottom);

