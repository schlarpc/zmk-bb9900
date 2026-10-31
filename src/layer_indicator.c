/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

// Big button backlight as a layer indicator:
//   layer 0: off, layer 1: steady, layer 2: breathing, layer 3 and above: blinking.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LEDS_NODE DT_CHOSEN(zitaotech_layer_indicator)
#define CHILD_COUNT(...) +1
#define NUM_LEDS (DT_FOREACH_CHILD(LEDS_NODE, CHILD_COUNT))

#define BRT CONFIG_ZMK_BB9900_LAYER_INDICATOR_BRT
#define MAX_BRT CONFIG_ZMK_BB9900_LAYER_INDICATOR_CYCLE_MAX_BRT
#define MIN_BRT CONFIG_ZMK_BB9900_LAYER_INDICATOR_CYCLE_MIN_BRT
#define STEP CONFIG_ZMK_BB9900_LAYER_INDICATOR_CYCLE_STEP

static const struct device *const leds = DEVICE_DT_GET(LEDS_NODE);

enum indicator_mode { MODE_OFF, MODE_ON, MODE_BREATHE, MODE_BLINK };

static struct {
    enum indicator_mode mode;
    int breathe_brt;
    bool breathe_up;
    bool blink_high;
} state = {
    .mode = MODE_OFF,
    .breathe_brt = MIN_BRT,
    .breathe_up = true,
    .blink_high = true,
};

static struct k_work_delayable update_work;

static void set_brightness(uint8_t brt) {
    for (int i = 0; i < NUM_LEDS; i++) {
        int rc = led_set_brightness(leds, i, brt);
        if (rc != 0) {
            LOG_ERR("Failed to set layer indicator LED %d: %d", i, rc);
        }
    }
}

static void update_work_handler(struct k_work *work) {
    switch (state.mode) {
    case MODE_OFF:
        set_brightness(0);
        break;
    case MODE_ON:
        set_brightness(BRT);
        break;
    case MODE_BREATHE:
        if (state.breathe_brt >= MAX_BRT) {
            state.breathe_up = false;
        } else if (state.breathe_brt <= MIN_BRT) {
            state.breathe_up = true;
        }
        state.breathe_brt =
            CLAMP(state.breathe_brt + (state.breathe_up ? STEP : -STEP), MIN_BRT, MAX_BRT);
        set_brightness(state.breathe_brt);
        k_work_reschedule(&update_work, K_MSEC(CONFIG_ZMK_BB9900_LAYER_INDICATOR_CYCLE_INTERVAL));
        break;
    case MODE_BLINK:
        state.blink_high = !state.blink_high;
        set_brightness(state.blink_high ? MAX_BRT : MIN_BRT);
        k_work_reschedule(&update_work, K_MSEC(CONFIG_ZMK_BB9900_LAYER_INDICATOR_BLINK_INTERVAL));
        break;
    }
}

static enum indicator_mode mode_for_layer(uint8_t layer) {
    switch (layer) {
    case 0:
        return MODE_OFF;
    case 1:
        return MODE_ON;
    case 2:
        return MODE_BREATHE;
    default:
        return MODE_BLINK;
    }
}

static int layer_indicator_listener(const zmk_event_t *eh) {
    enum indicator_mode mode = mode_for_layer(zmk_keymap_highest_layer_active());

    if (mode != state.mode) {
        state.mode = mode;
        k_work_reschedule(&update_work, K_NO_WAIT);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bb9900_layer_indicator, layer_indicator_listener);
ZMK_SUBSCRIPTION(bb9900_layer_indicator, zmk_layer_state_changed);

static int layer_indicator_init(void) {
    if (!device_is_ready(leds)) {
        LOG_ERR("Layer indicator LED device %s is not ready", leds->name);
        return -ENODEV;
    }

    k_work_init_delayable(&update_work, update_work_handler);
    k_work_reschedule(&update_work, K_NO_WAIT);

    return 0;
}

SYS_INIT(layer_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
