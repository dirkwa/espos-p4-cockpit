#include "subject_registry.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>

#include "cockpit_hal/ui.h"
#include "esp_log.h"
#include "espos_sk.h"

#include "zone_registry.h"

static const char* TAG = "jlp.reg";

namespace jlp {

namespace {

constexpr int kListenPeriodMs = 1000;

// One espOS subscription per path. The callback runs on the SignalK stream
// task with strings that die when it returns, so it copies the value into
// a heap string and hops onto the UI thread, where lv_subject_* is legal.
// Values arrive as JSON text (numbers, "strings", true/false, null);
// meta items carry the meta object.
void on_update(const espos_sk_update_t* u, void* arg) {
  auto* entry = static_cast<SubjectEntry*>(arg);
  if (u->value_json) {
    std::string v(u->value_json);
    cockpit_hal::ui::post([entry, v = std::move(v)]() {
      lv_subject_t* s = entry->subject;
      switch (entry->kind) {
        case SubjectKind::Float:
          if (v != "null") lv_subject_set_float(s, strtof(v.c_str(), nullptr));
          break;
        case SubjectKind::Int:
          if (v != "null") lv_subject_set_int(s, (int)strtol(v.c_str(), nullptr, 10));
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
    // raw meta object {units, description, zones, ...}: ZoneRegistry parses it
    std::string m(u->meta_json);
    std::string p(u->path);
    cockpit_hal::ui::post([p, m = std::move(m)]() {
      JsonDocument doc;
      if (deserializeJson(doc, m) != DeserializationError::Ok) return;
      JsonObjectConst meta = doc.as<JsonObjectConst>();
      if (meta.isNull()) return;
      zones().apply_meta(p, meta);
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
    slot.sub_handle = espos_sk_subscribe(path.c_str(), kListenPeriodMs, on_update, slot.entry.get());
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
    } else if (live && slot.sub_handle <= 0 && !is_local_sentinel(kv.first)) {
      slot.sub_handle = espos_sk_subscribe(kv.first.c_str(), kListenPeriodMs, on_update, slot.entry.get());
    }
  }
}

SubjectRegistry& registry() {
  static SubjectRegistry r;
  return r;
}

}  // namespace jlp
