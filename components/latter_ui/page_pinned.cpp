#include "esphome/core/defines.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

void render_pinned_card(esphome::display::Display &it, int cx_card, int cy_card,
                        const ShortcutItem &item, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int raw_w = (SCREEN_W < 380) ? 110 : 122;
  const bool is_active = (item.kind == ShortcutKind::ENTITY && item.state == 1);
  const bool is_unavail = (item.kind == ShortcutKind::ENTITY && item.state == 2);

  const Color bg_c = is_active ? mix_color_q8(kChromeGlassTint, ctx.theme_primary, 72)
                               : kChromeGlassTint;
  const Color brd_c = is_active ? ctx.theme_primary
                                : (is_unavail ? kChromeCardBrdOff : kChromeCardBorder);
  const Color icon_c = is_unavail ? ctx.col_gray : ctx.col_white;
  const Color text_c = is_unavail ? ctx.col_gray : ctx.col_white;

  const Color surf_bg = render_modern_card(
      it, cx_card, cy_card, raw_w, 86, 14,
      bg_c, brd_c, nullptr, text_c, ctx.font_small, GlassVariant::Clear, true);

  const int ix = cx_card;
  const int iy = cy_card - scale_y(13);
  const int ir = scale_s(14);
  const int mdi_px = std::max(18, 2 * ir);

  if (item.kind == ShortcutKind::ROUTE) {
    if (!item.icon.empty()) {
      render_mdi_icon(it, item.icon, ix, iy, mdi_px, icon_c, true, "mdi:apps");
    } else {
      ui::draw_route_icon(it, item.route_id, ix, iy, ir, icon_c, surf_bg, true);
    }
  } else {
    render_mdi_icon(it, item.icon, ix, iy, mdi_px, icon_c, true, "mdi:gesture-tap-button");
  }

  const int eff_w = (SCREEN_W > 520) ? scale_x(raw_w) : raw_w;
  const int max_lbl_w = std::max(48, eff_w - scale_x(14));
  const std::string fit_lbl = fit_text_to_width_font(item.label, max_lbl_w, ctx.font_small);
  fast_print(it, cx_card + 1, cy_card + scale_y(22) + 1, ctx.font_small, kChromeShadow,
             TextAlign::CENTER, fit_lbl.c_str());
  fast_print(it, cx_card, cy_card + scale_y(22), ctx.font_small, text_c,
             TextAlign::CENTER, fit_lbl.c_str());
}

}  // namespace

void render_pinned_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &sc = shortcuts_state();
  const auto &ly = layout();

  const int total_items = static_cast<int>(sc.pinned_items.size());
  if (total_items <= 0) {
    render_page_header(it, ctx.cx, "PINNED", ctx.font_small, ctx.col_white);
    render_empty_state(it, ctx.cx, ctx.cy, "Add pinned items in Latter Studio",
                       ctx.font_small, ctx.col_slate);
    return;
  }

  const int total_pages = std::max(1, (total_items + 5) / 6);
  const int raw_w = (SCREEN_W < 380) ? 110 : 122;
  const int eff_w = (SCREEN_W > 520) ? scale_x(raw_w) : raw_w;
  const int eff_h = (SCREEN_W > 520) ? scale_y(86) : 86;
  const int col_cx[2] = {ctx.cx - ly.tools_col_off, ctx.cx + ly.tools_col_off};
  const int row_cy[3] = {ly.tools_row_y(0), ly.tools_row_y(1), ly.tools_row_y(2)};
  const int min_vis_y = scale_y(75);
  const int max_vis_y = scale_y(395);

  auto draw_grid_for_page = [&](int pg_idx, int y_shift, bool register_hits) {
    const int base_idx = pg_idx * 6;
    for (int r = 0; r < 3; r++) {
      const int cy_card = row_cy[r] + y_shift;
      if (cy_card < min_vis_y || cy_card > max_vis_y) continue;
      for (int c = 0; c < 2; c++) {
        const int item_idx = base_idx + r * 2 + c;
        if (item_idx < 0 || item_idx >= total_items) continue;
        const int cx_card = col_cx[c];
        render_pinned_card(it, cx_card, cy_card, sc.pinned_items[static_cast<size_t>(item_idx)], ctx);
        if (register_hits) {
          const uint16_t slot = static_cast<uint16_t>(r * 2 + c);
          frame_hits().add_centered_rect(cx_card, cy_card, eff_w, eff_h,
                                         WidgetId::PINNED_CARD_BASE + slot,
                                         kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(item_idx));
        }
      }
    }
  };

  const int cur_pg = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
  if (rs.nav.menu_scroll_active && total_pages > 1) {
    const int full_span = scale_y(252);
    const int offset = menu_scroll_shift(full_span);
    const int out_dy = -rs.nav.menu_scroll_dir * offset;
    const int in_dy  = rs.nav.menu_scroll_dir * (full_span - offset);
    const int tgt_pg = std::clamp(rs.nav.menu_scroll_target_page, 0, total_pages - 1);
    draw_grid_for_page(cur_pg, out_dy, false);
    draw_grid_for_page(tgt_pg, in_dy, false);
  } else {
    const int bounce_y = edge_bounce().offset_px(
        PageId::PINNED, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);
    draw_grid_for_page(cur_pg, bounce_y, true);
  }

  if (rs.nav.menu_scroll_active && total_pages > 1 && !backdrop_active()) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, scale_y(82), ctx.col_bg);
    fill_rect_row_major(it, 0, scale_y(388), SCREEN_W, SCREEN_H - scale_y(388), ctx.col_bg);
  }

  render_page_header(it, ctx.cx, "PINNED", ctx.font_small, ctx.col_white);

  const int active_pip_pg = (rs.nav.menu_scroll_active && total_pages > 1)
                                ? std::clamp(rs.nav.menu_scroll_target_page, 0, total_pages - 1)
                                : cur_pg;
  if (!rs.nav.pinned_feedback.empty() &&
      rs.nav.pinned_feedback_ms > 0U &&
      static_cast<uint32_t>(ctx.now_ms - rs.nav.pinned_feedback_ms) < 1800U) {
    render_sub_header(it, ctx.cx, rs.nav.pinned_feedback, ctx.font_small, ctx.theme_secondary);
  } else if (total_pages > 1) {
    char pg_buf[24];
    std::snprintf(pg_buf, sizeof(pg_buf), "Page %d of %d", active_pip_pg + 1, total_pages);
    render_sub_header(it, ctx.cx, pg_buf, ctx.font_small, ctx.col_slate);
  }

  render_vertical_page_pips(it, active_pip_pg > 0, (active_pip_pg + 1) < total_pages,
                            ctx.col_bg, ctx.col_white, kChromePipInactive);
}

}  // namespace latter_ui
