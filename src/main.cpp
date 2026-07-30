/**
 * ESP32-P4 Cockpit — JSON Layout Player (jlp)
 *
 * Step 2: status overlay added. The overlay is the only always-on UI
 * element; future layout swaps reparent under overlay().content_root().
 */

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#ifdef COCKPIT_BOARD_4B
#include "sensesp_cockpit_display/hal/boards/waveshare_4b.h"
#else
#include "sensesp_cockpit_display/hal/boards/waveshare_7b.h"
#endif
#include "sensesp_cockpit_display/hal/boards/waveshare_audio.h"
#include "sensesp_cockpit_display/lvgl/lv_drivers.h"
#include "sensesp_cockpit_display/net/http_ota.h"
#include "sensesp_cockpit_display/net/remote_log.h"
#include "sensesp_wyoming_satellite/wyoming_satellite.h"
#ifndef COCKPIT_BOARD_4B
#include "sensesp_n2k_gateway.h"
#endif
#include "sensesp_app_builder.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/lambda_consumer.h"

#include "jlp/default_layout.h"
#include "jlp/idle_dimmer.h"
#include "jlp/layout/layout_manager.h"
#include "jlp/layout/store.h"
#include "jlp/net/http_api.h"
#include "jlp/net/layout_fetch.h"
#include "jlp/net/sk_put.h"
#include "jlp/net/sk_server.h"
#include "jlp/net/zone_fetch.h"
#include "jlp/zone_registry.h"
#include "jlp/net/mdns_announce.h"
#include "jlp/status_overlay.h"
#include "jlp/subject_registry.h"
#include "jlp/alert_overlay.h"
#include "jlp/audio/chime.h"
#include "jlp/audio/voice_control.h"
#include "jlp/notifications_registry.h"
#include "jlp/sun_state.h"
#include "jlp/wake_overlay.h"
#include "jlp/zone_registry.h"

using namespace sensesp;
using namespace sensesp_cockpit_display;

// SK server the firmware talks to: WS subscription, applicationData
// fetch, and per-path zone/value REST seeding all target this host.
// Surfaced on the connection-lost banner so the helm shows which
// server is unreachable.
static constexpr const char* kSkHost = "192.168.0.148";
static constexpr uint16_t kSkPort = 4100;

