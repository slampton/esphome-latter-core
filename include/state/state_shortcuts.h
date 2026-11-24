#pragma once

// ==============================================================================
// Latter Core: App Dock & Pinned Shortcuts State PODs
// (include/state/state_shortcuts.h)
// ==============================================================================

#include "state/state_common.h"
#include "state/state_tile_grid.h"

namespace latter_core {

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
  int8_t page = -1;        // 0..31 explicit logical page, -1 = auto-flow
  int8_t col = -1;         // 0..3 explicit column, -1 = auto-flow
  int8_t row = -1;         // 0..2 explicit row, -1 = auto-flow
  uint8_t col_span = 1;    // 1..4 column span
  uint8_t row_span = 1;    // 1..3 row span
};

struct PinnedPageTitle {
  uint8_t page = 0;
  std::string title = "";
};

struct ShortcutsState {
  static constexpr size_t kMaxDockItems = 24;
  static constexpr size_t kMaxPinnedItems = 256;
  static constexpr size_t kMaxPinnedPageTitles = 32;
  uint32_t epoch = 0;
  uint32_t rev = 0;
  bool dock_configured = false;
  bool live_received = false;
  uint16_t cold_pinned_total = 0;
  std::vector<ShortcutItem> dock_items{};
  std::vector<ShortcutItem> pinned_items{};
  std::vector<PinnedPageTitle> pinned_page_titles{};
  // Multi-chunk assembly buffer (commits atomically when chunk_index + 1 == chunk_count)
  uint32_t staging_epoch = 0;
  uint32_t staging_rev = 0;
  uint16_t staging_chunk_count = 0;
  uint16_t staging_next_chunk = 0;
  uint16_t staging_pinned_total = 0;
  std::vector<ShortcutItem> staging_dock_items{};
  std::vector<ShortcutItem> staging_pinned_items{};
  std::vector<PinnedPageTitle> staged_pinned_page_titles{};

  void clear() {
    *this = ShortcutsState{};
  }

  std::string_view find_pinned_page_title(int logical_page) const noexcept {
    if (logical_page < 0 || logical_page >= kTileGridMaxLogicalPages) return {};
    for (const auto &entry : pinned_page_titles) {
      if (static_cast<int>(entry.page) == logical_page && !entry.title.empty()) {
        return entry.title;
      }
    }
    return {};
  }
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
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.page + 1))) * 16777619U;
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.col + 1))) * 16777619U;
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.row + 1))) * 16777619U;
    h = (h ^ static_cast<uint32_t>(it.col_span)) * 16777619U;
    h = (h ^ static_cast<uint32_t>(it.row_span)) * 16777619U;
  }
  for (const auto &pt : st.pinned_page_titles) {
    h = (h ^ static_cast<uint32_t>(pt.page)) * 16777619U;
    for (unsigned char c : pt.title) {
      h = (h ^ static_cast<uint32_t>(c)) * 16777619U;
    }
    h = (h ^ 0xFFU) * 16777619U;
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

inline TileGridLayoutResult resolve_pinned_grid_layout(
    const ShortcutsState &st,
    int cols,
    int rows,
    TileGridPlacement *out_placements,
    int out_cap,
    int8_t *out_page_source_pages = nullptr,
    int max_pages = kTileGridMaxLogicalPages) noexcept {
  const int count = static_cast<int>(std::min<size_t>(st.pinned_items.size(), ShortcutsState::kMaxPinnedItems));
  if (count <= 0) {
    return resolve_tile_grid_layout(nullptr, 0, cols, rows, out_placements, out_cap,
                                    out_page_source_pages, max_pages);
  }
  TileGridInputItem in[ShortcutsState::kMaxPinnedItems]{};
  for (int i = 0; i < count; ++i) {
    const auto &it = st.pinned_items[static_cast<size_t>(i)];
    in[i] = TileGridInputItem{it.page, it.col, it.row, it.col_span, it.row_span, i};
  }
  return resolve_tile_grid_layout(in, count, cols, rows, out_placements, out_cap,
                                  out_page_source_pages, max_pages);
}

inline int pinned_total_pages_for(const ShortcutsState &st, int cols, int rows) noexcept {
  const auto res = resolve_pinned_grid_layout(st, cols, rows, nullptr, 0, nullptr, kTileGridMaxLogicalPages);
  return std::max(1, res.total_pages);
}

inline int pinned_total_pages(const ShortcutsState &st = shortcuts_state()) noexcept {
  const int cols = tile_grid_cols_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int rows = tile_grid_rows_for(LATTER_SCREEN_W, LATTER_SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  return pinned_total_pages_for(st, cols, rows);
}

inline std::string_view resolve_pinned_page_title(const ShortcutsState &st, int logical_page) noexcept {
  return st.find_pinned_page_title(logical_page);
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


}  // namespace latter_core
