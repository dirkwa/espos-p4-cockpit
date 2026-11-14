// SPDX-License-Identifier: Apache-2.0
#pragma once

// Panel color themes: day (light, for sunlight), dark and night (red on
// black, keeps night vision). Header-only so the WASM preview, which
// compiles widget_factory.cpp but not the rest of the firmware, gets the
// same palettes without a second copy.

#include <stdint.h>
#include <strings.h>

#include <atomic>

#include "lvgl.h"

namespace jlp::theme {

enum class Mode : uint8_t { Auto, Day, Dark, Night };

// What environment.mode last said. Unknown until the first value, which
// resolves like day so the helm stays bright without a server.
enum class Sky : uint8_t { Unknown, Day, Night };

struct Palette {
  Mode mode;
  uint32_t screen;      // layout background
  uint32_t tile;        // widget tile fill
  uint32_t edge;        // tile border, list rules
  uint32_t fg;          // primary text
  uint32_t muted;       // captions, units
  uint32_t accent;      // indicators, active tab
  uint32_t on_accent;   // text on accent fills
  uint32_t track;       // empty part of bars, arcs and rings
  uint32_t strip;       // tab strip and status strip
  uint32_t tab;         // inactive tab button
  uint32_t tab_fg;      // inactive tab text
  // Zone / notification fills, in ZoneState order from normal up:
  // normal, alert, warn, alarm, emergency.
  uint32_t zone[5];
  bool dark_base;       // LVGL default theme variant for unstyled parts
};

// Day: navy and white, the espOS blue as accent. The zone colors are one
// shade deeper than on the dark theme so they hold up on white.
inline constexpr Palette kDay{
    Mode::Day, 0xeef1f6, 0xffffff, 0xdde3ee, 0x0b1630, 0x5b6680,
    0x003399, 0xffffff, 0xdfe4ee, 0xffffff, 0xeef1f6, 0x5b6680,
    {0x1f8a3b, 0xb88400, 0xc75a12, 0xd32f2f, 0x7b3fc4},
    false};

// Dark: deep navy with the espOS signal yellow as accent, full maritime
// zone palette.
inline constexpr Palette kDark{
    Mode::Dark, 0x0a1022, 0x131c33, 0x223052, 0xeef2fa, 0x8a96b3,
    0xfbca02, 0x0a1022, 0x24304d, 0x0f1729, 0x1a2643, 0xc5cde0,
    {0x3fb950, 0xd29922, 0xdb6d28, 0xf85149, 0xa371f7},
    true};

// Night: red on black. Zones become red shades that grow brighter with
// severity; only alarm and emergency are bright enough to cost night
// vision, which is the point of them. Tiles fill only for those two (see
// paint_tile_zone); the lower shades color indicators and list rows.
inline constexpr Palette kNight{
    Mode::Night, 0x000000, 0x120303, 0x2e0808, 0xe0322b, 0x8a1f1a,
    0xc62828, 0x000000, 0x2a0707, 0x0a0101, 0x1a0404, 0x8a1f1a,
    {0x8f2219, 0xa8261d, 0xc22a1f, 0xff3b30, 0xff6a5c},
    true};

// Written on the SignalK stream task, read on the UI task.
inline std::atomic<Sky> g_sky{Sky::Unknown};
// UI task only.
inline Mode g_layout_mode = Mode::Auto;     // layout `theme.mode`
inline Mode g_day_base = Mode::Day;         // layout `theme.day_base`
inline Mode g_panel_choice = Mode::Auto;    // panel setting / preview, wins

inline bool parse_mode(const char* s, Mode* out) {
  if (!s) return false;
  if (!strcasecmp(s, "auto")) { *out = Mode::Auto; return true; }
  if (!strcasecmp(s, "day")) { *out = Mode::Day; return true; }
  if (!strcasecmp(s, "dark")) { *out = Mode::Dark; return true; }
  if (!strcasecmp(s, "night")) { *out = Mode::Night; return true; }
  return false;
}

inline const char* mode_name(Mode m) {
  switch (m) {
    case Mode::Day: return "day";
    case Mode::Dark: return "dark";
    case Mode::Night: return "night";
    case Mode::Auto: default: return "auto";
  }
}

// The theme actually shown: the panel's own choice, else the layout's,
// with auto following the sun.
inline Mode resolved() {
  Mode want = g_panel_choice != Mode::Auto ? g_panel_choice : g_layout_mode;
  if (want != Mode::Auto) return want;
  if (g_sky.load() == Sky::Night) return Mode::Night;
  return g_day_base == Mode::Dark ? Mode::Dark : Mode::Day;
}

inline const Palette& palette_for(Mode m) {
  switch (m) {
    case Mode::Dark: return kDark;
    case Mode::Night: return kNight;
    default: return kDay;
  }
}

inline const Palette& palette() { return palette_for(resolved()); }

// Zone fill for ZoneState / NotState index 0..4 (normal..emergency).
inline uint32_t state_color(int idx) {
  if (idx < 0) idx = 0;
  if (idx > 4) idx = 4;
  return palette().zone[idx];
}

// Text color that reads on a zone or accent `fill`. At night every red
// fill takes black text; the near-black surfaces keep the night red.
inline uint32_t ink_on(uint32_t fill) {
  const Palette& p = palette();
  if (p.mode == Mode::Night) {
    return ((fill >> 16) & 0xff) >= 0x88 ? 0x000000 : p.fg;
  }
  uint32_t r = (fill >> 16) & 0xff, g = (fill >> 8) & 0xff, b = fill & 0xff;
  uint32_t y = (r * 299 + g * 587 + b * 114) / 1000;  // BT.601
  return y >= 150 ? 0x0b1630 : 0xffffff;
}

// Re-initialise LVGL's default theme so parts the widgets leave unstyled
// (switch tracks, scrollbars) match the palette. LVGL refreshes every
// object using those styles, so only do it when the palette changed.
inline void sync_lvgl_theme() {
  static Mode applied = Mode::Auto;  // never a resolved mode: first call runs
  const Palette& p = palette();
  if (p.mode == applied) return;
  lv_display_t* d = lv_display_get_default();
  if (!d) return;
  lv_theme_t* t = lv_theme_default_init(d, lv_color_hex(p.accent),
                                        lv_color_hex(p.muted), p.dark_base,
                                        LV_FONT_DEFAULT);
  if (t) lv_display_set_theme(d, t);
  applied = p.mode;
}

}  // namespace jlp::theme
