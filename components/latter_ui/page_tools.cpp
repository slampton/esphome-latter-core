#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {

// ==============================================================================
// Shared Tools State & Helpers (declared in include/ui_pages.h)
// ==============================================================================

bool &stopwatch_modern_digital() {
  static bool modern = (LATTER_ROUND_DISPLAY == 0);
  return modern;
}

namespace {

std::string truncate_text(const std::string &raw, size_t max_chars) {
  if (max_chars == 0 || raw.empty()) return {};
  size_t count = 0;
  size_t byte_pos = 0;
  while (byte_pos < raw.size() && count < max_chars) {
    const unsigned char lead = static_cast<unsigned char>(raw[byte_pos]);
    size_t adv = 1;
    if ((lead & 0xE0U) == 0xC0U) adv = 2;
    else if ((lead & 0xF0U) == 0xE0U) adv = 3;
    else if ((lead & 0xF8U) == 0xF0U) adv = 4;
    if (byte_pos + adv > raw.size()) break;
    byte_pos += adv;
    count++;
  }
  if (byte_pos >= raw.size()) return raw;
  return raw.substr(0, byte_pos) + "…";
}

inline void render_rounded_rect(esphome::display::Display &it, int x0, int y0, int w, int h,
                                int r, esphome::Color col) {
  fill_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), col);
}

struct CalculatorState {
  double acc = 0.0;
  char pending_op = 0;
  char entry[16] = "0";
  bool fresh_entry = true;
  bool has_decimal = false;
  bool error = false;
};

CalculatorState &calc_state() {
  static CalculatorState st;
  return st;
}

void format_calc_double(double val, char *buf, size_t buf_len) {
  if (std::isnan(val) || std::isinf(val) || std::fabs(val) > 999999999999.0) {
    std::snprintf(buf, buf_len, "Error");
    calc_state().error = true;
    return;
  }
  if (std::fabs(val) < 1e-9) val = 0.0;
  std::snprintf(buf, buf_len, "%.6f", val);
  char *dot = std::strchr(buf, '.');
  if (dot != nullptr) {
    char *end = buf + std::strlen(buf) - 1;
    while (end > dot && *end == '0') {
      *end-- = '\0';
    }
    if (end == dot) {
      *dot = '\0';
    }
  }
  if (std::strlen(buf) > 12) {
    std::snprintf(buf, buf_len, "%.5g", val);
  }
}

void apply_calc_pending(CalculatorState &st, double rhs) {
  if (st.pending_op == '+') {
    st.acc += rhs;
  } else if (st.pending_op == '-') {
    st.acc -= rhs;
  } else if (st.pending_op == '*') {
    st.acc *= rhs;
  } else if (st.pending_op == '/') {
    if (std::fabs(rhs) < 1e-12) {
      st.error = true;
      std::snprintf(st.entry, sizeof(st.entry), "Error");
      st.pending_op = 0;
      st.fresh_entry = true;
      return;
    }
    st.acc /= rhs;
  } else {
    st.acc = rhs;
  }
  format_calc_double(st.acc, st.entry, sizeof(st.entry));
}

void render_die_face_pips(esphome::display::Display &it, int cx_d, int cy_d, int val, int spread,
                          int pip_r, esphome::Color pip_col, esphome::Color hi_col) {
  auto draw_pip = [&](int px, int py) {
    fast_filled_circle(it, px, py + 1, pip_r, hi_col);
    fast_filled_circle(it, px, py, pip_r, pip_col);
  };

  const int v = std::clamp(val, 1, 6);
  if (v == 1 || v == 3 || v == 5) {
    draw_pip(cx_d, cy_d);
  }
  if (v >= 2) {
    draw_pip(cx_d - spread, cy_d - spread);
    draw_pip(cx_d + spread, cy_d + spread);
  }
  if (v >= 4) {
    draw_pip(cx_d + spread, cy_d - spread);
    draw_pip(cx_d - spread, cy_d + spread);
  }
  if (v == 6) {
    draw_pip(cx_d - spread, cy_d);
    draw_pip(cx_d + spread, cy_d);
  }
}

void render_3d_die(esphome::display::Display &it, int cx_d, int cy_d, int sz, int val,
                   bool is_rolling, esphome::Color col_white, esphome::Color col_red,
                   esphome::Color col_gold) {
  using esphome::Color;
  const int r = sz / 2;
  const int x0 = cx_d - r;
  const int y0 = cy_d - r;
  const int cr = std::max(8, sz / 6);

  render_rounded_rect(it, x0 + 3, y0 + 5, sz, sz, cr, kChromeShadow);

  const Color ivory_body = is_rolling ? Color(248, 244, 228) : Color(242, 244, 248);
  const Color ivory_brd  = is_rolling ? col_gold : Color(188, 196, 208);
  render_card_box(it, x0, y0, sz, sz, ivory_body, ivory_brd, cr);

  fast_hline(it, x0 + cr, y0 + 2, sz - 2 * cr, col_white);
  fast_hline(it, x0 + cr, y0 + sz - 3, sz - 2 * cr, Color(205, 212, 222));

  const int spread = (sz * 27) / 100;
  const int pip_r  = std::max(4, sz / 11);
  const Color pip_c = (val == 1 || val == 4) ? col_red : Color(24, 28, 36);
  render_die_face_pips(it, cx_d, cy_d, val, spread, pip_r, pip_c, col_white);
}

