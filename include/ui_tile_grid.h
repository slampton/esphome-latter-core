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

// Pure horizontal inset from either side bezel at scanline y so a rectangle edge at
// [x_inset, w - 1 - x_inset] clears circular and rounded-rectangle display bezels
// with at least `margin` px radial/edge clearance.
inline constexpr int bezel_x_inset_for(int y, int w, int h, int corner_r,
                                       bool is_round, int margin = 10) noexcept {
  if (w <= 0 || h <= 0) return margin;
  const int cy = h / 2;
  if (is_round) {
    const int min_d = (w < h) ? w : h;
    const int r_eff = (min_d / 2) - margin;
    if (r_eff <= 0) return w / 2;
    const int dy = (y >= cy) ? (y - cy) : (cy - y);
    const int hw = chord_half_span_constexpr(r_eff, dy, false);
    const int inset = (w / 2) - hw;
    return (inset > margin) ? inset : margin;
  }
  const int cr = (corner_r > 0) ? corner_r : 0;
  if (cr > margin) {
    int dy = 0;
    if (y < cr) {
      dy = cr - y;
    } else if (y >= h - cr) {
      dy = y - (h - 1 - cr);
    }
    if (dy > 0) {
      const int r_eff = cr - margin;
      const int dx_corner = (dy < r_eff) ? chord_half_span_constexpr(r_eff, dy, false) : 0;
      const int corner_inset = cr - dx_corner;
      return (corner_inset > margin) ? corner_inset : margin;
    }
  }
  return margin;
}

// Pure bezel-safe adaptive tile-grid geometry across all rectangular, rounded-rect,
// round, square, and landscape panels (D-086).
inline constexpr TileGridGeom compute_tile_grid_geom(int w, int h, int corner_r,
                                                     bool is_round, int cols,
                                                     int rows) noexcept {
  TileGridGeom g{};
  const int eff_cols = (cols < 1) ? 1 : ((cols > latter_core::kTileGridMaxCols) ? latter_core::kTileGridMaxCols : cols);
  const int eff_rows = (rows < 1) ? 1 : ((rows > latter_core::kTileGridMaxRows) ? latter_core::kTileGridMaxRows : rows);
  g.cols = eff_cols;
  g.rows = eff_rows;

  const bool wearable_corners = (!is_round && corner_r >= 84 && w <= 440 && h <= 520);
  const int sub_hdr_y = is_round
                            ? scale_min_for(76, h)
                            : (wearable_corners ? 64 : ((h > 520) ? 88 : 56));
  const int home_bar_y = is_round ? scale_min_for(434, h) : (h - scale_y_for(11, h));

  int y_top = 0;
  int y_bot = 0;
  if (is_round) {
    y_top = sub_hdr_y + scale_min_for(18, h);
    const int sym_bot = h - y_top;
    const int bar_bot = home_bar_y - scale_min_for(28, h);
    y_bot = (sym_bot < bar_bot) ? sym_bot : bar_bot;
  } else {
    const int top_pad = (h > 520) ? ((h >= 700) ? 24 : 20) : 18;
    const int bot_pad = (h > 520)
                            ? ((h >= 700) ? 27 : 18)
                            : ((corner_r >= 84) ? 66 : ((corner_r >= 60) ? 40 : 24));
    y_top = sub_hdr_y + top_pad;
    y_bot = home_bar_y - bot_pad;
  }
  if (y_bot < y_top + eff_rows * 32) {
    y_bot = y_top + eff_rows * 32;
  }

  g.gap_x = (w >= 700) ? 16 : ((w < 380) ? 10 : 12);
  g.gap_y = (h >= 580) ? 16 : ((is_round || w < 380) ? 10 : 12);

  constexpr int kBezelMargin = 10;
  const int base_side_pad = (w >= 960) ? 36 : ((w >= 700) ? 32 : ((w < 380) ? 16 : 20));
  const int top_inset = bezel_x_inset_for(y_top, w, h, corner_r, is_round, kBezelMargin);
  const int bot_inset = bezel_x_inset_for(y_bot - 1, w, h, corner_r, is_round, kBezelMargin);
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
