/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>

#include "bongo_cat.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

// Animation state tracking
struct anim_data {
    const lv_img_dsc_t **frames;
    uint8_t frame_count;
    uint8_t current_frame;
    uint32_t duration_ms;
    lv_timer_t *timer;
};


LV_IMG_DECLARE(bongo_cat_none);
LV_IMG_DECLARE(bongo_cat_left1);
LV_IMG_DECLARE(bongo_cat_left2);
LV_IMG_DECLARE(bongo_cat_right1);
LV_IMG_DECLARE(bongo_cat_right2);
LV_IMG_DECLARE(bongo_cat_both1);
LV_IMG_DECLARE(bongo_cat_both1_open);
LV_IMG_DECLARE(bongo_cat_both2);

#define ANIMATION_SPEED_IDLE 10000
static const lv_img_dsc_t *idle_imgs[] = {
    &bongo_cat_both1_open,
    &bongo_cat_both1_open,
    &bongo_cat_both1_open,
    &bongo_cat_both1,
};

#define ANIMATION_SPEED_SLOW 2000
static const lv_img_dsc_t *slow_imgs[] = {
    &bongo_cat_left1,
    &bongo_cat_both1,
    &bongo_cat_both1,
    &bongo_cat_right1,
    &bongo_cat_both1,
    &bongo_cat_both1,
    &bongo_cat_left1,
    &bongo_cat_both1,
    &bongo_cat_both1,
};

#define ANIMATION_SPEED_MID 500
static const lv_img_dsc_t *mid_imgs[] = {
    &bongo_cat_left2,
    &bongo_cat_left1,
    &bongo_cat_none,
    &bongo_cat_right2,
    &bongo_cat_right1,
    &bongo_cat_none,
};

#define ANIMATION_SPEED_FAST 200
static const lv_img_dsc_t *fast_imgs[] = {
    &bongo_cat_both2,
    &bongo_cat_both1,
    &bongo_cat_none,
    &bongo_cat_none,
};

struct bongo_cat_wpm_status_state {
    uint8_t wpm;
};

enum anim_state {
    anim_state_none,
    anim_state_idle,
    anim_state_slow,
    anim_state_mid,
    anim_state_fast
} current_anim_state;

static struct anim_data current_anim;

// Timer callback to update animation frame
static void animation_timer_cb(lv_timer_t *timer) {
    lv_obj_t *img = timer->user_data;
    
    if (current_anim.frames == NULL || current_anim.frame_count == 0) {
        return;
    }
    
    // Update to next frame
    current_anim.current_frame = (current_anim.current_frame + 1) % current_anim.frame_count;
    lv_image_set_src(img, current_anim.frames[current_anim.current_frame]);
}

static void start_animation(lv_obj_t *img, const lv_img_dsc_t **frames, uint8_t count, uint32_t duration_ms) {
    // Stop existing timer if any
    if (current_anim.timer != NULL) {
        lv_timer_del(current_anim.timer);
        current_anim.timer = NULL;
    }
    
    // Setup new animation
    current_anim.frames = frames;
    current_anim.frame_count = count;
    current_anim.current_frame = 0;
    current_anim.duration_ms = duration_ms;
    
    // Set initial frame
    lv_image_set_src(img, frames[0]);
    
    // Create timer for frame updates
    current_anim.timer = lv_timer_create(animation_timer_cb, duration_ms / count, img);
}

static void set_animation(lv_obj_t *animing, struct bongo_cat_wpm_status_state state) {
    if (state.wpm < 5) {
        if (current_anim_state != anim_state_idle) {
            start_animation(animing, idle_imgs, sizeof(idle_imgs) / sizeof(lv_img_dsc_t *), ANIMATION_SPEED_IDLE);
            current_anim_state = anim_state_idle;
        }
    } else if (state.wpm < 30) {
        if (current_anim_state != anim_state_slow) {
            start_animation(animing, slow_imgs, sizeof(slow_imgs) / sizeof(lv_img_dsc_t *), ANIMATION_SPEED_SLOW);
            current_anim_state = anim_state_slow;
        }
    } else if (state.wpm < 70) {
        if (current_anim_state != anim_state_mid) {
            start_animation(animing, mid_imgs, sizeof(mid_imgs) / sizeof(lv_img_dsc_t *), ANIMATION_SPEED_MID);
            current_anim_state = anim_state_mid;
        }
    } else {
        if (current_anim_state != anim_state_fast) {
            start_animation(animing, fast_imgs, sizeof(fast_imgs) / sizeof(lv_img_dsc_t *), ANIMATION_SPEED_FAST);
            current_anim_state = anim_state_fast;
        }
    }
}

struct bongo_cat_wpm_status_state bongo_cat_wpm_status_get_state(const zmk_event_t *eh) {
    struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    return (struct bongo_cat_wpm_status_state) { .wpm = ev->state };
};

void bongo_cat_wpm_status_update_cb(struct bongo_cat_wpm_status_state state) {
    struct zmk_widget_bongo_cat *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_animation(widget->obj, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_bongo_cat, struct bongo_cat_wpm_status_state,
                            bongo_cat_wpm_status_update_cb, bongo_cat_wpm_status_get_state)

ZMK_SUBSCRIPTION(widget_bongo_cat, zmk_wpm_state_changed);

int zmk_widget_bongo_cat_init(struct zmk_widget_bongo_cat *widget, lv_obj_t *parent) {
    widget->obj = lv_image_create(parent);
    lv_obj_center(widget->obj);
    
    // Initialize animation data
    current_anim.frames = NULL;
    current_anim.frame_count = 0;
    current_anim.current_frame = 0;
    current_anim.duration_ms = 0;
    current_anim.timer = NULL;

    sys_slist_append(&widgets, &widget->node);

    widget_bongo_cat_init();

    return 0;
}

lv_obj_t *zmk_widget_bongo_cat_obj(struct zmk_widget_bongo_cat *widget) {
    return widget->obj;
}