void render_tool_launcher_card(esphome::display::Display &it, int cx_card, int cy_card,
                               int app_id, const char *label, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int card_w = (SCREEN_W < 380) ? 110 : 122;
  const Color surf_bg = render_modern_card(
      it, cx_card, cy_card, card_w, 86, 14,
      kChromeGlassTint, kChromeCardBorder, nullptr, ctx.col_white, ctx.font_small);

  const Color col_white = ctx.col_white;
  const Color theme_secondary = ctx.theme_secondary;
  const int ix = cx_card;
  const int iy = cy_card - scale_y(13);
  const int thick = (SCREEN_MIN_DIM > 520) ? 3 : 2;
  auto S = [](int v466) constexpr -> int { return scale_s(v466); };

  if (app_id == ToolsSubpage::TIMERS) {
    // 1. Timers: 2px-stroke hourglass with solid filled upper/lower sand bulbs
    fill_rect_row_major(it, ix - S(11), iy - S(14), 2 * S(11) + 1, thick, col_white);
    fill_rect_row_major(it, ix - S(11), iy + S(13), 2 * S(11) + 1, thick, col_white);
    for (int t = 0; t < thick; t++) {
      fast_line(it, ix - S(9) + t, iy - S(12), ix - S(2) + t, iy, col_white);
      fast_line(it, ix + S(9) - t, iy - S(12), ix + S(2) - t, iy, col_white);
      fast_line(it, ix - S(2) + t, iy, ix - S(9) + t, iy + S(12), col_white);
      fast_line(it, ix + S(2) - t, iy, ix + S(9) - t, iy + S(12), col_white);
    }
    fast_filled_triangle(it, ix - S(5), iy - S(7), ix + S(5), iy - S(7), ix, iy - S(2), col_white);
    fast_line(it, ix, iy - S(2), ix, iy + S(4), theme_secondary);
    fast_filled_triangle(it, ix - S(6), iy + S(11), ix + S(6), iy + S(11), ix, iy + S(4), col_white);

  } else if (app_id == ToolsSubpage::ALARMS) {
    // 2. Alarms: Twin solid filled bells, 2px clock bezel, angled legs & 2px hands
    fast_filled_circle(it, ix - S(10), iy - S(9), S(4), col_white);
    fast_filled_circle(it, ix + S(10), iy - S(9), S(4), col_white);
    fast_filled_circle(it, ix, iy + S(2), S(14), surf_bg);
    for (int t = 0; t < thick; t++) {
      fast_circle(it, ix, iy + S(2), S(12) - t, col_white);
      fast_line(it, ix - S(8) + t, iy + S(12), ix - S(11) + t, iy + S(16), col_white);
      fast_line(it, ix + S(8) - t, iy + S(12), ix + S(11) - t, iy + S(16), col_white);
      fast_line(it, ix + t, iy + S(2), ix + t, iy - S(5), col_white);
      fast_line(it, ix, iy + S(2) + t, ix + S(5), iy + S(5) + t, col_white);
    }
    fast_filled_circle(it, ix, iy + S(2), S(2), col_white);

  } else if (app_id == ToolsSubpage::STOPWATCH) {
    // 3. Stopwatch: 2px chronometer bezel, bold top crown, angled pusher & 2px sweep hand
    fill_rect_row_major(it, ix - S(4), iy - S(16), 2 * S(4) + 1, thick, col_white);
    fill_rect_row_major(it, ix - S(1), iy - S(16) + thick, 2 * S(1) + 1, S(4), col_white);
    for (int t = 0; t < thick; t++) {
      fast_line(it, ix + S(9) + t, iy - S(8) + t, ix + S(13) + t, iy - S(12) + t, col_white);
      fast_circle(it, ix, iy + S(2), S(12) - t, col_white);
    }
    fast_line(it, ix, iy - S(8), ix, iy - S(6), theme_secondary);
    fast_line(it, ix, iy + S(10), ix, iy + S(12), theme_secondary);
    fast_line(it, ix - S(10), iy + S(2), ix - S(8), iy + S(2), theme_secondary);
    fast_line(it, ix + S(8), iy + S(2), ix + S(10), iy + S(2), theme_secondary);
    fast_filled_circle(it, ix, iy + S(2), S(2), col_white);
    for (int t = 0; t < thick; t++) {
      fast_line(it, ix + t, iy + S(2), ix + S(6), iy - S(4) + t, col_white);
    }

  } else if (app_id == ToolsSubpage::DICE) {
    // 4. Dice Roll: Solid filled rounded cube with 5 crisp negative-space pips
    const int d_r = S(5);
    const int x0 = ix - S(13);
    const int y0 = iy - S(13);
    const int s = 2 * S(13) + 1;
    fill_rect_row_major(it, x0 + d_r, y0, s - 2 * d_r, s, col_white);
    fill_rect_row_major(it, x0, y0 + d_r, s, s - 2 * d_r, col_white);
    fast_filled_circle(it, x0 + d_r, y0 + d_r, d_r, col_white);
    fast_filled_circle(it, x0 + s - 1 - d_r, y0 + d_r, d_r, col_white);
    fast_filled_circle(it, x0 + d_r, y0 + s - 1 - d_r, d_r, col_white);
    fast_filled_circle(it, x0 + s - 1 - d_r, y0 + s - 1 - d_r, d_r, col_white);
    fast_filled_circle(it, ix, iy, S(2), surf_bg);
    fast_filled_circle(it, ix - S(7), iy - S(7), S(2), surf_bg);
    fast_filled_circle(it, ix + S(7), iy - S(7), S(2), surf_bg);
    fast_filled_circle(it, ix - S(7), iy + S(7), S(2), surf_bg);
    fast_filled_circle(it, ix + S(7), iy + S(7), S(2), surf_bg);

  } else if (app_id == ToolsSubpage::FLASHLIGHT) {
    // 5. Flashlight: Solid filled sculpted torch body & reflector head + 3 bold light beams
    fast_filled_triangle(it, ix - S(9), iy - S(7), ix + S(9), iy - S(7), ix - S(5), iy - S(1), col_white);
    fast_filled_triangle(it, ix + S(9), iy - S(7), ix - S(5), iy - S(1), ix + S(5), iy - S(1), col_white);
    fill_rect_row_major(it, ix - S(5), iy + S(1), 2 * S(5) + 1, S(14), col_white);
    fast_filled_circle(it, ix, iy + S(14), S(5), col_white);
    fill_rect_row_major(it, ix - S(1), iy + S(5), 2 * S(1) + 1, S(5), surf_bg);
    for (int t = 0; t < thick; t++) {
      fast_line(it, ix + t, iy - S(10), ix + t, iy - S(16), col_white);
      fast_line(it, ix - S(6) + t, iy - S(10), ix - S(9) + t, iy - S(15), col_white);
      fast_line(it, ix + S(6) - t, iy - S(10), ix + S(9) - t, iy - S(15), col_white);
    }

  } else if (app_id == ToolsSubpage::CALCULATOR) {
    // 6. Calculator: 2px frame, solid LCD display bar & 3x2 grid of solid keypad pills
    const int cw = 2 * S(12) + 1;
    const int ch = 2 * S(15) + 1;
    for (int t = 0; t < thick; t++) {
      fast_rect_outline(it, ix - S(12) + t, iy - S(15) + t, cw - 2 * t, ch - 2 * t, col_white);
    }
    fill_rect_row_major(it, ix - S(8), iy - S(11), 2 * S(8) + 1, S(6), col_white);
    for (int r = 0; r < 2; r++) {
      const int ky = iy - S(1) + r * S(6);
      fill_rect_row_major(it, ix - S(8), ky, S(4), S(3), col_white);
      fill_rect_row_major(it, ix - S(2), ky, S(4), S(3), col_white);
      fill_rect_row_major(it, ix + S(4), ky, S(4), S(3), col_white);
    }
    fill_rect_row_major(it, ix - S(8), iy + S(10), S(4), thick, col_white);
    fill_rect_row_major(it, ix - S(2), iy + S(10), S(10), thick, col_white);

  } else if (app_id == ToolsSubpage::LEVEL) {
    // 7. Bubble Level: 2px outer ring, cardinal crosshairs, inner ring & solid center bubble
    for (int t = 0; t < thick; t++) {
      fast_circle(it, ix, iy, S(14) - t, col_white);
      fast_line(it, ix + t, iy - S(17), ix + t, iy - S(13), col_white);
      fast_line(it, ix + t, iy + S(13), ix + t, iy + S(17), col_white);
      fast_line(it, ix - S(17), iy + t, ix - S(13), iy + t, col_white);
      fast_line(it, ix + S(13), iy + t, ix + S(17), iy + t, col_white);
    }
    fast_circle(it, ix, iy, S(7), theme_secondary);
    fast_filled_circle(it, ix, iy, S(4), col_white);

  } else if (app_id == ToolsSubpage::COIN_FLIP) {
    // 8. Coin Flip: 2px outer medallion rim, inner struck ring & solid house emblem
    for (int t = 0; t < thick; t++) {
      fast_circle(it, ix, iy, S(15) - t, col_white);
    }
    fast_circle(it, ix, iy, S(11), theme_secondary);
    fast_filled_triangle(it, ix - S(7), iy, ix + S(7), iy, ix, iy - S(7), col_white);
    fill_rect_row_major(it, ix - S(5), iy, 2 * S(5) + 1, S(7), col_white);
    fast_filled_circle(it, ix, iy + S(1), S(2), surf_bg);
    fast_line(it, ix, iy + S(3), ix, iy + S(6), surf_bg);

  } else if (app_id == ToolsSubpage::CALENDAR) {
    // 9. Calendar: 2px page frame, solid header band, twin binder rings & 3x2 date pips
    const int cal_w = 2 * S(13) + 1;
    const int cal_h = S(26);
    for (int t = 0; t < thick; t++) {
      fast_rect_outline(it, ix - S(13) + t, iy - S(12) + t, cal_w - 2 * t, cal_h - 2 * t, col_white);
    }
    fill_rect_row_major(it, ix - S(13) + 1, iy - S(12) + 1, cal_w - 2, S(6), col_white);
    fill_rect_row_major(it, ix - S(7), iy - S(15), S(3), S(5), col_white);
    fill_rect_row_major(it, ix + S(5), iy - S(15), S(3), S(5), col_white);
    for (int r = 0; r < 2; r++) {
      for (int c = 0; c < 3; c++) {
        fill_rect_row_major(it, ix - S(8) + c * S(7), iy - S(1) + r * S(6), S(3), S(3),
                            (r == 0 && c == 1) ? theme_secondary : col_white);
      }
    }

  } else if (app_id == ToolsSubpage::INTERCOM) {
    // 10. Intercom: Solid filled megaphone horn + handle & 2 radiating acoustic wave arcs
    fill_rect_row_major(it, ix - S(12), iy - S(3), S(5), S(7), col_white);
    fast_filled_triangle(it, ix - S(7), iy - S(3), ix - S(7), iy + S(3), ix + S(4), iy + S(10), col_white);
    fast_filled_triangle(it, ix - S(7), iy - S(3), ix + S(4), iy + S(10), ix + S(4), iy - S(10), col_white);
    fill_rect_row_major(it, ix - S(8), iy + S(4), S(3), S(6), theme_secondary);
    fast_line(it, ix + S(8), iy - S(5), ix + S(10), iy, col_white);
    fast_line(it, ix + S(10), iy, ix + S(8), iy + S(5), col_white);
    fast_line(it, ix + S(11), iy - S(8), ix + S(14), iy, theme_secondary);
    fast_line(it, ix + S(14), iy, ix + S(11), iy + S(8), theme_secondary);

  } else if (app_id == ToolsSubpage::CAMERAS) {
    // 12. Camera: Sculpted camera body, top prism & dual-ring lens
    fill_rect_row_major(it, ix - S(13), iy - S(8), S(26), S(19), col_white);
    fill_rect_row_major(it, ix - S(5), iy - S(12), S(10), S(4), col_white);
    fast_filled_circle(it, ix, iy + S(1), S(7), surf_bg);
    fast_circle(it, ix, iy + S(1), S(5), theme_secondary);
    fast_filled_circle(it, ix, iy + S(1), S(2), col_white);
    fast_filled_circle(it, ix + S(9), iy - S(5), S(1), surf_bg);
  }

  fast_print(it, cx_card + 1, cy_card + scale_y(22) + 1, ctx.font_small, kChromeShadow,
             TextAlign::CENTER, label);
  fast_print(it, cx_card, cy_card + scale_y(22), ctx.font_small, col_white,
             TextAlign::CENTER, label);
}

void render_tools_launcher(esphome::display::Display &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs) {
  struct ToolItemDef {
    int id;
    const char *label;
  };
#if LATTER_HAS_IMU
  static constexpr ToolItemDef kPage0[3][2] = {
      {{ToolsSubpage::TIMERS, "Timers"},         {ToolsSubpage::ALARMS, "Alarms"}},
      {{ToolsSubpage::STOPWATCH, "Stopwatch"},   {ToolsSubpage::LEVEL, "Level"}},
      {{ToolsSubpage::FLASHLIGHT, "Flashlight"}, {ToolsSubpage::CALCULATOR, "Calculator"}},
  };
  static constexpr ToolItemDef kPage1[3][2] = {
      {{ToolsSubpage::DICE, "Dice Roll"},        {ToolsSubpage::COIN_FLIP, "Coin Flip"}},
      {{ToolsSubpage::CALENDAR, "Calendar"},     {ToolsSubpage::INTERCOM, "Intercom"}},
      {{ToolsSubpage::CAMERAS, "Camera"},        {0, ""}},
  };
#else
  static constexpr ToolItemDef kPage0[3][2] = {
      {{ToolsSubpage::TIMERS, "Timers"},         {ToolsSubpage::ALARMS, "Alarms"}},
      {{ToolsSubpage::STOPWATCH, "Stopwatch"},   {ToolsSubpage::FLASHLIGHT, "Flashlight"}},
      {{ToolsSubpage::CALCULATOR, "Calculator"}, {ToolsSubpage::CALENDAR, "Calendar"}},
  };
  static constexpr ToolItemDef kPage1[3][2] = {
      {{ToolsSubpage::INTERCOM, "Intercom"},     {ToolsSubpage::CAMERAS, "Camera"}},
      {{ToolsSubpage::DICE, "Dice Roll"},        {ToolsSubpage::COIN_FLIP, "Coin Flip"}},
      {{0, ""},                                  {0, ""}},
  };
#endif

  const auto &ly = layout();
  const int raw_w = (SCREEN_W < 380) ? 110 : 122;
  const int eff_w = (SCREEN_W > 520) ? scale_x(raw_w) : raw_w;
  const int eff_h = (SCREEN_W > 520) ? scale_y(86) : 86;
  const int col_cx[2] = {ctx.cx - ly.tools_col_off, ctx.cx + ly.tools_col_off};
  const int row_cy[3] = {ly.tools_row_y(0), ly.tools_row_y(1), ly.tools_row_y(2)};
  const int min_vis_y = scale_y(75);
  const int max_vis_y = scale_y(395);

  auto draw_grid_for_page = [&](int pg_idx, int y_shift, bool register_hits) {
    const auto &grid = (pg_idx == 0) ? kPage0 : kPage1;
    for (int r = 0; r < 3; r++) {
      const int cy_card = row_cy[r] + y_shift;
      if (cy_card < min_vis_y || cy_card > max_vis_y) continue;
      for (int c = 0; c < 2; c++) {
        const int app_id = grid[r][c].id;
        if (app_id == 0) continue;
        const int cx_card = col_cx[c];
        render_tool_launcher_card(it, cx_card, cy_card, app_id, grid[r][c].label, ctx);
        if (register_hits) {
          const uint16_t slot = static_cast<uint16_t>(r * 2 + c);
          frame_hits().add_centered_rect(cx_card, cy_card, eff_w, eff_h,
                                         WidgetId::TOOLS_CARD_BASE + slot,
                                         kHitTap | kHitFocusSlot,
                                         static_cast<int16_t>(app_id));
        }
      }
    }
  };

  const int cur_pg = (rs.nav.tools_menu_page == 0) ? 0 : 1;
  if (rs.nav.menu_scroll_active) {
    const int full_span = scale_y(252);
    const int offset = menu_scroll_shift(full_span);
    const int out_dy = -rs.nav.menu_scroll_dir * offset;
    const int in_dy  = rs.nav.menu_scroll_dir * (full_span - offset);
    const int tgt_pg = (rs.nav.menu_scroll_target_page == 0) ? 0 : 1;
    draw_grid_for_page(cur_pg, out_dy, false);
    draw_grid_for_page(tgt_pg, in_dy, false);
  } else {
    draw_grid_for_page(cur_pg, 0, true);
  }

  if (rs.nav.menu_scroll_active && !backdrop_active()) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, scale_y(82), ctx.col_bg);
    fill_rect_row_major(it, 0, scale_y(388), SCREEN_W, SCREEN_H - scale_y(388), ctx.col_bg);
  }

  render_page_header(it, ctx.cx, "TOOLS", ctx.font_small, ctx.theme_primary);
  const int active_pip_pg = rs.nav.menu_scroll_active
                                ? ((rs.nav.menu_scroll_target_page == 0) ? 0 : 1)
                                : cur_pg;
  render_vertical_page_pips(it, active_pip_pg == 1, active_pip_pg == 0,
                            ctx.col_bg, ctx.col_white, kChromePipInactive);
}

