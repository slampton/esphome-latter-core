#include "ui_tile_grid.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace ui {

void render_tile_grid_page(Rgb565Surface &it, const UiFrameContext &ctx,
                           const TileGridPageSpec &spec) {
  const auto &rs = runtime_state();
  const auto &ly = layout();
  const char *title = (spec.page_title_upper != nullptr && spec.page_title_upper[0] != '\0')
                          ? spec.page_title_upper
                          : "";

  if (spec.entries == nullptr || spec.entry_count <= 0) {
    render_page_header(it, ctx.cx, title, ctx.font_small, ctx.col_white);
    if (spec.empty_message != nullptr && spec.empty_message[0] != '\0') {
      render_empty_state(it, ctx.cx, ctx.cy, spec.empty_message, ctx.font_small, ctx.col_slate);
    }
    if (can_go_home(rs)) {
      render_home_indicator_bar(it, ctx.cx, ly.home_bar_y);
    }
    return;
  }

  const int total_pages = std::max(1, spec.total_pages);
  const int cur_pg = std::clamp(spec.cur_page, 0, total_pages - 1);
  const int cols = tile_grid_cols_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int rows = tile_grid_rows_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const TileGridGeom geom = compute_tile_grid_geom(
      SCREEN_W, SCREEN_H, LATTER_CORNER_R, LATTER_ROUND_DISPLAY != 0, cols, rows);

  auto draw_page_tiles = [&](int pg_idx, int x_shift, bool register_hits) {
    int slot_on_page = 0;
    for (int i = 0; i < spec.entry_count; ++i) {
      const auto &e = spec.entries[i];
      if (static_cast<int>(e.visual_page) != pg_idx) continue;
      const TileRect rect = tile_grid_cell_rect(geom, e.col, e.row, e.col_span, e.row_span);
      const int shifted_x = rect.x + x_shift;
      if (shifted_x + rect.w <= 0 || shifted_x >= SCREEN_W) {
        ++slot_on_page;
        continue;
      }
      const uint16_t wid =
          (register_hits && spec.card_widget_base != WidgetId::NONE &&
           slot_on_page < kTileGridMaxSlotsPerPage)
              ? static_cast<uint16_t>(spec.card_widget_base + slot_on_page)
              : WidgetId::NONE;
      ui::grid_tile(it, shifted_x + rect.w / 2, rect.cy(), rect.w, rect.h,
                    e.label, e.active, e.disabled, e.accent_c, e.text_c,
                    ctx.font_small, e.draw_icon_fn, e.icon_ctx,
                    wid, e.widget_param, e.inverted_badge, e.has_accent,
                    e.col_span, e.row_span, e.state, ctx.font_date);
      ++slot_on_page;
    }
  };

  const bool scrolling = (rs.nav.menu_scroll_active && total_pages > 1);
  const int active_pg = scrolling
                            ? std::clamp(rs.nav.menu_scroll_target_page, 0, total_pages - 1)
                            : cur_pg;

  if (scrolling) {
    const int full_span = geom.grid_w + geom.gap_x;
    const int offset = menu_scroll_shift(full_span, ctx.now_ms);
    const int out_dx = -rs.nav.menu_scroll_dir * offset;
    const int in_dx = rs.nav.menu_scroll_dir * (full_span - offset);
    draw_page_tiles(cur_pg, out_dx, false);
    draw_page_tiles(active_pg, in_dx, false);
  } else {
    const int bounce_x = edge_bounce().offset_px(
        spec.page_id, GestureAxisLock::Horizontal, ctx.now_ms, LATTER_PANEL_PPI);
    draw_page_tiles(cur_pg, bounce_x, true);
  }

  // Header at ly.hdr_y: static title on single page; header_pager_bar with left/right chevrons on multi-page.
  if (total_pages > 1) {
    ui::header_pager_bar(it, ctx.cx, ly.hdr_y, std::string(title), ctx.font_small, ctx.col_white,
                         true, WidgetId::CHEVRON_LEFT, WidgetId::CHEVRON_RIGHT, -1, 1,
                         active_pg > 0, (active_pg + 1) < total_pages);
  } else {
    render_page_header(it, ctx.cx, title, ctx.font_small, ctx.col_white);
  }

  // Sub-header at ly.sub_hdr_y: transient feedback takes priority, else horizontal page pips.
  if (spec.feedback_text != nullptr && spec.feedback_text[0] != '\0') {
    const std::string fit_fb = fit_text_to_width_font(
        std::string(spec.feedback_text), safe_span(ly.sub_hdr_y, 20), ctx.font_small);
    render_sub_header(it, ctx.cx, fit_fb, ctx.font_small, ctx.theme_secondary);
  } else if (total_pages > 1) {
    render_dock_page_pips(it, ctx.cx, ly.sub_hdr_y, total_pages, active_pg,
                          ctx.col_white, kChromePipInactive);
  }

  if (can_go_home(rs)) {
    render_home_indicator_bar(it, ctx.cx, ly.home_bar_y);
  }
}

}  // namespace ui

}  // namespace latter_ui
