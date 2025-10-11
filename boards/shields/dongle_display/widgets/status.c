/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

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

static void draw_wpm_canvas(lv_obj_t *canvas_obj, const struct status_state *state) {
    lv_obj_t *canvas = canvas_obj;

    lv_draw_label_dsc_t label_dsc_wpm;
    init_label_dsc(&label_dsc_wpm, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);

    const int border = 1;
    const int graph_margin = 2;
    const int box_height = CANVAS_SIZE_H / 2;
    const int box_top = CANVAS_SIZE_H - box_height;
    int graph_bottom = box_top - border - 1;
    int graph_top = graph_margin;

    if (graph_bottom <= graph_top) {
        graph_bottom = CANVAS_SIZE_H - border - 1;
        graph_top = graph_margin;
    }

    int graph_height = graph_bottom - graph_top;
    if (graph_height < 1) {
        graph_height = 1;
    }

    /* Fill background */
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE_W, CANVAS_SIZE_H, &rect_black_dsc);

    /* Draw graph background */
    lv_canvas_draw_rect(canvas, border, graph_top - border, CANVAS_SIZE_W - 2 * border,
                        graph_bottom - graph_top + 2 * border, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, border + 1, graph_top, CANVAS_SIZE_W - 2 * (border + 1),
                        graph_bottom - graph_top, &rect_black_dsc);

    /* Draw WPM box and value */
    lv_canvas_draw_rect(canvas, 0, box_top, CANVAS_SIZE_W, box_height, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, border, box_top + border, CANVAS_SIZE_W - 2 * border,
                        box_height - 2 * border, &rect_black_dsc);

    char wpm_text[6] = {};
    snprintf(wpm_text, sizeof(wpm_text), "%d", state->wpm[9]);

    int text_width = CANVAS_SIZE_W - 2 * border - 4;
    if (text_width < 0) {
        text_width = CANVAS_SIZE_W;
    }

    int text_height = lv_font_get_line_height(label_dsc_wpm.font);
    int text_y = box_top + (box_height - text_height) / 2;
    if (text_y < box_top) {
        text_y = box_top;
    }

    lv_canvas_draw_text(canvas, CANVAS_SIZE_W - border - 2, text_y, text_width, &label_dsc_wpm,
                        wpm_text);

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

    int graph_width = CANVAS_SIZE_W - 2 * graph_margin;
    if (graph_width < 1) {
        graph_width = 1;
    }
    int step_x = graph_width / 9;
    if (step_x < 1) {
        step_x = 1;
    }

    lv_point_t points[10];
    for (int i = 0; i < 10; i++) {
        points[i].x = graph_margin + i * step_x;
        points[i].y = graph_bottom - (state->wpm[i] - min) * graph_height / range;
    }
    lv_canvas_draw_line(canvas, points, 10, &line_dsc);
}

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    for (int i = 0; i < 9; i++) {
        widget->state.wpm[i] = widget->state.wpm[i + 1];
    }
    widget->state.wpm[9] = state.wpm;

    draw_wpm_canvas(widget->obj, &widget->state);
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

    sys_slist_append(&widgets, &widget->node);
    widget_wpm_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
