/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT avago_a320

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/ext_power.h>

LOG_MODULE_REGISTER(input_a320, CONFIG_INPUT_LOG_LEVEL);

#define A320_REG_PRODUCT_ID 0x00
#define A320_REG_MOTION 0x02
#define A320_REG_DELTA_X 0x03
#define A320_REG_DELTA_Y 0x04
#define A320_REG_SQUAL 0x05
#define A320_REG_SHUTTER_UPPER 0x06
#define A320_REG_SHUTTER_LOWER 0x07
#define A320_REG_OBSERVATION 0x2e
#define A320_REG_SOFT_RESET 0x3a
#define A320_REG_INVERSE_PRODUCT_ID 0x3f
#define A320_REG_OFN_ENGINE 0x60
#define A320_REG_OFN_RESOLUTION 0x62
#define A320_REG_OFN_SPEED_CONTROL 0x63
#define A320_REG_OFN_SPEED_ST12 0x64

#define A320_PRODUCT_ID 0x83
#define A320_INVERSE_PRODUCT_ID 0x7c
#define A320_SOFT_RESET_VALUE 0x5a

#define A320_MOTION_MOT BIT(7)
#define A320_MOTION_OVF BIT(4)

#define A320_OFN_ENGINE_ENABLE BIT(7)
#define A320_OFN_ENGINE_SPEED BIT(6)
#define A320_OFN_ENGINE_ASSERT_DEASSERT BIT(5)
#define A320_OFN_ENGINE_FINGER BIT(2)

// Datasheet power-up value: wake and run at 500 CPI.
#define A320_OFN_RESOLUTION_DEFAULT 0x12

#define A320_OFN_SPEED_CONTROL_LOW_CPI BIT(1)
#define A320_OFN_SPEED_CONTROL_HIGH_CPI BIT(0)
#define A320_OFN_SPEED_CONTROL_INTERVAL(ms) ((((ms) / 4) - 1) << 2)

#define A320_SPEED_THRESHOLD_COUNT 8

// Datasheet timings: tVRT-NRST after power is stable, tMOT-RST after a reset.
#define A320_POWER_UP_DELAY_MS 100
#define A320_RESET_DELAY_MS 25

#define A320_HEALTH_CHECK_INTERVAL_MS 1000
#define A320_RETRY_DELAY_MS 100
#define A320_FAILURES_BEFORE_POWER_CYCLE 5
#define A320_POWER_OFF_MS 50

enum a320_state {
    A320_STATE_RESET,
    A320_STATE_CONFIGURE,
    A320_STATE_RUNNING,
    A320_STATE_POWER_ON,
};

struct a320_config {
    struct i2c_dt_spec bus;
    const struct device *ext_power;
    uint32_t polling_interval_ms;
    uint32_t motion_polling_interval_ms;
    uint32_t motion_timeout_ms;
    uint16_t liftoff_shutter_threshold;
    uint8_t ofn_engine;
    uint8_t ofn_speed_control;
    bool has_speed_thresholds;
    uint8_t speed_thresholds[A320_SPEED_THRESHOLD_COUNT];
};

struct a320_data {
    const struct device *dev;
    struct k_work_delayable work;
    enum a320_state state;
    uint8_t failures;
    int64_t next_health_check;
    int64_t last_motion;
    bool tracing_motion;
};

// Debug builds log every sample with image quality, shutter time and the current resolution
// step, to tune speed switching and liftoff filtering against real swipes.
static void a320_trace(const struct device *dev, bool motion, int8_t dx, int8_t dy) {
    const struct a320_config *cfg = dev->config;
    struct a320_data *data = dev->data;
    uint8_t squal = 0, sh_hi = 0, sh_lo = 0, res = 0;

    if (!motion) {
        if (data->tracing_motion) {
            LOG_DBG("trace lift");
            data->tracing_motion = false;
        }
        return;
    }

    data->tracing_motion = true;
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_SQUAL, &squal);
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_SHUTTER_UPPER, &sh_hi);
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_SHUTTER_LOWER, &sh_lo);
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_OFN_RESOLUTION, &res);
    LOG_DBG("trace t=%u dx=%d dy=%d squal=%u shutter=%u res=%u", k_uptime_get_32(), dx, dy,
            squal, (sh_hi << 8) | sh_lo, res & 0x07);
}

