#pragma once

#include "lvgl.h"

namespace jlp {

/** Full-screen "tap to wake" overlay shown while the backlight is in
 *  the idle-dim state. Sits above all widgets so the first tap that
 *  wakes the panel can't accidentally trigger a toggle or button
 *  underneath. Stays below the alert overlay so notifications still
 *  pop on top.
 *
 *  Created once at boot. Idle dimmer calls show() / hide() as it
 *  transitions on/off. */
class WakeOverlay {
 public:
  void init();
  void show();
  void hide();
  bool is_visible() const { return visible_; }
  // Re-color from the active theme; init() and every theme change.
  void restyle();

 private:
  lv_obj_t* root_ = nullptr;
  lv_obj_t* lbl_ = nullptr;
  bool visible_ = false;
};

WakeOverlay& wake_overlay();

}  // namespace jlp
