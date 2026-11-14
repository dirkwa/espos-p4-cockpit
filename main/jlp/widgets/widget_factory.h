#pragma once

#include "lvgl.h"
#include <ArduinoJson.h>
#include <cstdint>
#include <set>
#include <string>

#include "../theme.h"

namespace jlp {

class SubjectRegistry;

struct BuildCtx {
  lv_obj_t* parent;
  SubjectRegistry& reg;
  std::set<std::string>& live_paths;
};

// Construct a single widget from `spec` under `ctx.parent`. Returns the
// new widget root, or nullptr on validation failure (with the cause in
// `*err`). Caller owns failure recovery (tear down the build, keep the
// previous screen).
lv_obj_t* build_widget(BuildCtx& ctx, JsonObjectConst spec, std::string* err);

// Parse "#rrggbb" or "#rgb" into a 24-bit hex color. Returns true on
// success; on failure (missing field, malformed) leaves *out untouched.
// Exported so layout_manager can reuse it for the screen background,
// which lives outside a widget spec.
bool parse_hex_color(const char* s, uint32_t* out);

// The theme colors currently in effect, for save/restore around a layout
// build that may fail. apply_theme() has to run BEFORE widgets are built so
// they pick up the new colors, but a build that fails leaves the previous
// layout on screen -- its still-live widgets would then read the rejected
// layout's theme on their next update.
struct ThemeColors {
  bool fg_set = false;
  uint32_t fg = 0;
  bool accent_set = false;
  uint32_t accent = 0;
  theme::Mode mode = theme::Mode::Auto;
  theme::Mode day_base = theme::Mode::Day;
};
ThemeColors current_theme();
void restore_theme(const ThemeColors& t);

// Take the layout's top-level `theme`: `mode` (auto/day/dark/night),
// `day_base` (what auto shows by day) and the fg/accent overrides. A null
// object (or one missing a field) resets that field to the default rather
// than carrying over the previous layout's. Re-initialises LVGL's default
// theme, so call it before building widgets.
void apply_theme(JsonObjectConst theme);

// Tell any "stream" widgets that are direct children of `container` whether
// their screen is visible. Streaming runs ONLY while visible; the layout
// manager calls this from the screen switcher and after a layout swap.
// UI thread only.
void stream_widgets_notify_visibility(lv_obj_t* container, bool visible);

}  // namespace jlp
