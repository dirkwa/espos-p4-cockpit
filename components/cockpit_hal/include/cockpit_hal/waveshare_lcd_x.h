/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
 *
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-X, 7" variant: a 720x1280 PORTRAIT
 * ILI9881C over MIPI-DSI (2 lanes), GT911 touch on I2C (SDA 7, SCL 8),
 * backlight PWM on GPIO 26 (non-inverted), panel reset on GPIO 27.
 *
 * This is a different board from the Touch-LCD-7B, not a variant of it:
 * different controller, different resolution, different orientation and
 * different backlight/reset pins. The X series also ships 8" and 10.1"
 * panels (JD9365, 800x1280) which this driver does NOT cover.
 *
 * Values taken from Waveshare's own BSP, esp32_p4_wifi6_touch_lcd_x 2.0.3
 * (firmware/brookesia/components/ in waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X).
 *
 * ORIENTATION. The panel is physically portrait, but the cockpit's layouts
 * are landscape, so this driver presents a 1280x720 landscape screen and
 * rotates every flush 90 degrees into the portrait frame buffer using the
 * P4's PPA (Pixel Processing Accelerator). Rotation therefore costs no CPU.
 * width()/height() report the LANDSCAPE size; everything above the HAL is
 * unaware the panel is portrait, including touch, which is transformed back
 * in read().
 */
#pragma once

#include "sdkconfig.h"
#if CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7

#include "cockpit_hal/display_driver.h"
#include "driver/ppa.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace cockpit_hal {

class WaveshareXDisplay : public DisplayDriver {
 public:
  /// Native panel geometry: what the DSI link and the frame buffer use.
  static constexpr uint16_t kPanelWidth = 720;
  static constexpr uint16_t kPanelHeight = 1280;
  /// What the cockpit sees, after the 90-degree rotation.
  static constexpr uint16_t kWidth = kPanelHeight;   // 1280
  static constexpr uint16_t kHeight = kPanelWidth;   // 720
  static constexpr int kBytesPerPixel = 2;
  // ONE frame buffer: unlike the 7B, LVGL does not render into the DPI frame
  // buffer here -- it renders into its own PSRAM buffers and every flush is
  // rotated into this one by the PPA. A second frame buffer would be handed
  // out by the DPI driver and never written, and PPA writing one while the
  // DSI scans out the other is exactly the tearing this avoids.
  static constexpr int kNumBuffers = 1;
  static constexpr size_t kBufferSize = kPanelWidth * kPanelHeight * kBytesPerPixel;

  void init() override;
  uint16_t width() const override { return kWidth; }
  uint16_t height() const override { return kHeight; }
  void* get_draw_buffer(int index) override;
  size_t get_draw_buffer_size() override { return kBufferSize; }
  /// x/y/w/h are in LANDSCAPE coordinates; the rotation into the portrait
  /// frame buffer happens here.
  void flush(int x, int y, int w, int h, const void* buf) override;
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
  ppa_client_handle_t ppa_ = nullptr;
  /// PPA needs a cache-aligned, contiguous source; LVGL's draw buffer is
  /// neither guaranteed to be aligned nor safe to hand to DMA while LVGL
  /// may still touch it.
  void* rot_buf_ = nullptr;
  size_t rot_buf_size_ = 0;
  // Given by the DPI driver's on_color_trans_done callback: draw_bitmap is
  // asynchronous, so the source must stay intact until it fires.
  SemaphoreHandle_t trans_done_ = nullptr;
};

class WaveshareXTouch : public TouchDriver {
 public:
  void init() override;
  /// Returns LANDSCAPE coordinates, matching WaveshareXDisplay.
  TouchPoint read() override;

 private:
  bool initialized_ = false;
};

}  // namespace cockpit_hal

#endif  // CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7
