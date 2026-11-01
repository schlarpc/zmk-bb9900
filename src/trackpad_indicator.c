/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

// Trackpad backlight, lit while the scroll mode layer is active. It turns off while the
// keyboard is idle, like the keyboard backlight.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LEDS_NODE DT_CHOSEN(zitaotech_trackpad_indicator)
#define CHILD_COUNT(...) +1
#define NUM_LEDS (DT_FOREACH_CHILD(LEDS_NODE, CHILD_COUNT))

static const struct device *const leds = DEVICE_DT_GET(LEDS_NODE);

static void set_brightness(uint8_t brt) {
    for (int i = 0; i < NUM_LEDS; i++) {
        int rc = led_set_brightness(leds, i, brt);
        if (rc != 0) {
            LOG_ERR("Failed to set trackpad indicator LED %d: %d", i, rc);
        }
    }
}

static int trackpad_indicator_listener(const zmk_event_t *eh) {
    if (!device_is_ready(leds)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool lit = zmk_keymap_layer_active(CONFIG_ZMK_BB9900_TRACKPAD_INDICATOR_LAYER) &&
               zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;

    set_brightness(lit ? CONFIG_ZMK_BB9900_TRACKPAD_INDICATOR_BRT : 0);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bb9900_trackpad_indicator, trackpad_indicator_listener);
ZMK_SUBSCRIPTION(bb9900_trackpad_indicator, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(bb9900_trackpad_indicator, zmk_activity_state_changed);

static int trackpad_indicator_init(void) {
    if (!device_is_ready(leds)) {
        LOG_ERR("Trackpad indicator LED device %s is not ready", leds->name);
        return -ENODEV;
    }

    set_brightness(0);
    return 0;
}

SYS_INIT(trackpad_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
