#include "status_overlay.h"

#include <cstdio>
#include <initializer_list>

#include "settings_overlay.h"
#include "theme.h"

namespace jlp {

namespace {
constexpr int kStripHeight = 28;

lv_obj_t* make_label(lv_obj_t* parent, const char* init) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_label_set_text(l, init);
  return l;
}
}  // namespace

void StatusOverlay::init() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

  strip_ = lv_obj_create(scr);
  lv_obj_set_size(strip_, LV_HOR_RES, kStripHeight);
  lv_obj_align(strip_, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_border_width(strip_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(strip_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(strip_, 4, LV_PART_MAIN);
  lv_obj_set_flex_flow(strip_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip_, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable(strip_, false);

  lbl_host_ = make_label(strip_, "host:?");
  lbl_wifi_ = make_label(strip_, "wifi:?");
  lbl_sk_ = make_label(strip_, "sk:?");
  lbl_n2k_ = make_label(strip_, "n2k:?");
  lbl_sys_ = make_label(strip_, "up 0s heap ?");

  // The settings menu. The strip is only 28 px tall, so the button is wide
  // instead; holding a finger on the layout opens the menu too.
  gear_ = lv_button_create(strip_);
  lv_obj_set_size(gear_, 64, 20);
  lv_obj_set_style_bg_opa(gear_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(gear_, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(gear_, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(gear_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(gear_, 0, LV_PART_MAIN);
  gear_lbl_ = lv_label_create(gear_);
  lv_obj_set_style_text_font(gear_lbl_, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_label_set_text(gear_lbl_, LV_SYMBOL_SETTINGS);
  lv_obj_center(gear_lbl_);
  lv_obj_add_event_cb(
      gear_, [](lv_event_t*) { settings_overlay().open(); }, LV_EVENT_CLICKED,
      nullptr);

  // The rest of the screen — future layouts live here.
  content_root_ = lv_obj_create(scr);
  lv_obj_set_size(content_root_, LV_HOR_RES, LV_VER_RES - kStripHeight);
  lv_obj_align(content_root_, LV_ALIGN_TOP_MID, 0, kStripHeight);
  lv_obj_set_style_border_width(content_root_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(content_root_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(content_root_, 0, LV_PART_MAIN);
  lv_obj_set_scrollable(content_root_, false);

  // Connection-lost banner: a full-width red bar pinned to the bottom,
  // parented to the SCREEN (not the strip) so it shows even when a
  // layout sets status_overlay:false and hides the strip. Hidden until
  // the SK WS reports Disconnected. Created last so it paints above the
  // content; alert_overlay / wake_overlay move_foreground after us, so
  // a full-screen alarm still covers it.
  sk_lost_ = lv_obj_create(scr);
  lv_obj_set_size(sk_lost_, LV_HOR_RES, 40);
  lv_obj_align(sk_lost_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(sk_lost_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(sk_lost_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(sk_lost_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sk_lost_, 4, LV_PART_MAIN);
  lv_obj_set_scrollable(sk_lost_, false);
  lv_obj_set_hidden(sk_lost_, true);
  sk_lost_lbl_ = lv_label_create(sk_lost_);
  lv_obj_set_style_text_font(sk_lost_lbl_, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(sk_lost_lbl_, "Signal K connection lost");
  lv_obj_center(sk_lost_lbl_);

  restyle();
}

void StatusOverlay::restyle() {
  if (!strip_) return;
  const theme::Palette& p = theme::palette();
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(p.screen),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(content_root_, lv_color_hex(p.screen),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_color(strip_, lv_color_hex(p.strip), LV_PART_MAIN);
  for (lv_obj_t* l : {lbl_host_, lbl_wifi_, lbl_sk_, lbl_n2k_, lbl_sys_}) {
    lv_obj_set_style_text_color(l, lv_color_hex(p.fg), LV_PART_MAIN);
  }
  lv_obj_set_style_text_color(gear_lbl_, lv_color_hex(p.muted), LV_PART_MAIN);
  const uint32_t lost = theme::state_color(3);
  lv_obj_set_style_bg_color(sk_lost_, lv_color_hex(lost), LV_PART_MAIN);
  lv_obj_set_style_text_color(sk_lost_lbl_, lv_color_hex(theme::ink_on(lost)),
                              LV_PART_MAIN);
}

void StatusOverlay::set_hostname(const char* hostname) {
  if (!lbl_host_) return;
  lv_label_set_text(lbl_host_, hostname ? hostname : "host:?");
}

void StatusOverlay::set_wifi(const char* line) {
  if (!lbl_wifi_) return;
  lv_label_set_text_fmt(lbl_wifi_, "wifi:%s", line ? line : "?");
}

void StatusOverlay::set_sk(const char* line) {
  if (!lbl_sk_) return;
  lv_label_set_text_fmt(lbl_sk_, "sk:%s", line ? line : "?");
}

void StatusOverlay::set_sk_server(const char* host, uint16_t port) {
  snprintf(sk_server_, sizeof(sk_server_), "%s:%u", host ? host : "?",
           (unsigned)port);
}

void StatusOverlay::show_sk_lost() {
  if (!sk_lost_ || !sk_lost_lbl_) return;
  if (sk_server_[0]) {
    lv_label_set_text_fmt(sk_lost_lbl_,
                          "Signal K connection lost - %s", sk_server_);
  } else {
    lv_label_set_text(sk_lost_lbl_, "Signal K connection lost");
  }
  // Keep above the live content even after layout swaps reparent it.
  lv_obj_move_foreground(sk_lost_);
  lv_obj_set_hidden(sk_lost_, false);
}

void StatusOverlay::hide_sk_lost() {
  if (!sk_lost_) return;
  lv_obj_set_hidden(sk_lost_, true);
}

void StatusOverlay::set_n2k(int64_t rx_idle_seconds, unsigned clients) {
  if (!lbl_n2k_) return;
  if (rx_idle_seconds < 0) {
    lv_label_set_text_fmt(lbl_n2k_, "n2k:- cl:%u", clients);
  } else {
    lv_label_set_text_fmt(lbl_n2k_, "n2k:%llds cl:%u",
                          (long long)rx_idle_seconds, clients);
  }
}

void StatusOverlay::set_uptime_heap(uint32_t uptime_s, uint32_t free_heap) {
  if (!lbl_sys_) return;
  lv_label_set_text_fmt(lbl_sys_, "up %us heap %uK", (unsigned)uptime_s,
                        (unsigned)(free_heap / 1024));
}

void StatusOverlay::set_visible(bool visible) {
  if (visible == visible_) return;
  visible_ = visible;
  if (!strip_ || !content_root_) return;
  if (visible) {
    lv_obj_set_hidden(strip_, false);
    lv_obj_set_size(content_root_, LV_HOR_RES, LV_VER_RES - kStripHeight);
    lv_obj_align(content_root_, LV_ALIGN_TOP_MID, 0, kStripHeight);
  } else {
    lv_obj_set_hidden(strip_, true);
    lv_obj_set_size(content_root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(content_root_, LV_ALIGN_TOP_MID, 0, 0);
  }
}

StatusOverlay& overlay() {
  static StatusOverlay s;
  return s;
}

}  // namespace jlp
