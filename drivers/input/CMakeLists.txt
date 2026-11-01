# SPDX-License-Identifier: MIT

zephyr_library()
# For ZMK's drivers/ext_power.h.
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library_sources_ifdef(CONFIG_INPUT_A320 input_a320.c)
