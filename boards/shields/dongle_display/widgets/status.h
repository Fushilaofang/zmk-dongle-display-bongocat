/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *chart_bg;
    lv_obj_t *wpm_label;
    /* 保留画布缓冲区以兼容性 */
    lv_color_t cbuf[CANVAS_SIZE_W * CANVAS_SIZE_H];
    lv_color_t bg_cbuf[CANVAS_SIZE_W * CANVAS_SIZE_H];
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);
