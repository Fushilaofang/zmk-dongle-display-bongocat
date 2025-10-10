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
#include <zmk/event_manager.h>
#include <zmk/usb.h>
#include <stdint.h>
#include <zmk/events/endpoint_changed.h>
#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/events/ble_active_profile_changed.h>
#endif

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

/* Cache of last-known battery levels per source. UINT8_MAX = unknown */
static uint8_t last_levels[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];

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

    if (state.level > 0 || state.usb_present) {
        draw_battery(symbol, state.level, state.usb_present);
        lv_label_set_text_fmt(label, "%4u%%", state.level);
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(symbol);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    } else {
        /* 无电量数据：根据外围设备连接状态显示占位文本或 NC */
        if (state.source >= SOURCE_OFFSET) {
            int per_idx = state.source - SOURCE_OFFSET;
            bool connected = false;
#if defined(CONFIG_ZMK_BLE)
            if (per_idx >= 0 && per_idx < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
                connected = zmk_ble_profile_is_connected(per_idx);
            }
#endif
            if (connected) {
                /* 外围设备已连接但未上报电量 */
                lv_label_set_text(label, "  --");
            } else {
                /* 外围设备未连接 */
                lv_label_set_text(label, "  NC");
            }
        } else {
            /* 中央/本机无电量上报，显示占位 */
            lv_label_set_text(label, "  --");
        }

        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    }
}

void battery_status_update_cb(struct battery_state state) {
    /* Update cache for this source */
    if (state.source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET) {
        last_levels[state.source] = state.level;
    }

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
    }
    /* Initialize cache to unknown */
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        last_levels[i] = UINT8_MAX;
    }
    sys_slist_append(&widgets, &widget->node);

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}

/* Refresh listener: triggered when endpoints or BLE active profile change. We
 * rebuild a battery_state for each source from cached values and refresh the
 * UI so connected/disconnected state is reflected even if no battery event
 * was emitted.
 */
struct battery_refresh_state {};

static void battery_refresh_update_cb(struct battery_refresh_state _st) {
    struct zmk_widget_dongle_battery_status *widget;

    for (int src = 0; src < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; src++) {
        struct battery_state s = {
            .source = src,
            .level = (last_levels[src] == UINT8_MAX) ? 0 : last_levels[src],
            .usb_present = (src == 0) ? zmk_usb_is_powered() : false,
        };

        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_symbol(widget->obj, s); }
    }
}

static struct battery_refresh_state battery_refresh_get_state(const zmk_event_t *eh) { return (struct battery_refresh_state){}; }

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status_refresh, struct battery_refresh_state,
                            battery_refresh_update_cb, battery_refresh_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status_refresh, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_dongle_battery_status_refresh, zmk_ble_active_profile_changed);
#endif
