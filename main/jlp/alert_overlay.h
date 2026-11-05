#pragma once

#include <stdint.h>
#include <array>
#include <string>
#include <unordered_map>

#include "lvgl.h"
#include "notifications_registry.h"

namespace jlp {

/** Full-screen modal that pops above the active screen (and above
 *  the status overlay strip) whenever the notifications registry
 *  reports a pending notification with state >= min_state. Tapping
 *  ACK PUTs `state: "normal"` to the same path and the overlay
 *  re-evaluates; if more notifications remain it refills with the
 *  next-most-severe, else hides.
 *
 *  Created once at boot, configured by each LayoutManager::apply()
 *  from the layout's optional top-level `notifications` block. */
/** When a level sounds: never, when the notification's SK `method` asks
 *  for "sound" (the default), or always. */
enum class SoundPolicy : uint8_t { Off, Source, Always };

/** What the layout's `notifications.levels.<state>` asks of one state. */
struct LevelPolicy {
  bool show = false;                    // the overlay pops for it
  SoundPolicy sound = SoundPolicy::Off;
  uint16_t repeat_s = 0;                // chime again until acked; 0 = once
};

/** Policies for alert, warn, alarm, emergency, in that order. */
using LevelPolicies = std::array<LevelPolicy, 4>;

/** The policies a layout gets from `min_state` alone: shown and sounding
 *  (as the source asks) from that state up, nothing below, no repeat. */
LevelPolicies policies_from_min_state(NotState min_state);

class AlertOverlay {
 public:
  void init();

  /** Re-configure from a layout block. Safe to call on every
   *  layout swap. */
  void configure(bool enabled, const LevelPolicies& levels);

  /** Whether `n` is meant to reach the operator here: its level shows or
   *  would sound. Only such a notification wakes a dimmed panel. */
  bool wants_attention(const Notification& n) const;

 private:
  void rebuild();
  const LevelPolicy* policy(NotState s) const;
  bool sounds(const Notification& n) const;
  void play(const Notification& n);
  void repeat_tick();
  void sync_mutes();

  lv_obj_t* root_ = nullptr;      // hidden tile that covers the screen
  lv_obj_t* path_label_ = nullptr;
  lv_obj_t* state_label_ = nullptr;
  lv_obj_t* msg_label_ = nullptr;
  lv_obj_t* ack_button_ = nullptr;
  std::string current_path_;       // path-after-prefix shown right now
  // lv_tick when the overlay appeared or switched alarms; taps inside
  // the guard window after that are ignored (pop-under-finger).
  uint32_t shown_at_ = 0;

  bool enabled_ = true;
  LevelPolicies levels_ = policies_from_min_state(NotState::Alarm);
  uint8_t show_mask_ = 0;  // bit (1 << state) per shown level
  // lv_tick of the last chime per path, for repeat_s.
  std::unordered_map<std::string, uint32_t> last_chime_;
  uint32_t mutes_seen_ = 0;  // chime().mutes() when last_chime_ was checked
};

AlertOverlay& alert_overlay();

}  // namespace jlp