void render_timers_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto *slots = timer_slots();
  const int view_idx = timer_view_slot();

  if (view_idx == 0) {
    render_page_header(it, ctx.cx, "TIMERS", ctx.font_btn_bold, ctx.theme_primary);

    int best_run_idx = -1;
    int best_run_rem = 999999;
    for (int i = 0; i < 3; i++) {
      if (slots[i].running) {
        const int r_s = (slots[i].target_ms > ctx.now_ms)
                            ? static_cast<int>((slots[i].target_ms - ctx.now_ms + 999U) / 1000U)
                            : 0;
        if (r_s < best_run_rem) {
          best_run_rem = r_s;
          best_run_idx = i;
        }
      }
    }
    if (best_run_idx >= 0 && slots[best_run_idx].duration_sec > 0) {
      float prog = 0.0f;
      if (ctx.now_ms < slots[best_run_idx].target_ms) {
        prog = static_cast<float>(slots[best_run_idx].target_ms - ctx.now_ms) /
               (static_cast<float>(slots[best_run_idx].duration_sec) * 1000.0f);
      }
      render_progress_arc_r224(it, ctx.cx, ctx.cy, prog, ctx.col_gold);
    }

    const int t_card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : layout().tool_card3_w;
    const int t_left   = ctx.cx - (t_card_w / 2);
    const int div_x    = layout().tool_card3_div_x();

    for (int i = 0; i < 3; i++) {
      const auto &s = slots[i];
      const int y_mid = layout().tool_card3_y(i);
      int disp_sec = s.duration_sec;
      if (s.running) {
        disp_sec = (s.target_ms > ctx.now_ms)
                       ? static_cast<int>((s.target_ms - ctx.now_ms + 999U) / 1000U)
                       : 0;
      } else if (s.paused) {
        disp_sec = s.remaining_sec;
      }

      const Color c_bg  = s.running ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off();
      const Color c_brd = s.running ? ctx.col_gold
                                    : (s.paused ? ctx.theme_primary : ui::chrome_card_brd_off());
      render_card_box(it, t_left, y_mid - 37, t_card_w, 74, c_bg, c_brd, 16);

      char t_str[20];
      const int hrs  = disp_sec / 3600;
      const int mins = (disp_sec % 3600) / 60;
      const int secs = disp_sec % 60;
      if (hrs > 0) {
        std::snprintf(t_str, sizeof(t_str), "%d:%02d:%02d", hrs, mins, secs);
      } else {
        std::snprintf(t_str, sizeof(t_str), "%02d:%02d", mins, secs);
      }

      const Color time_c = s.running ? ctx.col_gold : (s.paused ? ctx.col_white : ctx.col_slate);
      fast_print(it, t_left + scale_x(18), y_mid - 12, ctx.font_date, time_c,
                 TextAlign::CENTER_LEFT, t_str);

      const std::string lbl = format_timer_slot_label(i);
      std::string sub_str = truncate_text(lbl, 15);
      if (s.running) sub_str += " • Active";
      else if (s.paused) sub_str += " • Paused";
      fast_print(it, t_left + scale_x(20), y_mid + 18, ctx.font_small,
                 s.running ? ctx.col_white : ctx.col_slate,
                 TextAlign::CENTER_LEFT, sub_str.c_str());

      if ((s.running || s.paused) && s.duration_sec > 0) {
        float p_f = static_cast<float>(disp_sec) / static_cast<float>(s.duration_sec);
        p_f = std::clamp(p_f, 0.0f, 1.0f);
        const int bar_w = std::max(40, div_x - (t_left + scale_x(20)) - scale_x(12));
        fill_rect_row_major(it, t_left + scale_x(20), y_mid + 31, bar_w, 3, kChromeBarTrack);
        const int fill_w = static_cast<int>(p_f * static_cast<float>(bar_w));
        if (fill_w > 0) {
          fill_rect_row_major(it, t_left + scale_x(20), y_mid + 31, fill_w, 3,
                              s.running ? ctx.col_gold : ctx.theme_primary);
        }
      }

      const int btn_cx = t_left + t_card_w - scale_x(46);
      const Color btn_bg  = s.running ? scale_color(ctx.col_gold, 0.22f) : ui::chrome_card_bg_on();
      const Color btn_brd = s.running ? ctx.col_gold
                                      : (s.paused ? ctx.col_green : ui::chrome_card_brd_on());
      render_glass_circle(it, btn_cx, y_mid, 21, btn_bg, btn_brd);
      if (s.running) {
        fill_rect_row_major(it, btn_cx - 6, y_mid - 7, 4, 15, ctx.col_gold);
        fill_rect_row_major(it, btn_cx + 3, y_mid - 7, 4, 15, ctx.col_gold);
      } else {
        const Color play_c = s.paused ? ctx.col_green : ctx.col_white;
        fast_filled_triangle(it, btn_cx - 5, y_mid - 8, btn_cx - 5, y_mid + 8,
                             btn_cx + 8, y_mid, play_c);
      }

      frame_hits().add_rect(t_left, y_mid - 37, div_x - t_left, 74,
                            WidgetId::TIMER_TAB_BASE + static_cast<uint16_t>(i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
      frame_hits().add_rect(div_x, y_mid - 37, (t_left + t_card_w) - div_x, 74,
                            WidgetId::TIMER_PRIMARY_BTN,
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }

    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y,
                       tools_direct_home() ? "< Home" : "< Tools");
    return;
  }

  const int s_idx = (view_idx >= 1 && view_idx <= 3) ? (view_idx - 1) : 0;
  const auto &s = slots[s_idx];
  const bool s_running = s.running;
  const bool s_paused  = s.paused;
  const int s_dur_sec  = s.duration_sec;
  const int disp_sec = s_running
                           ? ((s.target_ms > ctx.now_ms)
                                  ? static_cast<int>((s.target_ms - ctx.now_ms + 999U) / 1000U)
                                  : 0)
                           : (s_paused ? s.remaining_sec : s_dur_sec);

  if ((s_running || s_paused) && s_dur_sec > 0) {
    float prog = 0.0f;
    if (s_running) {
      if (ctx.now_ms < s.target_ms) {
        prog = static_cast<float>(s.target_ms - ctx.now_ms) /
               (static_cast<float>(s_dur_sec) * 1000.0f);
      }
    } else {
      prog = static_cast<float>(s.remaining_sec) / static_cast<float>(s_dur_sec);
    }
    render_progress_arc_r224(it, ctx.cx, ctx.cy, prog,
                             s_running ? ctx.col_gold : ctx.theme_primary);
  }

  const std::string hdr_title = truncate_text(format_timer_header_upper(s_idx), 18);
  render_page_header(it, ctx.cx, hdr_title, ctx.font_btn_bold, ctx.theme_primary);

  const char *sub_lbl = "TAP TIME TO START";
  Color sub_col = ctx.col_slate;
  if (s_running) {
    sub_lbl = "COUNTING DOWN • TAP TO PAUSE";
    sub_col = ctx.col_gold;
  } else if (s_paused) {
    sub_lbl = "PAUSED • TAP TO RESUME";
    sub_col = ctx.col_cyan;
  } else if (!s.name.empty() || s_dur_sec != (s_idx + 1) * 300) {
    sub_lbl = "TAP HERE TO RESET DEFAULT";
    sub_col = ctx.col_slate;
    frame_hits().add_rect(ctx.cx - scale_x(93), scale_y(110), scale_x(186), scale_y(65),
                          WidgetId::TIMER_RESET_BTN, kHitTap, static_cast<int16_t>(s_idx));
  }
  fast_print(it, ctx.cx, scale_y(122), ctx.font_small, sub_col, TextAlign::CENTER, sub_lbl);

  char t_buf[24];
  const int hrs  = disp_sec / 3600;
  const int mins = (disp_sec % 3600) / 60;
  const int secs = disp_sec % 60;
  if (hrs > 0) {
    std::snprintf(t_buf, sizeof(t_buf), "%d:%02d:%02d", hrs, mins, secs);
  } else {
    std::snprintf(t_buf, sizeof(t_buf), "%02d:%02d", mins, secs);
  }

  const Color card_brd = s_running ? ctx.col_gold
                                   : (s_paused ? ctx.col_green : ui::chrome_card_brd_on());
  const Color card_bg  = s_running ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off();
  const int timer_box_w = (hrs > 0) ? scale_x(278) : scale_x(204);
  render_card_box(it, ctx.cx - timer_box_w / 2, scale_y(218), timer_box_w, scale_y(68),
                     card_bg, card_brd, 18);
  frame_hits().add_rect(ctx.cx - timer_box_w / 2, scale_y(218), timer_box_w, scale_y(68),
                        WidgetId::TIMER_PRIMARY_BTN, kHitTap | kHitFocusSlot,
                        static_cast<int16_t>(s_idx));
  if (hrs > 0) {
    fast_print(it, ctx.cx, scale_y(252), ctx.font_date,
               s_running ? ctx.col_gold : ctx.col_white, TextAlign::CENTER, t_buf);
  } else {
    render_time_readout(it, ctx.cx, t_buf, ctx.font_chrono_time,
                        s_running ? ctx.col_gold : ctx.col_white);
  }

  if (!s_running && !s_paused) {
    static constexpr const char *kTimerStepperLabels[4] = {"-5m", "-1m", "+1m", "+5m"};
    ui::stepper_row(it, ctx.cx, scale_y(296), 44, kTimerStepperLabels,
                    ctx.font_small, ctx.col_white, WidgetId::STEPPER_BASE);
  } else {
    ui::pill_button(it, ctx.cx - scale_x(72), ctx.cx + scale_x(72), scale_y(318), 22,
                    ui::chrome_card_brd_on(), ui::chrome_card_bg_on(),
                    "RESET", ctx.col_slate, ctx.font_btn_bold,
                    WidgetId::TIMER_RESET_BTN, kHitTap | kHitFocusSlot,
                    static_cast<int16_t>(s_idx));
  }

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y,
                     tools_direct_home() ? "< Home" : "< Timers");
}

