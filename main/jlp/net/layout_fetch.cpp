#include "layout_fetch.h"

#include <string>

#include "cockpit_hal/ui.h"
#include "esp_log.h"
#include "espos_sk_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../layout/layout_manager.h"

static const char* TAG = "jlp.fetch";

namespace jlp {

namespace {

constexpr const char* kPath =
    "/signalk/v1/applicationData/global/p4-cockpit/1/layout.json";
// Same cap as POST /layout: a layout the push API would refuse is not
// worth applying from the server either.
constexpr size_t kMaxBodyBytes = 64 * 1024;

// Own task: the GET blocks (espos_sk_http.h) and the UI task must not. The
// body is handed back with ui::post so the apply runs on the UI task (LVGL
// single-writer).
void fetch_task(void*) {
  espos_sk_http_opts_t opts = {};
  opts.max_body = kMaxBodyBytes;
  espos_sk_http_resp_t r = {};
  esp_err_t err = espos_sk_http_get(kPath, &opts, &r);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "applicationData unreachable (%s); keeping current layout",
             esp_err_to_name(err));
  } else if (r.status != 200) {
    ESP_LOGI(TAG, "applicationData returned %d; keeping current layout",
             r.status);
  } else if (r.truncated) {
    // Never parse a clipped body: it would be rejected with a misleading
    // error, or worse, apply half a layout.
    ESP_LOGW(TAG, "applicationData layout exceeds %u bytes; keeping current layout",
             (unsigned)kMaxBodyBytes);
  } else {
    ESP_LOGI(TAG, "fetched %u bytes from applicationData", (unsigned)r.len);
    auto* body = new std::string(r.body, r.len);
    cockpit_hal::ui::post([body]() {
      auto res = layout_manager().apply(*body, ApplySource::BootFetched);
      if (!res.ok) {
        ESP_LOGW(TAG, "fetched layout rejected: %s", res.err.c_str());
      }
      delete body;
    });
  }
  espos_sk_http_resp_free(&r);
  vTaskDelete(NULL);
}

}  // namespace

void layout_fetch_async_apply() {
  // A few seconds after the connect, not on it: the zone seed for the
  // stored layout fires on the same connect and espOS runs its own
  // reconnect traffic, and the REST client bounds requests in flight — a
  // boot layout that lands a moment later is invisible, a fetch that gave
  // up waiting for a slot is not.
  cockpit_hal::ui::after(5000, []() {
    if (xTaskCreate(fetch_task, "jlp_fetch", 8192, nullptr, 4, NULL) != pdPASS) {
      ESP_LOGE(TAG, "failed to spawn fetch task");
    }
  });
}

}  // namespace jlp
