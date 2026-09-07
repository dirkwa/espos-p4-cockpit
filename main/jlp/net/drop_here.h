#pragma once

namespace jlp {

// Drop the anchor at the boat's current fix. The panel can't hold
// navigation.position as a normal subject (it's an object, not a scalar
// kind), so instead of caching it we fetch it once on demand: a detached
// task GETs it through the espOS REST client (espos_sk_get_value), then
// PUTs {latitude, longitude} to navigation.anchor.position, which the
// anchor-alarm plugin drops on. Works against the deployed plugin as-is
// (2.8.0+) — it supplies the position explicitly, so it doesn't need
// 2.9.0's optional-position rework. Fire-and-forget; safe to call from
// the UI task (the blocking GET runs on its own task).
void drop_anchor_here();

}  // namespace jlp
