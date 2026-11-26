// SPDX-License-Identifier: Apache-2.0
#include "layout_player.h"

#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "cockpit_hal/ui.h"

#include "../subject_registry.h"
#include "../widgets/widget_factory.h"

namespace jlp {

namespace {

constexpr int kSchemaVersion = 1;
constexpr size_t kMaxJsonBytes = 64 * 1024;


// A page swipe travels this far, mostly sideways. A swipe up for the
// overlay strip starts this close to the bottom edge and travels the
// second distance; the band is a little wider than a finger coming off
// the bezel needs.
constexpr int32_t kPageSwipeMin = 40;
constexpr int32_t kEdgeBand = 24;
constexpr int32_t kEdgeSwipeMin = 30;

struct ScreenSwitcherCtx {
  std::vector<lv_obj_t*> screens;
  std::vector<lv_obj_t*> tabs;
  // Screen ids, parallel to `screens`. Index is not a stable handle
  // across layout edits, so remote selection addresses screens by id.
  std::vector<std::string> ids;
  int active = 0;
  lv_obj_t* strip = nullptr;
  // tab_strip "overlay-bottom": the strip floats above the page, hidden
  // except while it peeks.
  bool overlay = false;
  bool peeking = false;
  uint32_t peek_start = 0;
};

// One per layout. Lives in heap, freed when the layout's root is
// destroyed (event_cb on root).
static void switcher_show(ScreenSwitcherCtx* ctx, int idx) {
  if (idx < 0 || idx >= (int)ctx->screens.size()) return;
  for (size_t i = 0; i < ctx->screens.size(); i++) {
    lv_obj_set_hidden(ctx->screens[i], (int)i != idx);
  }
  for (size_t i = 0; i < ctx->tabs.size(); i++) {
    bool a = (int)i == idx;
    const theme::Palette& p = theme::palette();
    lv_obj_set_style_bg_color(ctx->tabs[i], lv_color_hex(a ? p.accent : p.tab),
                              LV_PART_MAIN);
    lv_obj_t* lbl = lv_obj_get_child(ctx->tabs[i], 0);
    if (lbl) {
      lv_obj_set_style_text_color(
          lbl, lv_color_hex(a ? p.on_accent : p.tab_fg), LV_PART_MAIN);
    }
  }
  ctx->active = idx;
  // Stream widgets open/close their network stream with screen visibility —
  // a hidden screen must not keep pulling frames through the hosted link.
  for (size_t i = 0; i < ctx->screens.size(); i++) {
    stream_widgets_notify_visibility(ctx->screens[i], (int)i == idx);
  }
}

// Shows or hides an overlay strip; a fixed strip ignores it.
static void strip_peek(ScreenSwitcherCtx* ctx, bool on) {
  if (!ctx->overlay || ctx->strip == nullptr) return;
  ctx->peeking = on;
  if (on) ctx->peek_start = lv_tick_get();
  lv_obj_set_hidden(ctx->strip, !on);
}

// read_only: a transparent cover over each page's widgets takes every press,
// so no widget acts on it, while the tab strip stays outside the covers and
// a swipe that starts on one still turns the page. Tagged by user_data so
// it can be found again; the widgets never set that pointer.
char cover_tag;

lv_obj_t* cover_of(lv_obj_t* page) {
  const uint32_t n = lv_obj_get_child_count(page);
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t* c = lv_obj_get_child(page, (int32_t)i);
    if (lv_obj_get_user_data(c) == &cover_tag) return c;
  }
  return nullptr;
}

// False only when a cover could not be made.
bool set_cover(lv_obj_t* page, bool on) {
  lv_obj_t* cover = cover_of(page);
  if (!on) {
    if (cover != nullptr) lv_obj_delete(cover);
    return true;
  }
  if (cover == nullptr) {
    cover = lv_obj_create(page);
    if (cover == nullptr) return false;
    lv_obj_remove_style_all(cover);
    lv_obj_set_ignore_layout(cover, true);
    lv_obj_set_floating(cover, true);
    lv_obj_set_scrollable(cover, false);
    lv_obj_set_scroll_chain(cover, false);
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_size(cover, lv_pct(100), lv_pct(100));
    lv_obj_set_user_data(cover, &cover_tag);
  }
  lv_obj_move_to_index(cover, -1);
  return true;
}

// Builds the screen(s) under `root`. Single screen: widgets parented
// directly to root. Multi-screen: each screen gets its own container
// stacked top-to-top, only the active one is visible, and a bottom
// strip of tab buttons switches between them.
bool build_screens(lv_obj_t* root, JsonObjectConst doc, JsonArrayConst screens,
                   SubjectRegistry& reg, std::string* err,
                   std::set<std::string>* live_paths, unsigned* widget_count) {
  if (screens.size() == 0) { *err = "no screens"; return false; }

  if (screens.size() == 1) {
    JsonObjectConst s = screens[0];
    JsonArrayConst widgets = s["widgets"];
    BuildCtx ctx{root, reg, *live_paths};
    for (JsonObjectConst w : widgets) {
      if (!build_widget(ctx, w, err)) return false;
      ++*widget_count;
    }
    return true;
  }

  // Multi-screen: stack of hidden containers + bottom tab strip.
  const int strip_h = doc["tab_strip_height"] | 56;
  auto* sctx = new ScreenSwitcherCtx();
  // An unknown mode is drawn fixed: a newer client's value must not
  // hide the tabs on a device that cannot bring them back.
  sctx->overlay =
      strcmp(doc["tab_strip"] | kTabStripFixed, kTabStripOverlayBottom) == 0;

  // Free the switcher context when the layout root dies.
  lv_obj_set_user_data(root, sctx);
  lv_obj_add_event_cb(
      root,
      [](lv_event_t* e) {
        delete static_cast<ScreenSwitcherCtx*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
      },
      LV_EVENT_DELETE, nullptr);

  // `root` is a column: the visible page fills what the strip leaves, the
  // strip sits at the bottom. Sized from root's real height, which is the
  // display minus the status strip when that is shown. A page sized
  // LV_VER_RES - strip_h overlapped the tabs by the status strip's 28 px and,
  // being above them in z-order, took their taps (half of each tab at the
  // default 56 px). Hidden pages leave the flex flow. An overlay strip
  // is out of the flow: the pages take all of root and the strip floats
  // over their bottom.
  if (!sctx->overlay) {
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 0, LV_PART_MAIN);
  }

