/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zitaotech_input_processor_indicator_scroll

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include <zmk/hid_indicators.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct indicator_scroll_config {
    zmk_hid_indicators_t indicators;
    uint32_t interval_ms;
};

struct indicator_scroll_data {
    int16_t acc_x;
    int16_t acc_y;
    int64_t last_emit;
    // Divisor picked from the Y magnitude, shared with the X event of the same report.
    // 0 means "step by one in the direction of motion".
    int8_t divisor;
    bool frame_started;
    bool emitting;
};

static inline int sign(int v) { return (v > 0) - (v < 0); }

// Stepped curve from the original firmware, keyed on vertical motion magnitude.
static int8_t pick_divisor(int y) {
    int ay = abs(y);

    if (ay >= 128) {
        return 24;
    } else if (ay >= 64) {
        return 16;
    } else if (ay >= 32) {
        return 12;
    } else if (ay >= 21) {
        return 8;
    }
    return 0;
}

static int scroll_value(int v, int8_t divisor) { return divisor ? -v / divisor : -sign(v); }

static int indicator_scroll_handle_event(const struct device *dev, struct input_event *event,
                                         uint32_t param1, uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    const struct indicator_scroll_config *cfg = dev->config;
    struct indicator_scroll_data *data = dev->data;

    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!(zmk_hid_indicators_get_current_profile() & cfg->indicators)) {
        *data = (struct indicator_scroll_data){0};
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!data->frame_started) {
        int64_t now = k_uptime_get();
        data->frame_started = true;
        data->emitting = (now - data->last_emit) >= cfg->interval_ms;
        if (data->emitting) {
            data->last_emit = now;
        }
    }
    if (event->sync) {
        data->frame_started = false;
    }

    // The sensor's delta registers saturate at int8, so mirror that when accumulating.
    if (event->code == INPUT_REL_Y) {
        data->acc_y = CLAMP(data->acc_y + event->value, INT8_MIN, INT8_MAX);
    } else {
        data->acc_x = CLAMP(data->acc_x + event->value, INT8_MIN, INT8_MAX);
    }

    if (!data->emitting) {
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->code == INPUT_REL_Y) {
        int ay = abs(data->acc_y);
        data->divisor = pick_divisor(data->acc_y);
        event->code = INPUT_REL_WHEEL;
        event->value = ay <= 2 ? 0 : scroll_value(data->acc_y, data->divisor);
        data->acc_y = 0;
    } else {
        event->code = INPUT_REL_HWHEEL;
        event->value = scroll_value(data->acc_x, data->divisor);
        data->acc_x = 0;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api indicator_scroll_driver_api = {
    .handle_event = indicator_scroll_handle_event,
};

#define INDICATOR_SCROLL_INST(n)                                                                   \
    static struct indicator_scroll_data indicator_scroll_data_##n;                                 \
    static const struct indicator_scroll_config indicator_scroll_config_##n = {                    \
        .indicators = DT_INST_PROP(n, indicators),                                                 \
        .interval_ms = DT_INST_PROP(n, interval_ms),                                               \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &indicator_scroll_data_##n,                               \
                          &indicator_scroll_config_##n, POST_KERNEL,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &indicator_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INDICATOR_SCROLL_INST)
