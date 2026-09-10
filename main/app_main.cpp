/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
 *
 * espos-p4-cockpit — application entry.
 *
 * espOS provides WiFi, config store + web UI, SignalK discovery/token/
 * stream (in and out) and REST client, mDNS, logs, core dump, signed OTA
 * and the device watchdog policy (lowMemory, taskStalled, netDown,
 * skLinkStalled). This file wires the panel on top: display + LVGL
 * (cockpit_hal), the speaker/mics and the voice satellite, the JSON Layout
 * Player (jlp/) with its layout push API on :8081, the NMEA 2000 gateway
 * and the status/alert overlays.
 */
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cockpit_hal/ui.h"
#include "cockpit_hal/waveshare_7b.h"
#include "cockpit_hal/waveshare_audio.h"
#include "espos.h"
#include "espos_cfg_keys.h"
#include "espos_config.h"
#include "espos_event.h"
#include "espos_health.h"
#include "espos_mdns.h"
#include "espos_n2k/candump_tcp_server.h"
#include "espos_n2k/twai_receiver.h"
#include "espos_n2k_api.h"
#include "espos_n2k/twai_transmitter.h"
#include "espos_sk.h"
#include "espos_voice/wyoming_satellite.h"
#include "espos_wifi.h"

#include "jlp/alert_overlay.h"
#include "jlp/audio/chime.h"
#include "jlp/audio/voice_control.h"
#include "jlp/default_layout.h"
#include "jlp/idle_dimmer.h"
#include "jlp/layout/layout_manager.h"
#include "jlp/layout/store.h"
#include "jlp/mic_overlay.h"
#include "jlp/net/http_api.h"
#include "jlp/net/layout_fetch.h"
#include "jlp/net/sk_put.h"
#include "jlp/net/wake_discover.h"
#include "jlp/net/zone_fetch.h"
#include "jlp/notifications_registry.h"
#include "jlp/status_overlay.h"
#include "jlp/subject_registry.h"
#include "jlp/sun_state.h"
#include "jlp/wake_overlay.h"
#include "jlp/zone_registry.h"

static const char* TAG = "cockpit";

namespace {

// ---- SignalK stream state → overlay ------------------------------------
// Driven by espOS events. The handler below runs on the default event-loop
// task and only posts to the UI task; everything here is UI-task state.
// The boot-time layout fetch and zone seed fire once, on the first connect.
bool s_boot_fetch_done = false;
bool s_sk_connected = false;
espos_n2k::TwaiReceiver* s_n2k_rx = nullptr;
espos_n2k::CandumpTcpServer* s_n2k_server = nullptr;
// Set once the satellite exists, so the first SignalK connect can hand it
// to wake discovery.
espos_voice::WyomingSatellite* s_wyoming_sat = nullptr;
// True when cockpit.wake_host names a wake server explicitly; discovery then
// stays out of the way.
bool s_wake_configured = false;

void sk_stream_up() {
  s_sk_connected = true;
  jlp::overlay().set_sk("ok");
  jlp::overlay().hide_sk_lost();
  espos_sk_server_t srv;
  if (espos_sk_get_server(&srv) == ESP_OK) jlp::overlay().set_sk_server(srv.host, srv.port);
  if (!s_boot_fetch_done) {
    s_boot_fetch_done = true;
    // Seed quiet paths (SK only sends deltas on change) and pull the boot
    // layout from applicationData, both against the server espOS selected.
    const auto& boot_paths = jlp::layout_manager().known_paths();
    if (!boot_paths.empty()) {
      jlp::zone_fetch_for_paths(std::vector<std::string>(boot_paths.begin(), boot_paths.end()));
    }
    jlp::layout_fetch_async_apply();
  }
  // Waits for the SignalK connection because boot has no route yet. An
  // explicit cockpit.wake_host wins over whatever the server advertises.
  if (!s_wake_configured) jlp::wake_discover_start(s_wyoming_sat);
}

void sk_stream_down() {
  s_sk_connected = false;
  jlp::overlay().set_sk("down");
  jlp::overlay().show_sk_lost();
}

// Default event-loop task: copy nothing (these events carry no data the
// overlay needs), post, return. No LVGL here.
void on_espos_event(void*, esp_event_base_t, int32_t id, void*) {
  switch (id) {
    case ESPOS_EVENT_SK_STREAM_CONNECTED:
      cockpit_hal::ui::post(sk_stream_up);
      break;
    case ESPOS_EVENT_SK_STREAM_DISCONNECTED:
      cockpit_hal::ui::post(sk_stream_down);
      break;
    case ESPOS_EVENT_SK_TOKEN_APPROVED:
      // Approval precedes the first connect; once streaming, "ok" stands.
      cockpit_hal::ui::post([] {
        if (!s_sk_connected) jlp::overlay().set_sk("approved");
      });
      break;
    case ESPOS_EVENT_NETWORK_UP:
      cockpit_hal::ui::post([] {
        if (!s_sk_connected) jlp::overlay().set_sk("connecting");
      });
      break;
    case ESPOS_EVENT_NETWORK_DOWN:
      // The stream drops with the link and says "down" itself; this only
      // covers a panel that never got that far.
      cockpit_hal::ui::post([] {
        if (!s_sk_connected) jlp::overlay().set_sk("no network");
      });
      break;
    default:
      break;
  }
}

// 1 Hz on the UI task: the live figures on the status strip, and the one
// health condition that is the panel's to raise.
void poll_status_line() {
  espos_wifi_status_t w;
  if (espos_wifi_get_status(&w) == ESP_OK && w.sm.state == ESPOS_WIFI_ST_CONNECTED) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %ddBm", w.sm.link.ssid, (int)w.rssi);
    jlp::overlay().set_wifi(buf);
  } else {
    jlp::overlay().set_wifi("down");
  }
  const bool n2k_seen = s_n2k_rx && s_n2k_rx->ever_received();
  const int64_t rx_idle = n2k_seen ? s_n2k_rx->seconds_since_last_rx() : -1;
  jlp::overlay().set_n2k(rx_idle, s_n2k_server ? s_n2k_server->connected_clients() : 0);
  jlp::overlay().set_uptime_heap((uint32_t)(esp_timer_get_time() / 1000000), esp_get_free_heap_size());

