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

/* Graph layout within the 68x42 canvas */
#define GRAPH_LEFT 2
#define GRAPH_RIGHT (CANVAS_SIZE_W - 3)
#define GRAPH_TOP 2
#define GRAPH_BOTTOM (CANVAS_SIZE_H - 14)
#define GRAPH_WIDTH (GRAPH_RIGHT - GRAPH_LEFT)
#define GRAPH_HEIGHT (GRAPH_BOTTOM - GRAPH_TOP)
#define TEXT_AREA_TOP (GRAPH_BOTTOM + 2)

static void draw_wpm_background(struct zmk_widget_status *widget) {
    lv_obj_t *canvas = widget->obj;

    lv_canvas_set_buffer(canvas, widget->bg_cbuf, CANVAS_SIZE_W, CANVAS_SIZE_H, LV_IMG_CF_TRUE_COLOR);

    lv_draw_rect_dsc_t rect_bg_dsc;
    init_rect_dsc(&rect_bg_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_border_dsc;
    init_rect_dsc(&rect_border_dsc, LVGL_FOREGROUND);

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);
    lv_canvas_draw_rect(canvas, GRAPH_LEFT - 1, GRAPH_TOP - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2,
                        &rect_border_dsc);
    lv_canvas_draw_rect(canvas, GRAPH_LEFT, GRAPH_TOP, GRAPH_WIDTH, GRAPH_HEIGHT, &rect_bg_dsc);

    lv_canvas_set_buffer(canvas, widget->cbuf, CANVAS_SIZE_W, CANVAS_SIZE_H, LV_IMG_CF_TRUE_COLOR);
    memcpy(widget->cbuf, widget->bg_cbuf, sizeof(widget->bg_cbuf));
}

static void draw_wpm_canvas(struct zmk_widget_status *widget) {
    lv_obj_t *canvas = widget->obj;
    const struct status_state *state = &widget->state;

    memcpy(widget->cbuf, widget->bg_cbuf, sizeof(widget->bg_cbuf));

    lv_draw_label_dsc_t label_dsc_wpm;
    init_label_dsc(&label_dsc_wpm, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_RIGHT);
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;
    line_dsc.rounded = 1;

    char wpm_text[6] = {};
    snprintf(wpm_text, sizeof(wpm_text), "%d", state->wpm[9]);
    const int text_box_width = 24;
    const int text_x = GRAPH_RIGHT - text_box_width + 2;
    lv_canvas_draw_text(canvas, text_x, TEXT_AREA_TOP, text_box_width, &label_dsc_wpm, wpm_text);

    int max = 0;
    int min = 256;

    for (int i = 0; i < 10; i++) {
        if (state->wpm[i] > max) {
            max = state->wpm[i];
        }
        if (state->wpm[i] < min) {
            min = state->wpm[i];
        }
    }

    int range = max - min;
    if (range == 0) {
        range = 1;
    }

    lv_point_t points[10];
    for (int i = 0; i < 10; i++) {
        points[i].x = GRAPH_LEFT + (GRAPH_WIDTH * i) / 9;
        int value = (state->wpm[i] - min) * (GRAPH_HEIGHT - 2) / range;
        points[i].y = GRAPH_BOTTOM - 1 - value;
    }
    lv_canvas_draw_line(canvas, points, 10, &line_dsc);
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
    widget->obj = lv_canvas_create(parent);
    lv_obj_set_size(widget->obj, CANVAS_SIZE_W, CANVAS_SIZE_H);
    lv_canvas_set_buffer(widget->obj, widget->cbuf, CANVAS_SIZE_W, CANVAS_SIZE_H, LV_IMG_CF_TRUE_COLOR);

    draw_wpm_background(widget);
    draw_wpm_canvas(widget);

    sys_slist_append(&widgets, &widget->node);
    widget_wpm_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
