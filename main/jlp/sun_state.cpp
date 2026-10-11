#include "sun_state.h"

#include <strings.h>

#include "esp_log.h"
#include <string>

#include "espos_sk.h"

static const char* TAG = "jlp.sun";

namespace jlp {

void SunState::set_mode(const char* mode) {
  if (!mode || !*mode) return;
  DayNight next;
  if (strcasecmp(mode, "day") == 0) {
    next = DayNight::Day;
  } else if (strcasecmp(mode, "night") == 0) {
    next = DayNight::Night;
  } else {
    // environment.mode is only ever "day" or "night" (derived-data's
    // richer phase string lives on environment.sun); ignore anything
    // unexpected rather than guessing.
    ESP_LOGW(TAG, "unexpected environment.mode: %s", mode);
    return;
  }
  if (state_ == next) return;
  state_ = next;
  ESP_LOGI(TAG, "mode=%s", mode);
}

namespace {
void on_mode(const espos_sk_update_t* u, void*) {
  if (!u->value_json) return;
  // value is JSON text: "\"night\"" → night. Plain data assignment, no LVGL:
  // safe on the stream task.
  std::string v(u->value_json);
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
  sun_state().set_mode(v.c_str());
}
}  // namespace

void SunState::hook_sk_ws() {
  // A single exact path.
  int h = espos_sk_subscribe("environment.mode", 1000, on_mode, nullptr);
  ESP_LOGI(TAG, "subscribed to environment.mode (%d)", h);
}

SunState& sun_state() {
  static SunState s;
  return s;
}

}  // namespace jlp
