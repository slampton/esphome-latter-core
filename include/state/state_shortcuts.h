#pragma once

// ==============================================================================
// Latter Core SDK: App Dock & Pinned Shortcuts State PODs
// (include/state/state_shortcuts.h)
// ==============================================================================

#include "state/state_common.h"

namespace latter_ui {

// Protocol v3 shortcuts state (App Dock + Pinned App universal shortcut engine).
enum class ShortcutKind : uint8_t {
  ROUTE = 0,
  ENTITY = 1,
};

enum class ShortcutOrigin : uint8_t {
  NONE = 0,
  HOME_DOCK = 1,
  PINNED_APP = 2,
};

enum class ShortcutBadgeStyle : uint8_t {
  GLASS = 0,
  INVERTED = 1,
};

enum class ShortcutAccent : uint8_t {
  DEFAULT = 0,
  PRIMARY = 1,
  GOLD = 2,
  GREEN = 3,
  RED = 4,
  ORANGE = 5,
};

struct ShortcutItem {
  ShortcutKind kind = ShortcutKind::ROUTE;
  RouteId route_id = RouteId::NONE;
  std::string target = "";
  std::string action = "";
  std::string label = "";
  std::string icon = "";   // empty = native bespoke glyph (for ROUTE), or "mdi:<slug>"
  bool confirm = false;
  int8_t state = -1;       // -1 = stateless/route, 0 = off/closed/locked, 1 = on/open/unlocked, 2 = unavailable
  ShortcutBadgeStyle badge_style = ShortcutBadgeStyle::GLASS;
  ShortcutAccent accent = ShortcutAccent::DEFAULT;
};

struct ShortcutsState {
  static constexpr size_t kMaxDockItems = 24;
  static constexpr size_t kMaxPinnedItems = 256;
  uint32_t epoch = 0;
  uint32_t rev = 0;
  bool dock_configured = false;
  bool live_received = false;
  uint16_t cold_pinned_total = 0;
  std::vector<ShortcutItem> dock_items{};
  std::vector<ShortcutItem> pinned_items{};
  // Multi-chunk assembly buffer (commits atomically when chunk_index + 1 == chunk_count)
  uint32_t staging_epoch = 0;
  uint32_t staging_rev = 0;
  uint16_t staging_chunk_count = 0;
  uint16_t staging_next_chunk = 0;
  uint16_t staging_pinned_total = 0;
  std::vector<ShortcutItem> staging_dock_items{};
  std::vector<ShortcutItem> staging_pinned_items{};
};

struct ShortcutConfirmState {
  static constexpr uint32_t kAutoDismissMs = 10000U;
  bool open = false;
  uint32_t opened_ms = 0;
  bool is_danger = false;
  std::string title = "";
  std::string line1 = "";
  std::string line2 = "";
  std::string verb_btn = "";
  std::string entity_id = "";
  std::string action = "";

  void clear() { *this = ShortcutConfirmState{}; }
};


inline ShortcutsState &shortcuts_state() {
  static ShortcutsState st;
  return st;
}

// ==============================================================================
// Universal Shortcut Engine: Route Catalog Lookup, Protocol v3 Chunk Parser & Confirm Copy
// ==============================================================================
inline constexpr const ShortcutRouteSpec *find_shortcut_route(std::string_view token) noexcept {
  if (token == "app:settings") return nullptr;
  for (const auto &spec : kShortcutRoutes) {
    if (token == spec.token) return &spec;
  }
  return nullptr;
}

inline constexpr const ShortcutRouteSpec *shortcut_route_spec(RouteId id) noexcept {
  for (const auto &spec : kShortcutRoutes) {
    if (spec.id == id) return &spec;
  }
  return nullptr;
}

inline constexpr bool is_route_supported_on_board(RouteId id) noexcept {
  if (id == RouteId::NONE) return false;
  if (id == RouteId::SETTINGS_ROOT) return true;
  const auto *spec = shortcut_route_spec(id);
  if (spec == nullptr) return false;
  if (spec->cap == ShortcutRouteCap::ROUND && !LATTER_ROUND_DISPLAY) return false;
  if (spec->cap == ShortcutRouteCap::IMU && !LATTER_HAS_IMU) return false;
  if (spec->cap == ShortcutRouteCap::NO_IMU && LATTER_HAS_IMU) return false;
  return true;
}


bool parse_shortcut_record(std::string_view rec, ShortcutItem &out_item);
void append_shortcut_records_blob(std::string_view blob,
                                  std::vector<ShortcutItem> &dst,
                                  size_t max_total,
                                  bool is_pinned_list);
bool parse_v3_shortcuts_payload(std::string_view payload, ShortcutsState &st = shortcuts_state());
void build_shortcut_confirm_copy(const ShortcutItem &item,
                                 std::string &out_title,
                                 std::string &out_line1,
                                 std::string &out_line2,
                                 std::string &out_verb_btn,
                                 bool &out_is_danger);

inline uint32_t compute_shortcuts_ui_sig(const ShortcutsState &st = shortcuts_state()) noexcept {
  uint32_t h = 2166136261U;
  h = (h ^ st.epoch) * 16777619U;
  h = (h ^ st.rev) * 16777619U;
  h = (h ^ static_cast<uint32_t>(st.dock_items.size())) * 16777619U;
  h = (h ^ static_cast<uint32_t>(st.pinned_items.size())) * 16777619U;
  for (const auto &it : st.dock_items) {
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.state + 2))) * 16777619U;
    h = (h ^ static_cast<uint32_t>(it.badge_style)) * 16777619U;
    h = (h ^ static_cast<uint32_t>(it.accent)) * 16777619U;
  }
  for (const auto &it : st.pinned_items) {
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.state + 2))) * 16777619U;
    h = (h ^ static_cast<uint32_t>(it.badge_style)) * 16777619U;
    h = (h ^ static_cast<uint32_t>(it.accent)) * 16777619U;
  }
  return h;
}


inline constexpr int kMaxEffectiveDockItems = 25;

inline ShortcutItem *dock_items_scratch() {
  static std::vector<ShortcutItem> s_buf(static_cast<size_t>(kMaxEffectiveDockItems));
  return s_buf.data();
}


int collect_effective_dock_items(ShortcutItem *out_items,
                                 int max_items = kMaxEffectiveDockItems,
                                 const ShortcutsState &st = shortcuts_state());

inline int pinned_total_pages(const ShortcutsState &st = shortcuts_state()) noexcept {
  const size_t n = st.pinned_items.size();
  return (n == 0U) ? 1 : static_cast<int>((n + 5U) / 6U);
}

inline int get_mode_bar_start_index(int total_count, int win_start) {
  if (total_count <= 4 || win_start <= 0) return 0;
  return std::max(0, std::min(total_count - 4, win_start));
}

inline void sync_mode_bar_window(int &win_start, int active_idx, int total_count) {
  if (total_count <= 4) {
    win_start = 0;
    return;
  }
  win_start = get_mode_bar_start_index(total_count, win_start);
  if (active_idx < win_start) {
    win_start = active_idx;
  } else if (active_idx >= win_start + 4) {
    win_start = active_idx - 3;
  }
  win_start = std::max(0, std::min(total_count - 4, win_start));
}


}  // namespace latter_ui
