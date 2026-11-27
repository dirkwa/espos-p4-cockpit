// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace espos_voice {
class WyomingSatellite;
}

namespace jlp {

// Point the satellite at the server's wake service, so the wake word is
// configured once on the server instead of per panel. Call after the SignalK
// stream connects: boot has no route yet. Repeat calls are cheap; it re-runs
// only when the server changes, or after it stopped because the layout
// switched the wake word off.
void wake_discover_start(espos_voice::WyomingSatellite* sat);

}  // namespace jlp
