#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace jlp {

enum class ZoneState : uint8_t {
  Nominal,   // green
  Normal,    // green (alias)
  Alert,     // blue
  Warn,      // yellow
  Alarm,     // orange
  Emergency  // red
};

struct Zone {
  // SK metadata zones are in RAW units (same space as the path's
  // value before display scale+offset is applied). Match() expects
  // the raw value, not the display-formatted one.
  float lower;
  float upper;
  ZoneState state;
};

// Per-path zone list, populated from meta deltas pushed in-stream by
// signalk-server when the WS subscription includes sendMeta=all
// (default in SensESP since PR #965). No HTTP polling — zones arrive
// alongside value deltas and update automatically if SK's metadata
// changes.
class ZoneRegistry {
 public:
  // Wire into the SK WS client's meta callback at boot. Idempotent.
  void hook_sk_ws();

  // Returns the zone matching `raw_value` for `path`, or nullptr if
  // no zones for this path or value falls outside all zones.
  // `raw_value` is the SK delta value BEFORE any widget display
  // scale/offset is applied (SK zones live in raw units).
  const Zone* match(const std::string& path, float raw_value) const;

  // Returns the path's SK meta `description` if one was published, or
  // empty string. Used by widgets that want to show the human-readable
  // path name instead of (or alongside) a raw value — e.g. a label on
  // a switch state where "1" is meaningless but "BMS DnC" is the
  // operator-facing identifier of the relay.
  const std::string& description(const std::string& path) const;

  // Manually feed a meta object (e.g. from a test). Normally not
  // called by user code — the WS callback does this.
  void apply_meta(const std::string& path, const JsonObjectConst& meta);

 private:
  // WS-task → event_loop hand-off, same pattern as
  // NotificationsRegistry. The on_meta callback appends to pending_
  // under pending_mutex_; a 50 ms event_loop timer drains the whole
  // batch. With sendMeta=all, SK re-broadcasts meta for every known
  // path on (re)connect / after a layout push — dozens at once on a
  // real boat. The old per-delta onDelay(0) flooded event_loop's
  // queue with that burst and could stall a concurrent layout apply
  // past its 25 s budget (and trip the event_loop watchdog).
  struct PendingMeta {
    std::string path;
    JsonDocument doc;
  };
  void drain_pending();

  std::unordered_map<std::string, std::vector<Zone>> map_;
  std::unordered_map<std::string, std::string> descriptions_;

  std::vector<PendingMeta> pending_;
  StaticSemaphore_t pending_mutex_buffer_{};
  SemaphoreHandle_t pending_mutex_ = nullptr;
};

ZoneRegistry& zones();

// RGB hex for each state, matching SignalK convention.
uint32_t color_for_state(ZoneState s);

}  // namespace jlp
