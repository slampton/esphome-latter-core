#include "ui_pages.h"
#include "ui_tile_grid.h"

#include <algorithm>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace {

std::string to_ascii_upper(std::string_view sv) {
  std::string out(sv);
  for (char &ch : out) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
  }
  return out;
}

void draw_pinned_tile_icon(Rgb565Surface &it, int ix, int iy, int ir,
                           UiColor icon_c, bool shadow, void *user_ctx) {
  const auto *item = static_cast<const ShortcutItem *>(user_ctx);
  if (item == nullptr) return;
  const int mdi_px = std::max(14, 2 * ir);
  if (item->kind == ShortcutKind::ROUTE) {
    if (!item->icon.empty()) {
      render_mdi_icon(it, item->icon, ix, iy, mdi_px, icon_c, shadow, "mdi:apps");
    } else {
      ui::draw_route_icon(it, item->route_id, ix, iy, ir, icon_c, kChromeGlassTint, shadow);
    }
  } else {
    char state_slug_buf[96];
    const std::string_view eff_slug =
        resolve_state_mdi_slug(item->icon, item->state, state_slug_buf, sizeof(state_slug_buf));
    render_mdi_icon(it, eff_slug, ix, iy, mdi_px, icon_c, shadow, "mdi:gesture-tap-button");
  }
}

}  // namespace

void render_pinned_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &sc = shortcuts_state();

  if (sc.pinned_items.empty()) {
    ui::TileGridPageSpec empty_spec{};
    empty_spec.page_id = PageId::PINNED;
    empty_spec.card_widget_base = WidgetId::PINNED_CARD_BASE;
    empty_spec.page_title_upper = "PINNED";
    empty_spec.empty_message = "Add pinned items in Latter Studio";
    ui::render_tile_grid_page(it, ctx, empty_spec);
    return;
  }

  const int cols = tile_grid_cols_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int rows = tile_grid_rows_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  TileGridPlacement *placements = pinned_grid_placements_scratch();
  int8_t page_sources[kTileGridMaxLogicalPages]{};
  const auto layout_res = resolve_pinned_grid_layout(
      sc, cols, rows, placements, static_cast<int>(ShortcutsState::kMaxPinnedItems),
      page_sources, kTileGridMaxLogicalPages);
  const int total_pages = std::max(1, layout_res.total_pages);
  const int cur_pg = std::clamp(rs.nav.pinned_menu_page, 0, total_pages - 1);
  const int active_pg = (rs.nav.menu_scroll_active && total_pages > 1)
                            ? std::clamp(rs.nav.menu_scroll_target_page, 0, total_pages - 1)
                            : cur_pg;

  // Static BSS scratch keeps TileGridEntry[32] off ESPHome's 8 KB loopTask stack.
  static ui::TileGridEntry entries[2 * kTileGridMaxSlotsPerPage]{};
  int entry_count = 0;
  for (int i = 0; i < layout_res.placement_count && entry_count < 2 * kTileGridMaxSlotsPerPage; ++i) {
    const auto &pl = placements[i];
    if (pl.visual_page != cur_pg && (!rs.nav.menu_scroll_active || pl.visual_page != active_pg)) {
      continue;
    }
    if (pl.item_index < 0 || static_cast<size_t>(pl.item_index) >= sc.pinned_items.size()) continue;
    const auto &item = sc.pinned_items[static_cast<size_t>(pl.item_index)];
    const bool is_active = (item.kind == ShortcutKind::ENTITY && item.state == 1);
    const bool is_unavail = (item.kind == ShortcutKind::ENTITY && item.state == 2);
    ui::TileGridEntry e{};
    e.label = item.label.c_str();
    e.active = is_active;
    e.disabled = is_unavail;
    e.state = (item.kind == ShortcutKind::ENTITY) ? item.state : static_cast<int8_t>(-1);
    e.inverted_badge = (item.badge_style == ShortcutBadgeStyle::INVERTED);
    e.has_accent = (item.accent != ShortcutAccent::DEFAULT);
    e.accent_c = ui::resolve_shortcut_accent_color(item.accent, ctx, ctx.theme_primary);
    e.text_c = is_unavail ? ctx.col_gray : ctx.col_white;
    e.draw_icon_fn = draw_pinned_tile_icon;
    e.icon_ctx = const_cast<ShortcutItem *>(&item);
    e.widget_param = static_cast<int16_t>(pl.item_index);
    e.col_span = pl.col_span;
    e.row_span = pl.row_span;
    e.col = pl.col;
    e.row = pl.row;
    e.visual_page = pl.visual_page;
    entries[entry_count++] = e;
  }

  const int src_pg = static_cast<int>(page_sources[active_pg]);
  const std::string_view custom_title = resolve_pinned_page_title(sc, src_pg);
  const std::string title_upper =
      custom_title.empty() ? std::string("PINNED") : to_ascii_upper(custom_title);

  const bool show_fb = !rs.nav.pinned_feedback.empty() &&
                       rs.nav.pinned_feedback_ms > 0U &&
                       static_cast<uint32_t>(ctx.now_ms - rs.nav.pinned_feedback_ms) < 1800U;

  ui::TileGridPageSpec spec{};
  spec.page_id = PageId::PINNED;
  spec.card_widget_base = WidgetId::PINNED_CARD_BASE;
  spec.cur_page = cur_pg;
  spec.total_pages = total_pages;
  spec.page_title_upper = title_upper.c_str();
  spec.empty_message = "Add pinned items in Latter Studio";
  spec.feedback_text = show_fb ? rs.nav.pinned_feedback.c_str() : nullptr;
  spec.entries = entries;
  spec.entry_count = entry_count;
  ui::render_tile_grid_page(it, ctx, spec);
}

}  // namespace latter_ui