void setup() {
  SetupLogging(ESP_LOG_INFO);

#ifdef COCKPIT_BOARD_4B
  auto* display = new Waveshare4BDisplay();
  auto* touch = new Waveshare4BTouch();
#else
  auto* display = new Waveshare7BDisplay();
  auto* touch = new Waveshare7BTouch();
#endif
  lvgl_init(display, touch);

  // Panel speaker (ES8311 + NS4150B). Same hardware on 7B and 4B, so
  // this is board-agnostic. Drives the alert chime; the AudioDriver
  // sink is also the intended path for a future voice feed.
  auto* audio = new WaveshareAudio();
  audio->init();
  jlp::chime().init(audio);

  // Wyoming voice satellite (:10700). The boat's signalk-wyoming
  // orchestrator dials out to us: it plays TTS through the panel speaker and
  // (push-to-talk, via the voice widget) captures the mic. Board-agnostic
  // audio sink, so the same on 7B/4B.
  // Construct now (so /hello + the OTA-quiesce hook can reference it), but
  // START it only after the network stack is up (see below) — its TCP
  // server calls socket()/bind(), which assert against lwIP if run before
  // SensESPAppBuilder brings WiFi/lwIP online.
  auto* wyoming_sat = new sensesp_wyoming::WyomingSatellite(audio);
  jlp::http_api_set_wyoming(wyoming_sat);
  jlp::voice().init(wyoming_sat, audio);  // voice + audio-control widgets

  jlp::overlay().init();
  jlp::overlay().set_hostname("p4-cockpit");

  jlp::layout_manager().init(jlp::overlay().content_root());

  jlp::store_init();
  std::string stored;
  // Value-init so r.ok is false on the !store_read() path; Cppcheck
  // flags the default-init form as a use-of-uninitialized-member.
  jlp::ApplyResult r{};
  if (jlp::store_read(&stored)) {
    r = jlp::layout_manager().apply(stored, jlp::ApplySource::BootStore);
    if (!r.ok) {
      ESP_LOGW("main", "stored layout rejected (%s); clearing + falling back",
               r.err.c_str());
      // Drop the bad blob so the next boot doesn't keep re-reading and
      // re-rejecting the same content. The user can push a fresh layout
      // from the designer once the device is responsive; until then the
      // compiled default applies (next branch below).
      jlp::store_clear();
    }
  }
  if (!r.ok) {
    r = jlp::layout_manager().apply(jlp::kDefaultLayoutJson,
                                    jlp::ApplySource::BootDefault);
    if (!r.ok) {
      ESP_LOGE("main", "default layout rejected: %s", r.err.c_str());
    }
  }

  SensESPAppBuilder builder;
  auto app = builder.set_hostname("p4-cockpit")
                 ->set_wifi_client("MOIN", "Moin2018!")
                 ->set_wifi_access_point("", "")
                 ->set_sk_server(kSkHost, kSkPort)
                 ->get_app();

  // The JLP HTTP calls (layout fetch, zone/value REST) and the
  // connection-lost banner target whatever SensESP is configured to use
  // — set via the SensESP config web UI — so the whole panel follows
  // one server setting instead of the compile-time constant. kSkHost /
  // kSkPort are only the fallback until SensESP resolves an address.
  jlp::sk_server_set_default(kSkHost, kSkPort);
  {
    jlp::SkServer s = jlp::sk_server();
    jlp::overlay().set_sk_server(s.host.c_str(), s.port);
  }

  // After a layout swap that introduces paths SensESP isn't already
  // subscribed to (SensESP subscribes once at on_connected; listeners
  // added later are ignored until the next connect), seed their values
  // via REST and send an incremental WS subscribe so they stream live —
  // without a full ws->restart() and its multi-second reconnect storm.
  jlp::layout_manager().set_post_swap_hook(
      [](bool new_paths_introduced, const std::set<std::string>& new_paths,
         jlp::ApplySource src, const std::set<std::string>& all_paths) {
        // On a boot apply (layout from store / applicationData) NO path
        // is "new", but the quiet ones still need their current value
        // pulled: SK only sends deltas on change, so a value that last
        // changed before the WS subscribed (e.g. navigation.anchor.*
        // when the anchor was dropped before boot) never arrives, and
        // the widget sits at its initial 0 — the anchor dial reads
        // "ANCHOR UP" though the anchor is down. Seed the whole set in
        // that case. On a runtime push, keep seeding just the new paths.
        const bool boot = src == jlp::ApplySource::BootStore ||
                          src == jlp::ApplySource::BootFetched;
        const std::set<std::string>& to_fetch = boot ? all_paths : new_paths;
        if (to_fetch.empty()) return;

        // Pull SK meta (zones + description) and current value for the
        // target paths via HTTP REST. Per-path endpoint is ~25 ms on a
        // healthy LAN; the fetch task drives them serially so the burst
        // stays small.
        std::vector<std::string> v(to_fetch.begin(), to_fetch.end());
        jlp::SkServer s = jlp::sk_server();
        jlp::zone_fetch_for_paths(s.host, s.port, v);

        // SensESP only subscribes at WS connect, so a path first bound
        // by a push made after connect would otherwise never receive
        // live deltas — its widget would sit frozen at the seed. Send an
        // incremental subscribe for just the new paths so they start
        // streaming now. (Boot paths are covered by the WS's own connect
        // subscribe, so only new_paths need this.)
        //
        // This intentionally replaces a full ws->restart(): the restart's
        // reconnect burst floods event_loop with SK's initial-state
        // replay for 30+ seconds, wedging the next push / screenshot. An
        // incremental subscribe adds only the new paths, no replay storm.
        if (new_paths_introduced) jlp::subscribe_new_paths(new_paths);
      });

  remote_log_start(2323);
  http_ota_start(8080);
  jlp::http_api_start(8081);
  // Network is up now — safe to open the Wyoming satellite's TCP listener.
  wyoming_sat->start();
  jlp::mdns_announce_start(8081);
  jlp::zones().hook_sk_ws();
  jlp::notifications().hook_sk_ws();
  jlp::sun_state().hook_sk_ws();
  jlp::alert_overlay().init();
  // wake overlay must be initialised AFTER alert_overlay so that
  // alert_overlay's move_foreground (when it pops a notification)
  // still wins z-order over the wake-overlay.
  jlp::wake_overlay().init();
  jlp::idle_dimmer().init();
  // An ESCALATING notification wakes the panel so the alarm overlay is
  // actually visible. Clears and reductions don't — otherwise a brief
  // transient alarm that auto-resolves (e.g. AC compressor inrush
  // tripping InverterImbalance for a second) drags the helm out of
  // idle every cycle. Token discarded — dimmer is process-lifetime.
  (void)jlp::notifications().on_change([]() {
    if (jlp::notifications().last_change_was_escalation()) {
      jlp::idle_dimmer().wake();
    }
  });
  // --- SK WS state into the overlay ---
  //
  // The boot layout fetch is deferred to the first successful WS
  // connect rather than fired here: SensESP resolves the server
  // address (config value, or mDNS) only once it connects, so
  // sk_server() is empty until then. Fetching on connect means the GET
  // targets the SensESP-configured server, not the compiled default.
  auto ws_client = app->get_ws_client();
  ws_client->connect_to(new LambdaConsumer<SKWSConnectionState>(
      [](SKWSConnectionState state) {
        // This consumer runs on the event_loop task (connection_state_
        // is a TaskQueueProducer drained there), so lv_* calls here
        // need no marshaling.
        switch (state) {
          case SKWSConnectionState::kSKWSConnected: {
            jlp::overlay().set_sk("ok");
            jlp::overlay().hide_sk_lost();
            // Now that SensESP has resolved the server, point the
            // banner + one-shot boot layout fetch at it.
            jlp::SkServer s = jlp::sk_server();
            jlp::overlay().set_sk_server(s.host.c_str(), s.port);
            static bool s_boot_fetch_done = false;
            if (!s_boot_fetch_done) {
              s_boot_fetch_done = true;
              // Seed the boot layout's values now that the server is
              // resolved. The stored layout was applied before the
              // network was up (and before the post-swap hook existed),
              // so its seed never ran — a quiet path whose last delta
              // predates this boot (e.g. navigation.anchor.maxRadius
              // when the anchor was dropped before boot) would otherwise
              // sit at 0 and the widget render stale ("ANCHOR UP"). The
              // WS's own connect subscribe covers live streaming, so no
              // extra subscribe is needed. A BootFetched applicationData
              // apply, if it lands, seeds itself through the hook.
              const auto& boot_paths = jlp::layout_manager().known_paths();
              if (!boot_paths.empty()) {
                std::vector<std::string> v(boot_paths.begin(),
                                           boot_paths.end());
                jlp::zone_fetch_for_paths(s.host, s.port, v);
              }
              jlp::layout_fetch_async_apply(s.host, s.port);
            }
            break;
          }
          case SKWSConnectionState::kSKWSConnecting:
            jlp::overlay().set_sk("connecting");
            break;
          case SKWSConnectionState::kSKWSAuthorizing:
            jlp::overlay().set_sk("auth");
            break;
          case SKWSConnectionState::kSKWSDisconnected:
          default:
            jlp::overlay().set_sk("down");
            jlp::overlay().show_sk_lost();
            break;
        }
      }));

  // --- N2K gateway ---
  //
  // The 7B carries an onboard CAN transceiver on GPIO 21/22; the 4B
  // (smart-86-box wall panel) has none, so the whole gateway compiles
  // out there — no candump server, and no n2k-rx-stall watchdog (which
  // would otherwise reboot-loop with no bus attached).
#ifndef COCKPIT_BOARD_4B
  auto* receiver =
      new TwaiReceiver(TwaiReceiverConfig::waveshare_touch_lcd_7b());
  auto* transmitter = new TwaiTransmitter();
  auto* n2k_server = new CandumpTcpServer(receiver, transmitter);
  receiver->start();
  transmitter->start();
  n2k_server->start();

  sensesp_cockpit_display::set_ota_quiesce_callback(
      [receiver, transmitter, n2k_server, wyoming_sat]() {
        wyoming_sat->stop();
        n2k_server->stop();
        receiver->stop();
        transmitter->stop();
      });
#else
  // 4B has no N2K gateway, but the Wyoming satellite still needs to quiesce
  // its socket task before an OTA write.
  sensesp_cockpit_display::set_ota_quiesce_callback(
      [wyoming_sat]() { wyoming_sat->stop(); });
#endif

  // Watchdog: reboot on WiFi loss, heap exhaustion, or (7B only) N2K
  // rx stall once we've ever received a frame.
  event_loop()->onRepeat(30000, [
#ifndef COCKPIT_BOARD_4B
                                     receiver
#endif
  ]() {
    static int consecutive_fail = 0;
    bool ok = true;
    const char* reason = "";

    if (WiFi.status() != WL_CONNECTED) {
      ok = false;
      reason = "wifi disconnected";
    } else if (esp_get_free_heap_size() < 64 * 1024) {
      ok = false;
      reason = "heap exhausted";
    } else if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 40 * 1024 ||
               heap_caps_get_largest_free_block(
                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 12 * 1024) {
      // esp_get_free_heap_size() is total (DRAM + 32 MB PSRAM), so it
      // never trips on this board. But task stacks (e.g. SensESP's 8 KB
      // WS connect worker) must come from INTERNAL RAM — if that pool
      // leaks or fragments over days, xTaskCreate fails and the SK WS can
      // never reconnect, stranding the helm on "connection lost" while
      // total heap still reads ~30 MB. Reboot to recover.
      //
      // Two conditions, because xTaskCreate needs a *contiguous* 8-bit
      // internal block, not just total free bytes: the 40 KB free-size
      // reserve catches slow depletion, and the 12 KB largest-block check
      // catches fragmentation (the 8 KB stack + TCB won't fit even when
      // plenty of total free remains). Applies to both boards, so it sits
      // outside the 4B guard below.
      ok = false;
      reason = "internal RAM exhausted";
#ifndef COCKPIT_BOARD_4B
    } else if (receiver->ever_received() &&
               receiver->seconds_since_last_rx() > 30) {
      ok = false;
      reason = "n2k rx stalled";
#endif
    }

    if (!ok) {
      consecutive_fail++;
      ESP_LOGW("watchdog", "health check FAIL %d/3: %s", consecutive_fail,
               reason);
      if (consecutive_fail >= 3) {
        ESP_LOGE("watchdog", "rebooting due to: %s", reason);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
      }
    } else {
      consecutive_fail = 0;
    }
  });

  event_loop()->onRepeat(33, []() { lvgl_tick(); });

  // event_loop liveness watchdog.
  //
  // The health-check watchdog above runs ON event_loop, so if
  // event_loop ever deadlocks (a lock held and never released, an
  // infinite loop in a callback) it can't fire — the panel hangs and
  // needs a manual power-cycle. This second watchdog runs on its own
  // FreeRTOS task: event_loop bumps a monotonic heartbeat every tick;
  // the task samples it once a second and force-reboots if it hasn't
  // advanced for 15 s. 15 s is comfortably longer than the worst
  // legitimate event_loop stall we expect (a ~5 s screenshot burst,
  // a layout apply draining the WS reconnect storm) but short enough
  // that a real hang self-recovers instead of stranding the helm.
  static volatile uint32_t s_event_loop_heartbeat = 0;
  event_loop()->onRepeat(250, []() { s_event_loop_heartbeat++; });
  xTaskCreate(
      [](void*) {
        uint32_t last = 0;
        int stalled_s = 0;
        for (;;) {
          vTaskDelay(pdMS_TO_TICKS(1000));
          uint32_t now = s_event_loop_heartbeat;
          if (now == last) {
            if (++stalled_s >= 15) {
              ESP_LOGE("el_wdt",
                       "event_loop stalled %ds (hb=%lu); rebooting",
                       stalled_s, (unsigned long)now);
              esp_restart();
            }
          } else {
            stalled_s = 0;
            last = now;
          }
        }
      },
      "el_wdt", 2560, nullptr, configMAX_PRIORITIES - 1, nullptr);

  // 1s status tick: WiFi / N2K / uptime / heap into the overlay.
  event_loop()->onRepeat(1000, [
#ifndef COCKPIT_BOARD_4B
                                    receiver, n2k_server
#endif
  ]() {
    if (WiFi.status() == WL_CONNECTED) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%s %ddBm", WiFi.SSID().c_str(),
               WiFi.RSSI());
      jlp::overlay().set_wifi(buf);
    } else {
      jlp::overlay().set_wifi("down");
    }

#ifndef COCKPIT_BOARD_4B
    int64_t rx_idle =
        receiver->ever_received() ? receiver->seconds_since_last_rx() : -1;
    jlp::overlay().set_n2k(rx_idle, n2k_server->connected_clients());
#else
    // No CAN transceiver on the 4B — report the gateway as absent
    // rather than leaving the field showing a stale placeholder.
    jlp::overlay().set_n2k(-1, 0);
#endif

    jlp::overlay().set_uptime_heap(millis() / 1000,
                                   esp_get_free_heap_size());
  });

