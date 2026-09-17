/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution */
#include "sdkconfig.h"
#if CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7

#include "cockpit_hal/waveshare_lcd_x.h"
#include "cockpit_hal/i2c_bus.h"

#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9881c.h"
#include "esp_ldo_regulator.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char* TAG = "wsx";

// Waveshare BSP esp32_p4_wifi6_touch_lcd_x 2.0.3: these differ from the 7B
// on every line, which is why the 7B HAL shows a black screen here -- it
// drives GPIO 32, which this board does not connect to the backlight.
static constexpr gpio_num_t kBacklightGpio = GPIO_NUM_26;
static constexpr gpio_num_t kResetGpio = GPIO_NUM_27;
static constexpr int kLdoMipiChan = 3;
static constexpr int kLdoMipiMv = 2500;
static constexpr gpio_num_t kTouchSda = GPIO_NUM_7;
static constexpr gpio_num_t kTouchScl = GPIO_NUM_8;
// The GT911 answers at 0x5D or 0x14 depending on how the INT/RST strap
// resolves at power-up; Waveshare's BSP probes both and so must we. The 7B
// is always 0x5D, which is why its driver can hardcode one address.
static constexpr uint8_t kGT911Addr = 0x5D;
static constexpr uint8_t kGT911AddrBackup = 0x14;
static constexpr uint16_t kStatusReg = 0x814E;
static constexpr uint16_t kPointReg = 0x814F;

#include "ili9881c_init_cmds.inc"

namespace cockpit_hal {

// Runs from the DSI ISR. CONFIG_LCD_DSI_ISR_CACHE_SAFE keeps that ISR alive
// while the cache is disabled (flash writes), so everything it reaches must
// be in IRAM -- hence a free function, not a lambda.
static IRAM_ATTR bool trans_done_isr(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* ctx) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
  return woken == pdTRUE;
}

void WaveshareXDisplay::init() {
  init_ldo();
  ESP_LOGI(TAG, "MIPI-DSI bus up");
  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = 2;
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  // 80 MHz DPI clock over 2 lanes at RGB565 needs headroom; Waveshare's BSP
  // uses 1000 Mbps for this panel where the 7B (52 MHz) uses 900.
  bus_cfg.lane_bit_rate_mbps = 1000;
  ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus_));
  esp_lcd_dbi_io_config_t dbi_cfg = ILI9881C_PANEL_IO_DBI_CONFIG();
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus_, &dbi_cfg, &dbi_io_));

  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = 80;
  dpi_cfg.virtual_channel = 0;
  dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.num_fbs = kNumBuffers;
  // Native portrait geometry -- the rotation to landscape happens in flush().
  dpi_cfg.video_timing.h_size = kPanelWidth;
  dpi_cfg.video_timing.v_size = kPanelHeight;
  dpi_cfg.video_timing.hsync_back_porch = 239;
  dpi_cfg.video_timing.hsync_pulse_width = 50;
  dpi_cfg.video_timing.hsync_front_porch = 33;
  dpi_cfg.video_timing.vsync_back_porch = 20;
  dpi_cfg.video_timing.vsync_pulse_width = 30;
  dpi_cfg.video_timing.vsync_front_porch = 2;

  ili9881c_vendor_config_t vendor_cfg = {};
  vendor_cfg.init_cmds = kIli9881cInitCmds;
  vendor_cfg.init_cmds_size = sizeof(kIli9881cInitCmds) / sizeof(kIli9881cInitCmds[0]);
  vendor_cfg.mipi_config.dsi_bus = dsi_bus_;
  vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
  vendor_cfg.mipi_config.lane_num = 2;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = kResetGpio;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = &vendor_cfg;

  ESP_ERROR_CHECK(esp_lcd_new_panel_ili9881c(dbi_io_, &panel_cfg, &panel_));
  // draw_bitmap() only queues the copy; without waiting for this callback the
  // source is reused mid-transfer and the panel shows torn strips.
  trans_done_ = xSemaphoreCreateBinary();
  ESP_ERROR_CHECK(trans_done_ ? ESP_OK : ESP_ERR_NO_MEM);
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = trans_done_isr;
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_, &cbs, trans_done_));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel_, kNumBuffers, &framebuffers_[0]));
  memset(framebuffers_[0], 0, kBufferSize);

  // PPA does the 90-degree rotation in hardware. A software rotate of a
  // 1280x720 RGB565 frame is ~1.8 MB of strided single-pixel copies per
  // full refresh, which this panel cannot afford.
  ppa_client_config_t ppa_cfg = {};
  ppa_cfg.oper_type = PPA_OPERATION_SRM;
  ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &ppa_));

  init_backlight();
  ESP_LOGI(TAG, "display %dx%d landscape (panel %dx%d portrait, PPA-rotated), %d DMA buffers", kWidth, kHeight,
           kPanelWidth, kPanelHeight, kNumBuffers);
}

