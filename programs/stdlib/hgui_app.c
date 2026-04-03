#include <string.h>
#include <hgui/app.h>

static int hq_object_default_event(hq_object_t *self, const hq_event_t *ev) {
    (void)self;
    (void)ev;
    return 0;
}

void hq_object_init(hq_object_t *obj, uint32_t id, uint32_t kind) {
    if (!obj) return;
    memset(obj, 0, sizeof(*obj));
    obj->id = id;
    obj->kind = kind;
    obj->on_event = hq_object_default_event;
}

void hq_object_attach(hq_object_t *parent, hq_object_t *child) {
    hq_object_t *it;
    if (!parent || !child) return;
    if (child->parent == parent) return;
    hq_object_detach(child);
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
        return;
    }
    it = parent->first_child;
    while (it->next_sibling) it = it->next_sibling;
    it->next_sibling = child;
}

void hq_object_detach(hq_object_t *child) {
    hq_object_t *prev;
    hq_object_t *it;
    hq_object_t *parent;
    if (!child || !child->parent) return;
    parent = child->parent;
    prev = 0;
    it = parent->first_child;
    while (it) {
        if (it == child) {
            if (prev) prev->next_sibling = it->next_sibling;
            else parent->first_child = it->next_sibling;
            child->parent = 0;
            child->next_sibling = 0;
            return;
        }
        prev = it;
        it = it->next_sibling;
    }
}

hq_object_t *hq_object_first_child(hq_object_t *obj) {
    if (!obj) return 0;
    return obj->first_child;
}

hq_object_t *hq_object_next_sibling(hq_object_t *obj) {
    if (!obj) return 0;
    return obj->next_sibling;
}

static int hq_widget_accepts(const hq_widget_t *w, const hq_event_t *ev) {
    if (!w || !ev) return 0;
    if (!w->visible || !w->enabled) return 0;
    if (ev->target_id == 0u) return 1;
    return ev->target_id == w->obj.id;
}

void hq_widget_init(hq_widget_t *w, uint32_t id) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    hq_object_init(&w->obj, id, 1u);
    w->visible = 1u;
    w->enabled = 1u;
}

void hq_widget_add_child(hq_widget_t *parent, hq_widget_t *child) {
    if (!parent || !child) return;
    hq_object_attach(&parent->obj, &child->obj);
}

void hq_widget_set_geometry(hq_widget_t *w, ui_rect_t rect) {
    if (!w) return;
    w->rect = rect;
}

void hq_widget_set_visible(hq_widget_t *w, int visible) {
    if (!w) return;
    w->visible = visible ? 1u : 0u;
}

void hq_widget_set_enabled(hq_widget_t *w, int enabled) {
    if (!w) return;
    w->enabled = enabled ? 1u : 0u;
}

void hq_widget_enable_vbox(hq_widget_t *w, uint16_t item_height, uint16_t spacing, uint16_t padding) {
    if (!w) return;
    w->use_vbox = 1u;
    w->vbox.item_height = item_height;
    w->vbox.spacing = spacing;
    w->vbox.padding = padding;
}

void hq_widget_apply_layout(hq_widget_t *w) {
    ui_layout_t lay;
    hq_object_t *child;
    ui_rect_t inner;
    if (!w || !w->use_vbox) return;
    inner = w->rect;
    ui_layout_begin(&lay, inner, (int)w->vbox.padding, (int)w->vbox.item_height, (int)w->vbox.spacing);
    child = hq_object_first_child(&w->obj);
    while (child) {
        hq_widget_t *cw = (hq_widget_t*)child;
        cw->rect = ui_layout_take_row(&lay, (int)w->vbox.item_height);
        child = hq_object_next_sibling(child);
    }
}

int hq_widget_dispatch(hq_widget_t *w, const hq_event_t *ev) {
    hq_object_t *child;
    int handled = 0;
    if (!hq_widget_accepts(w, ev)) return 0;

    if (ev->type == HQ_EVENT_PAINT && w->use_vbox) hq_widget_apply_layout(w);
    if (w->on_widget_event) handled |= w->on_widget_event(w, ev);
    if (!handled && w->obj.on_event) handled |= w->obj.on_event(&w->obj, ev);
    if (!handled && ev->type == HQ_EVENT_PAINT && w->on_paint) {
        w->on_paint(w, ev);
        handled = 1;
    }

    child = hq_object_first_child(&w->obj);
    while (child) {
        handled |= hq_widget_dispatch((hq_widget_t*)child, ev);
        child = hq_object_next_sibling(child);
    }
    return handled;
}

void hq_layout_vbox_set(hq_widget_t *parent, hq_vbox_spec_t spec) {
    if (!parent) return;
    hq_widget_enable_vbox(parent, spec.item_height, spec.spacing, spec.padding);
}

void hq_layout_apply(hq_widget_t *parent) {
    hq_widget_apply_layout(parent);
}

static int hq_app_queue_empty(const hq_application_t *app) {
    return app->q_head == app->q_tail;
}

static int hq_app_queue_pop(hq_application_t *app, hq_event_t *ev) {
    if (!app || !ev) return 0;
    if (hq_app_queue_empty(app)) return 0;
    *ev = app->queue[app->q_head];
    app->q_head = (app->q_head + 1u) % HQ_EVENT_QUEUE_CAP;
    return 1;
}

void hq_app_init(hq_application_t *app, hq_widget_t *root) {
    if (!app) return;
    memset(app, 0, sizeof(*app));
    app->root = root;
    app->running = 1u;
    app->process_paint_on_tick = 1u;
}

int hq_app_post_event(hq_application_t *app, const hq_event_t *ev) {
    uint32_t next;
    if (!app || !ev) return -1;
    next = (app->q_tail + 1u) % HQ_EVENT_QUEUE_CAP;
    if (next == app->q_head) return -1;
    app->queue[app->q_tail] = *ev;
    app->q_tail = next;
    return 0;
}

void hq_app_quit(hq_application_t *app) {
    if (!app) return;
    app->running = 0u;
}

int hq_app_process_once(hq_application_t *app) {
    hq_event_t ev;
    if (!app || !app->root) return -1;
    if (!hq_app_queue_pop(app, &ev)) {
        ev.type = HQ_EVENT_TICK;
        ev.target_id = 0u;
        ev.arg0 = app->tick_counter++;
        ev.arg1 = 0u;
        ev.arg2 = 0u;
        ev.arg3 = 0u;
    }
    if (ev.type == HQ_EVENT_QUIT) {
        hq_app_quit(app);
        return 0;
    }
    (void)hq_widget_dispatch(app->root, &ev);
    if (app->process_paint_on_tick && ev.type == HQ_EVENT_TICK) {
        hq_event_t paint_ev;
        paint_ev.type = HQ_EVENT_PAINT;
        paint_ev.target_id = 0u;
        paint_ev.arg0 = 0u;
        paint_ev.arg1 = 0u;
        paint_ev.arg2 = 0u;
        paint_ev.arg3 = 0u;
        (void)hq_widget_dispatch(app->root, &paint_ev);
    }
    return 0;
}

int hq_app_exec(hq_application_t *app, uint32_t max_ticks) {
    uint32_t ticks = 0;
    if (!app) return -1;
    while (app->running) {
        if (hq_app_process_once(app) != 0) return -1;
        ticks++;
        if (max_ticks > 0u && ticks >= max_ticks) break;
    }
    return 0;
}
