/*
 * Copyright (c) 2023 ZitaoTech
 *
 * SPDX-License-Identifier: MIT
 */

// The BB9900 charging circuit back-feeds the battery onto VBUS, so the nRF52840 sees USB power
// even when the cable is unplugged and never reports a disconnect. Unplugging only shows up as
// the USB bus going into suspend. ZMK counts a suspended bus as USB power present and never
// enters deep sleep, so after the bus has been suspended for a while, tell ZMK the USB
// disconnected. If the host resumes or re-enumerates the bus, ZMK hears about it as usual.

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ZMK's USB status callback (app/src/usb.c), not declared in any header.
void usb_status_cb(enum usb_dc_status_code status, const uint8_t *params);

static void suspend_work_handler(struct k_work *work) {
    if (zmk_usb_get_status() != USB_DC_SUSPEND) {
        return;
    }

    LOG_INF("USB suspended for %d ms, treating it as unplugged",
            CONFIG_ZMK_BB9900_USB_SUSPEND_IS_UNPLUGGED_DELAY_MS);
    usb_status_cb(USB_DC_DISCONNECTED, NULL);
}

static K_WORK_DELAYABLE_DEFINE(suspend_work, suspend_work_handler);

static int usb_suspend_listener(const zmk_event_t *eh) {
    if (zmk_usb_get_status() == USB_DC_SUSPEND) {
        k_work_reschedule(&suspend_work,
                          K_MSEC(CONFIG_ZMK_BB9900_USB_SUSPEND_IS_UNPLUGGED_DELAY_MS));
    } else {
        k_work_cancel_delayable(&suspend_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bb9900_usb_suspend_unplug, usb_suspend_listener);
ZMK_SUBSCRIPTION(bb9900_usb_suspend_unplug, zmk_usb_conn_state_changed);
