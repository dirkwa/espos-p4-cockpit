#include "alert_overlay.h"

#include <iterator>

#include "esp_log.h"
#include "cockpit_hal/ui.h"
#include "audio/chime.h"
#include "net/sk_put.h"
#include "notifications_registry.h"
#include "zone_registry.h"  // color_for_state

static const char* TAG = "jlp.alert";

namespace jlp {

namespace {

uint32_t color_for_not_state(NotState s) {
  // Reuse the maritime palette from ZoneState. The mapping is
  // direct since NotState and ZoneState use the same names.
  switch (s) {
    case NotState::Nominal:
    case NotState::Normal:    return 0x3fb950;  // green (shouldn't show)
    case NotState::Alert:     return 0xd29922;  // yellow
    case NotState::Warn:      return 0xdb6d28;  // orange
    case NotState::Alarm:     return 0xf85149;  // red
    case NotState::Emergency: return 0xa371f7;  // purple
  }
  return 0xf85149;
}

}  // namespace

namespace {
// Message area: starts below the state + path headings and stops short of the
// ACK button, so a long message can never overlap it. LV_VER_RES resolves at
// runtime, so the height is computed in init() rather than as a constant.
constexpr int kMsgTop = 64;
constexpr int kAckH = 80;
constexpr int kPad = 32;
constexpr int kMsgGap = 12;
}  // namespace

void AlertOverlay::init() {
  // The modal lives on the active screen at z-order = top. Created
  // hidden; rebuild() will show it when there's a qualifying
  // notification to display.
  lv_obj_t* scr = lv_screen_active();
  root_ = lv_obj_create(scr);
  // Lower half only. A full-screen modal hid the whole layout for one line of
  // text and a button -- on a helm the instruments underneath are exactly what
  // you want to keep reading while acknowledging an alarm. It still covers the
  // ACK button's own area, so a stray tap can't reach a widget beneath it.
  lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES / 2);
  lv_obj_align(root_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root_, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_, 32, LV_PART_MAIN);
  lv_obj_set_hidden(root_, true);
  // Stay above future siblings (the layout tree is re-parented
  // under scr on every swap; we keep the overlay above that).
  lv_obj_move_foreground(root_);