static int a320_check_id(const struct device *dev) {
    const struct a320_config *cfg = dev->config;
    uint8_t id, inv_id;
    int ret;

    ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_PRODUCT_ID, &id);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_INVERSE_PRODUCT_ID, &inv_id);
    if (ret < 0) {
        return ret;
    }

    if (id != A320_PRODUCT_ID || inv_id != A320_INVERSE_PRODUCT_ID) {
        LOG_WRN("unexpected product ID 0x%02x / 0x%02x", id, inv_id);
        return -EIO;
    }

    return 0;
}

static int a320_configure(const struct device *dev) {
    const struct a320_config *cfg = dev->config;
    uint8_t engine;
    int ret;

    ret = a320_check_id(dev);
    if (ret < 0) {
        return ret;
    }

    if (cfg->ofn_engine & A320_OFN_ENGINE_SPEED) {
        ret = i2c_reg_write_byte_dt(&cfg->bus, A320_REG_OFN_RESOLUTION,
                                    A320_OFN_RESOLUTION_DEFAULT);
        if (ret < 0) {
            return ret;
        }

        ret = i2c_reg_write_byte_dt(&cfg->bus, A320_REG_OFN_SPEED_CONTROL,
                                    cfg->ofn_speed_control);
        if (ret < 0) {
            return ret;
        }

        for (int i = 0; cfg->has_speed_thresholds && i < A320_SPEED_THRESHOLD_COUNT; i++) {
            ret = i2c_reg_write_byte_dt(&cfg->bus, A320_REG_OFN_SPEED_ST12 + i,
                                        cfg->speed_thresholds[i]);
            if (ret < 0) {
                return ret;
            }
        }
    }

    ret = i2c_reg_write_byte_dt(&cfg->bus, A320_REG_OFN_ENGINE, cfg->ofn_engine);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_OFN_ENGINE, &engine);
    if (ret < 0) {
        return ret;
    }

    if (engine != cfg->ofn_engine) {
        LOG_WRN("OFN engine reads back 0x%02x, expected 0x%02x", engine, cfg->ofn_engine);
        return -EIO;
    }

    // Clear any motion latched before configuration.
    uint8_t discard;
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_MOTION, &discard);
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_DELTA_X, &discard);
    i2c_reg_read_byte_dt(&cfg->bus, A320_REG_DELTA_Y, &discard);

    return 0;
}

static int a320_read_motion(const struct device *dev, int8_t *dx, int8_t *dy) {
    const struct a320_config *cfg = dev->config;
    uint8_t motion, x, y;
    int ret;

    ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_MOTION, &motion);
    if (ret < 0) {
        return ret;
    }

    if (!(motion & A320_MOTION_MOT)) {
        return -ENODATA;
    }

    // Reading Motion latches the delta registers; read both to clear them, even on overflow.
    ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_DELTA_X, &x);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_DELTA_Y, &y);
    if (ret < 0) {
        return ret;
    }

    // On overflow the deltas hold the saturated values, which is still the right direction.
    if (motion & A320_MOTION_OVF) {
        LOG_DBG("motion overflow");
    }

    *dx = (int8_t)x;
    *dy = (int8_t)y;

    if (cfg->liftoff_shutter_threshold > 0) {
        uint8_t sh_hi, sh_lo;

        ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_SHUTTER_UPPER, &sh_hi);
        if (ret < 0) {
            return ret;
        }

        ret = i2c_reg_read_byte_dt(&cfg->bus, A320_REG_SHUTTER_LOWER, &sh_lo);
        if (ret < 0) {
            return ret;
        }

        uint16_t shutter = (sh_hi << 8) | sh_lo;
        if (shutter > cfg->liftoff_shutter_threshold) {
            LOG_DBG("dropping liftoff sample dx=%d dy=%d shutter=%u", *dx, *dy, shutter);
            return -EAGAIN;
        }
    }

    return 0;
}

