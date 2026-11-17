// SPDX-License-Identifier: Apache-2.0
#include "settings_overlay.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>

#include "alert_overlay.h"
#include "cockpit_hal/ui.h"
#include "esp_log.h"
#include "espos_cfg_keys.h"
#include "espos_config.h"
#include "idle_dimmer.h"
#include "theme.h"
#include "wake_overlay.h"
#include "widgets/widget_factory.h"

static const char* TAG = "jlp.settings";

namespace jlp {

namespace {

constexpr int32_t kBrightnessMin = 5;
constexpr int32_t kBrightnessMax = 100;
// Long enough that nobody opens it by resting a hand on the panel in a
// seaway, short enough to find by accident when looking for it.
constexpr uint32_t kHoldMs = 3000;
constexpr int32_t kHoldSlopPx = 24;
// Never leave a menu over the helm: it closes itself when untouched.
constexpr uint32_t kAutoCloseMs = 30000;

constexpr theme::Mode kModes[4] = {theme::Mode::Auto, theme::Mode::Day,
                                   theme::Mode::Dark, theme::Mode::Night};
constexpr const char* kModeLabels[4] = {"Auto", "Day", "Dark", "Night"};

int32_t brightness_setting() {
  int32_t pct = 95;
  espos_config_get_i32(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_BRIGHTNESS, &pct);
  return std::max(kBrightnessMin, std::min(kBrightnessMax, pct));
}

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_scrollable(o, false);
  return o;
}

lv_obj_t* big_button(lv_obj_t* parent, lv_obj_t** label, const char* text) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, 64);
  lv_obj_set_style_radius(b, 10, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(b, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
  *label = lv_label_create(b);
  lv_obj_set_style_text_font(*label, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_label_set_text(*label, text);
  lv_obj_center(*label);
  return b;
}

lv_obj_t* caption(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_label_set_text(l, text);
  return l;
}

// A hold on a control belongs to that control (a momentary button, a
// slider being trimmed, a remote dashboard), so the gesture only counts on
// tiles, labels and the background.
bool is_control(lv_obj_t* obj) {
  for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
    if (is_touch_control(o) ||
        lv_obj_check_type(o, &lv_button_class) ||
        lv_obj_check_type(o, &lv_slider_class) ||
        lv_obj_check_type(o, &lv_switch_class) ||
        lv_obj_check_type(o, &lv_image_class)) {
      return true;
    }
  }
  return false;
}

void hold_cb(lv_event_t* e) {
  auto* indev = static_cast<lv_indev_t*>(lv_event_get_current_target(e));
  const lv_event_code_t code = lv_event_get_code(e);
  static uint32_t start = 0;
  static lv_point_t start_pt = {0, 0};
  static bool armed = false;
  if (code == LV_EVENT_PRESSED) {
    auto* target = static_cast<lv_obj_t*>(lv_event_get_param(e));
    armed = !settings_overlay().is_open() && !wake_overlay().is_visible() &&
            !alert_overlay().is_visible() && !is_control(target);
    start = lv_tick_get();
    lv_indev_get_point(indev, &start_pt);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    armed = false;
  } else if (code == LV_EVENT_LONG_PRESSED_REPEAT && armed) {
    if (lv_tick_elaps(start) < kHoldMs) return;
    armed = false;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (std::abs(p.x - start_pt.x) > kHoldSlopPx ||
        std::abs(p.y - start_pt.y) > kHoldSlopPx) {
      return;
    }
    // Ignore the finger until it lifts: the object under it then gets
    // PRESS_LOST rather than a click, and the menu that opens under the
    // finger is not touched by it.
    lv_indev_wait_release(indev);
    settings_overlay().open();
  }
}

}  // namespace

