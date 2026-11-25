// SPDX-License-Identifier: Apache-2.0
//
// The layout engine without the device around it: parse, validate, build
// under a hidden staging parent, swap, plus the tab strip and page swipes.
// Besides LVGL and ArduinoJson it uses the widget factory, the subject
// registry, the theme, the alert level policies and the UI task's timers
// (cockpit_hal::ui::every/cancel), all of which the WASM build provides,
// so the browser runs this same code. Persistence, the status strip, the
// alert overlay, the backlight dimmer and value seeding stay with
// LayoutManager, which drives the player.
#pragma once

#include <ArduinoJson.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "lvgl.h"

#include "../alert_policy.h"
#include "../theme.h"

namespace jlp {

// tab_strip values this player draws: what the layout parser accepts
// and what /hello advertises.
inline constexpr const char* kTabStripFixed = "fixed";
inline constexpr const char* kTabStripOverlayBottom = "overlay-bottom";
inline constexpr const char* kTabStripModes[] = {kTabStripFixed,
                                                 kTabStripOverlayBottom};

// The layout's top-level `notifications` block, read the same way on every
// host that shows the alert overlay.
struct AlertConfig {
  bool enabled = true;
  LevelPolicies levels = policies_from_min_state(NotState::Alarm);
};
AlertConfig parse_alert_config(JsonObjectConst doc);

class LayoutPlayer {
 public:
  LayoutPlayer() = default;
  LayoutPlayer(const LayoutPlayer&) = delete;
  LayoutPlayer& operator=(const LayoutPlayer&) = delete;
  // Unhooks the press watcher and the peek timer, which point at this.
  ~LayoutPlayer();

  struct Result {
    bool ok = false;
    std::string err;
    std::string name;  // root "name" field on success
    unsigned screens = 0;
    unsigned widgets = 0;
    std::set<std::string> live_paths;  // every path the layout binds
  };

  // Runs with the validated layout after its tree is built and before it
  // replaces the old one, so layout-level chrome (status strip, alert
  // overlay, dimmer) changes in the same step as the widgets. Not called
  // when the build fails. `theme_rebuild` is true when the same layout is
  // rebuilt for another theme.
  using ChromeHook = std::function<void(JsonObjectConst doc, bool theme_rebuild)>;

  // `parent` is where the screens go. Presses are watched on the first
  // input device, so create it before calling this. Calling it again
  // re-hooks instead of hooking twice. UI TASK ONLY, as is everything
  // below except built_mode().
  void init(lv_obj_t* parent, ChromeHook chrome);

  // Builds `json` and swaps it in. On failure the current layout stays up.
  Result apply(const std::string& json);

  // Rebuilds the active layout if the theme to show changed, keeping the
  // selected tab. ok with no rebuild when nothing changed or nothing is
  // applied yet.
  Result rebuild_for_theme();

  bool has_layout() const { return !active_json_.empty(); }

  // Theme the active layout was built in. Safe from any task.
  theme::Mode built_mode() const { return built_mode_.load(); }

  // Remote equivalent of tapping a tab, by the layout's `screens[].id`
  // because the index is not stable across layout edits. False if the id
  // is unknown or the layout is single-screen (it has no tab strip).
  bool select_screen(const std::string& id);

  // Id of the screen shown; empty for a single-screen layout.
  std::string active_screen() const;

  // Screen ids in tab order; empty for a single-screen layout.
  std::vector<std::string> screen_ids() const;

 private:
  Result apply_impl(const std::string& json, bool theme_rebuild);

  void detach();
  static void on_press_event(lv_event_t* e);
  void on_press();
  void track_swipe();
  void cancel_swipe();
  void end_press();

  ChromeHook chrome_;
  // The applied layout's JSON, kept to rebuild it in another theme.
  std::string active_json_;
  // Written on the UI task, read by /hello on the HTTP task.
  std::atomic<theme::Mode> built_mode_{theme::Mode::Day};

  lv_obj_t* parent_ = nullptr;
  lv_obj_t* current_root_ = nullptr;
  lv_indev_t* indev_ = nullptr;
  lv_point_t press_{};
  bool press_at_edge_ = false;
  bool press_on_layout_ = false;  // the current press began on the layout
  lv_obj_t* pressed_ = nullptr;   // what LVGL holds as pressed; may be gone
  uint32_t swipe_timer_ = 0;
  uint32_t peek_timer_ = 0;
  uint32_t tab_peek_ms_ = 5000;
};

}  // namespace jlp
