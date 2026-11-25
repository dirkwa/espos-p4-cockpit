#include "layout_manager.h"

#include <ArduinoJson.h>
#include "cockpit_hal/ui.h"
#include "esp_log.h"

#include "../alert_overlay.h"
#include "../idle_dimmer.h"
#include "../mic_overlay.h"
#include "../settings_overlay.h"
#include "../status_overlay.h"
#include "../wake_overlay.h"
#include "store.h"

static const char* TAG = "jlp.layout";

namespace jlp {

void LayoutManager::init(lv_obj_t* parent) {
  player_.init(parent, [this](JsonObjectConst doc, bool theme_rebuild) {
    configure_chrome(doc, theme_rebuild);
  });
  initialised_ = true;
}

void LayoutManager::configure_chrome(JsonObjectConst doc, bool theme_rebuild) {
  // Apply status_overlay flag before the swap. Default true (matches
  // the original always-on behavior — existing layouts unchanged).
  // Toggling resizes content_root, so the staging tree needs to be
  // re-parented after the resize to inherit the new geometry.
  bool overlay_visible = true;
  if (doc["status_overlay"].is<bool>()) {
    overlay_visible = doc["status_overlay"];
  }
  overlay().set_visible(overlay_visible);

  const AlertConfig alerts = parse_alert_config(doc);
  alert_overlay().configure(alerts.enabled, alerts.levels);

  // Apply the backlight idle timeout (0 = disabled) and the dim-while-
  // idle brightness. Default dim_pct=0 (fully off); the wake overlay
  // takes the first tap so nothing underneath fires by accident.
  uint32_t idle_timeout_sec = 0;
  uint8_t idle_dim_pct = 0;
  JsonObjectConst display = doc["display"];
  if (!display.isNull()) {
    if (display["idle_timeout_sec"].is<uint32_t>()) {
      idle_timeout_sec = display["idle_timeout_sec"];
    }
    if (display["idle_dim_pct"].is<uint8_t>()) {
      idle_dim_pct = display["idle_dim_pct"];
    }
  }
  // A theme rebuild happens at sunrise and sunset, often with nobody at
  // the panel: re-arming would wake a dimmed screen.
  if (!theme_rebuild) idle_dimmer().configure(idle_timeout_sec, idle_dim_pct);
}

ApplyResult LayoutManager::apply(const std::string& json, ApplySource src) {
  ApplyResult r{false, "", "", 0, 0, ""};
  if (!initialised_) { r.err = "layout_manager not initialised"; return r; }

  LayoutPlayer::Result pr = player_.apply(json);
  r.widgets = pr.widgets;
  if (!pr.ok) {
    r.err = pr.err;
    return r;
  }
  restyle_overlays();

  // Persist only after a successful swap, and only for pushed layouts
  // (boot paths re-read the same persisted blob).
  if (src == ApplySource::PostLayout) {
    if (!store_write_atomic(json)) {
      r.warning = "layout rendered but not persisted";
    }
  }

  active_name_ = pr.name;
  active_source_ = src;

  // Compare against the previously-known path set so the post-swap
  // hook can decide whether to restart the WS (needed only if the
  // new layout introduced paths SK isn't currently subscribed to).
  // Capture the diff itself too — the hook uses it to fetch SK meta
  // for just the new paths instead of re-fetching everything.
  std::set<std::string> introduced;
  for (const auto& p : pr.live_paths) {
    if (known_paths_.find(p) == known_paths_.end()) {
      introduced.insert(p);
    }
  }
  bool new_paths = !introduced.empty();
  known_paths_ = pr.live_paths;
  if (post_swap_) post_swap_(new_paths, introduced, src, pr.live_paths);

  r.ok = true;
  r.name = active_name_;
  r.screens = pr.screens;
  ESP_LOGI(TAG,
           "applied layout '%s' (src=%d screens=%u widgets=%u paths=%u "
           "new=%d)",
           r.name.c_str(), (int)src, r.screens, r.widgets,
           (unsigned)pr.live_paths.size(), new_paths ? 1 : 0);
  return r;
}

void LayoutManager::refresh_theme() {
  if (theme::resolved() == player_.built_mode()) return;
  if (!player_.has_layout()) {
    // Nothing applied yet: the next apply builds in the new theme.
    restyle_overlays();
    return;
  }
  LayoutPlayer::Result r = player_.rebuild_for_theme();
  if (!r.ok) {
    // The layout built before, so this is memory for the second tree.
    // Keep the old one up and try again; refresh_theme() is a no-op
    // once something else rebuilt it.
    ESP_LOGW(TAG, "theme rebuild failed: %s, retrying in 30 s", r.err.c_str());
    cockpit_hal::ui::after(30000, [this] { refresh_theme(); });
    return;
  }
  restyle_overlays();
  ESP_LOGI(TAG, "rebuilt layout '%s' for theme %s", active_name_.c_str(),
           theme::mode_name(player_.built_mode()));
}

void LayoutManager::restyle_overlays() {
  overlay().restyle();
  alert_overlay().restyle();
  mic_overlay().restyle();
  wake_overlay().restyle();
  settings_overlay().restyle();
}

LayoutManager& layout_manager() {
  static LayoutManager m;
  return m;
}

}  // namespace jlp
