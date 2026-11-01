# Third-party notices

espos-p4-cockpit is licensed under the Apache License 2.0, see
[LICENSE](LICENSE). The following third-party code is vendored into this
repository under its own licence, which continues to apply to it unchanged:

| Component | Copyright | License |
| --- | --- | --- |
| `components/esp_lcd_st7703` | 2024 Espressif Systems (Shanghai) CO LTD | Apache-2.0 |
| `components/esp_lcd_ek79007` | 2023-2024 Espressif Systems (Shanghai) CO LTD | Apache-2.0 |
| `components/cockpit_hal/src/ili9881c_init_cmds.inc` | Waveshare, from the `esp32_p4_wifi6_touch_lcd_x` BSP 2.0.3 | Apache-2.0 |

Their SPDX headers are retained in the source files.

Libraries resolved at build time (ESP-IDF, espOS, the registry components in
`idf_component.yml`) are not part of this repository, but they are linked into
the published firmware images. espOS lists them with their licences in its
[THIRD-PARTY-NOTICES.md](https://github.com/signalk-espOS/espOS/blob/main/THIRD-PARTY-NOTICES.md);
most are MIT or Apache-2.0. Two need attention when you redistribute an image:

| Component | Used for | License |
| --- | --- | --- |
| `espressif/esp-sr` (WakeNet/AFE) | wake word, through espOS's voice component | Apache-2.0 for the API; the wake-word model binaries (`srmodels.bin`) are under Espressif's own model licence |
| `espressif/esp_new_jpeg` | JPEG encoding of `/screenshot` | Espressif proprietary (binary library); see the component's LICENSE |