// Returns the delay before the next step.
static uint32_t a320_fail(const struct device *dev, const char *what, int err) {
    const struct a320_config *cfg = dev->config;
    struct a320_data *data = dev->data;

    data->failures++;
    LOG_WRN("%s failed (%d), attempt %d", what, err, data->failures);

    // Only power cycle when ext power is meant to be on, so the T key can still turn it off.
    if (cfg->ext_power != NULL && data->failures >= A320_FAILURES_BEFORE_POWER_CYCLE &&
        ext_power_get(cfg->ext_power) > 0) {
        LOG_WRN("power cycling sensor");
        data->failures = 0;
        ext_power_disable(cfg->ext_power);
        data->state = A320_STATE_POWER_ON;
        return A320_POWER_OFF_MS;
    }

    data->state = A320_STATE_RESET;
    return A320_RETRY_DELAY_MS;
}

static uint32_t a320_step_running(const struct device *dev) {
    const struct a320_config *cfg = dev->config;
    struct a320_data *data = dev->data;
    int8_t dx, dy;
    int ret;

    int64_t now = k_uptime_get();

    ret = a320_read_motion(dev, &dx, &dy);
    if (IS_ENABLED(CONFIG_INPUT_LOG_LEVEL_DBG) && (ret == 0 || ret == -ENODATA)) {
        a320_trace(dev, ret == 0, dx, dy);
    }
    if (ret == 0 || ret == -EAGAIN) {
        data->last_motion = now;
    }
    if (ret == 0 && (dx != 0 || dy != 0)) {
        // Always report both axes, X first, so input processors can rely on the ordering.
        input_report_rel(dev, INPUT_REL_X, dx, false, K_FOREVER);
        input_report_rel(dev, INPUT_REL_Y, dy, true, K_FOREVER);
    } else if (ret < 0 && ret != -ENODATA && ret != -EAGAIN) {
        // Also expected while ext power is toggled off with the T key.
        return a320_fail(dev, "motion read", ret);
    }

    if (now >= data->next_health_check) {
        data->next_health_check = now + A320_HEALTH_CHECK_INTERVAL_MS;

        ret = a320_check_id(dev);
        if (ret < 0) {
            return a320_fail(dev, "health check", ret);
        }

        if (IS_ENABLED(CONFIG_INPUT_LOG_LEVEL_DBG)) {
            uint8_t obs;
            if (i2c_reg_read_byte_dt(&cfg->bus, A320_REG_OBSERVATION, &obs) == 0) {
                LOG_DBG("observation 0x%02x", obs);
                i2c_reg_write_byte_dt(&cfg->bus, A320_REG_OBSERVATION, 0);
            }
        }
    }

    if (now - data->last_motion < cfg->motion_timeout_ms) {
        return cfg->motion_polling_interval_ms;
    }
    return cfg->polling_interval_ms;
}

static void a320_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct a320_data *data = CONTAINER_OF(dwork, struct a320_data, work);
    const struct device *dev = data->dev;
    const struct a320_config *cfg = dev->config;
    uint32_t delay_ms;
    int ret;

    switch (data->state) {
    case A320_STATE_POWER_ON:
        ext_power_enable(cfg->ext_power);
        data->state = A320_STATE_RESET;
        delay_ms = A320_POWER_UP_DELAY_MS;
        break;

    case A320_STATE_RESET:
        ret = i2c_reg_write_byte_dt(&cfg->bus, A320_REG_SOFT_RESET, A320_SOFT_RESET_VALUE);
        if (ret < 0) {
            delay_ms = a320_fail(dev, "soft reset", ret);
            break;
        }
        data->state = A320_STATE_CONFIGURE;
        delay_ms = A320_RESET_DELAY_MS;
        break;

    case A320_STATE_CONFIGURE:
        ret = a320_configure(dev);
        if (ret < 0) {
            delay_ms = a320_fail(dev, "configure", ret);
            break;
        }
        LOG_INF("ready, OFN engine 0x%02x", cfg->ofn_engine);
        data->failures = 0;
        data->next_health_check = k_uptime_get() + A320_HEALTH_CHECK_INTERVAL_MS;
        data->state = A320_STATE_RUNNING;
        delay_ms = cfg->polling_interval_ms;
        break;

    case A320_STATE_RUNNING:
    default:
        delay_ms = a320_step_running(dev);
        break;
    }

    k_work_schedule(&data->work, K_MSEC(delay_ms));
}

