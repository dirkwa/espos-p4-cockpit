#pragma once

#include <string>
#include <vector>

namespace jlp {

// Fetch SK meta and the current value of each path over the espOS REST
// client (espos_sk_get_meta / espos_sk_get_value — the same endpoints the
// designer uses) and feed them to zones() and the subject registry.
//
// Why REST on top of the stream: espOS opens the stream with sendMeta=all,
// but the server only sends what changes, so a path that is quiet when the
// panel subscribes (a steady tank level, an idle switch) has neither zones
// nor a reading until it moves. One GET pair per path per session fills
// that gap. Async — the GETs block, so they run on a fetch task and the
// results marshal back to the UI task in one ui::post.
void zone_fetch_for_paths(const std::vector<std::string>& paths);

}  // namespace jlp
