// SPDX-License-Identifier: Apache-2.0
//
// What a layout asks of the alert overlay per notification state. Header
// only, so the layout player can read a layout's `notifications` block in
// the WASM build without linking the overlay, which needs the device's
// audio.
#pragma once

#include <stdint.h>
#include <array>

#include "notifications_registry.h"

namespace jlp {

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
inline LevelPolicies policies_from_min_state(NotState min_state) {
  LevelPolicies p{};
  for (size_t i = 0; i < p.size(); ++i) {
    const auto s = static_cast<NotState>(static_cast<int>(NotState::Alert) + static_cast<int>(i));
    p[i].show = s >= min_state;
    p[i].sound = s >= min_state ? SoundPolicy::Source : SoundPolicy::Off;
  }
  return p;
}

}  // namespace jlp
