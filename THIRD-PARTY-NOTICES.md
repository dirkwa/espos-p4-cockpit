# Third-party notices

espos-p4-cockpit is licensed under the Apache License 2.0, see
[LICENSE](LICENSE). The following third-party code is vendored into this
repository under its own licence, which continues to apply to it unchanged:

| Component | Copyright | License |
| --- | --- | --- |
| `components/esp_lcd_st7703` | 2024 Espressif Systems (Shanghai) CO LTD | Apache-2.0 |
| `components/esp_lcd_ek79007` | 2023-2024 Espressif Systems (Shanghai) CO LTD | Apache-2.0 |
| `components/cockpit_hal/src/ili9881c_init_cmds.inc` | Waveshare, from the `esp32_p4_wifi6_touch_lcd_x` BSP 2.0.3 | Apache-2.0 |

Their SPDX headers are retained in the source files. Libraries resolved at
build time (ESP-IDF, espOS, the registry components in `idf_component.yml`) are
governed by their own licences and are not part of this repository, but they
are linked into the published firmware images. Most are MIT or Apache-2.0.
`espressif/esp-sr` (the wake-word engine and its models, pulled in through
espOS's voice component) is under Espressif's own MIT-style licence, which
limits its use to Espressif chips; the firmware only runs on one.
