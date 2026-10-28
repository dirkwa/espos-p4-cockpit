#pragma once

#include "lvgl.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace jlp {

enum class ApplySource { Boot, BootStore, BootDefault, PostLayout, BootFetched };

struct ApplyResult {
  bool ok;
  std::string err;
  std::string name;       // root "name" field on success
  unsigned screens;
  unsigned widgets;
  std::string warning;    // non-fatal note, e.g. persistence failed
};

class LayoutManager {
 public:
  // `parent` is where the screens/tabview goes. Set once at boot.
  void init(lv_obj_t* parent);

  // Fires after every successful apply + swap. `new_paths` is the set
  // of bound paths the new layout introduced that weren't already in
  // the previous one (or all paths on first apply); the bool says
  // whether it is non-empty. espOS subscribes new paths by itself, but
  // SK streams only on change, so the hook seeds their values via REST.
  // `src` and `all_paths` let the hook re-seed values on a boot apply
  // (BootStore / BootFetched), where no paths are "new" but the quiet
  // ones — e.g. navigation.anchor.maxRadius — still need backfilling
  // because their delta was missed before the WS subscribed.
  using PostSwapHook =
      std::function<void(bool new_paths_introduced,
                         const std::set<std::string>& new_paths,
                         ApplySource src,
                         const std::set<std::string>& all_paths)>;
  void set_post_swap_hook(PostSwapHook h) { post_swap_ = std::move(h); }

  // Parses, validates, builds, and (on success) swaps in the new
  // layout. On failure, the current layout stays up.
  ApplyResult apply(const std::string& json, ApplySource src);

  const std::string& active_name() const { return active_name_; }
  ApplySource active_source() const { return active_source_; }

  // Remote equivalent of tapping a tab. Addressed by the layout's
  // `screens[].id` because the index is not stable across layout edits.
  // False if the id is unknown, or if the active layout is single-screen
  // (no tab strip exists, so there is nothing to select).
  //
  // UI TASK ONLY — all three touch LVGL. Callers on the HTTP task must
  // marshal with cockpit_hal::ui::post().
  bool select_screen(const std::string& id);

  // Id of the screen currently shown; empty for a single-screen layout.
  // Lets a caller see what the helm is on before changing it, and put it
  // back afterwards.
  std::string active_screen() const;

  // Screen ids of the active layout, in tab order. Empty when
  // single-screen.
  std::vector<std::string> screen_ids() const;

  // Paths bound by the currently-applied layout. Used by boot to seed
  // the values of a layout that was applied (from the store) before the
  // network — and thus before the post-swap hook — was ready.
  const std::set<std::string>& known_paths() const { return known_paths_; }

 private:
  lv_obj_t* parent_ = nullptr;
  lv_obj_t* current_root_ = nullptr;
  std::string active_name_;
  ApplySource active_source_ = ApplySource::Boot;
  PostSwapHook post_swap_;
  // Paths the currently-applied layout binds. Used to detect whether
  // the next swap introduces brand-new paths (which would require a
  // SK WS reconnect for the subscription to include them).
  std::set<std::string> known_paths_;
};

LayoutManager& layout_manager();

}  // namespace jlp