void render_alarms_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &prefs = persisted_prefs();
  const auto &al = rs.alarms;

  auto format_alarm_str = [&](int h24, int m, char *out, size_t out_sz) {
    if (prefs.is_24h_format) {
      std::snprintf(out, out_sz, "%02d:%02d", h24, m);
    } else {
      int h12 = h24 % 12;
      if (h12 == 0) h12 = 12;
      std::snprintf(out, out_sz, "%d:%02d %s", h12, m, (h24 >= 12) ? "PM" : "AM");
    }
  };
  auto rec_label = [](int rec) -> const char * {
    if (rec == 1) return "Weekdays";
    if (rec == 2) return "Weekends";
    if (rec == 3) return "Once";
    return "Daily";
  };

  if (al.edit_idx == 0) {
    render_page_header(it, ctx.cx, "ALARMS", ctx.font_btn_bold, ctx.theme_primary);

    const int a_card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : layout().tool_card3_w;
    const int t_left   = ctx.cx - (a_card_w / 2);
    const int a_div_x  = layout().tool_card3_div_x();

    const uint32_t now_epoch = ctx.now.is_valid() ? static_cast<uint32_t>(ctx.now.timestamp) : 0U;
    for (int i = 0; i < 3; i++) {
      const auto &ap = prefs.alarms[i];
      const int y_mid = layout().tool_card3_y(i);
      const bool is_snoozed = (now_epoch > 0U && al.snooze_until > now_epoch &&
                               (al.ringing_idx == (i + 1) || al.ringing_idx == 0));

      const Color c_bg  = (ap.enabled || is_snoozed) ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off();
      const Color c_brd = is_snoozed ? ctx.col_gold
                                     : (ap.enabled ? ctx.theme_secondary : ui::chrome_card_brd_off());
      render_card_box(it, t_left, y_mid - 37, a_card_w, 74, c_bg, c_brd, 16);

      char a_buf[24];
      format_alarm_str(ap.hour, ap.minute, a_buf, sizeof(a_buf));
      fast_print(it, t_left + scale_x(18), y_mid - 11, ctx.font_date,
                 (ap.enabled || is_snoozed) ? ctx.col_white : ctx.col_slate,
                 TextAlign::CENTER_LEFT, a_buf);

      char sub_b[40];
      if (is_snoozed) {
        const uint32_t s_rem = al.snooze_until - now_epoch;
        std::snprintf(sub_b, sizeof(sub_b), "Snoozed • %lum %02lus",
                      static_cast<unsigned long>(s_rem / 60U),
                      static_cast<unsigned long>(s_rem % 60U));
      } else {
        std::snprintf(sub_b, sizeof(sub_b), "%s • %s",
                      rec_label(ap.recurrence), ap.enabled ? "Active" : "Off");
      }
      fast_print(it, t_left + scale_x(20), y_mid + 18, ctx.font_small,
                 is_snoozed ? ctx.col_gold : (ap.enabled ? ctx.theme_secondary : ctx.col_slate),
                 TextAlign::CENTER_LEFT, sub_b);

      const int sw_cx = t_left + a_card_w - scale_x(42);
      ui::toggle_switch(it, sw_cx, y_mid, ap.enabled,
                        is_snoozed ? ctx.col_gold : ctx.theme_secondary);

      frame_hits().add_rect(t_left, y_mid - 37, a_div_x - t_left, 74,
                            WidgetId::ALARM_CARD_BASE + static_cast<uint16_t>(i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
      frame_hits().add_rect(a_div_x, y_mid - 37, (t_left + a_card_w) - a_div_x, 74,
                            WidgetId::ALARM_TOGGLE_BASE + static_cast<uint16_t>(i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }

    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y,
                       tools_direct_home() ? "< Home" : "< Tools");
    return;
  }

  const int idx = std::clamp(al.edit_idx - 1, 0, 2);
  const auto &ap = prefs.alarms[idx];

  char hdr_b[24];
  std::snprintf(hdr_b, sizeof(hdr_b), "EDIT ALARM %d", idx + 1);
  render_page_header(it, ctx.cx, hdr_b, ctx.font_btn_bold, ctx.theme_primary);

  fast_print(it, ctx.cx, scale_y(122), ctx.font_small,
             ap.enabled ? ctx.col_green : ctx.col_slate, TextAlign::CENTER,
             ap.enabled ? "ALARM ENABLED (TAP TIME TO TOGGLE)"
                        : "ALARM OFF (TAP TIME TO ENABLE)");

  char t_str[16];
  char ampm_str[8] = "";
  if (prefs.is_24h_format) {
    std::snprintf(t_str, sizeof(t_str), "%02d:%02d", ap.hour, ap.minute);
  } else {
    int h12 = ap.hour % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(t_str, sizeof(t_str), "%d:%02d", h12, ap.minute);
    std::snprintf(ampm_str, sizeof(ampm_str), "%s", (ap.hour >= 12) ? "PM" : "AM");
  }

  render_card_box(it, ctx.cx - scale_x(102), scale_y(150), scale_x(204), scale_y(78),
                     ap.enabled ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                     ap.enabled ? ctx.col_green : ui::chrome_card_brd_on(), 18);
  frame_hits().add_rect(ctx.cx - scale_x(102), scale_y(150), scale_x(204), scale_y(78),
                        WidgetId::ALARM_TOGGLE_BASE + static_cast<uint16_t>(idx),
                        kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));
  fast_print(it, ctx.cx - (ampm_str[0] ? scale_x(16) : 0), scale_y(189),
             ctx.font_chrono_time, ap.enabled ? ctx.col_white : ctx.col_slate,
             TextAlign::CENTER, t_str);
  if (ampm_str[0] != '\0') {
    fast_print(it, ctx.cx + scale_x(72), scale_y(196), ctx.font_btn_bold,
               ap.enabled ? ctx.col_gold : ctx.col_slate, TextAlign::CENTER, ampm_str);
  }

  const int rec_w = scale_x(216);
  const int rec_h = scale_y(34);
  render_card_box(it, ctx.cx - rec_w / 2, scale_y(242), rec_w, rec_h,
                     ui::chrome_card_bg_on(), ctx.theme_primary, scale_s(17));
  frame_hits().add_rect(ctx.cx - rec_w / 2, scale_y(242), rec_w, rec_h,
                        WidgetId::ALARM_RECUR_BTN, kHitTap | kHitFocusSlot, 0, scale_y(8));
  char rec_b[36];
  std::snprintf(rec_b, sizeof(rec_b), "Schedule: %s", rec_label(ap.recurrence));
  fast_print(it, ctx.cx, scale_y(259), ctx.font_small, ctx.col_white, TextAlign::CENTER, rec_b);

  static constexpr const char *kAlarmStepperLabels[4] = {"-1h", "-5m", "+5m", "+1h"};
  ui::stepper_row(it, ctx.cx, scale_y(292), 44, kAlarmStepperLabels,
                  ctx.font_small, ctx.col_white, WidgetId::STEPPER_BASE);

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y,
                     tools_direct_home() ? "< Home" : "< Alarms");
}

void render_stopwatch_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                              const StopwatchRuntimeState &sw) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const uint32_t total_ms = sw.running ? (ctx.now_ms - sw.start_time_ms) : sw.elapsed_ms;
  const int mins   = static_cast<int>((total_ms / 60000U) % 60U);
  const int secs   = static_cast<int>((total_ms / 1000U) % 60U);
  const int tenths = static_cast<int>((total_ms / 100U) % 10U);

  // Modern Digital Stopwatch (always used on rectangular/square displays; toggleable via header tap on round)
  if (!LATTER_ROUND_DISPLAY || stopwatch_modern_digital()) {
    const float min_prog = static_cast<float>(total_ms % 60000U) / 60000.0f;
    render_progress_arc_r224(it, ctx.cx, ctx.cy, min_prog,
                             sw.running ? ctx.theme_secondary : ctx.col_slate);

    render_page_header(it, ctx.cx, LATTER_ROUND_DISPLAY ? "STOPWATCH  (DIGITAL)" : "STOPWATCH",
                       ctx.font_small, ctx.theme_primary);
#if LATTER_ROUND_DISPLAY
    frame_hits().add_rect(ctx.cx - scale_min(110), 0, 2 * scale_min(110) + 1, scale_min(68),
                          WidgetId::SW_STYLE_BTN, kHitTap | kHitFocusSlot);
#endif

    char main_buf[32];
    std::snprintf(main_buf, sizeof(main_buf), "%02d:%02d.%d", mins, secs, tenths);
    render_time_readout(it, ctx.cx, main_buf, ctx.font_time, ctx.col_white);

    if (sw.lap_ms > 0) {
      const int l_mins   = static_cast<int>((sw.lap_ms / 60000U) % 60U);
      const int l_secs   = static_cast<int>((sw.lap_ms / 1000U) % 60U);
      const int l_tenths = static_cast<int>((sw.lap_ms / 100U) % 10U);
      char lap_buf[32];
      std::snprintf(lap_buf, sizeof(lap_buf), "SPLIT  %02d:%02d.%d", l_mins, l_secs, l_tenths);
      if (backdrop_active()) {
        fast_print(it, ctx.cx + 1, scale_y(192) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, lap_buf);
      }
      fast_print(it, ctx.cx, scale_y(192), ctx.font_small, ctx.theme_primary, TextAlign::CENTER, lap_buf);
    } else {
      const char *st_lbl = sw.running ? "Running" : "Ready";
      if (backdrop_active()) {
        fast_print(it, ctx.cx + 1, scale_y(192) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, st_lbl);
      }
      fast_print(it, ctx.cx, scale_y(192), ctx.font_small,
                 sw.running ? ctx.theme_secondary : ctx.col_slate, TextAlign::CENTER, st_lbl);
    }

    const char *hero_lbl = sw.running ? "PAUSE" : (total_ms > 0 ? "RESUME" : "START");
    const int hero_y = scale_y(248);
    const int hero_w = (SCREEN_W > 520) ? scale_x(188) : 188;
    const int hero_h = (SCREEN_W > 520) ? scale_y(44) : 44;
    render_modern_card(it, ctx.cx, hero_y, 188, 44, 12,
                       sw.running ? kChromeCardBgOn : kChromeGlassTint,
                       sw.running ? ctx.theme_secondary : ctx.col_white,
                       hero_lbl, ctx.col_white, ctx.font_small);
    frame_hits().add_rect(scale_x(40), scale_y(72), SCREEN_W - 2 * scale_x(40), scale_y(148),
                          WidgetId::SW_PRIMARY_BTN, kHitTap);
    frame_hits().add_centered_rect(ctx.cx, hero_y, hero_w, hero_h,
                                   WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot, 0, scale_y(8));

    const int sub_y  = scale_y(324);
    const int sub_dx = scale_x(70);
    const int sub_w  = (SCREEN_W > 520) ? scale_x(124) : 124;
    const int sub_h  = (SCREEN_W > 520) ? scale_y(46) : 46;
    render_modern_card(it, ctx.cx - sub_dx, sub_y, 124, 46, 12, kChromeGlassTint,
                       (sw.lap_ms > 0) ? ctx.col_red : kChromeCardBrdOn,
                       "LAP / SPLIT", ctx.col_white, ctx.font_small);
    frame_hits().add_centered_rect(ctx.cx - sub_dx, sub_y, sub_w, sub_h,
                                   WidgetId::SW_LAP_BTN, kHitTap | kHitFocusSlot, 0, scale_y(6));

    render_modern_card(it, ctx.cx + sub_dx, sub_y, 124, 46, 12, kChromeDangerBg, ctx.col_red,
                       "RESET", ctx.col_white, ctx.font_small);
    frame_hits().add_centered_rect(ctx.cx + sub_dx, sub_y, sub_w, sub_h,
                                   WidgetId::SW_RESET_BTN, kHitTap | kHitFocusSlot, 0, scale_y(6));

    if (!tools_direct_home()) {
      render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
    }
    return;
  }

