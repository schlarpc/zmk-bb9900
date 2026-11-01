/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zitaotech_input_processor_scroll_curve

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Gains are in 1/16 of a high-resolution unit, and 16 high-resolution units make one notch.
#define GAIN_SCALE 16
#define HI_RES_PER_NOTCH 16

struct scroll_curve_config {
    uint16_t knee;
    uint16_t low_gain;
    uint16_t high_gain;
};

struct scroll_curve_data {
    // Remainders per axis, in output units scaled by GAIN_SCALE.
    int32_t remainder[2];
};

static int32_t apply_curve(const struct scroll_curve_config *cfg, int32_t v) {
    int32_t mag = abs(v);
    int32_t low = MIN(mag, cfg->knee);
    int32_t high = mag - low;
    int32_t scaled = low * cfg->low_gain + high * cfg->high_gain;

    return v < 0 ? -scaled : scaled;
}

static int scroll_curve_handle_event(const struct device *dev, struct input_event *event,
                                     uint32_t param1, uint32_t param2,
                                     struct zmk_input_processor_state *state) {
    const struct scroll_curve_config *cfg = dev->config;
    struct scroll_curve_data *data = dev->data;
    int axis;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->code) {
    case INPUT_REL_X:
        axis = 0;
        event->code = INPUT_REL_HWHEEL;
        break;
    case INPUT_REL_Y:
        axis = 1;
        event->code = INPUT_REL_WHEEL;
        break;
    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int32_t divisor = GAIN_SCALE;
    if (!IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)) {
        divisor *= HI_RES_PER_NOTCH;
    }

    // Scroll opposite to the finger, like dragging the page.
    int32_t total = data->remainder[axis] - apply_curve(cfg, event->value);
    int32_t out = total / divisor;
    data->remainder[axis] = total - out * divisor;
    event->value = out;

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api scroll_curve_driver_api = {
    .handle_event = scroll_curve_handle_event,
};

#define SCROLL_CURVE_INST(n)                                                                       \
    static struct scroll_curve_data scroll_curve_data_##n;                                         \
    static const struct scroll_curve_config scroll_curve_config_##n = {                            \
        .knee = DT_INST_PROP(n, knee),                                                             \
        .low_gain = DT_INST_PROP(n, low_gain),                                                     \
        .high_gain = DT_INST_PROP(n, high_gain),                                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &scroll_curve_data_##n, &scroll_curve_config_##n,         \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                        \
                          &scroll_curve_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_CURVE_INST)
