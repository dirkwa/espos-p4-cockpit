#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <ArduinoJson.h>

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
// signalk-server (espOS opens the stream with sendMeta=all). No HTTP
// polling — zones arrive alongside value deltas and update
// automatically if SK's metadata changes. Meta is delivered per-path:
// SubjectRegistry's espos_sk_subscribe callback for each bound path
// posts it to the UI task, which calls apply_meta (the REST fetch in
// zone_fetch.cpp is a cold-start fallback that also calls apply_meta).
class ZoneRegistry {
 public:
  // Boot-time no-op kept for the call site. Zone metadata is wired
  // per-path by SubjectRegistry::get_or_create, not through a WS
  // wildcard here.
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

  // Feed a meta object for `path`. Called on the UI task by the
  // per-path subscription (SubjectRegistry) and by the REST cold-start
  // fallback (zone_fetch.cpp); also usable from a test.
  // Calls lv_subject_notify, so it MUST run on the UI task.
  void apply_meta(const std::string& path, const JsonObjectConst& meta);

 private:
  std::unordered_map<std::string, std::vector<Zone>> map_;
  std::unordered_map<std::string, std::string> descriptions_;
};

ZoneRegistry& zones();

// RGB hex for each state, matching SignalK convention.
uint32_t color_for_state(ZoneState s);

}  // namespace jlp