#if LATTER_ROUND_DISPLAY
  // Classic Round Chronograph Dial (default on round displays; tap 12 o'clock bezel for digital)
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  fast_circle(it, cx, cy, scale_min(230), ctx.col_white);
  fast_circle(it, cx, cy, scale_min(228), ctx.theme_secondary);
  fast_circle(it, cx, cy, scale_min(197), ctx.col_dark_gray);
  fast_circle(it, cx, cy, scale_min(196), ctx.theme_secondary);

  for (int s = 0; s < 60; s++) {
    const float t_sin = DIAL_SIN[s];
    const float t_cos = DIAL_COS[s];
    if (s % 5 == 0) {
      const float perp_x = -t_cos;
      const float perp_y = t_sin;
      const int x1 = cx + static_cast<int>(scale_min_f(196.0f) * t_sin);
      const int y1 = cy - static_cast<int>(scale_min_f(196.0f) * t_cos);
      const int x2 = cx + static_cast<int>(scale_min_f(227.0f) * t_sin);
      const int y2 = cy - static_cast<int>(scale_min_f(227.0f) * t_cos);
      fast_line(it, x1 + static_cast<int>(perp_x), y1 + static_cast<int>(perp_y),
                x2 + static_cast<int>(perp_x), y2 + static_cast<int>(perp_y), ctx.theme_secondary);
      fast_line(it, x1 - static_cast<int>(perp_x), y1 - static_cast<int>(perp_y),
                x2 - static_cast<int>(perp_x), y2 - static_cast<int>(perp_y), ctx.col_slate);
      fast_line(it, x1, y1, x2, y2, ctx.col_white);
    } else {
      const int x1 = cx + static_cast<int>(scale_min_f(210.0f) * t_sin);
      const int y1 = cy - static_cast<int>(scale_min_f(210.0f) * t_cos);
      const int x2 = cx + static_cast<int>(scale_min_f(227.0f) * t_sin);
      const int y2 = cy - static_cast<int>(scale_min_f(227.0f) * t_cos);
      fast_line(it, x1, y1, x2, y2, ctx.col_slate);
      fast_line(it, x1 + 1, y1, x2 + 1, y2, ctx.col_slate);
    }
  }

  static constexpr int kChronoTicks[11] = {0, 5, 10, 15, 20, 25, 35, 40, 45, 50, 55};
  static constexpr const char *kChronoStrs[11] = {"0", "5", "10", "15", "20", "25", "35", "40", "45", "50", "55"};
  for (int i = 0; i < 11; i++) {
    const int t_idx = kChronoTicks[i];
    const int nx = cx + static_cast<int>(scale_min_f(172.0f) * DIAL_SIN[t_idx]);
    const int ny = cy - static_cast<int>(scale_min_f(172.0f) * DIAL_COS[t_idx]);
    if (backdrop_active()) {
      fast_print(it, nx + 1, ny + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, kChronoStrs[i]);
    }
    fast_print(it, nx, ny, ctx.font_small, ctx.col_white, TextAlign::CENTER, kChronoStrs[i]);
  }

  if (backdrop_active()) {
    fast_print(it, cx + 1, scale_min(132) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, "CHRONOGRAPH");
  }
  fast_print(it, cx, scale_min(132), ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, "CHRONOGRAPH");

  char main_buf[32];
  std::snprintf(main_buf, sizeof(main_buf), "%02d:%02d.%d", mins, secs, tenths);
  if (backdrop_active()) {
    fast_print(it, cx + 1, scale_min(238) + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, main_buf);
  }
  fast_print(it, cx, scale_min(238), ctx.font_date, ctx.col_white, TextAlign::CENTER, main_buf);

  if (sw.lap_ms > 0) {
    const int l_mins   = static_cast<int>((sw.lap_ms / 60000U) % 60U);
    const int l_secs   = static_cast<int>((sw.lap_ms / 1000U) % 60U);
    const int l_tenths = static_cast<int>((sw.lap_ms / 100U) % 10U);
    char lap_buf[32];
    std::snprintf(lap_buf, sizeof(lap_buf), "SPLIT  %02d:%02d.%d", l_mins, l_secs, l_tenths);
    if (backdrop_active()) {
      fast_print(it, cx + 1, scale_min(274) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, lap_buf);
    }
    fast_print(it, cx, scale_min(274), ctx.font_small, ctx.theme_primary, TextAlign::CENTER, lap_buf);
  }

  const int btn_y     = cy + scale_min(99);
  const int btn_lap_x = cx - scale_min(57);
  const int btn_rst_x = cx + scale_min(57);
  const int btn_r     = scale_min(36);

  render_glass_circle(it, btn_lap_x, btn_y, btn_r, kChromeGlassTint,
                      (sw.lap_ms > 0) ? ctx.col_red : ctx.theme_secondary);
  fast_print(it, btn_lap_x + 1, btn_y + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, "LAP");
  fast_print(it, btn_lap_x, btn_y, ctx.font_small, ctx.col_white, TextAlign::CENTER, "LAP");

  render_glass_circle(it, btn_rst_x, btn_y, btn_r, kChromeGlassTint, kChromeCardBorder);
  fast_print(it, btn_rst_x + 1, btn_y + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, "RESET");
  fast_print(it, btn_rst_x, btn_y, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, "RESET");

  // Split-Second (Rattrapante) Ghost Hand via fast_sincos
  if (sw.lap_ms > 0) {
    const float lap_angle = (static_cast<float>(sw.lap_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
    float l_sin = 0.0f, l_cos = 1.0f;
    fast_sincos(lap_angle, l_sin, l_cos);
    const int lx = cx + static_cast<int>(scale_min_f(154.0f) * l_sin);
    const int ly_end = cy - static_cast<int>(scale_min_f(154.0f) * l_cos);
    fast_line(it, cx, cy, lx, ly_end, ctx.col_red);
    fast_line(it, cx + 1, cy, lx + 1, ly_end, ctx.col_red);
    fast_circle(it,
                cx + static_cast<int>(scale_min_f(158.0f) * l_sin),
                cy - static_cast<int>(scale_min_f(158.0f) * l_cos),
                scale_min(4), ctx.col_red);
  }

  // Main Central Arrow Hand (20 FPS High-Beat Sweep via fast_sincos)
  const float chrono_angle = (static_cast<float>(total_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
  float s_sin = 0.0f, s_cos = 1.0f;
  fast_sincos(chrono_angle, s_sin, s_cos);

  const int tip_x      = cx + static_cast<int>(scale_min_f(178.0f) * s_sin);
  const int tip_y      = cy - static_cast<int>(scale_min_f(178.0f) * s_cos);
  const int arr_base_x = cx + static_cast<int>(scale_min_f(152.0f) * s_sin);
  const int arr_base_y = cy - static_cast<int>(scale_min_f(152.0f) * s_cos);
  const int tail_x     = cx - static_cast<int>(scale_min_f(36.0f) * s_sin);
  const int tail_y     = cy + static_cast<int>(scale_min_f(36.0f) * s_cos);

  const float perp_x = s_cos;
  const float perp_y = s_sin;
  const int left_x   = arr_base_x + static_cast<int>(scale_min_f(8.0f) * perp_x);
  const int left_y   = arr_base_y + static_cast<int>(scale_min_f(8.0f) * perp_y);
  const int right_x  = arr_base_x - static_cast<int>(scale_min_f(8.0f) * perp_x);
  const int right_y  = arr_base_y - static_cast<int>(scale_min_f(8.0f) * perp_y);

  fast_line(it, tail_x + static_cast<int>(perp_x), tail_y + static_cast<int>(perp_y),
            arr_base_x + static_cast<int>(perp_x), arr_base_y + static_cast<int>(perp_y), ctx.theme_secondary);
  fast_line(it, tail_x - static_cast<int>(perp_x), tail_y - static_cast<int>(perp_y),
            arr_base_x - static_cast<int>(perp_x), arr_base_y - static_cast<int>(perp_y), ctx.col_slate);
  fast_line(it, tail_x, tail_y, arr_base_x, arr_base_y, ctx.col_white);

  fast_filled_triangle(it, tip_x, tip_y, left_x, left_y, right_x, right_y, ctx.col_white);
  fast_line(it, tip_x, tip_y, left_x, left_y, ctx.theme_secondary);
  fast_line(it, tip_x, tip_y, right_x, right_y, ctx.col_slate);
  fast_line(it, left_x, left_y, right_x, right_y, ctx.theme_secondary);

  fast_filled_circle(it, tail_x, tail_y, scale_min(8), ctx.col_dark_gray);
  fast_circle(it, tail_x, tail_y, scale_min(8), ctx.theme_secondary);
  fast_filled_circle(it, tail_x, tail_y, scale_min(3), ctx.theme_secondary);

  fast_filled_circle(it, cx, cy, scale_min(9), ctx.theme_secondary);
  fast_circle(it, cx, cy, scale_min(9), ctx.col_slate);
  fast_filled_circle(it, cx, cy, scale_min(6), ctx.col_dark_gray);
  fast_filled_circle(it, cx, cy, scale_min(2), ctx.col_white);

  // Hit-table order (top-most wins): dial face start/pause -> 12 o'clock style toggle -> LAP/RESET circles -> Back
  frame_hits().add_rect(scale_min(40), scale_min(69),
                        SCREEN_W - 2 * scale_min(40), scale_min(210),
                        WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_rect(cx - scale_min(110), 0, 2 * scale_min(110) + 1, scale_min(68),
                        WidgetId::SW_STYLE_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_circle(btn_lap_x, btn_y, btn_r,
                          WidgetId::SW_LAP_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, 8);
  frame_hits().add_circle(btn_rst_x, btn_y, btn_r,
                          WidgetId::SW_RESET_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, 8);

  if (!tools_direct_home()) {
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
  }
#endif
}

void render_dice_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::display::TextAlign;

  auto &ds = dice_state();
  bool rolling = ds.anim_active;
  int disp_v1 = ds.val_1;
  int disp_v2 = ds.val_2;
  int bounce_y1 = 0;
  int bounce_y2 = 0;

  if (rolling) {
    const uint32_t el = ctx.now_ms - ds.anim_start_ms;
    if (el >= 620U) {
      ds.anim_active = false;
      rolling = false;
    } else {
      const uint32_t step = el / 65U;
      disp_v1 = static_cast<int>(((step * 5U + 2U) % 6U) + 1U);
      disp_v2 = static_cast<int>(((step * 3U + 4U) % 6U) + 1U);
      const float damp = 1.0f - (static_cast<float>(el) / 620.0f);
      bounce_y1 = static_cast<int>(fast_sin(static_cast<float>(el) * 0.032f) * 14.0f * damp);
      bounce_y2 = static_cast<int>(fast_cos(static_cast<float>(el) * 0.036f) * 14.0f * damp);
    }
  }

  render_page_header(it, ctx.cx, "DICE ROLLER", ctx.font_btn_bold, ctx.theme_primary);

  if (rolling) {
    fast_print(it, ctx.cx, scale_y(112), ctx.font_small, ctx.col_gold,
               TextAlign::CENTER, "ROLLING...");
  } else {
    char sum_buf[32];
    const int total = disp_v1 + disp_v2;
    if (disp_v1 == disp_v2) {
      std::snprintf(sum_buf, sizeof(sum_buf), "TOTAL: %d  •  DOUBLES!", total);
    } else {
      std::snprintf(sum_buf, sizeof(sum_buf), "TOTAL: %d", total);
    }
    fast_print(it, ctx.cx, scale_y(112), ctx.font_date,
               (disp_v1 == disp_v2) ? ctx.col_gold : ctx.col_white,
               TextAlign::CENTER, sum_buf);
  }

  const int die_sz  = scale_min(104);
  const int die_off = scale_x(68);
  const int die_cy  = scale_y(218);
  render_3d_die(it, ctx.cx - die_off, die_cy + bounce_y1, die_sz, disp_v1,
                rolling, ctx.col_white, ctx.col_red, ctx.col_gold);
  render_3d_die(it, ctx.cx + die_off, die_cy + bounce_y2, die_sz, disp_v2,
                rolling, ctx.col_white, ctx.col_red, ctx.col_gold);

  ui::pill_button(it, ctx.cx - scale_x(84), ctx.cx + scale_x(84), scale_y(344), 23,
                  ctx.theme_primary, ui::chrome_card_bg_on(),
                  rolling ? "ROLLING..." : "ROLL DICE",
                  ctx.col_white, ctx.font_btn_bold,
                  WidgetId::DICE_ROLL_BTN, kHitTap | kHitFocusSlot);

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Tools");
}

void render_calculator_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &st = calc_state();
  const bool compact_w = (SCREEN_W < 380);
  const int disp_w = compact_w ? (SCREEN_W - 24) : scale_x(324);
  const int disp_x = ctx.cx - disp_w / 2;
  const int disp_y = scale_y(54);
  const int disp_h = scale_y(66);

  render_card_box(it, disp_x, disp_y, disp_w, disp_h,
                     ui::chrome_card_bg_off(), ui::chrome_card_brd_on(), 16);

  if (st.pending_op != 0 && !st.error) {
    const char *op_sym = (st.pending_op == '*') ? "×"
                         : (st.pending_op == '/') ? "÷"
                         : (st.pending_op == '-') ? "−" : "+";
    fast_print(it, disp_x + 16, disp_y + disp_h / 2, ctx.font_btn_bold,
               ctx.col_orange, TextAlign::CENTER_LEFT, op_sym);
  }

  fast_print(it, disp_x + disp_w - 16, disp_y + disp_h / 2,
             (std::strlen(st.entry) > 9) ? ctx.font_btn_bold : ctx.font_date,
             st.error ? ctx.col_red : ctx.col_white,
             TextAlign::CENTER_RIGHT, st.entry);

  static constexpr const char *kKeyLabels[5][4] = {
      {"C", "±", "%", "÷"},
      {"7", "8", "9", "×"},
      {"4", "5", "6", "−"},
      {"1", "2", "3", "+"},
      {"0", ".", "⌫", "="},
  };

  const int key_w = compact_w ? 64 : scale_x(74);
  const int key_h = scale_y(46);
  const int row_y0 = scale_y(134);
  const int row_stride = scale_y(52);

  for (int r = 0; r < 5; r++) {
    const int ky_mid = row_y0 + r * row_stride + key_h / 2;
    const int ky = ky_mid - key_h / 2;
    for (int c = 0; c < 4; c++) {
      const int kx_mid = layout().calc_col_x(c);
      const int kx = kx_mid - key_w / 2;

      Color k_bg  = ui::chrome_key_bg();
      Color k_brd = ui::chrome_key_brd();
      Color k_fg  = ctx.col_white;

      if (c == 3) {
        const char row_op = (r == 0) ? '/' : (r == 1) ? '*' : (r == 2) ? '-' : (r == 3) ? '+' : '=';
        const bool is_active_op = (r < 4 && st.pending_op == row_op && st.fresh_entry);
        k_bg  = is_active_op ? ctx.col_orange
                             : ((r == 4) ? scale_color(ctx.theme_primary, 0.38f)
                                         : scale_color(ctx.col_orange, 0.24f));
        k_brd = (r == 4) ? ctx.theme_primary : ctx.col_orange;
        k_fg  = is_active_op ? ctx.col_bg : ctx.col_white;
      } else if (r == 0) {
        k_bg  = ui::chrome_card_bg_on();
        k_brd = ui::chrome_card_brd_on();
        k_fg  = (c == 0) ? ctx.col_gold : ctx.col_cyan;
      }

      render_card_box(it, kx, ky, key_w, key_h, k_bg, k_brd, 12);
      fast_print(it, kx_mid, ky_mid, ctx.font_btn_bold, k_fg,
                 TextAlign::CENTER, kKeyLabels[r][c]);

      const uint16_t key_idx = static_cast<uint16_t>(r * 4 + c);
      frame_hits().add_rect(kx, ky, key_w, key_h,
                            WidgetId::CALC_KEY_BASE + key_idx,
                            kHitTap | kHitFocusSlot,
                            static_cast<int16_t>(key_idx));
    }
  }

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Tools");
}

void render_level_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                          const DeviceRuntimeState &rs) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const float roll  = std::isnan(rs.hw_imu_roll) ? 0.0f : rs.hw_imu_roll;
  const float pitch = std::isnan(rs.hw_imu_pitch) ? 0.0f : rs.hw_imu_pitch;
  const float tilt_mag = std::sqrt(roll * roll + pitch * pitch);
  const bool is_level  = (tilt_mag < 1.2f);

  render_page_header(it, ctx.cx, "SPIRIT LEVEL", ctx.font_btn_bold, ctx.theme_primary);

  const int v_cy   = scale_y(212);
  const int v_r    = scale_min(108);
  const int ring_r = scale_min(26);
  const Color accent = is_level ? ctx.col_green : ctx.col_gold;

  fast_filled_circle(it, ctx.cx, v_cy, v_r, ui::chrome_card_bg_off());
  fast_circle(it, ctx.cx, v_cy, v_r, is_level ? ctx.col_green : ui::chrome_card_brd_on());
  fast_circle(it, ctx.cx, v_cy, (v_r * 2) / 3, ui::chrome_card_brd_off());

  fast_hline(it, ctx.cx - v_r + 8, v_cy, (v_r - 8) * 2, ui::chrome_card_brd_off());
  fast_vline(it, ctx.cx, v_cy - v_r + 8, (v_r - 8) * 2, ui::chrome_card_brd_off());
  fast_circle(it, ctx.cx, v_cy, ring_r, accent);

  const float max_span = static_cast<float>(v_r - scale_min(20));
  float bx_off = (roll / 30.0f) * max_span;
  float by_off = (-pitch / 30.0f) * max_span;
  const float off_d = std::sqrt(bx_off * bx_off + by_off * by_off);
  if (off_d > max_span && off_d > 0.01f) {
    bx_off = (bx_off / off_d) * max_span;
    by_off = (by_off / off_d) * max_span;
  }

  const int bx = ctx.cx + static_cast<int>(std::round(bx_off));
  const int by = v_cy + static_cast<int>(std::round(by_off));
  const int bub_r = scale_min(18);
  fast_filled_circle(it, bx, by + 2, bub_r, kChromeShadow);
  fast_filled_circle(it, bx, by, bub_r, accent);
  fast_circle(it, bx, by, bub_r, ctx.col_white);
  fast_filled_circle(it, bx - bub_r / 3, by - bub_r / 3, std::max(2, bub_r / 4), ctx.col_white);

  char deg_buf[48];
  if (is_level) {
    std::snprintf(deg_buf, sizeof(deg_buf), "LEVEL  •  0.0°");
  } else {
    std::snprintf(deg_buf, sizeof(deg_buf), "X: %+.1f°   •   Y: %+.1f°", roll, pitch);
  }
  fast_print(it, ctx.cx, scale_y(352), ctx.font_btn_bold,
             is_level ? ctx.col_green : ctx.col_white, TextAlign::CENTER, deg_buf);

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Tools");
}

void render_coin_flip_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &cs = coin_state();
  bool flipping = cs.anim_active;
  int disp_side = cs.side;
  int coin_ry   = scale_min(74);
  int lift_y    = 0;

  if (flipping) {
    const uint32_t el = ctx.now_ms - cs.anim_start_ms;
    if (el >= 680U) {
      cs.anim_active = false;
      flipping = false;
    } else {
      const float t = static_cast<float>(el) / 680.0f;
      lift_y = -static_cast<int>(42.0f * 4.0f * t * (1.0f - t));
      const float spin_c = fast_cos(t * 5.0f * PI_F);
      coin_ry = std::max(6, static_cast<int>(std::fabs(spin_c) * static_cast<float>(scale_min(74))));
      disp_side = (spin_c >= 0.0f) ? cs.side : (1 - cs.side);
    }
  }

  render_page_header(it, ctx.cx, "COIN FLIP", ctx.font_btn_bold, ctx.theme_primary);

  const char *res_lbl = flipping ? "FLIPPING..." : ((disp_side == 0) ? "HEADS" : "TAILS");
  fast_print(it, ctx.cx, scale_y(112), ctx.font_date,
             flipping ? ctx.col_slate : ((disp_side == 0) ? ctx.col_gold : ctx.col_cyan),
             TextAlign::CENTER, res_lbl);

  const int coin_cy = scale_y(220) + lift_y;
  const int coin_rx = scale_min(74);
  const Color rim_c  = (disp_side == 0) ? ctx.col_gold : Color(195, 208, 224);
  const Color body_c = (disp_side == 0) ? Color(212, 158, 24) : Color(138, 154, 174);

  for (int dy = -coin_ry; dy <= coin_ry; dy++) {
    const float ny = static_cast<float>(dy) / static_cast<float>(std::max(1, coin_ry));
    const int hw = static_cast<int>(std::sqrt(std::max(0.0f, 1.0f - ny * ny)) *
                                    static_cast<float>(coin_rx));
    if (hw > 0) {
      fast_hline(it, ctx.cx - hw, coin_cy + dy, hw * 2 + 1, body_c);
      fast_pixel(it, ctx.cx - hw, coin_cy + dy, rim_c);
      fast_pixel(it, ctx.cx + hw, coin_cy + dy, rim_c);
    }
  }
  if (coin_ry >= scale_min(34)) {
    fast_print(it, ctx.cx, coin_cy, ctx.font_chrono_time, ctx.col_white,
               TextAlign::CENTER, (disp_side == 0) ? "H" : "T");
  }

  ui::pill_button(it, ctx.cx - scale_x(84), ctx.cx + scale_x(84), scale_y(344), 23,
                  ctx.theme_primary, ui::chrome_card_bg_on(),
                  flipping ? "FLIPPING..." : "FLIP COIN",
                  ctx.col_white, ctx.font_btn_bold,
                  WidgetId::COIN_FLIP_BTN, kHitTap | kHitFocusSlot);

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Tools");
}

