#include "subject_registry.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "cockpit_hal/ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "espos_sk.h"

#include "zone_registry.h"

static const char* TAG = "jlp.reg";

namespace jlp {

namespace {

constexpr int kListenPeriodMs = 1000;

// A delta read once, on the stream task: the freshness mark and the UI
// task's setter take the same reading. null counts as a reading (there is
// none now); a non-finite float, or text that is no number on a numeric
// path, does not, so a REST seed may still be the better one.
struct Reading {
  bool is_null = false;
  bool ok = true;
  float f = 0.0f;
  double n = 0.0;
};

Reading read_delta(SubjectKind kind, const std::string& v) {
  Reading r;
  if (v == "null") {
    r.is_null = true;
    return r;
  }
  char* end = nullptr;
  switch (kind) {
    case SubjectKind::Float:
      // strtof takes "inf", "nan" and 1e999 (+Inf); no observer (bar, arc,
      // zone match, label) is ready for a non-finite value. A JSON string or
      // other text converts to 0 with no characters consumed; that is no
      // reading either.
      r.f = strtof(v.c_str(), &end);
      r.ok = end != v.c_str() && std::isfinite(r.f);
      break;
    case SubjectKind::Int:
      // A toggle is Int-backed and Signal K sends a switch state as true or
      // false; those are 1 and 0, not text that is no number.
      if (v == "true" || v == "false") {
        r.n = v == "true" ? 1.0 : 0.0;
        break;
      }
      // Read as a JSON number, not just its leading integer digits: 1e3 is
      // 1000. A fraction is cut toward zero.
      r.n = strtod(v.c_str(), &end);
      r.ok = end != v.c_str() && std::isfinite(r.n);
      break;
    case SubjectKind::Bool:
    case SubjectKind::String:
      break;
  }
  return r;
}

// One espOS subscription per path. The callback runs on the SignalK stream
// task with strings that die when it returns, so it copies what it needs
// and hops onto the UI thread, where lv_subject_* is legal. Values arrive
// as JSON text (numbers, "strings", true/false, null); meta items carry
// the meta object.
void on_update(const espos_sk_update_t* u, void* arg) {
  // Taken first: a REST GET that starts while this callback runs must not
  // count as older than this delta.
  const int64_t rx_us = esp_timer_get_time();
  const auto* ctx = static_cast<const SubCtx*>(arg);
  SubjectEntry* entry = ctx->entry;
  const uint32_t gen = ctx->gen;
  // A callback of an ended subscription. The test is repeated on the UI
  // task: the subscription may end between here and there. A mark written
  // after it ended is older than any GET of a later subscription, so it
  // cannot refuse that GET's answer.
  if (entry->sub_gen != gen) return;
  if (u->value_json) {
    std::string v(u->value_json);
    const Reading r = read_delta(entry->kind, v);
    // Recorded here, on the stream task, before the hop: a REST seed
    // applied on the UI task while this delta waits in the queue must
    // already see it as newer.
    if (r.ok) entry->stream_rx_us = rx_us;
    cockpit_hal::ui::post([entry, gen, v = std::move(v), r, rx_us]() {
      if (entry->sub_gen != gen) return;
      // A REST seed whose GET started after this delta arrived was applied
      // while it waited in the queue; it is the newer reading.
      if (entry->seed_req_us > rx_us) return;
      lv_subject_t* s = entry->subject;
      switch (entry->kind) {
        case SubjectKind::Float:
          if (!r.is_null && r.ok) lv_subject_set_float(s, r.f);
          break;
        case SubjectKind::Int:
          if (!r.is_null && r.ok) {
            lv_subject_set_int(
                s, (int)std::clamp(r.n, (double)INT_MIN, (double)INT_MAX));
          }
          break;
        case SubjectKind::Bool:
          lv_subject_set_int(s, (v == "true" || v == "1") ? 1 : 0);
          break;
        case SubjectKind::String: {
          // strip the JSON quotes; non-string values (numbers, null) are
          // shown verbatim so a badly-typed binding is at least visible
          std::string txt = v;
          if (txt.size() >= 2 && txt.front() == '"' && txt.back() == '"') {
            txt = txt.substr(1, txt.size() - 2);
            // minimal unescape (\" \\)
            std::string out;
            out.reserve(txt.size());
            for (size_t i = 0; i < txt.size(); i++) {
              if (txt[i] == '\\' && i + 1 < txt.size()) {
                out += txt[++i];
              } else {
                out += txt[i];
              }
            }
            txt = out;
          }
          lv_subject_copy_string(s, txt.c_str());
          break;
        }
      }
    });
  } else if (u->meta_json) {
    // The meta object {units, description, zones, ...}, parsed here so the
    // freshness mark sees its real fields, then handed to ZoneRegistry.
    auto doc = std::make_shared<JsonDocument>();
    if (deserializeJson(*doc, u->meta_json) != DeserializationError::Ok) return;
    if (doc->as<JsonObjectConst>().isNull()) return;
    // Zones from the stream, an empty array (a clear) included, are newer
    // than a REST meta whose GET started earlier. Meta without them leaves
    // the mark alone.
    if ((*doc)["zones"].is<JsonArrayConst>()) entry->zones_rx_us = rx_us;
    std::string p(u->path);
    cockpit_hal::ui::post([entry, gen, p, doc, rx_us]() {
      if (entry->sub_gen != gen) return;
      // Zones from a REST GET that started after this arrived were applied
      // while it waited in the queue; only its description still counts.
      if (entry->zones_req_us > rx_us) doc->remove("zones");
      zones().apply_meta(p, doc->as<JsonObjectConst>());
    });
  }
}

// Widget binds that start with '@' are local action sentinels
// ("@brightness"): the panel feeds the subject itself, so there is
// nothing to subscribe to on the server.
bool is_local_sentinel(const std::string& path) {
  return !path.empty() && path[0] == '@';
}

}  // namespace

lv_subject_t* SubjectRegistry::get_or_create(const std::string& path,
                                             SubjectKind kind) {
  auto it = map_.find(path);
  if (it != map_.end()) {
    if (it->second.entry->kind != kind) {
      ESP_LOGW(TAG, "kind conflict on %s: existing=%d wanted=%d",
               path.c_str(), (int)it->second.entry->kind, (int)kind);
      return nullptr;
    }
    return it->second.entry->subject;
  }

  auto entry = std::make_unique<SubjectEntry>();
  entry->path = path;
  entry->kind = kind;
  entry->str_buf[0] = '\0';
  entry->str_prev[0] = '\0';

  // lv_subject_create() allocates the subject inside LVGL and returns it;
  // the deprecated lv_subject_init_* family initialised one the caller owned.
  // Safe to adopt here precisely because nothing destroys a subject:
  // garbage_collect() keeps them all for the device lifetime and only
  // unsubscribes, so there is no free path that could leak an LVGL list entry.
  //
  // A string subject is created WITHOUT storage (LVGL's init_string is called
  // with a null buffer), so its buffers are attached separately. They stay in
  // SubjectEntry: the value must outlive any single update, and
  // lv_subject_set_string_buffer_static() takes memory the caller keeps.
  switch (kind) {
    case SubjectKind::Float:
      entry->subject = lv_subject_create(LV_SUBJECT_TYPE_FLOAT);
      break;
    case SubjectKind::Int:
    case SubjectKind::Bool:
      entry->subject = lv_subject_create(LV_SUBJECT_TYPE_INT);
      break;
    case SubjectKind::String:
      entry->subject = lv_subject_create(LV_SUBJECT_TYPE_STRING);
      if (entry->subject != nullptr) {
        lv_subject_set_string_buffer_static(entry->subject, entry->str_buf,
                                            entry->str_prev,
                                            sizeof(entry->str_buf));
      }
      break;
  }
  if (entry->subject == nullptr) {
    // Out of LVGL memory, or a type this build disabled (LV_USE_FLOAT). A null
    // subject would be dereferenced by every observer that binds to it, so
    // refuse the path instead of registering a landmine.
    ESP_LOGE(TAG, "lv_subject_create failed for %s (kind=%d)", path.c_str(),
             (int)kind);
    return nullptr;
  }

  Slot slot;
  slot.entry = std::move(entry);

  // One espOS subscription per path delivers values AND meta (the stream
  // runs with sendMeta=all); it is (re)sent to the server on every
  // reconnect, and new paths bound after connect go out incrementally.
  // A panel-local sentinel ("@brightness") is fed by the panel itself and
  // gets none: the server has no such path.
  if (is_local_sentinel(path)) {
    slot.sub_handle = 0;
  } else {
    slot.sub_handle = subscribe(path, slot);
    if (slot.sub_handle <= 0) {
      ESP_LOGW(TAG, "subscribe %s failed (%d)", path.c_str(), slot.sub_handle);
    }
  }

  lv_subject_t* sub = slot.entry->subject;
  map_.emplace(path, std::move(slot));
  ESP_LOGI(TAG, "created subject for %s (kind=%d)", path.c_str(), (int)kind);

  return sub;
}

lv_subject_t* SubjectRegistry::lookup(const std::string& path) const {
  auto it = map_.find(path);
  if (it == map_.end()) return nullptr;
  return it->second.entry->subject;
}

std::optional<SubjectKind> SubjectRegistry::kind_of(
    const std::string& path) const {
  auto it = map_.find(path);
  if (it == map_.end()) return std::nullopt;
  return it->second.entry->kind;
}

bool SubjectRegistry::accept_seed(const std::string& path, int64_t requested_us) {
  auto it = map_.find(path);
  if (it == map_.end()) return false;
  SubjectEntry& e = *it->second.entry;
  if (e.stream_rx_us > requested_us || e.seed_req_us > requested_us) return false;
  e.seed_req_us = requested_us;
  return true;
}

bool SubjectRegistry::accept_zones(const std::string& path,
                                   int64_t requested_us) {
  auto it = map_.find(path);
  if (it == map_.end()) return true;
  SubjectEntry& e = *it->second.entry;
  if (e.zones_rx_us > requested_us || e.zones_req_us > requested_us) return false;
  e.zones_req_us = requested_us;
  return true;
}

int64_t SubjectRegistry::stream_rx_us(const std::string& path) const {
  auto it = map_.find(path);
  return it == map_.end() ? 0 : it->second.entry->stream_rx_us.load();
}

std::vector<std::string> SubjectRegistry::paths() const {
  std::vector<std::string> out;
  out.reserve(map_.size());
  for (auto& kv : map_) out.push_back(kv.first);
  return out;
}

void SubjectRegistry::garbage_collect(
    const std::set<std::string>& live_paths) {
  // Subjects stay for the device lifetime: widgets of the *previous*
  // layout may still be observing them while the swap animates, and a
  // stale value on a re-bound path is better than a dangling observer.
  // The subscription table is bounded (ESPOS_SK_MAX_SUBS), so unsubscribe
  // paths that no layout uses any more; the subject keeps its last value
  // and re-subscribes if a later layout binds it again.
  for (auto& kv : map_) {
    Slot& slot = kv.second;
    bool live = live_paths.count(kv.first) > 0;
    if (!live && slot.sub_handle > 0) {
      espos_sk_unsubscribe(slot.sub_handle);
      slot.sub_handle = 0;
      ++slot.entry->sub_gen;
      // Its value goes stale from here; a later layout's REST seed may
      // replace it again, but only one whose GET starts after this: an
      // answer still on its way belongs to the old binding. The zones keep
      // their marks, as the zones themselves stay.
      slot.entry->stream_rx_us = 0;
      slot.entry->seed_req_us = esp_timer_get_time();
    } else if (live && slot.sub_handle <= 0 && !is_local_sentinel(kv.first)) {
      slot.sub_handle = subscribe(kv.first, slot);
    }
  }
}

int SubjectRegistry::subscribe(const std::string& path, Slot& slot) {
  slot.ctxs.push_back(std::make_unique<SubCtx>(
      SubCtx{slot.entry.get(), slot.entry->sub_gen.load()}));
  return espos_sk_subscribe(path.c_str(), kListenPeriodMs, on_update,
                            slot.ctxs.back().get());
}

SubjectRegistry& registry() {
  static SubjectRegistry r;
  return r;
}

}  // namespace jlp
