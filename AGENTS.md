# sensesp-p4-cockpit

ESP32-P4 firmware that runs the **JSON Layout Player (JLP)** — a
runtime-loadable widget engine on a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B
panel. The UI is JSON pushed via HTTP and rendered live with LVGL; no
firmware rebuild per layout change. The device also acts as a SignalK ↔
NMEA 2000 gateway (TWAI rx/tx + a candump TCP server on port 2599).

Companion projects:
- **signalk-hmi-designer** — the SignalK webapp that designs and pushes
  layouts. Lives at `../signalk-hmi-designer`.
- **SensESP** — the released registry library, pinned
  `SignalK/SensESP@^3.5.0` in `platformio.ini`. The local fork is
  retired: `SKPrefixListener` (the wildcard path-family listener the
  notifications registry needs) and the subscription-dedupe fix both
  landed upstream in 3.5.0 (PRs #1048/#1049), and the earlier
  `sendMeta=all` + `on_meta`→`SKMetadataListener` work is upstream too.
  No `../SensESP` checkout is needed to build.
- **sensesp-cockpit-display** / **sensesp-n2k-gateway** /
  **sensesp-ble-gateway** — sister libs, symlinked, contribute HAL, OTA,
  N2K gateway, candump server.

## Where to start reading

| File / dir                                            | Why                                       |
|-------------------------------------------------------|-------------------------------------------|
| [README.md](README.md)                                | High-level overview + endpoint table      |
| [JLP-PROTOCOL.md](JLP-PROTOCOL.md)                    | **The wire contract** — schema, endpoints, widget catalogue, alert overlay |
| [src/main.cpp](src/main.cpp)                          | Boot sequence — single source of truth    |
| [src/jlp/](src/jlp/)                                  | All player code                           |
| [src/jlp/widgets/widget_factory.cpp](src/jlp/widgets/widget_factory.cpp) | Every widget kind in one file |
| [src/jlp/layout/layout_manager.cpp](src/jlp/layout/layout_manager.cpp)   | Apply pipeline + atomic swap   |
| [src/jlp/net/http_api.cpp](src/jlp/net/http_api.cpp)                    | `/hello`, `/layout`, `/screenshot`, `/healthz` |
| [src/jlp/notifications_registry.cpp](src/jlp/notifications_registry.cpp)| Notifications + ack state              |
| [src/jlp/alert_overlay.cpp](src/jlp/alert_overlay.cpp)                  | Full-screen alarm modal                |
| [platformio.ini](platformio.ini)                                       | Toolchain pins + symlink lib_deps      |

## Architecture invariants

These are non-negotiable. Maintaining them is more important than any
single feature.

1. **Never blank the helm.** Layout parse + LVGL build happens under a
   hidden staging parent. Only after the new tree is ready do we swap
   it onto the live screen and delete the old one. A failed push
   returns 400/422/500 and the previous layout keeps rendering.
2. **No optimistic switch latch.** Toggle visual state derives from the
   subscription only. Press handlers PUT and rely on the SK echo to
   flip; a 500 ms reconciliation timer snaps back if no echo arrives.
3. **LVGL is single-writer** on the `event_loop` task. The HTTP task
   parses + validates, then marshals build/swap onto event_loop via
   `event_loop()->onDelay(0, ...)`. SK listener consumers
   (`SKValueListener`, `SKMetadataListener`, `SKPrefixListener`)
   already fire on event_loop, so their `lv_*` work needs no
   marshaling. No `lv_*` call from any other task. Ever.
4. **OSS only**, programmatic LVGL API only — no LVGL Pro / XML
   runtime / GPL deps.
5. **Wire format is additive.** New optional fields are fine. Removing
   a field, renaming, or changing semantics bumps `schema` from 1 → 2.

## Pipeline: parse → validate → stage → swap → persist

`LayoutManager::apply()` ([src/jlp/layout/layout_manager.cpp](src/jlp/layout/layout_manager.cpp)):

1. **Parse** the JSON via ArduinoJson, capped at 64 KB POST body.
2. **Validate** schema version, widget kinds, duplicate ids, kind
   conflicts on shared paths, geometry.
3. **Build** the LVGL tree under a hidden staging parent.
4. **Re-configure layout-level chrome** — `status_overlay`,
   `alert_overlay`.
5. **Atomic swap**: un-hide staging, set as current root, delete the
   previous root.
6. **Persist** to `/lfs/layout.json` via tmp + rename — only for
   `ApplySource::PostLayout` after a successful swap.

`ApplySource`: `BootStore` (LittleFS) → `BootDefault`
(`default_layout.h`) → `BootFetched` (async GET from SK
`applicationData`) → `PostLayout`. Boot priority: store > default;
applicationData lands afterwards if different. `POST /layout` always
wins at runtime.

## SignalK wiring

- The SK WS subscribes with `sendMeta=all` (SensESP default), so
  metadata deltas arrive in-stream.
- `zone_registry` caches `{zones, description}` per path. The metadata
  is fed by a per-path `SKMetadataListener` that `SubjectRegistry`
  creates alongside each bound path's `SKValueListener` (the REST
  cold-start fetch in `zone_fetch.cpp` also calls `apply_meta`).
  Widgets that bind a path read from it on every value change. Zones
  live in **raw SK units**; match against the raw value, not the
  display-scaled one.
