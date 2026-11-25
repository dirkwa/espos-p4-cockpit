#pragma once

namespace espos_voice {
class WyomingSatellite;
}

namespace jlp {

// Registers the layout API (/hello, /layout, /screen, /screenshot, /healthz
// and the audio and stream diagnostics) on the espOS httpd and starts the
// worker task that runs its long handlers. Call after espos_start().
void http_api_register();

// Register the Wyoming voice satellite so /hello can report its status.
// Null (the default) reports "unavailable".
void http_api_set_wyoming(espos_voice::WyomingSatellite* sat);

}  // namespace jlp
