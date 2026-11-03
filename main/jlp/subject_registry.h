#pragma once

#include "lvgl.h"
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace jlp {

enum class SubjectKind { Float, Int, Bool, String };

struct SubjectEntry {
  std::string path;
  SubjectKind kind;
  // Owned by LVGL, not by this struct: lv_subject_create() allocates into an
  // LVGL-internal list and returns a pointer. Never deleted, which is correct
  // here rather than a leak -- garbage_collect() deliberately keeps every
  // subject for the device lifetime (widgets of the previous layout may still
  // be observing one while the swap animates), and the registry itself is a
  // function-local static. Nothing in this file destroys a SubjectEntry.
  lv_subject_t* subject = nullptr;
  // String storage (only used when kind == String). Stays here and is handed
  // to LVGL with lv_subject_set_string_buffer_static(), because a subject
  // created for LV_SUBJECT_TYPE_STRING has no buffer of its own.
  char str_buf[64];
  char str_prev[64];
  // esp_timer time (us) at which the stream task received the last delta
  // (null included) for this path, 0 for none since it was (re)subscribed.
  // zone_fetch skips its REST seed when this is later than the moment its
  // GET started. UI task only.
  int64_t stream_rx_us = 0;
  // When the GET of the last applied REST seed started, 0 for none. Two
  // fetches can overlap (boot layout and stream up); the older answer must
  // not replace the newer one. UI task only.
  int64_t seed_req_us = 0;
};

// Maps a widget bind -> lv_subject_t. Subjects are created lazily by
// widget builders during a layout build. For a SignalK path an espOS
// subscription is opened alongside so values (and meta) stream in; a
// panel-local sentinel (a bind starting with '@', such as "@brightness")
// gets a subject the panel feeds itself and no subscription.
//
// Threading: all calls happen on the UI thread. No mutex.
class SubjectRegistry {
 public:
  // Returns the subject for `path`, creating it (and its subscription)
  // on first call. nullptr if `path` exists with a different kind. A
  // path starting with '@' is a panel-local sentinel: it gets a subject
  // the panel feeds itself and no SignalK subscription.
  lv_subject_t* get_or_create(const std::string& path, SubjectKind kind);

  lv_subject_t* lookup(const std::string& path) const;
  /** Kind of the subject registered for `path`, or nullopt if the
   *  path isn't registered. Used by the zone-fetch path to drive
   *  the right typed setter when seeding a freshly-bound subject
   *  with SK's REST-fetched initial value. */
  std::optional<SubjectKind> kind_of(const std::string& path) const;
  /** When the stream last delivered a delta for `path` (esp_timer us),
   *  0 if none since it was (re)subscribed or the path is unknown. */
  int64_t stream_rx_us(const std::string& path) const;
  /** Whether a REST seed whose GET started at `requested_us` may be
   *  applied: not when a stream delta or an applied seed for the path is
   *  newer. Records it as the applied seed when it may. False for an
   *  unknown path. UI task only. */
  bool accept_seed(const std::string& path, int64_t requested_us);
  std::vector<std::string> paths() const;

  // Unsubscribe paths not in `live_paths` (subjects are kept).
  // Called after a successful layout swap.
  void garbage_collect(const std::set<std::string>& live_paths);

 private:
  struct Slot {
    std::unique_ptr<SubjectEntry> entry;
    int sub_handle = 0;   // espos_sk subscription (0 = none)
  };
  std::unordered_map<std::string, Slot> map_;
};

SubjectRegistry& registry();

}  // namespace jlp