void render_calendar_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                             const DeviceRuntimeState &rs) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &ly = layout();
  const int detail_mode = cal_detail_mode();

  if (detail_mode == 2) {
    const std::string &base_map_u = cal_detail_map_url();
    const bool has_map_img = !base_map_u.empty() &&
                             ctx.camera_snapshot_art.valid() &&
                             !rs.alert.loaded_camera_url.empty() &&
                             rs.alert.loaded_camera_url.find(base_map_u) != std::string::npos;

    if (has_map_img) {
      render_album_art_canvas(it, ctx.camera_snapshot_art, ctx.cx, false, false, false, false);
    } else {
      render_empty_state(it, ctx.cx, ctx.cy, "Loading Map...", ctx.font_small, ctx.col_slate);
    }

    render_card_box(it, ctx.cx - scale_x(132), scale_y(24), scale_x(264), 38,
                       kChromeGlassTint, ui::chrome_card_brd_on(), 19);
    const std::string loc_hdr = truncate_text(
        cal_detail_location().empty() ? "Event Map" : cal_detail_location(), 24);
    fast_print(it, ctx.cx, scale_y(43), ctx.font_small, ctx.col_white,
               TextAlign::CENTER, loc_hdr.c_str());

    const int zl_x = ly.map_zoom_left_x();
    const int zr_x = ly.map_zoom_right_x();
    const int zy   = ly.map_zoom_y;
    const int z_r  = scale_s(22);
    render_glass_circle(it, zl_x, zy, z_r, kChromeGlassTint, ui::chrome_card_brd_on());
    fast_hline(it, zl_x - scale_s(7), zy, scale_s(15), (cal_map_zoom() > 10) ? ctx.col_white : ctx.col_slate);
    fast_hline(it, zl_x - scale_s(7), zy + 1, scale_s(15), (cal_map_zoom() > 10) ? ctx.col_white : ctx.col_slate);
    frame_hits().add_circle(zl_x, zy, z_r, WidgetId::CAL_ZOOM_OUT_BTN,
                            kHitTap | kHitFocusCircle, 0, scale_s(16));

    render_glass_circle(it, zr_x, zy, z_r, kChromeGlassTint, ui::chrome_card_brd_on());
    const Color zi_c = (cal_map_zoom() < 18) ? ctx.col_white : ctx.col_slate;
    fast_hline(it, zr_x - scale_s(7), zy, scale_s(15), zi_c);
    fast_hline(it, zr_x - scale_s(7), zy + 1, scale_s(15), zi_c);
    fast_vline(it, zr_x, zy - scale_s(7), scale_s(15), zi_c);
    fast_vline(it, zr_x + 1, zy - scale_s(7), scale_s(15), zi_c);
    frame_hits().add_circle(zr_x, zy, z_r, WidgetId::CAL_ZOOM_IN_BTN,
                            kHitTap | kHitFocusCircle, 0, scale_s(16));

    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Event");
    return;
  }

  if (detail_mode == 1) {
    render_page_header(it, ctx.cx, "EVENT DETAILS", ctx.font_btn_bold, ctx.theme_primary);

    const int card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : ly.list_card_w;
    const int card_x = ctx.cx - card_w / 2;

    render_card_box(it, card_x, scale_y(108), card_w, scale_y(122),
                       ui::chrome_card_bg_on(), ui::chrome_card_brd_on(), 16);
    fast_print(it, card_x + 18, scale_y(136), ctx.font_small, ctx.col_gold,
               TextAlign::CENTER_LEFT,
               cal_detail_time().empty() ? "Upcoming" : cal_detail_time().c_str());

    const std::string t1 = truncate_text(cal_detail_title(), 24);
    fast_print(it, card_x + 18, scale_y(172), ctx.font_btn_bold, ctx.col_white,
               TextAlign::CENTER_LEFT, t1.c_str());

    const std::string &loc_s = cal_detail_location();
    const std::string &map_u = cal_detail_map_url();
    if (!loc_s.empty()) {
      render_card_box(it, card_x, scale_y(246), card_w, scale_y(74),
                         ui::chrome_card_bg_off(),
                         !map_u.empty() ? ctx.theme_primary : ui::chrome_card_brd_off(), 16);
      const std::string loc_trim = truncate_text(loc_s, 25);
      fast_print(it, card_x + 18, scale_y(272), ctx.font_small, ctx.col_white,
                 TextAlign::CENTER_LEFT, loc_trim.c_str());
      fast_print(it, card_x + 18, scale_y(298), ctx.font_small,
                 !map_u.empty() ? ctx.col_cyan : ctx.col_slate,
                 TextAlign::CENTER_LEFT,
                 !map_u.empty() ? "Tap to view map >" : "Location");
      if (!map_u.empty()) {
        frame_hits().add_rect(card_x, scale_y(246), card_w, scale_y(74),
                              WidgetId::CAL_EVENT_BASE, kHitTap | kHitFocusSlot, 0);
      }
    } else {
      fast_print(it, ctx.cx, scale_y(278), ctx.font_small, ctx.col_slate,
                 TextAlign::CENTER, "No location attached");
    }

    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Agenda");
    return;
  }

  if (cal_quick_add_open()) {
    render_page_header(it, ctx.cx, "QUICK ADD", ctx.font_btn_bold, ctx.theme_primary);

    static constexpr const char *kQuickLabels[4] = {
        "Reminder in 30m",
        "Meeting in 1h",
        "Errand in 2h",
        "Tomorrow 9:00 AM",
    };
    const int card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : ly.list_card_w;
    const int card_x = ctx.cx - card_w / 2;

    for (int i = 0; i < 4; i++) {
      const int ym = ly.slot5_y(i);
      render_card_box(it, card_x, ym - ly.slot5_h / 2, card_w, ly.slot5_h,
                         ui::chrome_card_bg_on(), ui::chrome_card_brd_on(), scale_s(14));
      fast_print(it, card_x + scale_x(18), ym, ctx.font_small, ctx.col_white,
                 TextAlign::CENTER_LEFT, kQuickLabels[i]);
      fast_print(it, card_x + card_w - scale_x(16), ym, ctx.font_small, ctx.col_gold,
                 TextAlign::CENTER_RIGHT, "+");
      frame_hits().add_rect(card_x, ym - ly.slot5_h / 2, card_w, ly.slot5_h,
                            WidgetId::CAL_PRESET_BASE + static_cast<uint16_t>(i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }

    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Cancel");
    return;
  }

  render_page_header(it, ctx.cx, "AGENDA", ctx.font_btn_bold, ctx.theme_primary);

  const int add_x1 = ctx.cx + scale_x(72);
  const int add_x2 = ctx.cx + scale_x(138);
  ui::pill_button(it, add_x1, add_x2, ly.hdr_y, 14,
                  ctx.theme_secondary, ui::chrome_card_bg_on(),
                  "+ Add", ctx.col_white, ctx.font_small,
                  WidgetId::CAL_QUICK_ADD_BTN, kHitTap | kHitFocusSlot);

  if (!cal_feedback_text().empty() && (ctx.now_ms - cal_feedback_ms() < 3500U)) {
    render_sub_header(it, ctx.cx, cal_feedback_text(), ctx.font_small, ctx.col_green);
  }

  const auto *slots = rs.slots_for(latter_rpc::SlotChannel::CALENDAR);
  const int cur_pg  = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::CALENDAR)];
  const int tot_pg  = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::CALENDAR)];

  int valid_cnt = 0;
  const bool compact_w = (SCREEN_W < 380);
  const int card_w = compact_w ? (SCREEN_W - 24) : ly.list_card_w;
  const int card_x = ctx.cx - card_w / 2;
  const int card_h = std::min(46, ly.slot5_stride - 6);

  for (int i = 0; i < 5; i++) {
    if (slots[i].name.empty()) continue;
    valid_cnt++;

    std::string ev_time, ev_title, ev_loc, ev_map;
    parse_calendar_slot_meta(slots[i].name, slots[i].id, ev_time, ev_title, ev_loc, ev_map);

    const int ym = ly.slot5_y(i);
    render_card_box(it, card_x, ym - card_h / 2, card_w, card_h,
                       ui::chrome_card_bg_off(), ui::chrome_card_brd_on(), 14);

    if (!ev_time.empty()) {
      const std::string t_short = truncate_text(ev_time, 9);
      fast_print(it, card_x + 14, ym, ctx.font_small, ctx.col_gold,
                 TextAlign::CENTER_LEFT, t_short.c_str());
      const std::string ttl_short = truncate_text(ev_title, compact_w ? 14 : 18);
      fast_print(it, card_x + scale_x(96), ym, ctx.font_small, ctx.col_white,
                 TextAlign::CENTER_LEFT, ttl_short.c_str());
    } else {
      const std::string ttl_short = truncate_text(ev_title, compact_w ? 22 : 27);
      fast_print(it, card_x + 14, ym, ctx.font_small, ctx.col_white,
                 TextAlign::CENTER_LEFT, ttl_short.c_str());
    }

    frame_hits().add_rect(card_x, ym - card_h / 2, card_w, card_h,
                          WidgetId::CAL_EVENT_BASE + static_cast<uint16_t>(i),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
  }

  if (valid_cnt == 0) {
    render_empty_state(it, ctx.cx, ctx.cy, "No Upcoming Events", ctx.font_small, ctx.col_slate);
  }

  if (tot_pg > 1) {
    render_vertical_page_pips(it, cur_pg > 1, cur_pg < tot_pg,
                              ctx.col_bg, ctx.col_white, kChromePipInactive);
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y,
                     tools_direct_home() ? "< Home" : "< Tools");
}

}  // namespace

