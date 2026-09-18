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
  lv_subject_t subject;
  // String storage (only used when kind == String).
  char str_buf[64];
  char str_prev[64];
};

// Maps SignalK path -> lv_subject_t. Subjects are created lazily by
// widget builders during a layout build, and an espOS SignalK
// subscription is opened alongside each so values (and meta) stream in.
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
