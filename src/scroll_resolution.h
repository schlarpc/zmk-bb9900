/*
 * Copyright (c) 2026 The zmk-bb9900 contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

// Whether the currently selected output (USB or a BLE profile) should get whole-notch scroll
// steps instead of high-resolution ones.
bool bb9900_scroll_notched(void);
