# SPDX-License-Identifier: MIT

add_subdirectory(drivers)

target_sources_ifdef(CONFIG_ZMK_BB9900_LAYER_INDICATOR app PRIVATE src/layer_indicator.c)
target_sources_ifdef(CONFIG_ZMK_BB9900_CAPSLOCK_INDICATOR app PRIVATE src/capslock_indicator.c)
target_sources_ifdef(CONFIG_ZMK_BB9900_INDICATOR_SCROLL app PRIVATE src/input_processor_indicator_scroll.c)