  // A null parent makes LVGL create a new screen instead of a child, so
  // a failed allocation must fail the build: only a complete tree swaps in.
  lv_obj_t* strip = lv_obj_create(root);
  if (strip == nullptr) {
    *err = "out of memory";
    return false;
  }
  lv_obj_set_size(strip, lv_pct(100), strip_h);
  lv_obj_set_style_bg_color(strip, lv_color_hex(theme::palette().strip),
                            LV_PART_MAIN);
  lv_obj_set_style_border_width(strip, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(strip, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(strip, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(strip, 4, LV_PART_MAIN);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable(strip, false);
  if (sctx->overlay) {
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_hidden(strip, true);
  }
  sctx->strip = strip;

  // Per-screen container above the strip, all sharing the same area.
  for (JsonObjectConst s : screens) {
    lv_obj_t* page = lv_obj_create(root);
    if (page == nullptr) {
      *err = "out of memory";
      return false;
    }
    lv_obj_set_width(page, lv_pct(100));
    if (sctx->overlay) {
      lv_obj_set_height(page, lv_pct(100));
    } else {
      lv_obj_set_flex_grow(page, 1);
    }
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    sctx->screens.push_back(page);
    sctx->ids.push_back(s["id"] | "");

    JsonArrayConst widgets = s["widgets"];
    BuildCtx ctx{page, reg, *live_paths};
    for (JsonObjectConst w : widgets) {
      if (!build_widget(ctx, w, err)) return false;
      ++*widget_count;
    }
  }
  // Last child: below the pages in the column, or painted above them
  // as an overlay.
  lv_obj_move_to_index(strip, -1);

  // Tab buttons (one per screen, flex-row equal widths).
  for (size_t i = 0; i < screens.size(); i++) {
    JsonObjectConst s = screens[i];
    const char* title = s["title"] | (s["id"] | "?");

    lv_obj_t* btn = lv_obj_create(strip);
    if (btn == nullptr) {
      *err = "out of memory";
      return false;
    }
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, lv_pct(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(theme::palette().tab),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(btn, false);

    lv_obj_t* lbl = lv_label_create(btn);
    if (lbl == nullptr) {
      *err = "out of memory";
      return false;
    }
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(theme::palette().tab_fg),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(lbl);

    // Stash the screen index as a small int in user_data.
    lv_obj_set_user_data(btn, (void*)(intptr_t)i);
    lv_obj_add_event_cb(
        btn,
        [](lv_event_t* e) {
          auto* w = static_cast<lv_obj_t*>(lv_event_get_target(e));
          auto* parent_root = lv_obj_get_parent(lv_obj_get_parent(w));
          auto* ctx =
              static_cast<ScreenSwitcherCtx*>(lv_obj_get_user_data(parent_root));
          int idx = (int)(intptr_t)lv_obj_get_user_data(w);
          if (ctx) {
            switcher_show(ctx, idx);
            strip_peek(ctx, false);  // the choice is made
          }
        },
        LV_EVENT_CLICKED, nullptr);

    sctx->tabs.push_back(btn);
  }

  switcher_show(sctx, 0);
  return true;
}

// Screen selection by id, for the tab strip's remote equivalent.
// Both must run on the UI task: they touch LVGL and read the ctx that
// the layout root owns. A single-screen layout has no switcher at all
// (build_screens() never allocates one), so user_data is NULL there —
// hence the guard rather than a bare cast.
static ScreenSwitcherCtx* switcher_of(lv_obj_t* root) {
  if (!root) return nullptr;
  return static_cast<ScreenSwitcherCtx*>(lv_obj_get_user_data(root));
}

// Covers or uncovers every page of a built layout. A single-screen layout
// has its widgets on the root itself. False when a page is left uncovered.
static bool cover_layout(lv_obj_t* root, bool on) {
  if (root == nullptr) return true;
  ScreenSwitcherCtx* ctx = switcher_of(root);
  if (ctx == nullptr) return set_cover(root, on);
  bool all = true;
  for (lv_obj_t* page : ctx->screens) all = set_cover(page, on) && all;
  return all;
}

// Subject kind a given widget type expects on its `bind` path. Must
// stay in lockstep with the get_or_create() calls in widget_factory.cpp.
// Returns nullopt for widgets that don't bind a path at all (button,
// notifications) — those skip the kind-conflict check.
std::optional<SubjectKind> expected_subject_kind(const char* type) {
  if (!type) return std::nullopt;
  if (!strcmp(type, "label"))    return SubjectKind::Float;
  if (!strcmp(type, "value"))    return SubjectKind::Float;
  if (!strcmp(type, "arc"))      return SubjectKind::Float;
  if (!strcmp(type, "bar"))      return SubjectKind::Float;
  if (!strcmp(type, "toggle"))   return SubjectKind::Int;
  // bargroup: handled separately because each sub-bar is its own
  // (path, kind) pair. button + notifications: no bind. Anything
  // unknown returns nullopt and is left to validate's existing
  // dispatch error path.
  return std::nullopt;
}

const char* subject_kind_name(SubjectKind k) {
  switch (k) {
    case SubjectKind::Float:  return "Float";
    case SubjectKind::Int:    return "Int";
    case SubjectKind::Bool:   return "Bool";
    case SubjectKind::String: return "String";
  }
  return "?";
}

// Record a (path, kind) requirement into `seen`. Returns false +
// fills *err on conflict (path already requested with a different
// kind).
bool record_path_kind(const std::string& path, SubjectKind kind,
                      std::unordered_map<std::string, SubjectKind>* seen,
                      const std::string& widget_label, std::string* err) {
  auto it = seen->find(path);
  if (it == seen->end()) {
    (*seen)[path] = kind;
    return true;
  }
  if (it->second != kind) {
    *err = std::string("kind conflict on bind path '") + path +
           "': " + widget_label + " wants " + subject_kind_name(kind) +
           " but a previous widget already bound it as " +
           subject_kind_name(it->second);
    return false;
  }
  return true;
}

// Validate global structure before building. Catches duplicates,
// missing required fields, AND kind conflicts on shared bind paths
// (e.g. one widget wants a path as Float, another as Int) so the
// layout is rejected up-front rather than half-built in LVGL.
bool validate(JsonObjectConst doc, std::string* err) {
  int schema = doc["schema"] | 0;
  if (schema != kSchemaVersion) {
    char buf[64];
    snprintf(buf, sizeof(buf), "unsupported schema %d (want %d)", schema,
             kSchemaVersion);
    *err = buf;
    return false;
  }
  JsonArrayConst screens = doc["screens"];
  if (screens.isNull() || screens.size() == 0) {
    *err = "no screens";
    return false;
  }

  std::unordered_set<std::string> ids;
  // path -> kind that's already been requested across the whole
  // layout. Survives screens, since widgets on different screens can
  // still bind the same path.
  std::unordered_map<std::string, SubjectKind> seen_paths;
  for (JsonObjectConst s : screens) {
    const char* sid = s["id"] | (const char*)nullptr;
    if (!sid) { *err = "screen missing id"; return false; }
    if (!ids.insert(sid).second) {
      *err = std::string("duplicate screen id: ") + sid;
      return false;
    }
    JsonArrayConst widgets = s["widgets"];
    std::unordered_set<std::string> wids;
    for (JsonObjectConst w : widgets) {
      const char* wid = w["id"] | (const char*)nullptr;
      if (!wid) { *err = "widget missing id"; return false; }
      if (!wids.insert(wid).second) {
        *err = std::string("duplicate widget id in screen ") + sid + ": " + wid;
        return false;
      }
      const char* type = w["type"] | (const char*)nullptr;
      if (!type) {
        *err = std::string("widget missing type in screen ") + sid;
        return false;
      }

      // Kind-conflict check. bargroup is a bag of independent sub-
      // bars; everything else has at most one bind path.
      const std::string label = std::string(type) + " '" + wid + "'";
      if (!strcmp(type, "bargroup")) {
        JsonArrayConst bars = w["bars"];
        for (JsonObjectConst b : bars) {
          const char* bp = b["bind"] | (const char*)nullptr;
          if (!bp) continue;  // sub-bar with no bind is a render-only stub
          if (!record_path_kind(bp, SubjectKind::Float, &seen_paths,
                                label, err)) {
            return false;
          }
        }
        continue;
      }
      const char* bind = w["bind"] | (const char*)nullptr;
      if (!bind) continue;  // toggle requires bind, others don't —
                            // missing-required-bind is build_*'s job
      auto kind = expected_subject_kind(type);
      if (!kind) continue;  // unknown / no-bind kinds: leave to dispatch
      if (!record_path_kind(bind, *kind, &seen_paths, label, err)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

// The press landed on the layout, not on an overlay above it (alert,
// wake, status strip).
static bool inside(lv_obj_t* obj, lv_obj_t* root) {
  for (lv_obj_t* o = obj; o != nullptr; o = lv_obj_get_parent(o)) {
    if (o == root) return true;
  }
  return false;
}

// A slider or an arc follows the finger; LVGL ends its drag itself.
static bool slides(lv_obj_t* obj, lv_obj_t* root) {
  for (lv_obj_t* o = obj; o != nullptr && o != root; o = lv_obj_get_parent(o)) {
    if (lv_obj_check_type(o, &lv_slider_class) ||
        lv_obj_check_type(o, &lv_arc_class)) {
      return true;
    }
  }
  return false;
}

// A press that must not turn the page. A drag on a slider or arc moves it.
// An image, which only the stream widget creates, forwards the drag to the
// remote browser, and its release would be lost with the page. A momentary
// button sent its press on touch-down, and a swipe would turn the page
// under it.
static bool drags(lv_obj_t* obj, lv_obj_t* root) {
  if (slides(obj, root)) return true;
  for (lv_obj_t* o = obj; o != nullptr && o != root; o = lv_obj_get_parent(o)) {
    if (lv_obj_check_type(o, &lv_image_class) || is_press_keeper(o)) {
      return true;
    }
  }
  return false;
}

AlertConfig parse_alert_config(JsonObjectConst doc) {
  // Defaults: enabled + min_state alarm. `levels` (optional) overrides, per
  // state, what min_state implies: show, sound (off / source / always) and
  // repeat_s. A state it leaves out keeps the min_state default, so an
  // older layout behaves as before.
  AlertConfig c;
  NotState notif_min = NotState::Alarm;
  JsonObjectConst notif = doc["notifications"];
  if (!notif.isNull()) {
    if (notif["enabled"].is<bool>()) c.enabled = notif["enabled"];
    notif_min = parse_not_state(notif["min_state"] | "alarm");
  }
  c.levels = policies_from_min_state(notif_min);
  JsonObjectConst lv = notif["levels"];
  static constexpr const char* kLevelNames[] = {"alert", "warn", "alarm", "emergency"};
  for (size_t i = 0; i < c.levels.size() && !lv.isNull(); ++i) {
    JsonObjectConst l = lv[kLevelNames[i]];
    if (l.isNull()) continue;
    if (l["show"].is<bool>()) c.levels[i].show = l["show"];
    const char* snd = l["sound"] | (const char*)nullptr;
    if (snd && !strcmp(snd, "off")) c.levels[i].sound = SoundPolicy::Off;
    if (snd && !strcmp(snd, "source")) c.levels[i].sound = SoundPolicy::Source;
    if (snd && !strcmp(snd, "always")) c.levels[i].sound = SoundPolicy::Always;
    if (l["repeat_s"].is<double>()) {
      // Read as a number, so a value past the int range clamps rather
      // than leaving the level at one chime.
      const double r = l["repeat_s"];
      c.levels[i].repeat_s = static_cast<uint16_t>(r < 0 ? 0 : (r > 3600 ? 3600 : r));
    }
  }
  return c;
}

LayoutPlayer::~LayoutPlayer() { detach(); }

void LayoutPlayer::detach() {
  cancel_swipe();
  if (peek_timer_ != 0) {
    cockpit_hal::ui::cancel(peek_timer_);
    peek_timer_ = 0;
  }
  if (indev_ != nullptr) {
    lv_indev_remove_event_cb_with_user_data(indev_, on_press_event, this);
    indev_ = nullptr;
  }
}

void LayoutPlayer::on_press_event(lv_event_t* e) {
  static_cast<LayoutPlayer*>(lv_event_get_user_data(e))->on_press();
}

void LayoutPlayer::init(lv_obj_t* parent, ChromeHook chrome) {
  detach();
  // A tree under another parent is the caller's to delete; the player
  // forgets it rather than steer or peek into it.
  if (parent != parent_) {
    current_root_ = nullptr;
    active_json_.clear();
  }
  parent_ = parent;
  chrome_ = std::move(chrome);
  // Presses are watched on the input device, not on an object: a swipe
  // may start on any widget of the page.
  indev_ = lv_indev_get_next(nullptr);
  if (indev_ != nullptr) {
    lv_indev_add_event_cb(indev_, on_press_event, LV_EVENT_PRESSED, this);
  }
  peek_timer_ = cockpit_hal::ui::every(250, [this]() {
    ScreenSwitcherCtx* ctx = switcher_of(current_root_);
    if (ctx != nullptr && ctx->peeking &&
        lv_tick_elaps(ctx->peek_start) >= tab_peek_ms_) {
      strip_peek(ctx, false);
    }
  });
}

void LayoutPlayer::on_press() {
  lv_indev_get_point(indev_, &press_);
  lv_obj_t* hit = lv_indev_search_obj(lv_screen_active(), &press_);
  // Where the press began decides for select_screen(): the widget keeps
  // the press (PRESS_LOCK) even when the finger moves onto an overlay.
  press_on_layout_ = inside(hit, current_root_);
  // Called from the PRESSED event, while LVGL has the pressed object set.
  pressed_ = lv_indev_get_active_obj();
  ScreenSwitcherCtx* ctx = switcher_of(current_root_);
  // A tracker still running belongs to a press that has ended, released
  // within one poll; this press gets its own, measured from its own start.
  cancel_swipe();
  if (ctx == nullptr) return;
  if (ctx->peeking) ctx->peek_start = lv_tick_get();  // still choosing
  if (!press_on_layout_ || drags(hit, current_root_)) return;
  press_at_edge_ = ctx->overlay && !ctx->peeking &&
                   press_.y >= LV_VER_RES - kEdgeBand;
  // PRESSING reaches objects only, so the movement is sampled here.
  swipe_timer_ = cockpit_hal::ui::every(20, [this]() { track_swipe(); });
}

void LayoutPlayer::cancel_swipe() {
  if (swipe_timer_ == 0) return;
  cockpit_hal::ui::cancel(swipe_timer_);
  swipe_timer_ = 0;
}

// Ends the current press now. lv_indev_wait_release alone sends the widget
// PRESS_LOST only when the finger lifts, so a button that has sent its
// press value would hold it while its page is already gone. The widget the
// press began on gets PRESS_LOST at once instead, before a page change
// hides it; on the release LVGL sends a second one, which a button that has
// already released ignores. It is the object LVGL holds as pressed, captured
// in on_press, since a press-locked widget keeps the press wherever the
// finger moves; lv_obj_is_valid covers one a layout swap has deleted. A
// slider or an arc is left to LVGL: their handlers expect a live input
// device.
void LayoutPlayer::end_press() {
  lv_obj_t* obj = pressed_;
  if (obj != nullptr && lv_obj_is_valid(obj) && inside(obj, current_root_) &&
      !slides(obj, current_root_)) {
    lv_obj_send_event(obj, LV_EVENT_PRESS_LOST, indev_);
  }
  lv_indev_wait_release(indev_);
}

void LayoutPlayer::track_swipe() {
  ScreenSwitcherCtx* ctx = switcher_of(current_root_);
  // A scrolling list keeps its own drag.
  bool done = ctx == nullptr ||
              lv_indev_get_state(indev_) != LV_INDEV_STATE_PRESSED ||
              lv_indev_get_scroll_obj(indev_) != nullptr;
  if (!done) {
    lv_point_t p{};
    lv_indev_get_point(indev_, &p);
    const int32_t dx = p.x - press_.x;
    const int32_t dy = p.y - press_.y;
    if (LV_ABS(dx) >= kPageSwipeMin && LV_ABS(dx) > 2 * LV_ABS(dy)) {
      // The widget under the finger gets PRESS_LOST instead of a click.
      end_press();
      // Left brings the next page, as on a phone, and the last page
      // wraps to the first.
      const int n = (int)ctx->screens.size();
      switcher_show(ctx, (ctx->active + (dx < 0 ? 1 : -1) + n) % n);
      strip_peek(ctx, true);  // shows where the swipe landed
      done = true;
    } else if (press_at_edge_ && -dy >= kEdgeSwipeMin && -dy > LV_ABS(dx)) {
      end_press();
      strip_peek(ctx, true);
      done = true;
    }
  }
  if (done) cancel_swipe();
}

LayoutPlayer::Result LayoutPlayer::apply(const std::string& json) {
  return apply_impl(json, false);
}

LayoutPlayer::Result LayoutPlayer::rebuild_for_theme() {
  if (active_json_.empty() || theme::resolved() == built_mode_) {
    Result r;
    r.ok = true;
    return r;
  }
  // By index: the same layout has the same screens, and an id may be empty.
  ScreenSwitcherCtx* before = switcher_of(current_root_);
  const int active = before != nullptr ? before->active : -1;
  Result r = apply_impl(active_json_, true);
  ScreenSwitcherCtx* after = switcher_of(current_root_);
  if (r.ok && after != nullptr && active > 0 &&
      active < (int)after->screens.size()) {
    switcher_show(after, active);
  }
  return r;
}

LayoutPlayer::Result LayoutPlayer::apply_impl(const std::string& json,
                                              bool theme_rebuild) {
  Result r;
  if (!parent_) { r.err = "layout player not initialised"; return r; }

  if (json.size() > kMaxJsonBytes) {
    r.err = "layout too large";
    return r;
  }

  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  if (de) { r.err = std::string("parse: ") + de.c_str(); return r; }

  if (!validate(doc.as<JsonObjectConst>(), &r.err)) return r;

  // Atomic swap: build the new tree as a child of `parent_` but hidden,
  // then unhide on success and delete the old. If the build fails, the
  // current layout stays up untouched. Staging is a regular child of
  // `parent_`: lv_obj_create(NULL) would make a screen, and a screen cannot
  // be moved under another object in LVGL 9 (its widgets never render).
  lv_obj_t* staging = lv_obj_create(parent_);
  if (staging == nullptr) {
    r.err = "out of memory";
    return r;
  }
  lv_obj_set_size(staging, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(staging, 0, 0);
  lv_obj_set_style_border_width(staging, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(staging, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(staging, 0, LV_PART_MAIN);
  lv_obj_set_scrollable(staging, false);
  lv_obj_set_hidden(staging, true);

  // Screen background: `theme.bg`, else the palette's screen color, opaque
  // either way (set below). It belongs to the staging root rather than a
  // global screen style, so it swaps with the tree and never bleeds into
  // the next layout.
  JsonObjectConst theme_spec = doc["theme"];

  // Theme mode and fg/accent — must run before build_screens() below so
  // newly built widgets pick up the layout's theme immediately. These are
  // process-wide, so a build that fails has to put them back: the
  // previous layout stays on screen, and its still-live widgets would
  // otherwise read the REJECTED layout's theme on their next update.
  const ThemeColors prev_theme = current_theme();
  apply_theme(theme_spec);

  // `theme.bg` overrides the palette's screen color, except at night
  // where it would undo the red.
  uint32_t screen_bg = theme::palette().screen;
  uint32_t theme_bg = 0;
  if (theme::palette().mode != theme::Mode::Night && !theme_spec.isNull() &&
      parse_hex_color(theme_spec["bg"] | (const char*)nullptr, &theme_bg)) {
    screen_bg = theme_bg;
  }
  lv_obj_set_style_bg_color(staging, lv_color_hex(screen_bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(staging, LV_OPA_COVER, LV_PART_MAIN);

  JsonArrayConst screens = doc["screens"];
  if (!build_screens(staging, doc.as<JsonObjectConst>(), screens, registry(),
                     &r.err, &r.live_paths, &r.widgets)) {
    restore_theme(prev_theme);
    lv_obj_delete(staging);
    return r;
  }

  // Like any part of the tree, a cover that cannot be made fails the build:
  // an uncovered page of a read-only display would take writes.
  if (read_only_ && !cover_layout(staging, true)) {
    r.err = "out of memory";
    restore_theme(prev_theme);
    lv_obj_delete(staging);
    return r;
  }

  if (chrome_) chrome_(doc.as<JsonObjectConst>(), theme_rebuild);

  // A swipe in progress belongs to the old layout: further movement must
  // not turn a page of the new one.
  cancel_swipe();
  // A held widget is deleted with the old layout, and a deleted button
  // sends no release: a button that has sent its press value would leave
  // it pressed. End the press while the widget still exists.
  if (press_on_layout_ && indev_ != nullptr &&
      lv_indev_get_state(indev_) == LV_INDEV_STATE_PRESSED) {
    end_press();
  }

  lv_obj_t* old_root = current_root_;
  lv_obj_set_hidden(staging, false);
  current_root_ = staging;
  if (old_root) lv_obj_delete(old_root);

  // Single-screen layouts have no switcher, so nothing else ever marks
  // their widgets visible. Multi-screen roots got switcher_show(0) during
  // the staging build — a stream widget's start then races the old
  // layout's teardown and retries on its watch timer, so no extra call is
  // needed there.
  if (screens.size() == 1) {
    stream_widgets_notify_visibility(current_root_, true);
  }

  built_mode_ = theme::resolved();
  if (&json != &active_json_) active_json_ = json;
  r.ok = true;
  r.name = doc["name"] | "(unnamed)";
  r.screens = screens.size();
  return r;
}

bool LayoutPlayer::set_read_only(bool on) {
  if (on == read_only_) return true;
  // Uncovering cannot fail; a half-covered layout goes back to none.
  if (!cover_layout(current_root_, on)) {
    cover_layout(current_root_, false);
    return false;
  }
  read_only_ = on;
  if (!on || current_root_ == nullptr) return true;
  // The covers are hit-tested by their coordinates: a press before the next
  // refresh must already land on them.
  lv_obj_update_layout(current_root_);
  // A widget held now keeps its press under the new cover, and a button
  // that sent its press value must get its release. A press on the tab
  // strip, or a swipe begun there, goes on.
  ScreenSwitcherCtx* ctx = switcher_of(current_root_);
  if (pressed_ != nullptr && indev_ != nullptr && press_on_layout_ &&
      lv_indev_get_state(indev_) == LV_INDEV_STATE_PRESSED &&
      lv_obj_is_valid(pressed_) && inside(pressed_, current_root_) &&
      !(ctx != nullptr && inside(pressed_, ctx->strip))) {
    end_press();
  }
  return true;
}

bool LayoutPlayer::select_screen(const std::string& id) {
  ScreenSwitcherCtx* ctx = switcher_of(current_root_);
  if (!ctx) return false;
  for (size_t i = 0; i < ctx->ids.size(); i++) {
    if (ctx->ids[i] == id) {
      // The remote choice wins over a swipe still being tracked. When it
      // changes the page, a press on the layout ends too: the widget gets
      // PRESS_LOST, not a click, so a toggle on a page no longer shown
      // cannot send a PUT. A tap on the page that stays keeps its click, and
      // a press on an overlay above the layout (alert, wake) is left alone.
      // A swipe that met its threshold has ended its press already.
      cancel_swipe();
      if ((int)i != ctx->active && press_on_layout_ && indev_ != nullptr &&
          lv_indev_get_state(indev_) == LV_INDEV_STATE_PRESSED) {
        end_press();
      }
      switcher_show(ctx, (int)i);
      strip_peek(ctx, false);  // as a tab tap does
      return true;
    }
  }
  return false;
}

std::string LayoutPlayer::active_screen() const {
  ScreenSwitcherCtx* ctx = switcher_of(current_root_);
  if (!ctx) return "";
  if (ctx->active < 0 || ctx->active >= (int)ctx->ids.size()) return "";
  return ctx->ids[ctx->active];
}

std::vector<std::string> LayoutPlayer::screen_ids() const {
  ScreenSwitcherCtx* ctx = switcher_of(current_root_);
  if (!ctx) return {};
  return ctx->ids;
}

}  // namespace jlp
