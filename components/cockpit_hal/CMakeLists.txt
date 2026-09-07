# SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
idf_component_register(
    SRCS src/waveshare_7b.cpp src/waveshare_audio.cpp src/ui.cpp
    INCLUDE_DIRS include
    REQUIRES espos_audio lvgl__lvgl esp_lcd esp_lcd_ek79007 esp_driver_i2s esp_driver_i2c espressif__esp_codec_dev
    PRIV_REQUIRES esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_driver_i2s esp_timer espressif__esp_codec_dev espos_health)
