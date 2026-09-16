# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#
# Both board drivers are compiled in, and each one's contents are guarded by
# its CONFIG_COCKPIT_BOARD_* symbol so exactly one provides the panel and the
# shared i2c_bus(). REQUIRES cannot be made conditional on CONFIG_*: component
# CMakeLists run once in early expansion BEFORE Kconfig values exist, and the
# requirements resolved in that pass are the ones used -- an if() here silently
# takes the else branch and the chosen board's headers go missing.
idf_component_register(
    SRCS src/waveshare_7b.cpp src/waveshare_lcd_x.cpp src/waveshare_audio.cpp src/ui.cpp
    INCLUDE_DIRS include
    REQUIRES espos_audio lvgl__lvgl esp_lcd esp_lcd_ek79007 waveshare__esp_lcd_ili9881c esp_driver_ppa
             esp_driver_i2s esp_driver_i2c espressif__esp_codec_dev
    PRIV_REQUIRES esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_driver_i2s esp_timer espressif__esp_codec_dev espos_health esp_mm)