void* WaveshareXDisplay::get_draw_buffer(int index) { return framebuffers_[index % kNumBuffers]; }

void WaveshareXDisplay::flush(int x, int y, int w, int h, const void* buf) {
  if (!ppa_) return;

  // PPA reads the source by DMA and requires a cache-aligned buffer; LVGL's
  // draw buffer is an ordinary PSRAM allocation, so stage through our own.
  size_t need = static_cast<size_t>(w) * h * kBytesPerPixel;
  if (need > rot_buf_size_) {
    if (rot_buf_) heap_caps_free(rot_buf_);
    size_t align = 64;  // L1/L2 cache line on the P4
    rot_buf_size_ = (need + align - 1) & ~(align - 1);
    rot_buf_ = heap_caps_aligned_alloc(align, rot_buf_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rot_buf_) {
      ESP_LOGE(TAG, "no PSRAM for %u-byte rotation buffer", (unsigned)rot_buf_size_);
      rot_buf_size_ = 0;
      return;
    }
  }
  memcpy(rot_buf_, buf, need);
  esp_cache_msync(rot_buf_, rot_buf_size_, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  // Landscape (x,y,w,h) -> portrait frame buffer. A 90-degree CCW rotation
  // maps landscape (x,y) to portrait (y, W_land-1-x), so a w x h landscape
  // block becomes an h x w portrait block at that corner.
  ppa_srm_oper_config_t op = {};
  op.in.buffer = rot_buf_;
  op.in.pic_w = w;
  op.in.pic_h = h;
  op.in.block_w = w;
  op.in.block_h = h;
  op.in.block_offset_x = 0;
  op.in.block_offset_y = 0;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.out.buffer = framebuffers_[0];
  op.out.buffer_size = kBufferSize;
  op.out.pic_w = kPanelWidth;
  op.out.pic_h = kPanelHeight;
  op.out.block_offset_x = y;
  op.out.block_offset_y = kWidth - (x + w);
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  op.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
  op.scale_x = 1.0f;
  op.scale_y = 1.0f;
  // Blocking: the rotation must be complete before draw_bitmap sends the
  // frame buffer to the panel, or the DSI scans out a half-rotated frame.
  op.mode = PPA_TRANS_MODE_BLOCKING;

  esp_err_t err = ppa_do_scale_rotate_mirror(ppa_, &op);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PPA rotate failed: %s", esp_err_to_name(err));
    return;
  }

  // PPA wrote the frame buffer by DMA; push it out so the DSI reads it.
  esp_lcd_panel_draw_bitmap(panel_, op.out.block_offset_x, op.out.block_offset_y, op.out.block_offset_x + h,
                            op.out.block_offset_y + w, framebuffers_[0]);
}

void WaveshareXDisplay::wait_flush_done() {
  if (!trans_done_) return;
  if (xSemaphoreTake(trans_done_, pdMS_TO_TICKS(100)) == pdTRUE) return;
  // Timed out. The ISR may still give the semaphore afterwards, and a binary
  // semaphore holds that token: the NEXT wait would then return immediately
  // on a completion that belongs to THIS transfer, and flush() would rewrite
  // the source and frame buffer while the DSI is still reading them. That is
  // exactly the tearing this wait exists to prevent, and it would never
  // recover on its own. Drain the stale token instead, so a missed deadline
  // costs one late frame rather than permanent corruption.
  ESP_LOGW(TAG, "flush did not complete within 100 ms");
  xSemaphoreTake(trans_done_, 0);
}

