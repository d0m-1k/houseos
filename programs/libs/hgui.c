#include <hgui_api.h>

static void api_layout_begin(ui_layout_t *lay, ui_rect_t bounds, int padding, int row_h, int gap) {
    if (!lay) return;
    lay->outer = bounds;
    lay->cursor_x = bounds.x + padding;
    lay->cursor_y = bounds.y + padding;
    lay->gap = gap;
    lay->row_h = row_h;
}

static ui_rect_t api_layout_take_row(ui_layout_t *lay, int h) {
    ui_rect_t r = {0, 0, 0, 0};
    int row_h;
    if (!lay) return r;
    row_h = (h > 0) ? h : lay->row_h;
    r.x = lay->cursor_x;
    r.y = lay->cursor_y;
    r.w = lay->outer.w - (lay->cursor_x - lay->outer.x) * 2;
    r.h = row_h;
    lay->cursor_y += row_h + lay->gap;
    return r;
}

static ui_rect_t api_rect_inset(ui_rect_t r, int pad) {
    ui_rect_t out = r;
    out.x += pad;
    out.y += pad;
    out.w -= pad * 2;
    out.h -= pad * 2;
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

static void api_split_h(ui_rect_t src, int left_w, int gap, ui_rect_t *left, ui_rect_t *right) {
    if (left) {
        left->x = src.x;
        left->y = src.y;
        left->w = left_w;
        left->h = src.h;
        if (left->w < 0) left->w = 0;
    }
    if (right) {
        right->x = src.x + left_w + gap;
        right->y = src.y;
        right->w = src.w - left_w - gap;
        right->h = src.h;
        if (right->w < 0) right->w = 0;
    }
}

static void api_split_v(ui_rect_t src, int top_h, int gap, ui_rect_t *top, ui_rect_t *bottom) {
    if (top) {
        top->x = src.x;
        top->y = src.y;
        top->w = src.w;
        top->h = top_h;
        if (top->h < 0) top->h = 0;
    }
    if (bottom) {
        bottom->x = src.x;
        bottom->y = src.y + top_h + gap;
        bottom->w = src.w;
        bottom->h = src.h - top_h - gap;
        if (bottom->h < 0) bottom->h = 0;
    }
}

int hgui_get_api(uint32_t ver, hgui_api_t *out, uint32_t out_size) {
    uint32_t i;
    if (ver != HGUI_API_VERSION || !out || out_size < sizeof(hgui_api_t)) return -1;
    for (i = 0; i < sizeof(*out); i++) ((uint8_t*)out)[i] = 0u;
    out->layout_begin = api_layout_begin;
    out->layout_take_row = api_layout_take_row;
    out->rect_inset = api_rect_inset;
    out->split_h = api_split_h;
    out->split_v = api_split_v;
    return 0;
}
