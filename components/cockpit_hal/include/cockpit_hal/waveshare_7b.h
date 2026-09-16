/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
 *
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-7B: 1024x600 EK79007 over MIPI-DSI
 * (2 lanes), GT911 touch on I2C (SDA 7, SCL 8), backlight PWM on GPIO 32.
 */
#pragma once

#include "sdkconfig.h"
#if !CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7

#include "cockpit_hal/display_driver.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace cockpit_hal {

class Waveshare7BDisplay : public DisplayDriver {
 public:
  static constexpr uint16_t kWidth = 1024;
  static constexpr uint16_t kHeight = 600;
  static constexpr int kBytesPerPixel = 2;
  static constexpr int kNumBuffers = 2;
  static constexpr size_t kBufferSize = kWidth * kHeight * kBytesPerPixel;

  void init() override;
  uint16_t width() const override { return kWidth; }
  uint16_t height() const override { return kHeight; }
  void* get_draw_buffer(int index) override;
  size_t get_draw_buffer_size() override { return kBufferSize; }
  void flush(int x, int y, int w, int h, const void* buf) override;
  /// Blocks until the last flush()'s DMA copy into the frame buffer is done.
  void wait_flush_done() override;
  void set_brightness(uint8_t pct) override;
  void set_display_on(bool on) override;

 private:
  void init_ldo();
  void init_backlight();
  esp_lcd_dsi_bus_handle_t dsi_bus_ = nullptr;
  esp_lcd_panel_io_handle_t dbi_io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  void* framebuffers_[kNumBuffers] = {};
  // Given by the DPI driver's on_color_trans_done callback: draw_bitmap is
  // asynchronous, so LVGL must not reuse the draw buffer until it fires.
  SemaphoreHandle_t trans_done_ = nullptr;
};

class Waveshare7BTouch : public TouchDriver {
 public:
  void init() override;
  TouchPoint read() override;

 private:
  bool initialized_ = false;
};

}  // namespace cockpit_hal

#endif  // !CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7