- `notifications_registry` observes the whole `notifications.*` family
  via an `SKPrefixListener("notifications.")` (they're dynamic — no
  per-path listener can cover them). Each notification is keyed by the
  path-after-prefix; the registry tracks `{state, message}` and an
  `acked_` map for the local-ack feature.
- **ACKing a notification** sends an inbound SK delta with
  `state: "normal"` via `SKWSClient::sendTXT`. SignalK PUT-to-path is
  **not** wired through the server's notification manager — only the
  delta route is.
- Toggles/buttons emit SK PUT via SensESP's `SKPutRequest<T>`. Per-kind
  helpers in [src/jlp/net/sk_put.{h,cpp}](src/jlp/net/sk_put.h):
  `put_bool`, `put_int`, `put_float`, `put_string`,
  `put_notification_ack` (delta-based).

## Subjects, listeners, lifetimes

- `SubjectRegistry::get_or_create(path, kind)` lazily creates an
  `lv_subject_t` per bound path and registers a SensESP
  `SKValueListener<T>` that pushes incoming values into the subject.
- Subjects survive layout swaps; same path bound to the same kind is
  reused. **Kind conflicts** (e.g. one widget wants Float, another
  wants Int on the same path) are caught at validate-time and reject
  the layout.
- `NotificationsRegistry::on_change` returns an `ObserverToken`. **Any
  widget that captures a pointer in the callback MUST deregister via
  `off_change(token)` on `LV_EVENT_DELETE`**, otherwise the next
  notification delta after teardown will use-after-free. See
  `build_list()` for the pattern. The alert overlay is exempt — it's a
  process-lifetime singleton.

## Build / flash / debug

```bash
scripts/build.sh -e p4_cockpit         # build (7B, the default)
pio run -e p4_cockpit -t upload        # flash via /dev/ttyACM0
pio device monitor                     # local serial
nc <device-ip> 2323                    # remote ESP-IDF log stream
curl -sf http://<device-ip>:8081/hello | jq .
```

Prefer `scripts/build.sh` over a bare `pio run` on a small machine: a
full ESP-IDF build otherwise saturates every core (load ~8 on a 4-core
Pi) and the editor/SSH session stops being scheduled. The wrapper runs
the build at `nice -n 15`, `ionice -c3` and `-j $(nproc)-1` so one core
stays free for interactive work, and holds a lock so two builds can never
run at once (two concurrent `pio run`s put ~2x the core count of
compilers on the machine and drop the editor regardless of nice level;
they also race on `.pio/`). A second invocation waits for the first;
`BUILD_NOWAIT=1` makes it fail fast instead. It takes the same arguments
as `pio run`.

Two board targets share a common `[common]` base in `platformio.ini`:

- `p4_cockpit` — Waveshare 7B (1024×600 EK79007), onboard N2K gateway.
- `p4_cockpit_4b` — Waveshare 4B (720×720 ST7703). The 4B has no CAN
  transceiver, so `COCKPIT_BOARD_4B` compiles the N2K gateway out and
  swaps the board HAL. Build/flash with `-e p4_cockpit_4b`.

`/hello` reports the live panel geometry from the active HAL
(`display.w`/`display.h`), so the designer maps its canvas to whichever
board is connected.

If flashing dies with `OSError: [Errno 71] Protocol error` on
`_setDTRandRTS`, the cdc_acm CDC state is stuck. Manual download mode
(hold BOOT, tap RESET, release BOOT) always works; replug also helps.

### LVGL 9.5 helium asm

LVGL 9.5 ships `src/draw/sw/blend/helium/lv_blend_helium.S` which the
RISC-V toolchain can't assemble. `scripts/strip_lvgl_helium.py` runs
pre-build and deletes the directory. **Don't** remove the script or its
`extra_scripts` entry in `platformio.ini` without solving the upstream
problem.

### SensESP dependency

