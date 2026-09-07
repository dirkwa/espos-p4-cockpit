#include "zone_fetch.h"

#include <ArduinoJson.h>
#include <cstdlib>
#include <memory>

#include "cockpit_hal/ui.h"
#include "esp_log.h"
#include "espos_sk_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../subject_registry.h"
#include "../zone_registry.h"

static const char* TAG = "jlp.zonefetch";

namespace jlp {

namespace {

struct FetchArgs {
  std::vector<std::string> paths;
};

// A parsed meta blob waiting to be applied on the UI task. The
// JsonDocument keeps the deserialized tree alive across the task → UI
// hand-off; apply_meta reads it on the UI side.
struct MetaResult {
  std::string path;
  JsonDocument doc;
};

// A parsed value seed waiting to be applied on the UI task. The value is
// extracted into a plain variant (number-or-bool) on the fetch side so the
// UI side doesn't have to keep the source document around. String paths
// are seeded from description() at build time, so they're never carried
// here.
struct ValueResult {
  std::string path;
  bool is_float = false;
  float f = 0.0f;
  int i = 0;
};

// Fetch + parse one path's meta. Appends a MetaResult to `out` if the
// blob carries zones or a description. Does NOT touch the UI task — the
// whole batch is drained in a single post by the caller.
void fetch_meta(const std::string& path, std::vector<MetaResult>* out) {
  char* json = nullptr;
  esp_err_t err = espos_sk_get_meta(path.c_str(), &json);
  if (err != ESP_OK) {
    // NOT_FOUND is routine: paths the server exposes without meta (seen on
    // navigation.anchor.* while the anchor is up). A refusal means the
    // token is missing or stale, which silently strips zones from quiet
    // paths — worth a line even though espOS re-verifies the token itself.
    if (err == ESP_ERR_NOT_ALLOWED) {
      ESP_LOGW(TAG, "%s: meta refused — SK token missing or rejected", path.c_str());
    } else {
      ESP_LOGD(TAG, "%s: no meta (%s)", path.c_str(), esp_err_to_name(err));
    }
    return;
  }
  MetaResult r;
  DeserializationError de = deserializeJson(r.doc, json);
  free(json);
  if (de) return;
  // Only keep it if there's actually a zones array or description —
  // saves map churn on paths with no useful metadata.
  JsonObjectConst obj = r.doc.as<JsonObjectConst>();
  if (obj["zones"].isNull() && obj["description"].isNull()) return;
  r.path = path;
  out->push_back(std::move(r));
}

// Fetch the path's current value and stage it for seeding. Without this,
// paths whose value rarely changes (solar current at idle, steady SOC,
// idle switch state) sit at the subject's initial 0 forever because SK
// only sends deltas on change — the firmware never sees a value after
// subscribing, even though the REST node has the current reading ready to
// go. Like fetch_meta, this stays off the UI task; the value is extracted
// here and the result applied in the batch drain.
void fetch_value(const std::string& path, std::vector<ValueResult>* out) {
  char* json = nullptr;
  if (espos_sk_get_value(path.c_str(), &json) != ESP_OK) return;

  // espos_sk_get_value hands back the node's "value" member alone — a bare
  // number, bool, string or object.
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  free(json);
  if (de) return;
  JsonVariantConst v = doc.as<JsonVariantConst>();
  if (v.isNull()) return;

  ValueResult r;
  r.path = path;
  // Check bool and int before float: ArduinoJson reports is<float>() true
  // for integers too, so testing float first would route an integer
  // reading through as<float>() and lose precision on large values.
  if (v.is<bool>()) {
    r.i = v.as<bool>() ? 1 : 0;
  } else if (v.is<int>()) {
    r.i = v.as<int>();
  } else if (v.is<float>() || v.is<double>()) {
    r.is_float = true;
    r.f = v.as<float>();
  } else {
    // String / object values aren't seeded — string subjects read
    // description() at build time and the str_buf is owned by the stream
    // listener (writing it here would race).
    return;
  }
  out->push_back(std::move(r));
}

void fetch_task(void* arg) {
  auto* a = static_cast<FetchArgs*>(arg);

  // Accumulate every parsed result on the fetch task, then apply the
  // whole batch in ONE UI callback. The old form marshaled one
  // cockpit_hal::ui::after(0) per fetch — two per path — so a 31-widget
  // layout flooded the UI task with 62 callbacks, each firing
  // lv_subject_notify. Under the concurrent layout apply + littlefs
  // reformat, that storm pushed the UI task past the 15 s liveness
  // threshold and rebooted the panel.
  auto metas = std::make_shared<std::vector<MetaResult>>();
  auto values = std::make_shared<std::vector<ValueResult>>();

  for (const auto& path : a->paths) {
    fetch_meta(path, metas.get());
    fetch_value(path, values.get());
  }
  ESP_LOGI(TAG, "fetched meta + value for %u paths (%u meta, %u val)",
           (unsigned)a->paths.size(), (unsigned)metas->size(),
           (unsigned)values->size());

  // Single hop onto the UI task to apply the whole batch.
  cockpit_hal::ui::post([metas, values]() {
    for (auto& m : *metas) {
      zones().apply_meta(m.path, m.doc.as<JsonObjectConst>());
    }
    for (auto& vr : *values) {
      auto kind = registry().kind_of(vr.path);
      if (!kind) continue;  // path not bound by any widget
      lv_subject_t* sub = registry().lookup(vr.path);
      if (!sub) continue;
      switch (*kind) {
        case SubjectKind::Float:
          if (vr.is_float) lv_subject_set_float(sub, vr.f);
          else lv_subject_set_float(sub, (float)vr.i);
          break;
        case SubjectKind::Int:
        case SubjectKind::Bool:
          // Coerce a float reading (SK may report an int path's value
          // as 5.0) so the seed isn't silently dropped — mirrors the
          // int→float coercion the Float case does.
          if (vr.is_float) lv_subject_set_int(sub, (int)vr.f);
          else lv_subject_set_int(sub, vr.i);
          break;
        case SubjectKind::String:
          break;  // seeded from description() at build time
      }
    }
  });

  delete a;
  vTaskDelete(NULL);
}

}  // namespace

void zone_fetch_for_paths(const std::vector<std::string>& paths) {
  if (paths.empty()) return;
  auto* a = new FetchArgs{paths};
  if (xTaskCreate(fetch_task, "jlp_zonefetch", 8192, a, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn zone fetch task");
    delete a;
  }
}

}  // namespace jlp
