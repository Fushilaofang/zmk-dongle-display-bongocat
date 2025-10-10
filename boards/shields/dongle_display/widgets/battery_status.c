/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#  include <zmk/events/ble_active_profile_changed.h>
#endif
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "battery_status.h"

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
    #define SOURCE_OFFSET 1
#else
    #define SOURCE_OFFSET 0
#endif

#ifndef ZMK_SPLIT_BLE_PERIPHERAL_COUNT
#  define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 0
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_state {
    uint8_t source;
    uint8_t level;
    bool usb_present;
};

struct battery_object {
    lv_obj_t *symbol;
    lv_obj_t *label;
} battery_objects[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];
    
static lv_color_t battery_image_buffer[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET][5 * 8];

/* 状态跟踪：记录每个 source 的最后更新时间、是否有数据以及连接状态 */
static uint32_t battery_last_update_ms[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];
static bool battery_has_data[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];
/* 由事件驱动的连接状态（true = 已连接），索引与 battery_objects 对应 */
static bool battery_connected[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];

static void draw_battery(lv_obj_t *canvas, uint8_t level, bool usb_present) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);

    if (usb_present) {
        rect_fill_dsc.bg_opa = LV_OPA_TRANSP;
        rect_fill_dsc.border_color = lv_color_white();
        rect_fill_dsc.border_width = 1;
    }

    lv_canvas_set_px(canvas, 0, 0, lv_color_white());
    lv_canvas_set_px(canvas, 4, 0, lv_color_white());

    if (level <= 10 || usb_present) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 5, &rect_fill_dsc);
    } else if (level <= 30) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 4, &rect_fill_dsc);
    } else if (level <= 50) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 3, &rect_fill_dsc);
    } else if (level <= 70) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 2, &rect_fill_dsc);
    } else if (level <= 90) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 1, &rect_fill_dsc);
    }
}

static void set_battery_symbol(lv_obj_t *widget, struct battery_state state) {
    if (state.source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET) {
        return;
    }
    LOG_DBG("source: %d, level: %d, usb: %d", state.source, state.level, state.usb_present);
    lv_obj_t *symbol = battery_objects[state.source].symbol;
    lv_obj_t *label = battery_objects[state.source].label;
    int idx = state.source;
    /* 记录更新时间并标记有数据（但不用于断开判定） */
    battery_last_update_ms[idx] = k_uptime_get_32();
    battery_has_data[idx] = true;

    /* 根据连接状态与是否有数据选择显示 */
    if (!battery_connected[idx]) {
        /* 未连接：显示 NC */
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label, "  NC");
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
        return;
    }

    if (state.level > 0 || state.usb_present) {
        draw_battery(symbol, state.level, state.usb_present);
        lv_label_set_text_fmt(label, "%4u%%", state.level);
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(symbol);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    } else {
        /* 已连接但无电池数据：显示占位 "--" */
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label, "  --");
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    }
}

/* 更新某个 source 的连接状态并刷新显示 */
static void update_connection_state_for_source(int source, bool connected) {
    if (source < 0 || source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET) {
        return;
    }
    battery_connected[source] = connected;

    /* 根据当前是否有数据和连接状态更新 UI */
    lv_obj_t *symbol = battery_objects[source].symbol;
    lv_obj_t *label = battery_objects[source].label;

    if (!connected) {
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label, " NC");
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
        battery_has_data[source] = false;
        return;
    }

    /* connected == true */
    if (battery_has_data[source]) {
        /* 保持现有显示（电量事件会更新具体数值），这里仅确保标签/图标可见 */
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    } else {
        /* 已连接但尚无数据：显示占位 */
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label, "  --");
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    }
}

void battery_status_update_cb(struct battery_state state) {
    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_symbol(widget->obj, state); }
}

static struct battery_state peripheral_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    return (struct battery_state){
        .source = ev->source + SOURCE_OFFSET,
        .level = ev->state_of_charge,
    };
}

/* 处理 endpoint/connection 类型事件以更新 battery_connected 数组
 * 注意：事件结构来自 ZMK core，仓库内未包含其定义，这里假设
 * endpoint_changed 或 ble_active_profile_changed 可用于判断连接。
 * 如果有更精确的 peripheral connect/disconnect 事件，应替换为该事件。
 */
static void connection_status_event_cb(const zmk_event_t *eh) {
    /* 尝试识别 endpoint_changed 或 ble_active_profile_changed
     * 对于中央/本机电池（source == 0），使用 zmk_battery/usb 事件即可。
     * 对于外设（source >= SOURCE_OFFSET），如果有明确的外围设备连接事件，
     * 应从事件中提取对应的 source 索引并调用 update_connection_state_for_source。
     * 这里我们做最小处理：当 endpoint_changed 表示 transport 为 BLE 时，
     * 我们将所有 peripheral source 标记为已连接；当 transport 为 USB，标记为未连接。
     * 这 is a heuristic fallback;理想的实现需使用具体的 peripheral connect event.
     */
    const struct zmk_endpoint_changed *ep = as_zmk_endpoint_changed(eh);
    if (ep != NULL) {
        if (ep->selected_endpoint.transport == ZMK_TRANSPORT_BLE) {
            /* 将所有 peripheral source 标记为已连接 */
            for (int i = SOURCE_OFFSET; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; ++i) {
                update_connection_state_for_source(i, true);
            }
        } else {
            for (int i = SOURCE_OFFSET; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; ++i) {
                update_connection_state_for_source(i, false);
            }
        }
        return;
    }

#if IS_ENABLED(CONFIG_ZMK_BLE)
    const struct zmk_ble_active_profile_changed *bp = as_zmk_ble_active_profile_changed(eh);
    if (bp != NULL) {
        /* bp->connected 可以指示 active profile 是否连接；作为启发式处理，
         * 我们将 peripheral source[1..] 标记为 bp->connected（如果外设索引存在）
         */
        for (int i = SOURCE_OFFSET; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; ++i) {
            update_connection_state_for_source(i, bp->connected);
        }
        return;
    }
#endif

static struct battery_state central_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_state) {
        .source = 0,
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh) { 
    if (as_zmk_peripheral_battery_state_changed(eh) != NULL) {
        return peripheral_battery_status_get_state(eh);
    } else {
        return central_battery_status_get_state(eh);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct battery_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_peripheral_battery_state_changed);

/* 订阅连接/端点相关事件以获得外围设备连接状态更新 */
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_ble_active_profile_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
#endif /* !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
#endif /* IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY) */

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[i], 5, 8, LV_IMG_CF_TRUE_COLOR);

        lv_obj_align(image_canvas, LV_ALIGN_TOP_RIGHT, 0, i * 10);
        lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -7, i * 10);

        lv_obj_add_flag(image_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        
        battery_objects[i] = (struct battery_object){
            .symbol = image_canvas,
            .label = battery_label,
        };
        battery_has_data[i] = false;
        battery_last_update_ms[i] = 0;
        battery_connected[i] = false;
    }

    sys_slist_append(&widgets, &widget->node);

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