  // A bus that talked and then fell silent is a wedged TWAI node, and a
  // restart is what recovers it — so the condition is fatal, and espOS's
  // policy counts the strikes (espos/docs/health.md). A bus that never spoke
  // is not a fault: the panel may run without N2K at all. Level-triggered
  // and idempotent, so re-reporting every second is the intended shape.
  const bool stalled = n2k_seen && rx_idle > 30;
  espos_health_report_ex("n2kBus", stalled ? ESPOS_HEALTH_ALARM : ESPOS_HEALTH_NORMAL,
                         stalled ? "no frames for 30 s" : "", ESPOS_HEALTH_F_REBOOT_ON_ALARM);
}

// espos_start()'s before_network hook: everything the user should see and
// hear before the radio comes up. Runs on the main task after log + config.
esp_err_t panel_up(void*) {
  // ---- display + LVGL first: the panel shows something within a second
  static cockpit_hal::Waveshare7BDisplay display;
  static cockpit_hal::Waveshare7BTouch touch;
  cockpit_hal::ui::start(&display, &touch);
  {
    int32_t pct = 95;
    espos_config_get_i32(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_BRIGHTNESS, &pct);
    display.set_brightness((uint8_t)pct);
  }

  // Panel speaker + mics (ES8311 + NS4150B, ES7210). Same hardware on 7B
  // and 4B. Drives the alert chime and the voice satellite. Must come
  // after the touch HAL: the codecs share its I2C bus.
  static cockpit_hal::WaveshareAudio audio;
  audio.init();

  // Wyoming voice satellite (:10700): the boat's signalk-wyoming
  // orchestrator dials in, plays TTS through the speaker and captures the
  // mic (push-to-talk via the voice widget or the wake word). Wake runs
  // on-device (esp-sr WakeNet "Hi ESP" from the model partition) unless
  // cockpit.wake_host names a signalk-openwakeword server, in which case
  // the mic streams there and any custom-trained word works.
  espos_voice::WyomingSatelliteConfig wy_cfg;
  // Set explicitly: espos_voice defaults to a generic "espos", and this is
  // the name the orchestrator and Home Assistant show for the device.
  wy_cfg.name = "cockpit";
  {
    char wake_host[64] = "";
    espos_config_get_str(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_WAKE_HOST, wake_host, sizeof(wake_host), nullptr);
    char wake_word[32] = "";
    espos_config_get_str(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_WAKE_WORD, wake_word, sizeof(wake_word), nullptr);
    if (wake_host[0]) {
      wy_cfg.on_device_wake = false;
      wy_cfg.wake_host = wake_host;
      wy_cfg.wake_port = 10400;
      if (wake_word[0]) wy_cfg.wake_words = {wake_word};
      // An explicit host is a deliberate choice -- a second wake server, or a
      // specific word. Discovery would overwrite it with the SK server and the
      // setting would silently do nothing.
      s_wake_configured = true;
    } else {
      wy_cfg.on_device_wake = true;
    }
  }
  wy_cfg.wake_input_gain = 6;   // mic quiet (~665 raw); x6 into WakeNet range
  wy_cfg.wake_threshold = 0.45f;
  // Fixed gain, not RMS normalisation: the normaliser is an AGC that lifts
  // quiet ambient toward the same target as speech, compressing the
  // speech-vs-silence contrast the orchestrator's energy gate endpoints on
  // (~1.5:1 at the gate; the mic's real ratio is ~4:1). The orchestrator's
  // adaptive floor handles absolute levels.
  wy_cfg.mic_stream_target_rms = 0;
  wy_cfg.mic_stream_gain = 8;
  // The cue tells the talker when the mic opens; without it they pause and
  // the endpointer closes on silence before the question begins. Safe with
  // the shared I2S bus: the orchestrator mutes the cue + 0.5 s before
  // endpointing, so the tone cannot seed its noise floor.
  wy_cfg.awake_cue = true;
  static espos_voice::WyomingSatellite wyoming_sat(&audio, wy_cfg);
  s_wyoming_sat = &wyoming_sat;
  wyoming_sat.set_mic_muted_fn([] { return jlp::voice().mic_muted(); });
  jlp::http_api_set_wyoming(&wyoming_sat);

  // ---- JLP on the UI thread: overlay, layout manager, stored/default layout
  cockpit_hal::ui::post([] {
    char host[33] = "";
    espos_config_get_str(ESPOS_CFG_NS_WIFI, ESPOS_CFG_WIFI_HOSTNAME, host, sizeof(host), nullptr);
    jlp::overlay().init();
    jlp::overlay().set_hostname(host[0] ? host : "cockpit");
    jlp::overlay().set_sk("no network");   // events take it from here
    jlp::layout_manager().init(jlp::overlay().content_root());
    jlp::store_init();
    jlp::chime().init(&audio);
    jlp::voice().init(&wyoming_sat, &audio);
    std::string stored;
    jlp::ApplyResult r{};
    if (jlp::store_read(&stored)) {
      r = jlp::layout_manager().apply(stored, jlp::ApplySource::BootStore);
      if (!r.ok) {
        ESP_LOGW(TAG, "stored layout rejected (%s); clearing + falling back", r.err.c_str());
        jlp::store_clear();
      }
    }
    if (!r.ok) {
      r = jlp::layout_manager().apply(jlp::kDefaultLayoutJson, jlp::ApplySource::BootDefault);
      if (!r.ok) ESP_LOGE(TAG, "default layout rejected: %s", r.err.c_str());
    }
    // After a layout swap that introduces new paths, seed their values via
    // REST (SK only streams on change); espOS subscribes them itself.
    jlp::layout_manager().set_post_swap_hook(
        [](bool new_paths_introduced, const std::set<std::string>& new_paths, jlp::ApplySource src,
           const std::set<std::string>& all_paths) {
          const bool boot = src == jlp::ApplySource::BootStore || src == jlp::ApplySource::BootFetched;
          const std::set<std::string>& to_fetch = boot ? all_paths : new_paths;
          if (to_fetch.empty()) return;
          // No server yet (the stored layout applies before the network):
          // the first stream connect seeds known_paths() instead.
          espos_sk_server_t srv;
          if (espos_sk_get_server(&srv) == ESP_OK) {
            jlp::zone_fetch_for_paths(std::vector<std::string>(to_fetch.begin(), to_fetch.end()));
          }
          if (new_paths_introduced) jlp::subscribe_new_paths(new_paths);
        });
    jlp::zones().hook_sk_ws();
    jlp::notifications().hook_sk_ws();
    jlp::sun_state().hook_sk_ws();
    jlp::alert_overlay().init();
    jlp::mic_overlay().init();    // after alert_overlay: an alarm wins
    jlp::wake_overlay().init();   // after alert_overlay: z-order
    jlp::idle_dimmer().init();
    (void)jlp::notifications().on_change([]() {
      if (jlp::notifications().last_change_was_escalation()) jlp::idle_dimmer().wake();
    });
    cockpit_hal::ui::every(1000, poll_status_line);
  });
  return ESP_OK;
}

}  // namespace

