/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution */
#include "cockpit_hal/waveshare_7b.h"
#include "cockpit_hal/i2c_bus.h"

#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lcd_ek79007.h"
#include "esp_ldo_regulator.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char* TAG = "ws7b";

static constexpr gpio_num_t kBacklightGpio = GPIO_NUM_32;
static constexpr gpio_num_t kResetGpio = GPIO_NUM_33;
static constexpr int kLdoMipiChan = 3;
static constexpr int kLdoMipiMv = 2500;
static constexpr gpio_num_t kTouchSda = GPIO_NUM_7;
static constexpr gpio_num_t kTouchScl = GPIO_NUM_8;
static constexpr uint8_t kGT911Addr = 0x5D;
static constexpr uint16_t kStatusReg = 0x814E;
static constexpr uint16_t kPointReg = 0x814F;

namespace cockpit_hal {

// Runs from the DSI ISR. CONFIG_LCD_DSI_ISR_CACHE_SAFE keeps that ISR
// alive while the cache is disabled (flash writes), so everything it
// reaches must be in IRAM — hence a free function, not a lambda.
static IRAM_ATTR bool trans_done_isr(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* ctx) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
  return woken == pdTRUE;
}

void Waveshare7BDisplay::init() {
  init_ldo();
  ESP_LOGI(TAG, "MIPI-DSI bus up");
  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = 2;
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = 900;
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus_));
  esp_lcd_dbi_io_config_t dbi_cfg = EK79007_PANEL_IO_DBI_CONFIG();
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus_, &dbi_cfg, &dbi_io_));

  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = 52;
  dpi_cfg.virtual_channel = 0;
  dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.num_fbs = kNumBuffers;
  dpi_cfg.video_timing.h_size = 1024;
  dpi_cfg.video_timing.v_size = 600;
  dpi_cfg.video_timing.hsync_back_porch = 160;
  dpi_cfg.video_timing.hsync_pulse_width = 10;
  dpi_cfg.video_timing.hsync_front_porch = 160;
  dpi_cfg.video_timing.vsync_back_porch = 23;
  dpi_cfg.video_timing.vsync_pulse_width = 1;
  dpi_cfg.video_timing.vsync_front_porch = 12;

  ek79007_vendor_config_t vendor_cfg = {};
  vendor_cfg.init_cmds = nullptr;
  vendor_cfg.init_cmds_size = 0;
  vendor_cfg.mipi_config.dsi_bus = dsi_bus_;
  vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
  vendor_cfg.mipi_config.lane_num = 2;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = kResetGpio;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = &vendor_cfg;

  ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(dbi_io_, &panel_cfg, &panel_));
  // draw_bitmap() only queues the copy; without waiting for this callback
  // LVGL overwrites the draw buffer mid-transfer and the panel shows
  // torn/stale strips (a periodic full-width flash).
  trans_done_ = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = trans_done_isr;
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_, &cbs, trans_done_));
  gpio_set_level(kResetGpio, 1);
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel_, kNumBuffers, &framebuffers_[0], &framebuffers_[1]));
  memset(framebuffers_[0], 0, kBufferSize);
  memset(framebuffers_[1], 0, kBufferSize);
  init_backlight();
  ESP_LOGI(TAG, "display %dx%d RGB565, %d DMA buffers", kWidth, kHeight, kNumBuffers);
}

void* Waveshare7BDisplay::get_draw_buffer(int index) { return framebuffers_[index % kNumBuffers]; }

void Waveshare7BDisplay::flush(int x, int y, int w, int h, const void* buf) {
  esp_lcd_panel_draw_bitmap(panel_, x, y, x + w, y + h, buf);
}

void Waveshare7BDisplay::wait_flush_done() {
  if (trans_done_) xSemaphoreTake(trans_done_, pdMS_TO_TICKS(100));
}

void Waveshare7BDisplay::init_ldo() {
  esp_ldo_channel_handle_t ldo = nullptr;
  esp_ldo_channel_config_t cfg = {};
  cfg.chan_id = kLdoMipiChan;
  cfg.voltage_mv = kLdoMipiMv;
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&cfg, &ldo));
}

void Waveshare7BDisplay::init_backlight() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.timer_num = LEDC_TIMER_1;
  timer.freq_hz = 1000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer));
  ledc_channel_config_t ch = {};
  ch.gpio_num = kBacklightGpio;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel = LEDC_CHANNEL_1;
  ch.timer_sel = LEDC_TIMER_1;
  ch.duty = 0;
  ch.hpoint = 0;
  ch.flags.output_invert = 1;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (1023 * 95) / 100));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
}

void Waveshare7BDisplay::set_brightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (1023 * pct) / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void Waveshare7BDisplay::set_display_on(bool on) {
  if (panel_) esp_lcd_panel_disp_on_off(panel_, on);
}

// ---- GT911 over the IDF i2c_master driver -------------------------------

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_gt = nullptr;

i2c_master_bus_handle_t i2c_bus() { return s_bus; }

static bool gt911_read_regs(uint16_t reg, uint8_t* buf, size_t len) {
  uint8_t a[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
  return i2c_master_transmit_receive(s_gt, a, 2, buf, len, 50) == ESP_OK;
}

static uint8_t gt911_read_reg(uint16_t reg) {
  uint8_t v = 0;
  gt911_read_regs(reg, &v, 1);
  return v;
}

static void gt911_write_reg(uint16_t reg, uint8_t val) {
  uint8_t d[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val};
  i2c_master_transmit(s_gt, d, 3, 50);
}

void Waveshare7BTouch::init() {
  i2c_master_bus_config_t bus = {};
  bus.i2c_port = I2C_NUM_0;
  bus.sda_io_num = kTouchSda;
  bus.scl_io_num = kTouchScl;
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.flags.enable_internal_pullup = true;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_bus));
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = kGT911Addr;
  dev.scl_speed_hz = 400000;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_gt));

  uint8_t id[4] = {};
  gt911_read_regs(0x8140, id, 4);
  if (id[0] == '9' && id[1] == '1' && id[2] == '1') {
    ESP_LOGI(TAG, "GT911 touch initialized");
    gt911_write_reg(kStatusReg, 0x00);
    initialized_ = true;
  } else {
    ESP_LOGE(TAG, "GT911 not found (ID: %c%c%c)", id[0], id[1], id[2]);
  }
}

TouchDriver::TouchPoint Waveshare7BTouch::read() {
  static TouchPoint last = {0, 0, false};
  TouchPoint pt = {last.x, last.y, false};
  if (!initialized_) return pt;
  uint8_t status = gt911_read_reg(kStatusReg);
  if (!(status & 0x80)) {
    last.pressed = false;
    return pt;
  }
  {
    uint8_t cnt = status & 0x0F;
    if (cnt > 0 && cnt <= 5) {
      uint8_t t[8] = {};
      gt911_read_regs(kPointReg, t, 8);
      pt.x = t[1] | (t[2] << 8);
      pt.y = t[3] | (t[4] << 8);
      pt.pressed = true;
      if (!last.pressed) ESP_LOGI(TAG, "touch down x=%d y=%d (n=%d)", pt.x, pt.y, cnt);
      last = pt;
    }
    gt911_write_reg(kStatusReg, 0x00);
  }
  return pt;
}

}  // namespace cockpit_hal
