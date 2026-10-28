// SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#include "sk_put.h"

#include <ArduinoJson.h>
#include <cstdio>

#include "esp_log.h"
#include "espos_sk.h"

static const char* TAG = "jlp.put";

namespace jlp {

namespace {

// espos_sk_put() takes the value as JSON text and tracks the requestId;
// the server's answer lands in the log (and espOS' ws.put counters).
void on_done(const char* id, const char* state, int code, const char* msg, void*) {
  if (code >= 400 || (state && state[0] == 'F') || (state && state[0] == 'T')) {
    ESP_LOGW(TAG, "PUT %s: %s %d %s", id, state, code, msg);
  } else {
    ESP_LOGI(TAG, "PUT %s: %s %d", id, state, code);
  }
}

void send(const std::string& path, const char* value_json) {
  esp_err_t err = espos_sk_put(path.c_str(), value_json, on_done, nullptr);
  if (err != ESP_OK) ESP_LOGW(TAG, "PUT %s not sent: %s", path.c_str(), esp_err_to_name(err));
}

}  // namespace

void put_bool(const std::string& path, bool value) {
  ESP_LOGI(TAG, "PUT %s = %s", path.c_str(), value ? "true" : "false");
  send(path, value ? "true" : "false");
}

void put_int(const std::string& path, int value) {
  ESP_LOGI(TAG, "PUT %s = %d", path.c_str(), value);
  char v[16];
  snprintf(v, sizeof(v), "%d", value);
  send(path, v);
}

void put_float(const std::string& path, float value) {
  ESP_LOGI(TAG, "PUT %s = %f", path.c_str(), value);
  char v[32];
  snprintf(v, sizeof(v), "%.6g", (double)value);
  send(path, v);
}

void put_string(const std::string& path, const std::string& value) {
  ESP_LOGI(TAG, "PUT %s = \"%s\"", path.c_str(), value.c_str());
  JsonDocument doc;
  doc.set(value);
  std::string s;
  serializeJson(doc, s);
  send(path, s.c_str());
}

// null: raise the anchor etc. — the server's action handler acts on the
// PUT regardless of the value's type.
void put_null(const std::string& path) {
  ESP_LOGI(TAG, "PUT %s = null", path.c_str());
  send(path, "null");
}

// {latitude, longitude}: the anchor-alarm plugin's putPosition handler
// drops the anchor at this position. Called from the drop-here fetch
// task; espos_sk_put is thread-safe, no marshaling needed.
void put_position(const std::string& path, double latitude, double longitude) {
  ESP_LOGI(TAG, "PUT %s = {lat=%.6f, lon=%.6f}", path.c_str(), latitude, longitude);
  char v[80];
  snprintf(v, sizeof(v), "{\"latitude\":%.7f,\"longitude\":%.7f}", latitude, longitude);
  send(path, v);
}

// ---- notification ACK ----
//
// SignalK PUT requests against notifications.* paths are NOT honored by
// the server's PUT dispatcher (the modern notification API routes ACKs
// via REST endpoints keyed by UUID). But the server's notification-
// handler interception path consumes incoming deltas on notifications.*
// and syncs the alarm's state to the delta's `state` field, so an inbound
// delta with state="normal" achieves the ACK. Sent raw over the stream.
void put_notification_ack(const std::string& path_after_prefix) {
  std::string full = "notifications." + path_after_prefix;
  ESP_LOGI(TAG, "ACK %s (delta state=normal)", full.c_str());
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["context"] = "vessels.self";
  JsonArray updates = root["updates"].to<JsonArray>();
  JsonObject update = updates.add<JsonObject>();
  JsonArray values = update["values"].to<JsonArray>();
  JsonObject v = values.add<JsonObject>();
  v["path"] = full;
  JsonObject val = v["value"].to<JsonObject>();
  val["state"] = "normal";
  val["message"] = "";
  val["method"].to<JsonArray>();
  std::string s;
  serializeJson(doc, s);
  esp_err_t err = espos_sk_send_raw(s.c_str());
  if (err != ESP_OK) ESP_LOGW(TAG, "ACK not sent: %s", esp_err_to_name(err));
}

}  // namespace jlp
