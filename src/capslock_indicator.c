/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

// Trackpad backlight as a caps lock indicator, driven by the host's HID LED report.

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/hid_indicators.h>
#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LEDS_NODE DT_CHOSEN(zitaotech_capslock_indicator)
#define CHILD_COUNT(...) +1
#define NUM_LEDS (DT_FOREACH_CHILD(LEDS_NODE, CHILD_COUNT))

static const struct device *const leds = DEVICE_DT_GET(LEDS_NODE);

static void set_brightness(uint8_t brt) {
    for (int i = 0; i < NUM_LEDS; i++) {
        int rc = led_set_brightness(leds, i, brt);
        if (rc != 0) {
            LOG_ERR("Failed to set caps lock indicator LED %d: %d", i, rc);
        }
    }
}

static int capslock_indicator_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);

    if (ev == NULL || !device_is_ready(leds)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    set_brightness((ev->indicators & HID_INDICATOR_CAPS_LOCK)
                       ? CONFIG_ZMK_BB9900_CAPSLOCK_INDICATOR_BRT
                       : 0);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bb9900_capslock_indicator, capslock_indicator_listener);
ZMK_SUBSCRIPTION(bb9900_capslock_indicator, zmk_hid_indicators_changed);

static int capslock_indicator_init(void) {
    if (!device_is_ready(leds)) {
        LOG_ERR("Caps lock indicator LED device %s is not ready", leds->name);
        return -ENODEV;
    }

    set_brightness(0);
    return 0;
}

SYS_INIT(capslock_indicator_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
