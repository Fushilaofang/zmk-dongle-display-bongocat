/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct wpm_status_state {
    uint8_t wpm;
};

static void draw_wpm_background(struct zmk_widget_status *widget) {
    lv_obj_t *container = widget->obj;

    // Create the chart background area
    if (!widget->chart_bg) {
        widget->chart_bg = lv_obj_create(container);
        lv_obj_set_size(widget->chart_bg, 68, 42);
        lv_obj_set_pos(widget->chart_bg, 0, 21);
        lv_obj_set_style_bg_color(widget->chart_bg, LVGL_BACKGROUND, 0);
        lv_obj_set_style_border_color(widget->chart_bg, LVGL_FOREGROUND, 0);
        lv_obj_set_style_border_width(widget->chart_bg, 1, 0);
        lv_obj_clear_flag(widget->chart_bg, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void draw_wpm_canvas(struct zmk_widget_status *widget) {
    lv_obj_t *container = widget->obj;
    const struct status_state *state = &widget->state;

    char wpm_text[6] = {};
    snprintf(wpm_text, sizeof(wpm_text), "%d", state->wpm[9]);
    
    // Create or update WPM label
    if (!widget->wpm_label) {
        widget->wpm_label = lv_label_create(container);
        lv_obj_set_style_text_color(widget->wpm_label, LVGL_FOREGROUND, 0);
        lv_obj_set_style_text_font(widget->wpm_label, &lv_font_unscii_8, 0);
        lv_obj_set_pos(widget->wpm_label, 42, 52);
        lv_obj_set_size(widget->wpm_label, 24, 8);
    }
    lv_label_set_text(widget->wpm_label, wpm_text);

    // For now, skip the complex line chart drawing since it requires canvas
    // TODO: Implement line chart using LVGL chart widget or simple line objects
}

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    for (int i = 0; i < 9; i++) {
        widget->state.wpm[i] = widget->state.wpm[i + 1];
    }
    widget->state.wpm[9] = state.wpm;

    draw_wpm_canvas(widget);
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_wpm_status(widget, state); }
}

struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    return (struct wpm_status_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                            wpm_status_get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    // Create a simple container instead of canvas
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, CANVAS_SIZE_W, CANVAS_SIZE_H);
    lv_obj_set_style_bg_color(widget->obj, LVGL_BACKGROUND, 0);
    lv_obj_set_style_border_color(widget->obj, LVGL_FOREGROUND, 0);
    lv_obj_set_style_border_width(widget->obj, 1, 0);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    // Initialize buffer for compatibility
    memset(widget->cbuf, 0, sizeof(widget->cbuf));
    memset(widget->bg_cbuf, 0, sizeof(widget->bg_cbuf));
    
    // Initialize object pointers
    widget->chart_bg = NULL;
    widget->wpm_label = NULL;
    
    // Initialize WPM state
    for (int i = 0; i < 10; i++) {
        widget->state.wpm[i] = 0;
    }

    draw_wpm_background(widget);
    draw_wpm_canvas(widget);

    sys_slist_append(&widgets, &widget->node);
    widget_wpm_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }