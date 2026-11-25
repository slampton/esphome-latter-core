#pragma once

// ==============================================================================
// Latter UI Unified Variable-Span Tile-Grid Geometry & Renderer
// (include/ui_tile_grid.h)
// Pure C++20 UI header — zero ESPHome or HAL includes.
// Shared by PageId::PINNED and PageId::TOOLS (ToolsSubpage::LAUNCHER).
// ==============================================================================

#include "state/state_tile_grid.h"
#include "ui_panel_metrics.h"
#include "ui_widgets.h"

#include <algorithm>
#include <cstdint>

namespace latter_ui {

struct TileGridGeom {
  int cols{2};
  int rows{3};
  int grid_x{0};
  int grid_y{0};
  int grid_w{0};
  int grid_h{0};
  int gap_x{12};
  int gap_y{12};
  int cell_w{100};
  int cell_h{86};
};

struct TileRect {
  int x{0};
  int y{0};
  int w{0};
  int h{0};

  constexpr int cx() const noexcept { return x + w / 2; }
  constexpr int cy() const noexcept { return y + h / 2; }
};

// Pure bezel-safe adaptive tile-grid geometry across all rectangular, rounded-rect,
// round, square, and landscape panels (D-086 / D-087).
inline constexpr TileGridGeom compute_tile_grid_geom(int w, int h, int corner_r,
                                                     bool is_round, int cols,
                                                     int rows,
                                                     int ppi = LATTER_PANEL_PPI,
                                                     int view_scale_pct = 0,
                                                     bool is_stationary = (LATTER_IS_STATIONARY != 0)) noexcept {
  TileGridGeom g{};
  const int eff_cols = (cols < 1) ? 1 : ((cols > latter_core::kTileGridMaxCols) ? latter_core::kTileGridMaxCols : cols);
  const int eff_rows = (rows < 1) ? 1 : ((rows > latter_core::kTileGridMaxRows) ? latter_core::kTileGridMaxRows : rows);
  g.cols = eff_cols;
  g.rows = eff_rows;

  const auto ly = compute_panel_layout(w, h, is_round, corner_r >= 84, ppi, corner_r, view_scale_pct, is_stationary);
  const int s = ly.scale_milli;

  int y_top = 0;
  int y_bot = 0;
  if (is_round) {
    y_top = ly.sub_hdr_y + std::max(10, dp_px_for(16, s));
    const int sym_bot = h - y_top;
    const int bar_bot = ly.home_bar_y - std::max(14, dp_px_for(22, s));
    y_bot = (sym_bot < bar_bot) ? sym_bot : bar_bot;
  } else {
    const int top_pad = std::max(12, dp_px_for(16, s));
    const int corner_bot_pad = (corner_r >= 84) ? dp_px_for(48, s)
                                                : ((corner_r >= 60) ? dp_px_for(32, s) : dp_px_for(20, s));
    const int bot_pad = std::max(dp_px_for(18, s), corner_bot_pad);
    y_top = ly.sub_hdr_y + top_pad;
    y_bot = ly.home_bar_y - bot_pad;
  }
  if (y_bot < y_top + eff_rows * 32) {
    y_bot = y_top + eff_rows * 32;
  }

  g.gap_x = std::clamp(dp_px_for(12, s), 8, 20);
  g.gap_y = std::clamp(dp_px_for(12, s), 8, 20);

  const int bezel_margin = std::max(8, dp_px_for(10, s));
  const int base_side_pad = std::clamp(dp_px_for(22, s), 14, 36);
  const int top_inset = bezel_x_inset_for(y_top, w, h, corner_r, is_round, bezel_margin);
  const int bot_inset = bezel_x_inset_for(y_bot - 1, w, h, corner_r, is_round, bezel_margin);
  int x_inset = (top_inset > bot_inset) ? top_inset : bot_inset;
  if (!is_round && base_side_pad > x_inset) {
    x_inset = base_side_pad;
  }

  const int avail_w = std::max(eff_cols * 32, w - 2 * x_inset);
  const int avail_h = std::max(eff_rows * 32, y_bot - y_top);

  g.cell_w = std::max(24, (avail_w - (eff_cols - 1) * g.gap_x) / eff_cols);
  g.cell_h = std::max(24, (avail_h - (eff_rows - 1) * g.gap_y) / eff_rows);
  g.grid_w = g.cell_w * eff_cols + (eff_cols - 1) * g.gap_x;
  g.grid_h = g.cell_h * eff_rows + (eff_rows - 1) * g.gap_y;
  g.grid_x = (w - g.grid_w) / 2;
  g.grid_y = y_top + (avail_h - g.grid_h) / 2;
  return g;
}

inline constexpr TileRect tile_grid_cell_rect(const TileGridGeom &g, int col, int row,
                                              int col_span, int row_span) noexcept {
  const int c0 = std::clamp(col, 0, std::max(0, g.cols - 1));
  const int r0 = std::clamp(row, 0, std::max(0, g.rows - 1));
  const int cw = std::clamp(col_span, 1, std::max(1, g.cols - c0));
  const int rh = std::clamp(row_span, 1, std::max(1, g.rows - r0));
  return TileRect{
      g.grid_x + c0 * (g.cell_w + g.gap_x),
      g.grid_y + r0 * (g.cell_h + g.gap_y),
      cw * g.cell_w + (cw - 1) * g.gap_x,
      rh * g.cell_h + (rh - 1) * g.gap_y,
  };
}

namespace ui {

struct TileGridEntry {
  const char *label{""};
  bool active{false};
  bool disabled{false};
  int8_t state{-1};  // -1 = stateless, 0 = off, 1 = on, 2 = unavailable
  bool inverted_badge{false};
  bool has_accent{false};
  UiColor accent_c{UiColor::WHITE};
  UiColor text_c{UiColor::WHITE};
  TileDrawIconFn draw_icon_fn{nullptr};
  void *icon_ctx{nullptr};
  int16_t widget_param{0};
  uint8_t col_span{1};
  uint8_t row_span{1};
  int8_t col{0};
  int8_t row{0};
  int8_t visual_page{0};
};

struct TileGridPageSpec {
  int page_id{0};
  uint16_t card_widget_base{WidgetId::NONE};
  int cur_page{0};
  int total_pages{1};
  const char *page_title_upper{""};
  const char *empty_message{""};
  const char *feedback_text{nullptr};
  const TileGridEntry *entries{nullptr};
  int entry_count{0};
};

void render_tile_grid_page(Rgb565Surface &it, const UiFrameContext &ctx,
                           const TileGridPageSpec &spec);

}  // namespace ui

}  // namespace latter_ui
