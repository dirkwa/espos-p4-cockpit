#include "mic_overlay.h"

#include "audio/voice_control.h"
#include "cockpit_hal/ui.h"
#include "esp_log.h"

static const char* TAG = "jlp.mic_ov";

namespace jlp {
namespace {
// Big enough to read across a cockpit, small enough not to hide a gauge.
constexpr int kSize = 132;
constexpr int kMargin = 24;
}  // namespace

void MicOverlay::init() {
  lv_obj_t* scr = lv_screen_active();
  root_ = lv_obj_create(scr);
  lv_obj_set_size(root_, kSize, kSize);
  // Top-right: the helm's status corner, and clear of the notification
  // list which grows from the left.
  lv_obj_align(root_, LV_ALIGN_TOP_RIGHT, -kMargin, kMargin);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x10307a), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root_, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_radius(root_, kSize / 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(root_, lv_color_hex(0x63b3ff), LV_PART_MAIN);
  lv_obj_set_style_border_width(root_, 4, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);
  lv_obj_set_scrollable(root_, false);
  // Purely informational: never eat a touch meant for the layout beneath.
  lv_obj_set_clickable(root_, false);
  lv_obj_set_hidden(root_, true);

  auto plain = [](lv_obj_t* o, uint32_t colour, int radius) {
    lv_obj_set_style_bg_color(o, lv_color_hex(colour), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
  };

  constexpr uint32_t kInk = 0xe6edf3;  // the mic itself
  constexpr uint32_t kBg = 0x10307a;   // overlay background, for cut-outs
  constexpr int kCapW = 46;
  constexpr int kCapH = 62;

  base_ = lv_obj_create(root_);
  lv_obj_set_size(base_, 52, 7);
  lv_obj_align(base_, LV_ALIGN_CENTER, 0, 48);
  plain(base_, kInk, 3);

  stem_ = lv_obj_create(root_);
  lv_obj_set_size(stem_, 11, 16);
  lv_obj_align(stem_, LV_ALIGN_CENTER, 0, 36);
  plain(stem_, kInk, 0);

  // Filled box with a smaller box punched out, leaving the U: cheaper than an
  // arc and holds its silhouette at any size.
  yoke_ = lv_obj_create(root_);
  lv_obj_set_size(yoke_, 74, 52);
  lv_obj_align(yoke_, LV_ALIGN_CENTER, 0, 6);
  plain(yoke_, kInk, 26);

  yoke_cut_ = lv_obj_create(yoke_);
  lv_obj_set_size(yoke_cut_, 54, 40);
  lv_obj_align(yoke_cut_, LV_ALIGN_TOP_MID, 0, -2);
  plain(yoke_cut_, kBg, 20);

  capsule_ = lv_obj_create(root_);
  lv_obj_set_size(capsule_, kCapW, kCapH);
  lv_obj_align(capsule_, LV_ALIGN_CENTER, 0, -22);
  plain(capsule_, kInk, kCapW / 2);

  constexpr int kDot = 5;
  constexpr int kStepX = 10;
  constexpr int kStepY = 9;
  for (int row = 0; row < 5; ++row) {
    const bool odd = (row % 2) == 1;
    const int cols = odd ? 3 : 4;
    for (int col = 0; col < cols; ++col) {
      lv_obj_t* d = lv_obj_create(capsule_);
      lv_obj_set_size(d, kDot, kDot);
      const int x = (col - (cols - 1) / 2.0f) * kStepX;
      const int y = (row - 2) * kStepY;
      lv_obj_align(d, LV_ALIGN_CENTER, x, y);
      plain(d, kBg, kDot / 2);
    }
  }

  // Poll rather than take a callback: state() is atomic and the mic widget
  // already reads it this way, so there is no new cross-task contract. 100 ms
  // is well under the reaction time this is meant to confirm.
  //
  // ui::every, not lv_timer_create: the house helper takes the LVGL lock the
  // rest of the UI code relies on.
  cockpit_hal::ui::every(100, [] {
    mic_overlay().set_visible(voice().state_code() == 2);  // 2 = listening
  });
  ESP_LOGI(TAG, "init: overlay ready");
}

void MicOverlay::set_visible(bool on) {
  if (!root_) return;
  if (on != visible_) {
    if (on) {
      lv_obj_set_hidden(root_, false);
      // Layout swaps re-parent the widget tree under the screen, so re-assert
      // z-order on every show. The alert overlay calls move_foreground when it
      // pops, so an alarm still covers this.
      lv_obj_move_foreground(root_);
    } else {
      lv_obj_set_hidden(root_, true);
    }
    visible_ = on;
    pulse_ = 0;
    ESP_LOGI(TAG, "%s", on ? "listening" : "idle");
  }
  if (!visible_) return;
  // Breathe the ring so it reads as live rather than a frozen icon -- a static
  // badge is easy to mistake for a stuck overlay.
  pulse_ = (uint8_t)((pulse_ + 1) % 20);
  const bool bright = pulse_ < 10;
  lv_obj_set_style_border_color(
      root_, lv_color_hex(bright ? 0x9ed0ff : 0x2b6cb0), LV_PART_MAIN);
}

MicOverlay& mic_overlay() {
  static MicOverlay m;
  return m;
}

}  // namespace jlp
