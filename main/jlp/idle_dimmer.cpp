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
  end_manual_off();
  lv_display_trigger_activity(NULL);
  if (!on_) set_on(true);
}

void IdleDimmer::end_manual_off() {
  if (!manual_off_) return;
  manual_off_ = false;
  cockpit_hal::ui::cancel(manual_poll_);
}

void IdleDimmer::sleep_now() {
  if (!manual_off_) {
    manual_off_ = true;
    // Finer than the dimming poll, so a tap lights the screen at once;
    // only runs while the screen is off.
    manual_poll_ = cockpit_hal::ui::every(100, [this]() {
      // Activity later than the tap that chose "Screen off" (its release
      // is what called us) means someone touched the dark screen.
      const uint32_t since_off = lv_tick_elaps(manual_off_tick_);
      if (since_off > lv_display_get_inactive_time(NULL) + 200) wake();
    });
  }
  manual_off_tick_ = lv_tick_get();
  set_on(false);
  // set_on(false) leaves the idle level, which may be a glow; off means off.
  if (auto* d = cockpit_hal::ui::display()) d->set_brightness(0);
}

IdleDimmer& idle_dimmer() {
  static IdleDimmer d;
  return d;
}

}  // namespace jlp