#ifndef COCKPIT_BOARD_4B
  event_loop()->onRepeat(5000, [receiver, n2k_server]() {
    int64_t rx_idle = receiver->seconds_since_last_rx();
    // iram = free INTERNAL RAM (task stacks + WiFi/LWIP buffers live
    // here, not PSRAM); it — not the PSRAM-inflated total heap — is what
    // the watchdog reboots on. iram_min = lowest internal free ever seen
    // (the deepest burst dip); iram_big = largest contiguous internal
    // block (what xTaskCreate needs); psram = free PSRAM. heap = total.
    ESP_LOGI("main",
             "n2k: rx_idle=%llds cl=%u | heap=%lu iram=%u "
             "iram_min=%u iram_big=%u psram=%lu",
             (long long)(receiver->ever_received() ? rx_idle : -1),
             (unsigned)n2k_server->connected_clients(),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  });
#else
  // 4B has no N2K log, but the internal-RAM pressure (and the watchdog
  // that reboots on it) apply here too — emit the same breakdown so it's
  // observable on this board as well.
  event_loop()->onRepeat(5000, []() {
    ESP_LOGI("main",
             "heap=%lu iram=%u iram_min=%u iram_big=%u psram=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  });
#endif

  // One-time boot marker: the fresh internal-RAM baseline right after
  // setup, before the WS connects and the layout's listeners load. The
  // gap between this and the loaded steady-state is the structural cost
  // that lands in internal RAM; keeping it visible makes a regression
  // (or a config change that relocates buffers to PSRAM) obvious.
  ESP_LOGI("main", "boot: iram=%u iram_big=%u psram=%lu",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                      MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void loop() { event_loop()->tick(); }
