# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
file(GLOB_RECURSE jlp_sources ${CMAKE_CURRENT_LIST_DIR}/jlp/*.cpp)
# espos_ota is named although nothing here calls it: consumers build with
# MINIMAL_BUILD and espos_start() only starts the optional stacks that are in
# the component list, so dropping it would silently drop OTA.
idf_component_register(
    SRCS app_main.cpp ${jlp_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES cockpit_hal espos_core espos_event espos_health espos_audio espos_voice espos_n2k
                  esp_app_format espos_config espos_wifi espos_sk espos_ota
                  lvgl__lvgl esp_http_server esp_timer nvs_flash
                  bblanchon__arduinojson espressif__esp_new_jpeg esp_driver_jpeg mbedtls)

espos_config_add_descriptor(config/cockpit.json)
