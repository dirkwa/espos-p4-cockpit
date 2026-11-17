// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include "lvgl.h"

namespace jlp {

// The panel's own settings, reachable without the designer or a browser:
// backlight brightness, the color theme and screen off. Opened from the
// gear in the status strip, or by holding a finger still off any control for
// three seconds, which also works when a layout hides the strip.
//
// Values go through espos_config (cockpit.brightness, cockpit.theme), so
// the config web UI, the designer and this menu always agree; the change
// callbacks in app_main apply them. Process-lifetime singleton on the UI
// task, like the other overlays.
class SettingsOverlay {
 public:
  void init();
  void open();
  void close();
  bool is_open() const { return open_; }
  // Re-color from the active theme; init() and every theme change.
  void restyle();

 private:
  void sync_values();
  void style_theme_buttons();

  lv_obj_t* root_ = nullptr;      // dimmed backdrop, tap closes
  lv_obj_t* card_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* close_ = nullptr;
  lv_obj_t* close_lbl_ = nullptr;
  lv_obj_t* caption_bright_ = nullptr;
  lv_obj_t* slider_ = nullptr;
  lv_obj_t* bright_val_ = nullptr;
  lv_obj_t* caption_theme_ = nullptr;
  lv_obj_t* theme_btns_[4] = {};
  lv_obj_t* theme_lbls_[4] = {};
  lv_obj_t* off_ = nullptr;
  lv_obj_t* off_lbl_ = nullptr;
  lv_obj_t* hint_ = nullptr;
  bool open_ = false;
  bool dragging_ = false;
};

SettingsOverlay& settings_overlay();

}  // namespace jlp