void WaveshareXDisplay::init_ldo() {
  esp_ldo_channel_handle_t ldo = nullptr;
  esp_ldo_channel_config_t cfg = {};
  cfg.chan_id = kLdoMipiChan;
  cfg.voltage_mv = kLdoMipiMv;
  ESP_ERROR_CHECK(esp_ldo_acquire_channel(&cfg, &ldo));
}

void WaveshareXDisplay::init_backlight() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.timer_num = LEDC_TIMER_1;
  timer.freq_hz = 5000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer));
  ledc_channel_config_t ch = {};
  ch.gpio_num = kBacklightGpio;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel = LEDC_CHANNEL_1;
  ch.timer_sel = LEDC_TIMER_1;
  ch.duty = 0;
  ch.hpoint = 0;
  // NOT inverted, unlike the 7B: on this board duty is proportional to
  // brightness (Waveshare BSP bsp_display_brightness_set).
  ch.flags.output_invert = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
  set_brightness(95);
}

void WaveshareXDisplay::set_brightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (1023 * pct) / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void WaveshareXDisplay::set_display_on(bool on) {
  if (panel_) esp_lcd_panel_disp_on_off(panel_, on);
}

// ---- GT911 over the IDF i2c_master driver -------------------------------
// Same part and same pins as the 7B, but the coordinates it reports are in
// the panel's native portrait frame and are rotated to landscape in read().

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_gt = nullptr;

// The board's shared I2C bus: created here, reused by the audio codecs.
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

void WaveshareXTouch::init() {
  i2c_master_bus_config_t bus = {};
  bus.i2c_port = I2C_NUM_0;
  bus.sda_io_num = kTouchSda;
  bus.scl_io_num = kTouchScl;
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.flags.enable_internal_pullup = true;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &s_bus));
  for (uint8_t addr : {kGT911Addr, kGT911AddrBackup}) {
    if (i2c_master_probe(s_bus, addr, 100) != ESP_OK) {
      ESP_LOGD(TAG, "no I2C device at 0x%02X", addr);
      continue;
    }
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = addr;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(s_bus, &dev, &s_gt) != ESP_OK) continue;

    uint8_t id[4] = {};
    gt911_read_regs(0x8140, id, 4);
    // This board reports "927", not "911": the X ships a GT927, which is the
    // same GT9xx register map (0x8140 product id, 0x814E status, 0x814F
    // points) and is driven identically. Accept the family, not one part
    // number, or a working controller is rejected as absent.
    if (id[0] == '9' && id[1] >= '0' && id[1] <= '9' && id[2] >= '0' && id[2] <= '9') {
      ESP_LOGI(TAG, "GT%c%c%c touch initialized at 0x%02X", id[0], id[1], id[2], addr);
      gt911_write_reg(kStatusReg, 0x00);
      initialized_ = true;
      break;
    }
    ESP_LOGW(TAG, "device at 0x%02X is not a GT9xx (ID: %c%c%c)", addr, id[0], id[1], id[2]);
    i2c_master_bus_rm_device(s_gt);
    s_gt = nullptr;
  }
  if (!initialized_) ESP_LOGE(TAG, "no GT9xx touch at 0x%02X or 0x%02X", kGT911Addr, kGT911AddrBackup);
}

TouchDriver::TouchPoint WaveshareXTouch::read() {
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
      uint16_t px = t[1] | (t[2] << 8);
      uint16_t py = t[3] | (t[4] << 8);
      // Inverse of the display's 90-degree CCW rotation, so the cockpit sees
      // the same landscape frame for touch as it draws into.
      pt.x = WaveshareXDisplay::kWidth - 1 - py;
      pt.y = px;
      pt.pressed = true;
      if (!last.pressed) ESP_LOGI(TAG, "touch down x=%d y=%d (panel %d,%d n=%d)", pt.x, pt.y, px, py, cnt);
      last = pt;
    }
    gt911_write_reg(kStatusReg, 0x00);
  }
  return pt;
}

}  // namespace cockpit_hal

#endif  // CONFIG_COCKPIT_BOARD_WAVESHARE_LCD_X_7