void reset_calculator_state() {
  auto &st = calc_state();
  st.acc = 0.0;
  st.pending_op = 0;
  std::snprintf(st.entry, sizeof(st.entry), "0");
  st.fresh_entry = true;
  st.has_decimal = false;
  st.error = false;
}

void handle_calculator_keypad_tap(int tx, int ty) {
  const bool compact_w = (SCREEN_W < 380);
  const int key_w = compact_w ? 68 : scale_x(78);
  const int key_h = scale_y(50);
  const int row_y0 = scale_y(134);
  const int row_stride = scale_y(52);

  int hit_r = -1;
  int hit_c = -1;
  for (int r = 0; r < 5; r++) {
    const int ky_mid = row_y0 + r * row_stride + scale_y(23);
    if (std::abs(ty - ky_mid) <= key_h / 2) {
      hit_r = r;
      break;
    }
  }
  for (int c = 0; c < 4; c++) {
    const int kx_mid = layout().calc_col_x(c);
    if (std::abs(tx - kx_mid) <= key_w / 2) {
      hit_c = c;
      break;
    }
  }
  if (hit_r < 0 || hit_c < 0) return;

  auto &st = calc_state();
  if (st.error && !(hit_r == 0 && hit_c == 0)) {
    reset_calculator_state();
  }

  auto cur_val = [&]() -> double {
    return std::strtod(st.entry, nullptr);
  };

  if (hit_r == 0 && hit_c == 0) {
    reset_calculator_state();
    return;
  }
  if (hit_r == 0 && hit_c == 1) {
    const double v = -cur_val();
    format_calc_double(v, st.entry, sizeof(st.entry));
    return;
  }
  if (hit_r == 0 && hit_c == 2) {
    const double v = cur_val() * 0.01;
    format_calc_double(v, st.entry, sizeof(st.entry));
    st.fresh_entry = true;
    return;
  }
  if (hit_c == 3) {
    const double rhs = cur_val();
    if (hit_r == 4) {
      if (st.pending_op != 0) {
        apply_calc_pending(st, rhs);
        st.pending_op = 0;
      }
      st.fresh_entry = true;
      st.has_decimal = (std::strchr(st.entry, '.') != nullptr);
    } else {
      const char next_op = (hit_r == 0) ? '/' : (hit_r == 1) ? '*' : (hit_r == 2) ? '-' : '+';
      if (!st.fresh_entry && st.pending_op != 0) {
        apply_calc_pending(st, rhs);
      } else {
        st.acc = rhs;
      }
      st.pending_op = next_op;
      st.fresh_entry = true;
      st.has_decimal = false;
    }
    return;
  }

  if (hit_r == 4 && hit_c == 2) {
    if (st.fresh_entry) return;
    const size_t len = std::strlen(st.entry);
    if (len <= 1 || (len == 2 && st.entry[0] == '-')) {
      std::snprintf(st.entry, sizeof(st.entry), "0");
      st.fresh_entry = true;
      st.has_decimal = false;
    } else {
      if (st.entry[len - 1] == '.') st.has_decimal = false;
      st.entry[len - 1] = '\0';
    }
    return;
  }

  if (hit_r == 4 && hit_c == 1) {
    if (st.fresh_entry) {
      std::snprintf(st.entry, sizeof(st.entry), "0.");
      st.fresh_entry = false;
      st.has_decimal = true;
    } else if (!st.has_decimal && std::strlen(st.entry) < 11) {
      std::strcat(st.entry, ".");
      st.has_decimal = true;
    }
    return;
  }

  int digit = 0;
  if (hit_r == 1) digit = 7 + hit_c;
  else if (hit_r == 2) digit = 4 + hit_c;
  else if (hit_r == 3) digit = 1 + hit_c;
  else if (hit_r == 4 && hit_c == 0) digit = 0;

  if (st.fresh_entry || std::strcmp(st.entry, "0") == 0) {
    std::snprintf(st.entry, sizeof(st.entry), "%d", digit);
    st.fresh_entry = false;
    st.has_decimal = false;
  } else if (std::strlen(st.entry) < 11) {
    const size_t len = std::strlen(st.entry);
    st.entry[len] = static_cast<char>('0' + digit);
    st.entry[len + 1] = '\0';
  }
}

