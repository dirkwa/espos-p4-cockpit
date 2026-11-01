/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace cockpit_hal {

class DisplayDriver {
 public:
  virtual ~DisplayDriver() = default;
  virtual void init() = 0;
  virtual uint16_t width() const = 0;
  virtual uint16_t height() const = 0;
  virtual void* get_draw_buffer(int index) = 0;
  virtual size_t get_draw_buffer_size() = 0;
  virtual void flush(int x, int y, int w, int h, const void* buf) = 0;
  /// Block until the last flush() has actually consumed the source buffer.
  /// Panels whose flush() is synchronous can leave this empty.
  virtual void wait_flush_done() {}
  virtual void set_brightness(uint8_t /*pct*/) {}
  virtual void set_display_on(bool /*on*/) {}
};

class TouchDriver {
 public:
  virtual ~TouchDriver() = default;
  virtual void init() = 0;
  struct TouchPoint {
    uint16_t x;
    uint16_t y;
    bool pressed;
  };
  virtual TouchPoint read() = 0;
};

}  // namespace cockpit_hal