  state_label_ = lv_label_create(root_);
  lv_obj_set_style_text_color(state_label_, lv_color_hex(0xf85149),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(state_label_, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_label_set_text(state_label_, "ALARM");
  lv_obj_align(state_label_, LV_ALIGN_TOP_MID, 0, 0);

  path_label_ = lv_label_create(root_);
  lv_obj_set_style_text_color(path_label_, lv_color_hex(0x8b949e),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(path_label_, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(path_label_, "");
  lv_obj_align_to(path_label_, state_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  msg_label_ = lv_label_create(root_);
  lv_obj_set_style_text_color(msg_label_, lv_color_hex(0xe6edf3),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(msg_label_, &lv_font_montserrat_20,
                             LV_PART_MAIN);
  lv_label_set_text(msg_label_, "");
  lv_label_set_long_mode(msg_label_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg_label_, LV_HOR_RES - 80);
  // Bound the height and align to the top of the message area rather than
  // letting the label grow from the centre: on the half-height overlay a
  // four-line message grew far enough down to sit under the ACK button. The
  // region ends where the button begins, and anything longer scrolls.
  lv_obj_set_height(msg_label_,
                    LV_VER_RES / 2 - 2 * kPad - kAckH - kMsgTop - kMsgGap);
  lv_obj_align(msg_label_, LV_ALIGN_TOP_MID, 0, kMsgTop);
  lv_obj_set_style_text_align(msg_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  ack_button_ = lv_button_create(root_);
  lv_obj_set_size(ack_button_, 220, 80);
  lv_obj_align(ack_button_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(ack_button_, lv_color_hex(0x3fb950), LV_PART_MAIN);
  lv_obj_set_style_radius(ack_button_, 8, LV_PART_MAIN);
  lv_obj_t* ack_lbl = lv_label_create(ack_button_);
  lv_obj_set_style_text_color(ack_lbl, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_text_font(ack_lbl, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_label_set_text(ack_lbl, "ACK");
  lv_obj_center(ack_lbl);

  lv_obj_add_event_cb(
      ack_button_,
      [](lv_event_t*) {
        AlertOverlay& self = alert_overlay();
        if (self.current_path_.empty()) return;
        // Pop-under-finger guard: alarms pop over the lower half of the
        // screen — exactly where a stream/chart tab is being touched. A
        // tap that lands right after the overlay appeared (or switched
        // to the next alarm) was almost certainly aimed at the widget
        // underneath, not at ACK; with a dozen bus alarms rotating, that
        // turns into accidental ack whack-a-mole. Ignore it.
        if (lv_tick_elaps(self.shown_at_) < 500) return;
        // acknowledge() fires on_change(), which rebuilds the overlay
        // and moves current_path_ on — take a copy first so the SK ACK
        // names the alarm the operator actually tapped.
        const std::string path = self.current_path_;
        // Local ack: suppress this path in the registry so the
        // overlay dismisses NOW and the device stays usable, even
        // though the N2K bus keeps re-asserting the alarm. Re-arms
        // when the condition clears or escalates (see the registry).
        //
        // The server ack is sent only while the path hasn't proven to
        // re-assert: for a bus-backed alarm our state=normal delta just
        // triggers a clear -> re-raise flap network-wide (see
        // NotificationsRegistry::acknowledge).
        if (notifications().acknowledge(path)) {
          put_notification_ack(path);
        }
      },
      LV_EVENT_CLICKED, nullptr);

  notifications().on_change([this]() {
    // Sound BEFORE rebuild, and only on an escalation (a notification
    // appeared or worsened) — never on a clear, ack, or message edit,
    // so acking one alarm doesn't re-chime the next-most-severe. Gate
    // on the same enabled_/min_state_ the overlay shows, so a muted or
    // below-threshold notification stays silent. configure()'s rebuild
    // (layout swaps) doesn't run through here, so swaps are silent too.
    // Chime only when the alert that ESCALATED asked for sound (via its
    // SK `method`) — not most_severe(), which could be a different,
    // quieter alert. A visual-only or server-silenced alert shows on the
    // overlay but stays quiet. Still gate on min_state via most_severe so
    // a below-threshold escalation doesn't beep.
    // ... and never for an escalation on an already-acked path: a
    // bus-backed alarm flaps back to its acked severity after every
    // ack-clear echo, and re-beeping an alarm the operator just
    // silenced is the one thing an ack must reliably prevent.
    // Whether the escalated level sounds is the layout's choice per level
    // (notifications.levels): off, as the source's `method` asks, or always.
    if (enabled_ && notifications().last_change_was_escalation() &&
        !notifications().is_acknowledged(notifications().last_changed_path())) {
      const Notification* n = notifications().find(notifications().last_changed_path());
      if (n && sounds(*n)) play(*n);
    }
    rebuild();
  });

  // Repeats for levels with repeat_s, until acked, cleared or muted.
  cockpit_hal::ui::every(1000, [this]() { repeat_tick(); });

  ESP_LOGI(TAG, "alert overlay initialised");
}

LevelPolicies policies_from_min_state(NotState min_state) {
  LevelPolicies p{};
  for (size_t i = 0; i < p.size(); ++i) {
    const auto s = static_cast<NotState>(static_cast<int>(NotState::Alert) + static_cast<int>(i));
    p[i].show = s >= min_state;
    p[i].sound = s >= min_state ? SoundPolicy::Source : SoundPolicy::Off;
  }
  return p;
}

void AlertOverlay::configure(bool enabled, const LevelPolicies& levels) {
  enabled_ = enabled;
  levels_ = levels;
  show_mask_ = 0;
  for (size_t i = 0; i < levels_.size(); ++i) {
    if (levels_[i].show) {
      show_mask_ |= static_cast<uint8_t>(1u << (static_cast<unsigned>(NotState::Alert) + i));
    }
  }
  rebuild();
}

const LevelPolicy* AlertOverlay::policy(NotState s) const {
  if (s < NotState::Alert) return nullptr;  // nominal / normal never alert
  return &levels_[static_cast<size_t>(s) - static_cast<size_t>(NotState::Alert)];
}

bool AlertOverlay::sounds(const Notification& n) const {
  const LevelPolicy* p = policy(n.state);
  if (!p) return false;
  return p->sound == SoundPolicy::Always ||
         (p->sound == SoundPolicy::Source && n.wants_sound);
}

bool AlertOverlay::wants_attention(const Notification& n) const {
  const LevelPolicy* p = policy(n.state);
  // A hidden level that would only chime has nothing to offer a muted
  // panel: waking it would light an empty screen.
  return enabled_ && p != nullptr && (p->show || (sounds(n) && !chime().muted()));
}

void AlertOverlay::play(const Notification& n) {
  chime().play(n.state);
  last_chime_[n.path] = lv_tick_get();
}

void AlertOverlay::repeat_tick() {
  if (!enabled_) return;
  // Forget paths that are no longer pending (acked, cleared, gone), so a
  // later alarm on the same path starts its own count.
  for (auto it = last_chime_.begin(); it != last_chime_.end();) {
    const Notification* n = notifications().find(it->first);
    const bool pending = n && n->state >= NotState::Alert &&
                         !notifications().is_acknowledged(it->first);
    it = pending ? std::next(it) : last_chime_.erase(it);
  }
  // One repeat at a time, since clips from several at once would only
  // queue up: the due one that is most overdue, the most severe on a tie.
  // Picking by severity alone would let a pending emergency with a short
  // interval starve every other level's repeat.
  const Notification* best = nullptr;
  uint32_t best_overdue = 0;
  for (const auto& kv : last_chime_) {
    const Notification* n = notifications().find(kv.first);
    const LevelPolicy* p = n != nullptr ? policy(n->state) : nullptr;
    if (p == nullptr || p->repeat_s == 0 || !sounds(*n)) continue;
    const uint32_t elapsed = lv_tick_elaps(kv.second);
    const uint32_t interval = p->repeat_s * 1000u;
    if (elapsed < interval) continue;
    const uint32_t overdue = elapsed - interval;
    if (best == nullptr || overdue > best_overdue ||
        (overdue == best_overdue && n->state > best->state)) {
      best = n;
      best_overdue = overdue;
    }
  }
  if (best != nullptr) play(*best);
}

void AlertOverlay::rebuild() {
  if (!root_) return;
  lv_obj_move_foreground(root_);  // re-pin above any newly-swapped layout
  if (!enabled_) {
    lv_obj_set_hidden(root_, true);
    current_path_.clear();
    return;
  }
  const Notification* n = notifications().most_severe(show_mask_);
  if (!n) {
    lv_obj_set_hidden(root_, true);
    current_path_.clear();
    return;
  }
  // Arm the pop-under-finger guard whenever the overlay appears or moves
  // on to a different alarm — that's the moment a tap meant for the
  // widget underneath could land on ACK instead.
  if (lv_obj_is_hidden(root_) || current_path_ != n->path) {
    shown_at_ = lv_tick_get();
  }
  current_path_ = n->path;
  lv_label_set_text(state_label_, not_state_name(n->state));
  lv_obj_set_style_text_color(state_label_,
                              lv_color_hex(color_for_not_state(n->state)),
                              LV_PART_MAIN);
  lv_label_set_text(path_label_, n->path.c_str());
  lv_label_set_text(msg_label_, n->message.empty() ? "(no message)"
                                                   : n->message.c_str());
  lv_obj_set_hidden(root_, false);
}

AlertOverlay& alert_overlay() {
  static AlertOverlay r;
  return r;
}

}  // namespace jlp
