#include "drop_here.h"

#include <ArduinoJson.h>
#include <cstdlib>

#include "esp_log.h"
#include "espos_sk_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sk_put.h"

static const char* TAG = "jlp.drophere";

namespace jlp {

namespace {

void drop_task(void*) {
  char* json = nullptr;
  esp_err_t err = espos_sk_get_value("navigation.position", &json);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "position GET failed (%s) — not dropping", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }

  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  free(json);
  if (de) {
    ESP_LOGW(TAG, "position parse failed — not dropping");
    vTaskDelete(NULL);
    return;
  }
  JsonObjectConst v = doc.as<JsonObjectConst>();
  const double kNoFix = 1e9;
  double lat = v["latitude"] | kNoFix;
  double lon = v["longitude"] | kNoFix;
  // Reject a missing/out-of-range fix before PUTting a bogus drop.
  if (!(lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0)) {
    ESP_LOGW(TAG, "no valid GPS fix (lat=%.3f lon=%.3f) — not dropping", lat,
             lon);
    vTaskDelete(NULL);
    return;
  }

  // PUT the fix to navigation.anchor.position → plugin drops there.
  put_position("navigation.anchor.position", lat, lon);
  ESP_LOGI(TAG, "drop-here: PUT %.6f, %.6f", lat, lon);
  vTaskDelete(NULL);
}

}  // namespace

void drop_anchor_here() {
  if (xTaskCreate(drop_task, "jlp_drophere", 8192, nullptr, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn drop-here task");
  }
}

}  // namespace jlp