DiceRollState &dice_state() {
  static DiceRollState st;
  return st;
}

void trigger_dice_roll(uint32_t now_ms, uint32_t rand_a, uint32_t rand_b) {
  auto &ds = dice_state();
  ds.val_1 = static_cast<int>((rand_a % 6U) + 1U);
  ds.val_2 = static_cast<int>((rand_b % 6U) + 1U);
  ds.anim_active = true;
  ds.anim_start_ms = now_ms;
}

CoinFlipState &coin_state() {
  static CoinFlipState st;
  return st;
}

void trigger_coin_flip(uint32_t now_ms, uint32_t rand_val) {
  auto &cs = coin_state();
  cs.side = static_cast<int>(rand_val & 1U);
  cs.anim_active = true;
  cs.anim_start_ms = now_ms;
}

std::string &cal_detail_time() {
  static std::string s;
  return s;
}

std::string &cal_detail_title() {
  static std::string s;
  return s;
}

std::string &cal_detail_location() {
  static std::string s;
  return s;
}

std::string &cal_detail_map_url() {
  static std::string s;
  return s;
}

int &cal_map_zoom() {
  static int z = 14;
  return z;
}

int &cal_map_pan_x() {
  static int px = 0;
  return px;
}

int &cal_map_pan_y() {
  static int py = 0;
  return py;
}

std::string &cal_feedback_text() {
  static std::string s;
  return s;
}

uint32_t &cal_feedback_ms() {
  static uint32_t ms = 0;
  return ms;
}

void parse_calendar_slot_meta(const std::string &slot_name, const std::string &slot_id,
                              std::string &out_time, std::string &out_title,
                              std::string &out_loc, std::string &out_map_url) {
  out_time.clear();
  out_title = slot_name;
  out_loc.clear();
  out_map_url.clear();

  const size_t bullet = slot_name.find(" • ");
  if (bullet != std::string::npos) {
    out_time  = slot_name.substr(0, bullet);
    out_title = slot_name.substr(bullet + std::strlen(" • "));
  } else {
    const size_t pipe = slot_name.find('|');
    if (pipe != std::string::npos) {
      out_time  = slot_name.substr(0, pipe);
      out_title = slot_name.substr(pipe + 1);
    }
  }

  if (!slot_id.empty()) {
    const size_t p1 = slot_id.find('|');
    if (p1 == std::string::npos) {
      if (slot_id.rfind("http://", 0) == 0 || slot_id.rfind("https://", 0) == 0 ||
          slot_id.rfind("/api/", 0) == 0) {
        out_map_url = slot_id;
      } else {
        out_loc = slot_id;
      }
    } else {
      const size_t p2 = slot_id.find('|', p1 + 1);
      if (p2 == std::string::npos) {
        out_loc     = slot_id.substr(0, p1);
        out_map_url = slot_id.substr(p1 + 1);
      } else {
        out_loc     = slot_id.substr(p1 + 1, p2 - (p1 + 1));
        out_map_url = slot_id.substr(p2 + 1);
      }
    }
  }
}

void render_alert_ringing_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const bool pulse_on = ((st.now_ms / 400U) % 2U) == 0U;
  const Color ring_col = st.alarm_ringing
                             ? (pulse_on ? ctx.col_gold : ctx.col_orange)
                             : (st.find_my_active
                                    ? (pulse_on ? ctx.col_cyan : ctx.col_white)
                                    : (pulse_on ? ctx.col_cyan : ctx.col_gold));

  for (int dr = 0; dr < 4; dr++) {
    fast_circle(it, st.cx, st.cy, SCREEN_MIN_R - 4 - dr, ring_col);
  }
  if (pulse_on) {
    fast_circle(it, st.cx, st.cy, SCREEN_MIN_R - 13, scale_color(ring_col, 0.45f));
  }

  char hdr_buf[36];
  if (st.alarm_ringing) {
    if (st.alarm_ringing_idx >= 1 && st.alarm_ringing_idx <= 3) {
      std::snprintf(hdr_buf, sizeof(hdr_buf), "ALARM %d", st.alarm_ringing_idx);
    } else {
      std::snprintf(hdr_buf, sizeof(hdr_buf), "ALARM");
    }
  } else if (st.find_my_active) {
    std::snprintf(hdr_buf, sizeof(hdr_buf), "FIND MY DEVICE");
  } else if (!st.va_timer_name.empty()) {
    std::string up = st.va_timer_name;
    for (char &c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::snprintf(hdr_buf, sizeof(hdr_buf), "%s", truncate_text(up, 18).c_str());
  } else {
    std::snprintf(hdr_buf, sizeof(hdr_buf), "TIMER COMPLETE");
  }

  fast_print(it, st.cx, scale_y(96), ctx.font_btn_bold, ring_col, TextAlign::CENTER, hdr_buf);

  if (st.alarm_ringing && ctx.now.is_valid()) {
    char clk_buf[16];
    esphome::ESPTime now_copy = ctx.now;
    now_copy.strftime(clk_buf, sizeof(clk_buf), st.is_24h_format ? "%H:%M" : "%I:%M");
    const char *p_str = (!st.is_24h_format && clk_buf[0] == '0') ? (clk_buf + 1) : clk_buf;
    render_time_readout(it, st.cx, p_str, ctx.font_chrono_time, st.col_white);

    int rec = st.alarm1_recurrence;
    if (st.alarm_ringing_idx == 2) rec = st.alarm2_recurrence;
    else if (st.alarm_ringing_idx == 3) rec = st.alarm3_recurrence;
    const char *rec_lbl = (rec == 1) ? "Weekdays"
                          : (rec == 2) ? "Weekends"
                          : (rec == 3) ? "One-Time Alarm" : "Daily";
    fast_print(it, st.cx, scale_y(248), ctx.font_small, st.col_slate, TextAlign::CENTER, rec_lbl);
  } else if (st.find_my_active) {
    render_time_readout(it, st.cx, "HERE!", ctx.font_chrono_time, st.col_white);
    fast_print(it, st.cx, scale_y(248), ctx.font_small, ctx.col_cyan,
               TextAlign::CENTER, "Ringing at full alert volume");
  } else {
    render_time_readout(it, st.cx, "00:00", ctx.font_chrono_time, st.col_white);
    fast_print(it, st.cx, scale_y(248), ctx.font_small, ctx.col_gold,
               TextAlign::CENTER, "Time's up!");
  }

  if (st.alarm_ringing) {
    ui::pill_button(it, st.cx - scale_x(112), st.cx + scale_x(112), scale_y(312), 24,
                    ctx.col_gold, ui::chrome_card_bg_on(),
                    "SNOOZE (5 MIN)", ctx.col_gold, ctx.font_btn_bold,
                    WidgetId::ALERT_SNOOZE_BTN, kHitTap | kHitFocusSlot, 0);
    ui::pill_button(it, st.cx - scale_x(92), st.cx + scale_x(92), scale_y(374), 22,
                    ctx.col_red, ui::chrome_danger_bg(),
                    "DISMISS", st.col_white, ctx.font_btn_bold,
                    WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 1);
  } else {
    ui::pill_button(it, st.cx - scale_x(102), st.cx + scale_x(102), scale_y(342), 25,
                    ring_col, ui::chrome_card_bg_on(),
                    st.find_my_active ? "FOUND IT" : "DISMISS",
                    st.col_white, ctx.font_btn_bold,
                    WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 0);
  }
}

void render_tools_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const int sub = rs.nav.tools_subpage;

  if (sub == ToolsSubpage::INTERCOM || sub == ToolsSubpage::CAMERAS) {
    render_comms_page(it, ctx);
    return;
  }

  if (sub == ToolsSubpage::FLASHLIGHT) {
    if (!frame_base(it, ctx.col_white)) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, ctx.col_white);
    }
    return;
  }

  switch (sub) {
    case ToolsSubpage::TIMERS:
      render_timers_subpage(it, ctx);
      break;
    case ToolsSubpage::ALARMS:
      render_alarms_subpage(it, ctx, rs);
      break;
    case ToolsSubpage::STOPWATCH:
      render_stopwatch_subpage(it, ctx, rs.stopwatch);
      break;
    case ToolsSubpage::DICE:
      render_dice_subpage(it, ctx);
      break;
    case ToolsSubpage::CALCULATOR:
      render_calculator_subpage(it, ctx);
      break;
    case ToolsSubpage::LEVEL:
      render_level_subpage(it, ctx, rs);
      break;
    case ToolsSubpage::COIN_FLIP:
      render_coin_flip_subpage(it, ctx);
      break;
    case ToolsSubpage::CALENDAR:
      render_calendar_subpage(it, ctx, rs);
      break;
    case ToolsSubpage::LAUNCHER:
    default:
      render_tools_launcher(it, ctx, rs);
      break;
  }
}

}  // namespace latter_ui
