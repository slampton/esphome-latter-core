#pragma once

// ==============================================================================
// Latter Core: Pure 2D Variable-Span Tile-Grid Traits & Bitmask Layout Solver
// (include/state/state_tile_grid.h)
// Pure C++20 stdlib header in latter_core — zero ui_*.h, ESPHome, or HAL includes.
// ==============================================================================

#include <algorithm>
#include <cstdint>

namespace latter_core {

static constexpr int kTileGridMaxCols = 4;
static constexpr int kTileGridMaxRows = 3;
static constexpr int kTileGridMaxLogicalPages = 32;
static constexpr int kTileGridMaxSlotsPerPage = 16;

constexpr int tile_grid_cols_for(int w, int h, bool is_round,
                                 int ppi = 266, int view_scale_pct = 0,
                                 bool is_stationary = false) noexcept {
  if (is_round) return 2;
  const int eff_ppi = (ppi > 50) ? ppi : 266;
  const int eff_vs = (view_scale_pct > 0) ? view_scale_pct : (is_stationary ? 145 : 100);
  const int s_min = (is_stationary || eff_vs >= 120) ? 900 : 760;
  const int raw_s = (eff_ppi * eff_vs * 10 + 133) / 266;
  const int s = (raw_s > s_min) ? raw_s : s_min;
  const int w_dp = (w * 1000 + s / 2) / s;
  if (w_dp >= 800 || (w >= 800 && h >= 480)) return 4;
  if (w >= 600 && h >= 600) return 3;
  return 2;
}

constexpr int tile_grid_rows_for(int w, int h, bool is_round,
                                 int ppi = 266, int view_scale_pct = 0,
                                 bool is_stationary = false) noexcept {
  const int eff_ppi = (ppi > 50) ? ppi : 266;
  const int eff_vs = (view_scale_pct > 0) ? view_scale_pct : (is_stationary ? 145 : 100);
  const int s_min = (is_stationary || eff_vs >= 120) ? 900 : 760;
  const int raw_s = (eff_ppi * eff_vs * 10 + 133) / 266;
  const int s = (raw_s > s_min) ? raw_s : s_min;
  const int h_dp = (h * 1000 + s / 2) / s;
  if (is_round && (h < 400 || h_dp < 400)) return 2;
  if (!is_round && w >= 800 && h < 540) return 2;
  return 3;
}

struct TileGridInputItem {
  int8_t page = -1;
  int8_t col = -1;
  int8_t row = -1;
  uint8_t col_span = 1;
  uint8_t row_span = 1;
  int16_t item_index = -1;
};

struct TileGridPlacement {
  int8_t visual_page = 0;
  int8_t source_page = 0;
  int8_t col = 0;
  int8_t row = 0;
  uint8_t col_span = 1;
  uint8_t row_span = 1;
  int16_t item_index = -1;
};

static_assert(sizeof(TileGridInputItem) <= 8, "TileGridInputItem must stay compact (<= 8 bytes)");
static_assert(sizeof(TileGridPlacement) <= 8, "TileGridPlacement must stay compact (<= 8 bytes)");

struct TileGridLayoutResult {
  int placement_count = 0;
  int total_pages = 1;
};

namespace detail {

static constexpr int kMaxTileGridSolverItems = 256;
static_assert(kMaxTileGridSolverItems <= 256, "TileGridSolverScratch indices fit in uint8_t");

// Static BSS scratch keeps resolve_tile_grid_layout() off the 8 KB ESPHome loopTask stack.
struct TileGridSolverScratch {
  uint16_t occ[kTileGridMaxLogicalPages]{};
  int8_t page_origin[kTileGridMaxLogicalPages]{};
  int8_t logical_to_visual[kTileGridMaxLogicalPages]{};
  uint8_t deferred_idx[kMaxTileGridSolverItems]{};
  uint8_t autoflow_idx[kMaxTileGridSolverItems]{};
};

inline TileGridSolverScratch &tile_grid_solver_scratch() noexcept {
  static TileGridSolverScratch s_scratch{};
  return s_scratch;
}

constexpr uint16_t tile_grid_rect_mask(int cols, int col, int row, int w, int h) noexcept {
  uint16_t mask = 0U;
  for (int r = row; r < row + h; ++r) {
    for (int c = col; c < col + w; ++c) {
      mask = static_cast<uint16_t>(mask | static_cast<uint16_t>(1U << (r * cols + c)));
    }
  }
  return mask;
}

}  // namespace detail

// Zero-heap fixed-capacity bitmask solver (cols * rows <= 12 <= 16 bits/page).
// Pass 1: Explicit placements on their logical page if in-bounds and unobstructed;
//         out-of-bounds or colliding explicit items defer to Pass 2 in reading order
//         (page, row, col, orig_idx).
// Pass 2: Place deferred items first (starting from their logical page), then auto-flow
//         items (starting from page 0), scanning row-major for the first open w x h rect.
// Compaction: Collapse empty logical pages into contiguous visual_page = 0 .. total_pages - 1,
//             recording out_page_source_pages[visual_page] = spawning logical page index so
//             custom page titles follow overflowed pages cleanly.
inline TileGridLayoutResult resolve_tile_grid_layout(
    const TileGridInputItem *items,
    int count,
    int cols,
    int rows,
    TileGridPlacement *out_placements,
    int out_cap,
    int8_t *out_page_source_pages = nullptr,
    int max_pages = kTileGridMaxLogicalPages) noexcept {
  TileGridLayoutResult res{};
  res.placement_count = 0;
  res.total_pages = 1;

  const int eff_cols = std::clamp(cols, 1, kTileGridMaxCols);
  const int eff_rows = std::clamp(rows, 1, kTileGridMaxRows);
  const int eff_max_pages = std::clamp(max_pages, 1, kTileGridMaxLogicalPages);
  const int eff_out_cap = (out_placements != nullptr && out_cap > 0) ? out_cap : 0;

  if (items == nullptr || count <= 0) {
    if (out_page_source_pages != nullptr && max_pages > 0) {
      out_page_source_pages[0] = 0;
    }
    return res;
  }

  const int n = std::min(count, detail::kMaxTileGridSolverItems);

  auto &scratch = detail::tile_grid_solver_scratch();
  uint16_t (&occ)[kTileGridMaxLogicalPages] = scratch.occ;
  int8_t (&page_origin)[kTileGridMaxLogicalPages] = scratch.page_origin;
  int8_t (&logical_to_visual)[kTileGridMaxLogicalPages] = scratch.logical_to_visual;
  uint8_t (&deferred_idx)[detail::kMaxTileGridSolverItems] = scratch.deferred_idx;
  uint8_t (&autoflow_idx)[detail::kMaxTileGridSolverItems] = scratch.autoflow_idx;
  for (int p = 0; p < kTileGridMaxLogicalPages; ++p) {
    occ[p] = 0U;
    page_origin[p] = -1;
    logical_to_visual[p] = -1;
  }

  int deferred_cnt = 0;
  int autoflow_cnt = 0;

  int placed_cnt = 0;
  auto emit_logical = [&](int logical_pg, int col, int row, int w, int h, int16_t item_idx) noexcept {
    if (placed_cnt < eff_out_cap) {
      TileGridPlacement p{};
      p.visual_page = static_cast<int8_t>(logical_pg);  // Temporarily stores logical page
      p.source_page = static_cast<int8_t>(logical_pg);
      p.col = static_cast<int8_t>(col);
      p.row = static_cast<int8_t>(row);
      p.col_span = static_cast<uint8_t>(w);
      p.row_span = static_cast<uint8_t>(h);
      p.item_index = item_idx;
      out_placements[placed_cnt++] = p;
    }
  };

  // Pass 1: Explicit placements
  for (int i = 0; i < n; ++i) {
    const auto &it = items[i];
    const bool is_explicit = (it.page >= 0 && it.col >= 0 && it.row >= 0);
    if (!is_explicit) {
      autoflow_idx[autoflow_cnt++] = static_cast<uint8_t>(i);
      continue;
    }
    const int w = std::clamp(static_cast<int>(it.col_span), 1, eff_cols);
    const int h = std::clamp(static_cast<int>(it.row_span), 1, eff_rows);
    const int pg = static_cast<int>(it.page);
    const int col = static_cast<int>(it.col);
    const int row = static_cast<int>(it.row);
    if (pg < eff_max_pages && col + w <= eff_cols && row + h <= eff_rows) {
      const uint16_t mask = detail::tile_grid_rect_mask(eff_cols, col, row, w, h);
      if ((occ[pg] & mask) == 0U) {
        occ[pg] = static_cast<uint16_t>(occ[pg] | mask);
        page_origin[pg] = static_cast<int8_t>(pg);
        emit_logical(pg, col, row, w, h, it.item_index);
        continue;
      }
    }
    deferred_idx[deferred_cnt++] = static_cast<uint8_t>(i);
  }

  // Sort deferred explicit items in reading order: (page, row, col, orig_idx)
  for (int i = 1; i < deferred_cnt; ++i) {
    const uint8_t key = deferred_idx[i];
    const auto &ka = items[key];
    int j = i - 1;
    while (j >= 0) {
      const auto &kb = items[deferred_idx[j]];
      const bool greater =
          (kb.page != ka.page) ? (kb.page > ka.page) :
          (kb.row != ka.row)   ? (kb.row > ka.row) :
          (kb.col != ka.col)   ? (kb.col > ka.col) :
                                 (deferred_idx[j] > key);
      if (!greater) break;
      deferred_idx[j + 1] = deferred_idx[j];
      --j;
    }
    deferred_idx[j + 1] = key;
  }

  auto place_first_fit = [&](const TileGridInputItem &it, int start_page, int8_t origin_hint) noexcept {
    const int w = std::clamp(static_cast<int>(it.col_span), 1, eff_cols);
    const int h = std::clamp(static_cast<int>(it.row_span), 1, eff_rows);
    const int p0 = std::clamp(start_page, 0, eff_max_pages - 1);
    for (int pg = p0; pg < eff_max_pages; ++pg) {
      for (int r = 0; r <= eff_rows - h; ++r) {
        for (int c = 0; c <= eff_cols - w; ++c) {
          const uint16_t mask = detail::tile_grid_rect_mask(eff_cols, c, r, w, h);
          if ((occ[pg] & mask) == 0U) {
            occ[pg] = static_cast<uint16_t>(occ[pg] | mask);
            if (page_origin[pg] < 0) {
              page_origin[pg] = (origin_hint >= 0) ? origin_hint : static_cast<int8_t>(pg);
            }
            emit_logical(pg, c, r, w, h, it.item_index);
            return;
          }
        }
      }
    }
  };

  // Pass 2a: Deferred explicit items (cross-device reflow / collision recovery)
  for (int i = 0; i < deferred_cnt; ++i) {
    const auto &it = items[deferred_idx[i]];
    const int clamped_src = std::clamp(static_cast<int>(it.page), 0, eff_max_pages - 1);
    place_first_fit(it, clamped_src, static_cast<int8_t>(clamped_src));
  }

  // Pass 2b: Auto-flow items (unplaced)
  for (int i = 0; i < autoflow_cnt; ++i) {
    const auto &it = items[autoflow_idx[i]];
    const int start_pg = (it.page >= 0 && it.page < eff_max_pages) ? static_cast<int>(it.page) : 0;
    const int8_t hint = (it.page >= 0 && it.page < eff_max_pages) ? it.page : static_cast<int8_t>(-1);
    place_first_fit(it, start_pg, hint);
  }

  // Compaction: collapse empty logical pages into contiguous visual_page = 0 .. total_pages - 1
  int vis_pages = 0;
  for (int p = 0; p < eff_max_pages; ++p) {
    if (occ[p] != 0U) {
      logical_to_visual[p] = static_cast<int8_t>(vis_pages);
      if (out_page_source_pages != nullptr && vis_pages < max_pages) {
        out_page_source_pages[vis_pages] = (page_origin[p] >= 0) ? page_origin[p] : static_cast<int8_t>(p);
      }
      ++vis_pages;
    }
  }
  if (vis_pages == 0) {
    vis_pages = 1;
    if (out_page_source_pages != nullptr && max_pages > 0) {
      out_page_source_pages[0] = 0;
    }
  }

  // Remap output placements to compacted visual_page and final source_page
  for (int i = 0; i < placed_cnt; ++i) {
    const int lp = static_cast<int>(out_placements[i].visual_page);
    out_placements[i].visual_page = logical_to_visual[lp];
    out_placements[i].source_page = (page_origin[lp] >= 0) ? page_origin[lp] : static_cast<int8_t>(lp);
  }

  // Sort output placements in clean reading order: (visual_page, row, col, item_index)
  for (int i = 1; i < placed_cnt; ++i) {
    const TileGridPlacement key = out_placements[i];
    int j = i - 1;
    while (j >= 0) {
      const auto &b = out_placements[j];
      const bool greater =
          (b.visual_page != key.visual_page) ? (b.visual_page > key.visual_page) :
          (b.row != key.row)                 ? (b.row > key.row) :
          (b.col != key.col)                 ? (b.col > key.col) :
                                               (b.item_index > key.item_index);
      if (!greater) break;
      out_placements[j + 1] = out_placements[j];
      --j;
    }
    out_placements[j + 1] = key;
  }

  res.placement_count = placed_cnt;
  res.total_pages = vis_pages;
  return res;
}

}  // namespace latter_core