static int a320_init(const struct device *dev) {
    const struct a320_config *cfg = dev->config;
    struct a320_data *data = dev->data;

    if (!i2c_is_ready_dt(&cfg->bus)) {
        LOG_ERR("I2C bus %s is not ready", cfg->bus.bus->name);
        return -ENODEV;
    }

    data->dev = dev;
    data->state = A320_STATE_RESET;
    k_work_init_delayable(&data->work, a320_work_handler);
    k_work_schedule(&data->work, K_MSEC(A320_POWER_UP_DELAY_MS));

    return 0;
}

#define A320_OFN_ENGINE_FEATURES(n)                                                                \
    ((DT_INST_PROP(n, speed_switching) ? A320_OFN_ENGINE_SPEED : 0) |                              \
     (DT_INST_PROP(n, assert_deassert) ? A320_OFN_ENGINE_ASSERT_DEASSERT : 0) |                    \
     (DT_INST_PROP(n, finger_detect) ? A320_OFN_ENGINE_FINGER : 0))

#define A320_OFN_ENGINE_BITS(n)                                                                    \
    (A320_OFN_ENGINE_FEATURES(n) ? (A320_OFN_ENGINE_ENABLE | A320_OFN_ENGINE_FEATURES(n)) : 0)

#define A320_DEFINE(n)                                                                             \
    BUILD_ASSERT(DT_INST_PROP_LEN_OR(n, speed_thresholds, A320_SPEED_THRESHOLD_COUNT) ==           \
                     A320_SPEED_THRESHOLD_COUNT,                                                   \
                 "speed-thresholds needs exactly 8 values");                                       \
    static struct a320_data a320_data_##n;                                                         \
    static const struct a320_config a320_config_##n = {                                            \
        .bus = I2C_DT_SPEC_INST_GET(n),                                                            \
        .ext_power = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, ext_power),                              \
                                 (DEVICE_DT_GET(DT_INST_PHANDLE(n, ext_power))), (NULL)),          \
        .polling_interval_ms = DT_INST_PROP(n, polling_interval_ms),                               \
        .motion_polling_interval_ms = DT_INST_PROP(n, motion_polling_interval_ms),                 \
        .motion_timeout_ms = DT_INST_PROP(n, motion_timeout_ms),                                   \
        .liftoff_shutter_threshold = DT_INST_PROP(n, liftoff_shutter_threshold),                   \
        .ofn_engine = A320_OFN_ENGINE_BITS(n),                                                     \
        .ofn_speed_control =                                                                       \
            A320_OFN_SPEED_CONTROL_INTERVAL(DT_INST_PROP(n, speed_check_interval_ms)) |            \
            A320_OFN_SPEED_CONTROL_LOW_CPI |                                                       \
            (DT_INST_PROP(n, high_cpi) ? A320_OFN_SPEED_CONTROL_HIGH_CPI : 0),                     \
        .has_speed_thresholds = DT_INST_NODE_HAS_PROP(n, speed_thresholds),                        \
        .speed_thresholds = DT_INST_PROP_OR(n, speed_thresholds, {0}),                             \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, a320_init, NULL, &a320_data_##n, &a320_config_##n, POST_KERNEL,       \
                          CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(A320_DEFINE)
