#pragma once

namespace jlp {

// Schedules a one-shot GET of the fleet layout from the SignalK server's
// applicationData and, on success, hands the body to LayoutManager::apply
// with ApplySource::BootFetched. Anything but a 200 (no layout stored, no
// server, unreachable) silently leaves the current layout alone.
//
// Call once the SignalK stream is up; safe from the UI task (the blocking
// GET runs on its own task).
void layout_fetch_async_apply();

}  // namespace jlp
