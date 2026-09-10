# espos-p4-cockpit

ESP32-P4 firmware for the [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B) — a 1024×600 capacitive-touch helm display that doubles as an NMEA 2000 ↔ SignalK gateway.

**2.x runs on [espOS](https://github.com/dirkwa/espOS)** (pure ESP-IDF 6, no
Arduino, no SensESP): WiFi + provisioning portal, the config store and web
UI, SignalK discovery / access token / delta stream in both directions,
logs, core dumps and signed OTA with rollback all come from the `espos/`
submodule; this repository is the panel on top — display HAL, LVGL, the
JSON Layout Player and (in later 2.x phases) audio/voice, the N2K gateway
and the BLE gateway. 1.x (PlatformIO + SensESP) lives on the `master`
branch until 2.x reaches feature parity.

The UI is **runtime-loadable**: instead of rebuilding firmware per layout change, the device boots a JSON layout, renders it with LVGL widgets, and binds each widget to a SignalK path over WebSocket. Push a new layout via HTTP from [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer) and the screen swaps atomically — no flash, no reboot.

## Capabilities

- Native LVGL UI on the 7B's MIPI-DSI panel, capacitive touch.
- **JSON Layout Player (JLP)** — widgets driven by a JSON schema, hot-swap via `POST /layout`.
- Built-in widget kinds: `label`, `value`, `toggle`, `arc`, `bar`, `bargroup`, `button`, `notifications`.
- Two-way SignalK binding: widgets observe paths, taps emit SK PUTs (bool, int, float, string, notification ACK).
- **Zone-color tinting** from SK path metadata (nominal / alert / warn / alarm / emergency), maritime-helm palette.
- **Notifications subsystem** — subscribes to `notifications.*`, surfaces a full-screen alert overlay when any pending notification meets the configured `min_state`, ACK button PUTs the cleared state back as a delta.
- **Audible alert chime** — the panel speaker (ES8311 codec + NS4150B amp) sounds a severity-shaped tone when a notification newly escalates past `min_state` (single beep for warn, urgent double for alarm, fast triple for emergency). Silent on ack/clear.
- **Idle backlight dimmer** — configurable timeout + dim-to brightness per layout. Touch, an incoming notification, or a fresh push all wake the panel.
- **Per-widget colors** — optional `bg_color` / `fg_color` overrides (zone state still wins for alarm cases).
- **Configurable font size** on `label` / `value` widgets via `display.font_size`.
- Always-on status overlay (WiFi, SK WS, N2K rx age, heap, uptime) survives every layout swap.
- **NMEA 2000 gateway** — TWAI receiver/transmitter plus a candump-style TCP server (port 2599) so the bus is reachable from a laptop. `GET /api/v1/n2k` says what the bus is doing without a serial cable.

  **If the bus is silent, reboot the panel first.** The TWAI driver starts at boot and does not pick up a bus connected afterwards, so a panel powered before its network stays deaf until it is restarted — and it does not recover on its own, however long you wait. That order is the normal one on a boat, which makes this the common case rather than an edge one. Nothing reports it either: "never received" is deliberately not an alarm, because a panel may run with no N2K at all. Cause unproven, evidence in [signalk-espOS/espOS#15](https://github.com/signalk-espOS/espOS/issues/15).

  `GET /api/v1/n2k` tells the two apart: `running: true` with `frames: 0` *and* `errors: 0` means the wire is electrically quiet — unplugged, unpowered, or the driver missed the bus. Errors climbing with no frames means the opposite: the bus is live and not being understood (`ack_err` alone = nothing else listening; `stuff_err`/`form_err` = wrong bitrate).
- **Persistence** — successful pushes are saved to the `layout` NVS partition so the layout survives a power cycle, with a compiled-in default fallback.
- **Boot-time fetch** from SK `applicationData` so a fresh device picks up the fleet's last-known layout.
- mDNS-announced as `_signalk-player._tcp` so designers can discover it.
- Everything else a device needs — WiFi setup portal, config UI at `http://<device>/`, SignalK server discovery + access request, the REST client the panel fetches meta/values/layouts with, mDNS, the device watchdog (low memory, stalled task, stalled SignalK link), signed OTA from a URL or a version manifest with automatic rollback, log ring and core-dump viewer — is espOS ([its docs](https://github.com/dirkwa/espOS/tree/main/docs)).

Port status (2.x): display, layouts, SK values/meta/notifications, anchor
PUTs, the layout API, the audio/voice satellite (ES8311 + ES7210, esp-sr
WakeNet or signalk-openwakeword) and the NMEA 2000 gateway are all ported
and running on the panel. The BLE gateway is **not** part of 2.x: 1.x
linked the library but never instantiated it, and BLE scanning through the
C6 is blocked upstream — it stays a separate concern.

## Boot

`app_main` is one `espos_start()` call: espOS brings up its log ring and
config store, runs the panel bring-up as the `before_network` hook (display,
touch, LVGL and the stored layout are on screen within a second, then audio
and the voice satellite), and only then starts the HTTP server, WiFi, SignalK
and OTA. The N2K gateway, the layout API on :8081 and its
`_signalk-player._tcp` record follow. From there the panel reacts to espOS
events — network up/down, token approved, stream connected/disconnected —
rather than polling: the first stream connect seeds quiet paths and fetches
the fleet layout, and drives the status strip and the connection-lost banner.

Watchdog: espOS's health policy owns it (`espos/docs/health.md`) — `lowMemory`,
`taskStalled`, `netDown` (never fatal) and `skLinkStalled` are built in; the
panel adds `n2kBus` (fatal once a bus that talked falls silent for 30 s) and
registers its UI task as a watched task. Every condition also reaches the
server as `notifications.espos.<hostname>.<key>`.

## Boot priority chain

The device never blanks. At boot the JLP applies layouts in this order, and any later push from `POST /layout` (the design loop) takes precedence at runtime:

1. **NVS `layout` partition** — the last successful push.
2. **Compiled-in default** — minimal "no layout pushed yet" fallback in `default_layout.h`.
3. **SignalK `applicationData`** — async GET, applied if it differs from what's already on screen.

If a pushed or fetched layout fails parse/validate/build, the previous layout stays live and the request returns an error — the screen never goes dark.

## HTTP endpoints

| Port | Method | Endpoint        | Purpose                                       |
|------|--------|-----------------|-----------------------------------------------|
| 8081 | GET    | `/hello`        | Capability descriptor (schema, widgets, display, active layout, screenshot formats) |
| 8081 | POST   | `/layout`       | Apply a layout JSON (≤64 KB), atomic swap + NVS persist |
| 8081 | GET    | `/screenshot`   | Framebuffer dump. Default: software-encoded JPEG. `?fmt=bmp` for the legacy RGB565 BMP. |
| 8081 | POST   | `/screen`       | Select the active screen by `screens[].id` (remote tab tap) |
| 8081 | GET    | `/healthz`      | Liveness probe                                |
| 80   | *      | `/`, `/api/v1/…` | espOS web UI + REST API (config, WiFi, SignalK, OTA, logs, core dump) |
| 80   | GET    | `/api/v1/n2k`   | CAN bus diagnostics: `running`, `ever_received`, `idle_s`, frames, dropped, errors, bus_off, decoded last-error flags |
| 2599 | TCP    | (candump)       | Stream raw N2K frames in candump format       |
| 10700| TCP    | (wyoming)       | Voice satellite: the orchestrator dials in    |

The full contract — request/response shapes, schema, widget fields, error codes — is in [JLP-PROTOCOL.md](JLP-PROTOCOL.md).

## Architecture

```
   ┌────────────────────────────────────────────────────────┐
   │ HMI Designer (browser) ──POST /layout──> JLP HTTP API  │
   │                         <─── /hello /screenshot ───    │
   └────────────────────────────────────────────────────────┘
                                  │
                                  ▼
        ┌───────────────────────────────────────────────┐
        │ JLP LayoutManager (UI task, LVGL-only)        │
        │  parse → validate → stage (hidden) → swap     │
        │                                  → NVS store  │
        │                  → idle_dimmer.configure(...)  │
        │                  → alert_overlay.configure(...)│
        └───────────────────────────────────────────────┘
              │                                  ▲
              │ creates                          │ subject updates +
              ▼                                  │ zone meta + notifs
   ┌───────────────────────┐         ┌──────────────────────────┐
   │ LVGL widget tree      │         │ Subject + Zone +         │
   │ label / value /       │         │ Notifications registries │
   │ toggle / arc / bar /  │         └──────────────────────────┘
   │ bargroup / button /   │                  ▲
   │ notifications         │                  │
   └───────────────────────┘                  │ on_meta / on_value
                                              │ + REST /meta fetch
                                       ┌──────────────┐
                                       │ espOS SK     │──> SK server
                                       │ stream       │
                                       │ (sendMeta=all)│
                                       └──────────────┘

   ┌───────────────────────────────┐
   │ N2K gateway (TWAI rx/tx)      │── candump TCP server (port 2599)
   └───────────────────────────────┘
```

LVGL is single-threaded. One FreeRTOS task (`cockpit_hal::ui`) owns it; the HTTP task parses + validates a posted layout, then hands the build/swap over with `ui::post(...)`, and espOS' SignalK stream callbacks do the same for values, meta and notifications, so only one task ever touches LVGL.

The **status overlay** sits above the layout tabview in z-order and is created once at boot; it survives every layout swap. The **alert overlay** sits above the status overlay and pops up whenever an incoming notification clears the configured `min_state` threshold.

## Idle backlight dimmer

Layouts can opt the device into a power-save mode via the top-level `display` block:

```jsonc
{
  "schema": 1,
  "name": "Helm",
  "display": {
    "idle_timeout_sec": 300,   // 0 / omitted = always on
    "idle_dim_pct": 80         // 0-100, default 80
  },
  "screens": [...]
}
```

Hardware note: the Waveshare 7B's GT911 touch controller's sensitivity drops sharply as soon as the LCD backlight dims, because the LCD's continued sync signals dominate the touch sensing layer. **Tap-wake is only reliable at brightnesses close to full.** The 80 % default reads as clearly "asleep" while keeping tap-wake working. Lower dim levels save more power but only notifications and fresh layout pushes will then wake the panel.

Wake sources:
- any touch (always, but only effective at high `idle_dim_pct`)
- any incoming SignalK notification at or above the configured `min_state`
- a fresh `POST /layout`

## Build & flash

Pure ESP-IDF, pinned to the version in `.idf-version` (the same one espOS
pins). No PlatformIO, no Arduino.

```bash
git clone --recursive https://github.com/dirkwa/espos-p4-cockpit   # espos/ is a submodule
. ~/esp-idf-v6.0.2/export.sh                                          # or wherever that IDF lives
scripts/build.sh -DIDF_TARGET=esp32p4      # first build sets the target
scripts/build.sh                           # afterwards
scripts/build.sh -p /dev/ttyACM0 flash monitor
```

`scripts/build.sh` hands everything to espOS's wrapper
(`espos/scripts/build.sh`): one lock per machine so two espOS builds never
run at once, `nice`/`ionice`, `idf.py reconfigure` followed by `ninja` on
half the cores (IDF 6's `idf.py` has no `-j`; `BUILD_JOBS` overrides), and
`TMPDIR` moved off the RAM disk. Anything that is not a plain build
(`flash`, `monitor`, `menuconfig`) it passes to `idf.py` unchanged, still
locked. On a 4-core Pi a bare `idf.py build` takes the machine away from
your editor; on a big workstation it is fine.

No Node is needed: the espOS web UI bundle is committed in
`espos/ui/dist-gz` and packed into the `storage` partition by the build.

The sdkconfig is layered: espOS's `sdkconfig.d/espos.defaults(.esp32p4)`
first, this project's `sdkconfig.defaults` (only what differs — flash,
PSRAM, LVGL, esp-sr, the panel's memory tuning) on top, then a git-ignored
`sdkconfig.local` for bench overrides. After changing any of them delete
`sdkconfig` (or `build/sdkconfig`) once — IDF applies defaults only to a
fresh one.

The first build generates a *development* app-signing key
(`secure_boot_signing_key.pem`, git-ignored). Devices flashed with a
dev-key build only accept OTA images signed with that same key — for
anything you ship, create and keep your own key (espos/docs/ota.md).

Published releases are signed with a separate release key held as the
`COCKPIT_SIGNING_KEY_PEM` repository secret, so a locally-built device
does **not** accept release OTAs until it has been USB-flashed once with a
release image. After that first flash, release OTAs work normally.

### Forking

Ordinary builds need nothing: a development key is generated when
`secure_boot_signing_key.pem` is absent, so pushes and pull requests on a
fork build unchanged. Only **publishing a release** needs a decision —
either give CI a signing key so devices can take OTA updates:

```sh
espsecure generate-signing-key --version 2 --scheme rsa3072 signing_key.pem
gh secret set COCKPIT_SIGNING_KEY_PEM < signing_key.pem
```

…or set the repository *variable* `COCKPIT_ALLOW_UNSIGNED_RELEASE=true` to
release a USB-flash-only build, which is labelled as such in its own
release notes.

The reasoning behind both — why losing the key strands every flashed
device, and why there is deliberately no committed default key — is
[espOS' OTA documentation](https://github.com/dirkwa/espOS/blob/main/docs/ota.md#releasing-and-forking),
since signing is inherited from espOS and applies to every project built
on it.

To build such an image locally, drop the release key in as
`secure_boot_signing_key.pem` and rebuild. ESP-IDF's signing step depends
only on the unsigned binary, so swapping the key would otherwise leave the
*previous* signature in place with nothing in the build log to say so; the
root `CMakeLists.txt` fingerprints the key and forces a re-link when it
changes. Confirm before flashing anything you cannot recover over the air:

```sh
espsecure verify-signature --version 2 --keyfile <key>.pem build/cockpit.bin
```

**Provisioning**: no credentials are compiled in. A fresh panel raises the
`espOS-xxxx` setup portal (WiFi + SignalK) — or write an NVS image with
`wifi.ssid0/psk0` and `sk.server_*` (espos/docs/wifi.md). The SignalK access
request appears in the server's Security → Access Requests; approve it
once. Updates afterwards go over espOS OTA (`http://<device>/` → OTA, or
`POST /api/v1/ota {"url": …}`), signed and rollback-protected.

The build produces `build/cockpit.bin` (signed) and `build/storage.bin`
(the espOS web UI); `python scripts/merge_firmware.py --build-dir
build --chip esp32p4 --out p4_cockpit-merged.bin` makes the single image
the release workflow attaches (flash at `0x0`). Releases publish it as
`p4_cockpit-<version>-merged.bin`, alongside
`p4_cockpit-<version>-ota.bin` — the bare app image for OTA.

## Wake word

The hands-free wake word is now **runtime configuration**, not a build
variant. `cockpit.wake_host` empty runs on-device esp-sr WakeNet ("Hi ESP",
from the `model` partition); set it to the host running
[signalk-openwakeword](https://github.com/dirkwa/signalk-openwakeword) (plus
`cockpit.wake_word`) to stream the mic there instead and use any
custom-trained word. Both are set from the espOS config UI at
`http://<device>/` — no reflash. `GET /hello` reports which mode is live
under `wake`.

## Flashing without a toolchain (browser-based)

For the very first flash — or recovery if a device won't boot —
[ESP Tool](https://www.espboards.dev/tools/program/) flashes over USB
directly from the browser (Chrome/Edge). Every published
[release](https://github.com/dirkwa/espos-p4-cockpit/releases) has
`p4_cockpit-<version>-merged.bin` (bootloader + partition table + app +
web UI + wake-word model), flashable in one shot at offset `0x0`, flash
settings `dio` / `80m` / `16MB`. Hold **BOOT**, tap **RESET** if the board does not auto-enter
download mode.

## Push your first layout

Once the device log (`http://<device>/` → Logs) shows `advertising _signalk-player._tcp on port 8081`:

```bash
IP=<device-ip>
curl -sf http://$IP:8081/hello | jq .              # see what the device supports
curl -sf -X POST -H "Content-Type: application/json" \
  --data-binary @my-layout.json \
  http://$IP:8081/layout                            # swap atomically
```

Or open the SK admin UI, launch **HMI Designer**, point it at `http://<device-ip>:8081`, and drag.

## Related projects

- [signalk-hmi-designer](https://github.com/dirkwa/signalk-hmi-designer) — the SignalK webapp that designs and pushes layouts (drag-and-drop canvas, pixel-perfect WASM preview, live mirror mode).
- [sensesp-p4-cockpit-wasm](https://github.com/dirkwa/sensesp-p4-cockpit-wasm) — the same `widget_factory.cpp` compiled to WebAssembly so the designer can render layouts pixel-identically to the device without one being connected.
- [espOS](https://github.com/dirkwa/espOS) — the ESP-IDF 6 runtime under 2.x (submodule `espos/`).
- **sensesp-cockpit-display / sensesp-n2k-gateway / sensesp-ble-gateway / sensesp-wyoming-satellite** — the 1.x Arduino libraries; their contents are being folded into `components/` here during the 2.x port.

## License

espos-p4-cockpit (and sensesp-p4-cockpit 1.0.0 and later, its previous name) is
**source available, not open source**.
See [LICENSE.md](LICENSE.md).

**You may**, free of charge: run it on your own boat or fleet, private or
commercial; use it for internal company operations; modify it for your own use;
use it in education and research; and provide professional services around it.

**You may not**: redistribute it, or publish a modified version of it — as
source, firmware images or otherwise. Verbatim copies of official releases
(including the published firmware binaries) may be mirrored and cached.

Releases 0.3.0 and earlier were published without a license file. The vendored
Espressif display drivers under `components/` remain Apache-2.0; see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