extern "C" void app_main(void) {
  // Subscribed before anything can post, so the first NETWORK_UP and the
  // first stream connect are not missed.
  for (int32_t id : {ESPOS_EVENT_NETWORK_UP, ESPOS_EVENT_NETWORK_DOWN, ESPOS_EVENT_SK_TOKEN_APPROVED,
                     ESPOS_EVENT_SK_STREAM_CONNECTED, ESPOS_EVENT_SK_STREAM_DISCONNECTED}) {
    ESP_ERROR_CHECK(espos_event_subscribe(id, on_espos_event, nullptr));
  }

  // ---- espOS: log → config → panel (before_network) → httpd → wifi → sk → ota
  espos_start_opts_t opts = ESPOS_START_OPTS_DEFAULT;
  opts.app_name = "p4-cockpit";   // "p4-cockpit <hostname>" in the server's access-request list
  opts.before_network = panel_up;
  ESP_ERROR_CHECK(espos_start(&opts));

  // ---- NMEA 2000 gateway: TWAI rx/tx + candump TCP server (:2599)
  // Pins live here, not in espos_n2k: the 7B's on-board TJA1051T CAN
  // transceiver is wired to GPIO22/21, which is a fact about this board.
  static espos_n2k::TwaiReceiver n2k_rx(
      {.tx_pin = GPIO_NUM_22, .rx_pin = GPIO_NUM_21});
  static espos_n2k::TwaiTransmitter n2k_tx;
  static espos_n2k::CandumpTcpServer n2k_server(&n2k_rx, &n2k_tx);
  s_n2k_rx = &n2k_rx;
  s_n2k_server = &n2k_server;
  n2k_rx.start();
  n2k_tx.start();
  n2k_server.start();
  /* GET /api/v1/n2k on the espOS server: whether the driver is up, whether
   * anything has ever arrived, how long ago, and whether the controller is
   * seeing bus errors. Without it a silent bus and an unplugged one are the
   * same empty candump socket.
   *
   * Read `frames: 0, errors: 0` with `running: true` as "the wire is
   * electrically quiet", and check the obvious cause FIRST: the TWAI driver
   * started here at boot does not pick up a bus that is connected
   * afterwards. Observed 2026-09-10 -- the panel sat at frames: 0 for
   * 18 minutes after the bus was rewired and then took 15346 frames within
   * seconds of a reboot. So on a boat, where the panel is routinely powered
   * before the network it listens to, a reboot is the first thing to try and
   * not the last. Whether that is IDF's driver or espos_n2k's start path is
   * unproven (signalk-espOS/espOS#15). */
  espos_n2k_api_register(&n2k_rx);

  // ---- the layout push API (designer) on its own port + mDNS
  int32_t api_port = 8081;
  espos_config_get_i32(ESPOS_CFG_NS_COCKPIT, ESPOS_CFG_COCKPIT_API_PORT, &api_port);
  jlp::http_api_start((uint16_t)api_port);
  // Keep widgets + firmware in lockstep with /hello in http_api.cpp. mDNS
  // browsers use these for capability discovery before falling back to a
  // real /hello fetch. Canonical kinds only, for the same reason /hello
  // lists them: this is what a client should offer, not every spelling the
  // device will accept. espOS holds the record until its responder is up
  // and re-announces it after every reconnect.
  static const char* const kPlayerTxt[] = {
      "schema=1",
      "widgets=label,value,toggle,arc,bar,bargroup,button,notifications,"
      "anchor,anchor_track,voice,speaker,mic,volume,slider,stream",
      "firmware=p4-cockpit-jlp-2.0.0",
      "api=/layout,/hello,/healthz,/screenshot",
  };
  esp_err_t merr = espos_mdns_add_service("_signalk-player", "_tcp", (uint16_t)api_port, kPlayerTxt,
                                          sizeof(kPlayerTxt) / sizeof(kPlayerTxt[0]));
  if (merr != ESP_OK) ESP_LOGW(TAG, "_signalk-player._tcp not advertised: %s", esp_err_to_name(merr));
  // Network is up (lwIP initialised by espOS): safe to open the satellite's
  // TCP listener and the wake pipeline.
  s_wyoming_sat->start();

  ESP_LOGI(TAG, "boot: iram=%u iram_big=%u psram=%lu", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
