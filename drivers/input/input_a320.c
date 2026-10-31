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

LOG_MODULE_REGISTER(input_a320, CONFIG_INPUT_LOG_LEVEL);

#define A320_REG_MOTION 0x02
#define A320_REG_DELTA_X 0x03
#define A320_REG_DELTA_Y 0x04

#define A320_MOTION_MOT BIT(7)
#define A320_MOTION_OVF BIT(4)

struct a320_config {
    struct i2c_dt_spec bus;
    uint32_t polling_interval_ms;
};

struct a320_data {
    const struct device *dev;
    struct k_work_delayable poll_work;
};

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

    if (motion & A320_MOTION_OVF) {
        LOG_DBG("motion overflow, dropping sample");
        return -EOVERFLOW;
    }

    *dx = (int8_t)x;
    *dy = (int8_t)y;
    return 0;
}

static void a320_poll(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct a320_data *data = CONTAINER_OF(dwork, struct a320_data, poll_work);
    const struct device *dev = data->dev;
    const struct a320_config *cfg = dev->config;
    int8_t dx, dy;

    int ret = a320_read_motion(dev, &dx, &dy);
    if (ret == 0 && (dx != 0 || dy != 0)) {
        // Always report both axes, X first, so input processors can rely on the ordering.
        input_report_rel(dev, INPUT_REL_X, dx, false, K_FOREVER);
        input_report_rel(dev, INPUT_REL_Y, dy, true, K_FOREVER);
    } else if (ret < 0 && ret != -ENODATA && ret != -EOVERFLOW) {
        // Expected while ext power is toggled off; keep polling so the sensor resumes on its own.
        LOG_DBG("read failed: %d", ret);
    }

    k_work_schedule(&data->poll_work, K_MSEC(cfg->polling_interval_ms));
}

static int a320_init(const struct device *dev) {
    const struct a320_config *cfg = dev->config;
    struct a320_data *data = dev->data;

    if (!i2c_is_ready_dt(&cfg->bus)) {
        LOG_ERR("I2C bus %s is not ready", cfg->bus.bus->name);
        return -ENODEV;
    }

    data->dev = dev;
    k_work_init_delayable(&data->poll_work, a320_poll);
    k_work_schedule(&data->poll_work, K_MSEC(cfg->polling_interval_ms));

    return 0;
}

#define A320_DEFINE(n)                                                                             \
    static struct a320_data a320_data_##n;                                                         \
    static const struct a320_config a320_config_##n = {                                            \
        .bus = I2C_DT_SPEC_INST_GET(n),                                                            \
        .polling_interval_ms = DT_INST_PROP(n, polling_interval_ms),                               \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, a320_init, NULL, &a320_data_##n, &a320_config_##n, POST_KERNEL,       \
                          CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(A320_DEFINE)
