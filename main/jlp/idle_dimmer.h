#pragma once

#include <stdint.h>

namespace jlp {

// Backlight power-save manager. Polls LVGL's input-inactivity counter
// once per second on the event_loop task and turns the panel backlight
// off after the configured idle timeout. Any touch event resets the
// counter automatically (LVGL does this), notifications and layout
// pushes wake explicitly via wake().
//
// 0 = disabled (backlight always on at on_brightness).
class IdleDimmer {
 public:
  // Wires the 1 Hz poll into event_loop. Call once at boot, after
  // lvgl_init().
  void init();

  // Reconfigure from a layout. Re-arms the idle window if currently
  // dimmed. dim_pct is the brightness while idle (0 = fully off,
  // tap-wake still works). The dimmer also shows a "tap to wake"
  // overlay while dimmed so the first wake-tap can't accidentally
  // hit a toggle or button underneath.
  void configure(uint32_t idle_timeout_sec, uint8_t dim_pct);

  // Force the backlight on and re-arm the idle timer. Called from
  // notification deltas and layout pushes.
  void wake();

  // The brightness used while awake. Follows the `cockpit.brightness`
  // setting: without this the dimmer would restore its own hardcoded
  // default on the next wake and undo a brightness the user just set.
  // Applies immediately when the panel is currently on.
  void set_on_brightness(uint8_t pct);
  uint8_t on_brightness() const { return on_brightness_pct_; }

  bool is_on() const { return on_; }
  uint32_t idle_timeout_sec() const { return idle_timeout_sec_; }
  uint8_t dim_pct() const { return dim_pct_; }

 private:
  void set_on(bool on);

  uint32_t idle_timeout_sec_ = 0;   // 0 = disabled
  uint8_t on_brightness_pct_ = 95;  // overwritten from config at boot
  // 0 = fully off by default. Once the wake overlay covers the layout
  // the user can't accidentally trigger anything underneath, and the
  // GT911 still detects the first tap fine through the dark panel
  // (the inverted-brightness bug that made earlier tests fail is gone).
  uint8_t dim_pct_ = 0;
  bool on_ = true;
};

IdleDimmer& idle_dimmer();

}  // namespace jlp