void SettingsOverlay::init() {
  lv_obj_t* scr = lv_screen_active();
  root_ = plain(scr);
  lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root_, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_clickable(root_, true);
  lv_obj_add_event_cb(
      root_,
      [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
          settings_overlay().close();
        }
      },
      LV_EVENT_CLICKED, nullptr);

  card_ = plain(root_);
  lv_obj_set_size(card_, 620, LV_SIZE_CONTENT);
  lv_obj_center(card_);
  lv_obj_set_style_bg_opa(card_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card_, 14, LV_PART_MAIN);
  lv_obj_set_style_border_width(card_, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card_, 24, LV_PART_MAIN);
  lv_obj_set_style_pad_row(card_, 14, LV_PART_MAIN);
  lv_obj_set_flex_flow(card_, LV_FLEX_FLOW_COLUMN);
  // Taps on the card must not reach the backdrop and close it.
  lv_obj_set_clickable(card_, true);

  lv_obj_t* head = plain(card_);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  title_ = lv_label_create(head);
  lv_obj_set_style_text_font(title_, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_label_set_text(title_, "Settings");
  close_ = big_button(head, &close_lbl_, LV_SYMBOL_CLOSE);
  lv_obj_set_width(close_, 64);
  lv_obj_add_event_cb(
      close_, [](lv_event_t*) { settings_overlay().close(); },
      LV_EVENT_CLICKED, nullptr);

  caption_bright_ = caption(card_, "Brightness");
  lv_obj_t* row = plain(card_);
  lv_obj_set_size(row, LV_PCT(100), 56);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 24, LV_PART_MAIN);
  // Inset so the knob, which overhangs the track, is not clipped.
  lv_obj_set_style_pad_hor(row, 16, LV_PART_MAIN);
  slider_ = lv_slider_create(row);
  lv_obj_set_flex_grow(slider_, 1);
  lv_obj_set_height(slider_, 14);
  lv_slider_set_range(slider_, kBrightnessMin, kBrightnessMax);
  lv_obj_set_style_pad_all(slider_, 12, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(slider_, 0, LV_PART_KNOB);
  lv_obj_set_style_outline_width(slider_, 0, LV_PART_MAIN);
  // Bigger than the visible knob, for a wet thumb.
  lv_obj_set_ext_click_area(slider_, 20);
  bright_val_ = lv_label_create(row);
  lv_obj_set_style_text_font(bright_val_, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_set_width(bright_val_, 70);
  lv_obj_set_style_text_align(bright_val_, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  // Preview on the backlight while dragging, persist on release: the
  // config callback in app_main then hands the level to the idle dimmer.
  lv_obj_add_event_cb(
      slider_,
      [](lv_event_t* e) {
        auto* w = static_cast<lv_obj_t*>(lv_event_get_target(e));
        SettingsOverlay& s = settings_overlay();
        s.dragging_ = true;
        const int32_t v = lv_slider_get_value(w);
        lv_label_set_text_fmt(s.bright_val_, "%d %%", (int)v);
        if (auto* d = cockpit_hal::ui::display()) d->set_brightness((uint8_t)v);
      },
      LV_EVENT_VALUE_CHANGED, nullptr);
  auto commit = [](lv_event_t* e) {
    auto* w = static_cast<lv_obj_t*>(lv_event_get_target(e));
    settings_overlay().dragging_ = false;
    if (espos_config_set_i32(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_BRIGHTNESS,
                             lv_slider_get_value(w)) != ESP_OK) {
      // Nothing was stored, so no callback puts the backlight back.
      ESP_LOGW(TAG, "brightness not saved");
      idle_dimmer().set_on_brightness((uint8_t)brightness_setting());
      settings_overlay().sync_values();
    }
  };
  lv_obj_add_event_cb(slider_, commit, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(slider_, commit, LV_EVENT_PRESS_LOST, nullptr);

  caption_theme_ = caption(card_, "Theme");
  lv_obj_t* themes = plain(card_);
  lv_obj_set_size(themes, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(themes, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(themes, 10, LV_PART_MAIN);
  for (int i = 0; i < 4; ++i) {
    theme_btns_[i] = big_button(themes, &theme_lbls_[i], kModeLabels[i]);
    lv_obj_set_flex_grow(theme_btns_[i], 1);
    lv_obj_add_event_cb(
        theme_btns_[i],
        [](lv_event_t* e) {
          const int idx = (int)(intptr_t)lv_event_get_user_data(e);
          // The config callback in app_main applies it and rebuilds the
          // layout; this only marks the choice right away.
          espos_config_set_str(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_THEME,
                               theme::mode_name(kModes[idx]));
        },
        LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  off_ = big_button(card_, &off_lbl_, LV_SYMBOL_POWER "  Screen off");
  lv_obj_set_width(off_, LV_PCT(100));
  lv_obj_add_event_cb(
      off_,
      [](lv_event_t*) {
        settings_overlay().close();
        idle_dimmer().sleep_now();
      },
      LV_EVENT_CLICKED, nullptr);

  hint_ = caption(card_, "Tap the screen to wake it. Hold a finger still "
                         "for 3 s on a free spot to open this menu.");
  lv_obj_set_width(hint_, LV_PCT(100));
  lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(hint_, &lv_font_montserrat_14, LV_PART_MAIN);

  lv_obj_set_hidden(root_, true);

  if (lv_indev_t* indev = lv_indev_get_next(nullptr)) {
    lv_indev_add_event_cb(indev, hold_cb, LV_EVENT_ALL, nullptr);
  }
  cockpit_hal::ui::every(1000, [] {
    SettingsOverlay& s = settings_overlay();
    if (s.is_open() && lv_display_get_inactive_time(nullptr) > kAutoCloseMs) {
      s.close();
    }
  });
  restyle();
}

void SettingsOverlay::sync_values() {
  const int32_t pct = brightness_setting();
  lv_slider_set_value(slider_, pct, LV_ANIM_OFF);
  lv_label_set_text_fmt(bright_val_, "%d %%", (int)pct);
  style_theme_buttons();
}

void SettingsOverlay::style_theme_buttons() {
  const theme::Palette& p = theme::palette();
  for (int i = 0; i < 4; ++i) {
    const bool on = kModes[i] == theme::g_panel_choice;
    lv_obj_set_style_bg_color(theme_btns_[i], lv_color_hex(on ? p.accent : p.tab),
                              LV_PART_MAIN);
    lv_obj_set_style_text_color(theme_lbls_[i],
                                lv_color_hex(on ? p.on_accent : p.tab_fg),
                                LV_PART_MAIN);
  }
}

void SettingsOverlay::restyle() {
  if (!root_) return;
  const theme::Palette& p = theme::palette();
  lv_obj_set_style_bg_color(card_, lv_color_hex(p.tile), LV_PART_MAIN);
  lv_obj_set_style_border_color(card_, lv_color_hex(p.edge), LV_PART_MAIN);
  lv_obj_set_style_text_color(title_, lv_color_hex(p.fg), LV_PART_MAIN);
  for (lv_obj_t* l : {caption_bright_, caption_theme_, hint_}) {
    lv_obj_set_style_text_color(l, lv_color_hex(p.muted), LV_PART_MAIN);
  }
  lv_obj_set_style_text_color(bright_val_, lv_color_hex(p.fg), LV_PART_MAIN);
  for (lv_obj_t* b : {close_, off_}) {
    lv_obj_set_style_bg_color(b, lv_color_hex(p.tab), LV_PART_MAIN);
  }
  for (lv_obj_t* l : {close_lbl_, off_lbl_}) {
    lv_obj_set_style_text_color(l, lv_color_hex(p.tab_fg), LV_PART_MAIN);
  }
  lv_obj_set_style_bg_color(slider_, lv_color_hex(p.track), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider_, lv_color_hex(p.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider_, lv_color_hex(p.accent), LV_PART_KNOB);
  style_theme_buttons();
}

void SettingsOverlay::open() {
  if (!root_ || open_) return;
  // An alarm owns the screen until it is acknowledged.
  if (alert_overlay().is_visible()) return;
  sync_values();
  lv_obj_set_hidden(root_, false);
  // Above the layout, which is rebuilt under the screen; the alert
  // overlay still moves itself above this when an alarm comes in.
  lv_obj_move_foreground(root_);
  open_ = true;
  ESP_LOGI(TAG, "opened");
}

void SettingsOverlay::close() {
  if (!root_ || !open_) return;
  lv_obj_set_hidden(root_, true);
  open_ = false;
  // A drag cut short (auto-close mid-gesture) must not leave a preview
  // level on the backlight that the setting does not hold.
  if (dragging_) {
    dragging_ = false;
    idle_dimmer().set_on_brightness((uint8_t)brightness_setting());
  }
}

SettingsOverlay& settings_overlay() {
  static SettingsOverlay s;
  return s;
}

}  // namespace jlp