`SensESP=SignalK/SensESP@^3.5.0` in `lib_deps` is named explicitly so
this pin wins over the transitive `SignalK/SensESP>=3.3.0` the sister
libs declare. It resolves the released library from the registry into
`.pio/libdeps/<env>/SensESP` — no local checkout. Everything the
firmware relies on (`SKValueListener`, `SKMetadataListener`,
`SKPrefixListener`, `sendMeta=all`) is in 3.5.0. Bump the pin to adopt a
newer release; the local fork that used to live at `../SensESP` is
retired.

## Adding a widget kind

Each kind lives as a `build_<kind>(BuildCtx&, JsonObjectConst,
std::string* err)` function in
[widget_factory.cpp](src/jlp/widgets/widget_factory.cpp).

Mandatory:

1. Call `parse_colors(spec)` for `bg_color` / `fg_color` overrides;
   zone match wins when both apply.
2. Create the LVGL tree under `ctx.parent`. Zero the default outline
   and shadow (`lv_obj_set_style_outline_width/shadow_width(... 0
   ...)`); LVGL's default theme draws them and the designer won't
   match.
3. If you call `ctx.reg.get_or_create(path, kind)`, also
   `ctx.live_paths.insert(path)` so the registry knows which subjects
   are live for the new layout (for future GC).
4. Free all heap-allocated context in an `LV_EVENT_DELETE` handler. If
   you registered a `notifications().on_change(...)` observer, also
   call `off_change(token)` first.
5. Add the kind to the `/hello` widgets dictionary in
   [http_api.cpp](src/jlp/net/http_api.cpp) with its supported fields.
6. Dispatch in `build_widget()` at the bottom of widget_factory.cpp.
7. Document the kind in [JLP-PROTOCOL.md](JLP-PROTOCOL.md).

The same fields list must appear in the designer's `schema.ts`. The
designer refuses to push widget kinds the device doesn't advertise.

## Threading model

| Task              | Touches LVGL? | Notes |
|-------------------|---------------|-------|
| `event_loop` task | yes           | LVGL tick + listener callbacks + layout build/swap + alert overlay |
| `httpd_api` (8081)| no directly   | Parses + validates POSTs, marshals to event_loop, waits on completion semaphore |
| `httpd_ota` (8080)| no            | OTA only |
| `remote_log` (2323)| no           | TCP log forwarder |
| `esp_timer` 1 ms  | no            | `lv_tick_inc(1)` only — lock-free |
| SK WS task        | no            | Receives raw deltas onto a queue; `process_received_updates` drains + dispatches to listeners on the event_loop task |
| `audio` task      | no            | Drains the chime clip queue; blocking I2S write to the ES8311. `WaveshareAudio::play_pcm` (called from event_loop) copies + enqueues, never blocks |

## Repo conventions

- **Build/test gate**: `pio run -e p4_cockpit` must succeed. There are
  no host tests today — verify on device via remote log + curl probes.
- **Commits and PR titles**: Angular Conventional Commits —
  `type(scope): subject`, imperative, subject ≤ 50 chars. Types:
  `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`,
  `chore`. Scope is optional (`fix(wake): ...`). The release notes are
  generated from PR titles, so a vague title becomes a vague changelog
  entry. Commits stay focused and atomic.
- **Never commit local/boat configuration.** No WiFi SSIDs or
  passwords, no server IPs, no personal wake words — not in source,
  `platformio.ini`, or sdkconfig. `strings` on a firmware image prints
  every one of them, and the merged binary is a public release asset.
  Anything site-specific is a build-time `-D` with an empty default, so
  a stock build ships unprovisioned and comes up on the SensESP config
  portal.
- **Never auto-commit, never auto-push.** Do both only when the user
  explicitly asks.
- **No release-flow work** (version bumps, tags) unless the user says
  release.
- **No AI attribution anywhere.** No "Co-Authored-By: Claude", no
  CLAUDE.md content in the repo body, no AI-tool mentions in commits
  / PRs / code.
- **Code review**: `cr review --plain --type committed --base master`
  on a feature branch. Save output the first time; `cr` is rate-limited
  ~50 min between runs.
- **Comments**: WHY only. No echo comments, no "added for issue #X"
  rot bait, no multi-paragraph docstrings.

## Out of scope (deferred to v0.3+)

- Map / chart widget (raster or vector).
- Polar / AIS-radar plot.
- Media player binding.
- Text-input + canned-reply pills.
- List widget v2: vessels.\* iterator for AIS.
- Alert overlay `ack_method: "toast"`.
- LVGL-WASM pixel-perfect preview.
