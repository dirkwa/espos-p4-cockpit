# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#
# espos-p4-cockpit: a pure ESP-IDF 6 project. WiFi, config, the web UI,
# SignalK discovery/token/stream and REST client, mDNS, the health watchdog
# and signed OTA come from the espOS submodule (espos/); this project adds
# the display HAL, LVGL, audio/voice, the N2K gateway and the JSON Layout
# Player. Build:  . <ESP-IDF 6.0.2>/export.sh; scripts/build.sh -DIDF_TARGET=esp32p4
cmake_minimum_required(VERSION 3.22)

# The IDF version pin, espos/components on EXTRA_COMPONENT_DIRS, the sdkconfig
# defaults (espOS's first, this project's sdkconfig.defaults on top), the
# partition table, the signing key and the re-sign-on-key-change workaround
# all live in espOS. They used to be ~55 lines copied into this file,
# ble-gateway's and espOS's own, which had already started to drift — and the
# signing half of it is exactly the code that must not, since getting it
# wrong yields a panel that flashes fine over USB and then rejects every OTA.
#
# The panel keeps its own partition table rather than espOS's 16mb.csv: the
# esp-sr "model" partition sits at the offset the 1.x firmware flashed it to,
# so a model already on a device survives the update.
include("${CMAKE_CURRENT_LIST_DIR}/espos/cmake/espos_project.cmake")
espos_project_prologue(NAME "p4-cockpit"
                       PARTITIONS "${CMAKE_CURRENT_LIST_DIR}/p4_16mb.csv"
                       COMPONENTS espos_voice espos_n2k)

# The 4B (720x720 ST7703) HAL is parked until a board is on hand again; its
# vendored driver predates the ESP-IDF 6 esp_lcd API. Drop it from the build
# rather than carry a dead #ifdef.
set(EXCLUDE_COMPONENTS esp_lcd_st7703)

# LVGL configuration lives in the project, not in Kconfig.
idf_build_set_property(COMPILE_DEFINITIONS "-DLV_CONF_PATH=\"${CMAKE_CURRENT_LIST_DIR}/components/cockpit_hal/lv_conf.h\"" APPEND)

project(cockpit)

# espOS web UI (the bundle committed in espos/ui/dist-gz) → LittleFS "storage" image.
espos_project_ui_partition(NAME "cockpit")
