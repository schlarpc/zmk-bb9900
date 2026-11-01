/*
 * Copyright (c) 2026 The zmk-bb9900 contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Per-output smooth scrolling. ZMK enables high-resolution scrolling globally, and it starts
// every connection assuming the host supports it, so hosts that never do (iOS, macOS) scroll
// about 16x too fast. This behavior toggles whole-notch scrolling for the currently selected
// output (USB or one BLE profile) and saves the choice; the scroll curve processor checks it.

#define DT_DRV_COMPAT zitaotech_behavior_scroll_resolution

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>

#include "scroll_resolution.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(ZMK_ENDPOINT_COUNT <= 32, "One bit per endpoint");

// Bit set: that endpoint gets whole notches.
static uint32_t notched_endpoints;

static int current_endpoint_index(void) {
    return zmk_endpoint_instance_to_index(zmk_endpoint_get_selected());
}

bool bb9900_scroll_notched(void) { return notched_endpoints & BIT(current_endpoint_index()); }

#if IS_ENABLED(CONFIG_SETTINGS)
static int scroll_resolution_settings_load_cb(const char *name, size_t len,
                                              settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "notched", &next) && !next) {
        if (len != sizeof(notched_endpoints)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, &notched_endpoints, sizeof(notched_endpoints));
        return MIN(rc, 0);
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(bb9900_scroll, "bb9900_scroll", NULL,
                               scroll_resolution_settings_load_cb, NULL, NULL);

static void save_work_handler(struct k_work *work) {
    settings_save_one("bb9900_scroll/notched", &notched_endpoints, sizeof(notched_endpoints));
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_handler);
#endif

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    int index = current_endpoint_index();

    notched_endpoints ^= BIT(index);
    LOG_INF("Scrolling for endpoint %d is now %s", index,
            (notched_endpoints & BIT(index)) ? "notched" : "smooth");

#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_scroll_resolution_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_scroll_resolution_driver_api);
