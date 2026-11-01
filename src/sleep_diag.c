/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

// Debug aid for deep sleep. Sleep only happens on battery, where USB logging is gone, so this
// records what happened and logs a summary every few seconds once USB is back:
// the USB status ZMK sees (sleep is skipped while it reports power), how often sleep was
// attempted, and which device, if any, refused to suspend. To find that device it dry-runs a
// suspend and resume of every candidate just before ZMK's real attempt.

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/usb.h>

#include <hal/nrf_power.h>

LOG_MODULE_REGISTER(bb9900_sleep_diag, LOG_LEVEL_INF);

static struct {
    uint32_t idle_events;
    uint32_t sleep_events;
    int usb_status_at_idle;
    int usb_status_at_sleep;
    const char *failed_device;
    int failed_ret;
    uint32_t dry_runs;
    // VBUS detect samples, once a second: how many saw VBUS present and absent.
    uint32_t vbus_present;
    uint32_t vbus_absent;
    // USB status changes as seen by ZMK, oldest first: status and uptime in seconds.
    struct {
        int status;
        uint32_t at;
    } history[16];
    size_t history_len;
    int last_status;
} diag = {
    .last_status = -1,
    .usb_status_at_idle = -1,
    .usb_status_at_sleep = -1,
};

static void dry_run_suspend(void) {
    const struct device *devs;
    size_t devc = z_device_get_all_static(&devs);

    diag.dry_runs++;

    for (const struct device *dev = devs + devc - 1; dev >= devs; dev--) {
        if (!device_is_ready(dev) || pm_device_is_busy(dev) || pm_device_wakeup_is_enabled(dev) ||
            pm_device_runtime_is_enabled(dev)) {
            continue;
        }

        int ret = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
        if (ret == -ENOSYS || ret == -ENOTSUP || ret == -EALREADY) {
            continue;
        }
        if (ret < 0) {
            diag.failed_device = dev->name;
            diag.failed_ret = ret;
            continue;
        }
        pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
    }
}

static int sleep_diag_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_IDLE:
        diag.idle_events++;
        diag.usb_status_at_idle = zmk_usb_get_status();
        break;
    case ZMK_ACTIVITY_SLEEP:
        diag.sleep_events++;
        diag.usb_status_at_sleep = zmk_usb_get_status();
        dry_run_suspend();
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bb9900_sleep_diag, sleep_diag_listener);
ZMK_SUBSCRIPTION(bb9900_sleep_diag, zmk_activity_state_changed);

static void sample_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_work_handler);

static void sample_work_handler(struct k_work *work) {
    if (nrf_power_usbregstatus_vbusdet_get(NRF_POWER)) {
        diag.vbus_present++;
    } else {
        diag.vbus_absent++;
    }

    int status = zmk_usb_get_status();
    if (status != diag.last_status) {
        diag.last_status = status;
        if (diag.history_len == ARRAY_SIZE(diag.history)) {
            memmove(&diag.history[0], &diag.history[1],
                    sizeof(diag.history) - sizeof(diag.history[0]));
            diag.history_len--;
        }
        diag.history[diag.history_len].status = status;
        diag.history[diag.history_len].at = k_uptime_get_32() / 1000;
        diag.history_len++;
    }

    k_work_schedule(&sample_work, K_SECONDS(1));
}

static void report_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(report_work, report_work_handler);

static void report_work_handler(struct k_work *work) {
    LOG_INF("uptime=%us usb_status=%d powered=%d | idle_events=%u usb_status_at_idle=%d | "
            "sleep_events=%u usb_status_at_sleep=%d dry_runs=%u failed=%s (%d)",
            k_uptime_get_32() / 1000, zmk_usb_get_status(), zmk_usb_is_powered(),
            diag.idle_events, diag.usb_status_at_idle, diag.sleep_events,
            diag.usb_status_at_sleep, diag.dry_runs,
            diag.failed_device ? diag.failed_device : "none", diag.failed_ret);
    LOG_INF("vbus now=%d, samples present=%u absent=%u",
            nrf_power_usbregstatus_vbusdet_get(NRF_POWER), diag.vbus_present, diag.vbus_absent);
    for (size_t i = 0; i < diag.history_len; i++) {
        LOG_INF("usb history %u: status=%d at=%us", (unsigned)i, diag.history[i].status,
                diag.history[i].at);
    }
    k_work_schedule(&report_work, K_SECONDS(5));
}

static int sleep_diag_init(void) {
    k_work_schedule(&report_work, K_SECONDS(5));
    k_work_schedule(&sample_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(sleep_diag_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
