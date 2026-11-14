#include "idle_dimmer.h"

#include "esp_log.h"
#include "lvgl.h"

#include "cockpit_hal/ui.h"
#include "sun_state.h"
#include "wake_overlay.h"

static const char* TAG = "jlp.dimmer";

namespace jlp {

void IdleDimmer::set_on(bool on) {
  if (on == on_) return;
  on_ = on;
  auto* d = cockpit_hal::ui::display();
  if (!d) return;
  if (on) {
    // Hide the wake overlay BEFORE raising the backlight so the user
    // doesn't see the layout content flash up under the overlay.
    wake_overlay().hide();
    d->set_brightness(on_brightness_pct_);
  } else {
    // Show the "tap to wake" overlay BEFORE killing the backlight so
    // the panel doesn't briefly show the layout before going dark,
    // and so the first wake-tap can't fall through to a toggle or
    // button underneath.
    wake_overlay().show();
    d->set_brightness(dim_pct_);
  }
  ESP_LOGI(TAG, "backlight %s (pct=%u)", on ? "on" : "off",
           (unsigned)(on ? on_brightness_pct_ : dim_pct_));
}

void IdleDimmer::set_on_brightness(uint8_t pct) {
  on_brightness_pct_ = pct;
  // Only touch the hardware if the panel is awake; while dimmed the dim
  // level is what belongs on screen, and the new value takes effect on the
  // next wake.
  if (!on_) return;
  auto* d = cockpit_hal::ui::display();
  if (d) d->set_brightness(pct);
}

void IdleDimmer::init() {
  // A press, not inactivity time, ends "Screen off": the tap that chose it
  // was already released when sleep_now() ran, so any press is a new one,
  // however soon it follows.
  if (lv_indev_t* indev = lv_indev_get_next(nullptr)) {
    lv_indev_add_event_cb(
        indev,
        [](lv_event_t*) {
          if (idle_dimmer().manual_off_) idle_dimmer().wake();
        },
        LV_EVENT_PRESSED, nullptr);
  }
  // LVGL tracks input inactivity automatically. Poll on a 1 Hz cadence
  // — finer resolution is wasteful for human-scale timeouts.
  cockpit_hal::ui::every(1000, [this]() {
    if (manual_off_) return;
    if (idle_timeout_sec_ == 0) {
      if (!on_) set_on(true);
      return;
    }
    // Day/night gating: only let the panel dim during night. Day
    // (or Unknown, e.g. SK hasn't published sunrise/sunset yet)
    // is fail-safe-bright. Sun classification is refreshed cheap
    // on every tick — classify() is two integer comparisons.
    if (sun_state().classify() != DayNight::Night) {
      if (!on_) set_on(true);
      return;
    }
    uint32_t inactive_ms = lv_display_get_inactive_time(NULL);
    bool should_be_on = inactive_ms < idle_timeout_sec_ * 1000U;
    if (should_be_on != on_) set_on(should_be_on);
  });
}

void IdleDimmer::configure(uint32_t idle_timeout_sec, uint8_t dim_pct) {
  if (dim_pct > 100) dim_pct = 100;
  if (idle_timeout_sec_ != idle_timeout_sec || dim_pct_ != dim_pct) {
    idle_timeout_sec_ = idle_timeout_sec;
    dim_pct_ = dim_pct;
    ESP_LOGI(TAG, "idle timeout=%us dim_pct=%u",
             (unsigned)idle_timeout_sec_, (unsigned)dim_pct_);
  }
  // Always re-arm + wake regardless of whether the config actually
  // changed. A fresh layout push means the operator is at the panel
  // right now, even if the values match what was already loaded.
  wake();
}

void IdleDimmer::wake() {
  manual_off_ = false;
  lv_display_trigger_activity(NULL);
  if (!on_) set_on(true);
}

void IdleDimmer::sleep_now() {
  manual_off_ = true;
  set_on(false);
  // set_on(false) leaves the idle level, which may be a glow; off means off.
  if (auto* d = cockpit_hal::ui::display()) d->set_brightness(0);
}

IdleDimmer& idle_dimmer() {
  static IdleDimmer d;
  return d;
}

}  // namespace jlp
