#include "wake_overlay.h"

#include "esp_log.h"

static const char* TAG = "jlp.wake";

namespace jlp {

void WakeOverlay::init() {
  lv_obj_t* scr = lv_screen_active();
  root_ = lv_obj_create(scr);
  lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_, 0, LV_PART_MAIN);
  lv_obj_set_scrollable(root_, false);
  // Eat the wake tap so it doesn't fall through to the layout's
  // toggles / buttons underneath.
  lv_obj_set_clickable(root_, true);
  lv_obj_set_hidden(root_, true);

  lv_obj_t* lbl = lv_label_create(root_);
  lv_label_set_text(lbl, "TAP TO WAKE");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xe6edf3), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_36, LV_PART_MAIN);
  lv_obj_center(lbl);
}

void WakeOverlay::show() {
  if (!root_ || visible_) return;
  lv_obj_set_hidden(root_, false);
  // Pull above any later-created siblings (layout swaps re-parent the
  // widget tree under the screen). Alert overlay still wins because
  // it calls move_foreground after us when it's enabled.
  lv_obj_move_foreground(root_);
  visible_ = true;
  ESP_LOGI(TAG, "shown");
}

void WakeOverlay::hide() {
  if (!root_ || !visible_) return;
  lv_obj_set_hidden(root_, true);
  visible_ = false;
  ESP_LOGI(TAG, "hidden");
}

WakeOverlay& wake_overlay() {
  static WakeOverlay w;
  return w;
}

}  // namespace jlp
