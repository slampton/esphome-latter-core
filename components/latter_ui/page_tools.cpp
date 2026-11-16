#include "esphome/core/defines.h"
#include "hal_battery.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {

namespace {

int wrap_text_lines(const std::string &text, int max_w, esphome::display::BaseFont *font,
                    std::string out_lines[], int max_lines) {
  const std::string clean = sanitize_utf8_for_font(text);
  int line_cnt = 0;
  size_t pos = 0;
  while (pos < clean.size() && line_cnt < max_lines) {
    while (pos < clean.size() && clean[pos] == ' ') pos++;
    if (pos >= clean.size()) break;
    if (line_cnt == max_lines - 1) {
      out_lines[line_cnt++] = fit_text_to_width_font(clean.substr(pos), max_w, font);
      break;
    }
    size_t best_end = pos;
    size_t scan = pos;
    while (scan < clean.size()) {
      const size_t nxt_space = clean.find(' ', scan);
      const size_t word_end = (nxt_space == std::string::npos) ? clean.size() : nxt_space;
      const std::string cand = clean.substr(pos, word_end - pos);
      int w = 0, xo = 0, b = 0, h = 0;
      font->measure(cand.c_str(), &w, &xo, &b, &h);
      if (w <= max_w) {
        best_end = word_end;
        scan = (nxt_space == std::string::npos) ? clean.size() : (nxt_space + 1);
      } else {
        break;
      }
    }
    if (best_end == pos) {
      out_lines[line_cnt++] = fit_text_to_width_font(clean.substr(pos), max_w, font);
      break;
    }
    out_lines[line_cnt++] = clean.substr(pos, best_end - pos);
    pos = best_end;
  }
  return line_cnt;
}

struct CalculatorState {
  double prev_val = 0.0;
  int op = 0;  // 0 = none, 1 = +, 2 = -, 3 = *, 4 = /
  char entry[20] = "0";
  char history[48] = "";
  bool new_entry = true;
  bool error = false;
};

CalculatorState &calc_state() {
  static CalculatorState st;
  return st;
}

void format_calc_num_buf(double v, char *buf, size_t buf_len) {
  if (std::isnan(v) || std::isinf(v) || std::fabs(v) > 999999999999.0) {
    std::snprintf(buf, buf_len, "Error");
    calc_state().error = true;
    return;
  }
  if (std::fabs(v) < 1e-9) v = 0.0;
  if (std::fabs(v - std::round(v)) < 1e-7 && std::fabs(v) < 1e12) {
    std::snprintf(buf, buf_len, "%lld", static_cast<long long>(std::round(v)));
    return;
  }
  std::snprintf(buf, buf_len, "%.8g", v);
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
  const int ix = cx_card;
  const int iy = cy_card - scale_y(13);
  const int thick = (SCREEN_MIN_DIM > 520) ? 3 : 2;
  auto S = [](int v466) constexpr -> int { return scale_s(v466); };
  const int ir = S(14);

#if LATTER_ROUND_DISPLAY
  if (app_id == ToolsSubpage::STOPWATCH) {
    ui::draw_icon_stopwatch(it, ix, iy, ir, col_white, true);
  } else
#endif
  if (app_id == ToolsSubpage::DICE) {
    // Dice Roll: Solid filled rounded cube with 5 crisp negative-space pips
    const int d_r = S(5);
    const int x0 = ix - S(13);
    const int y0 = iy - S(13);
    const int s = 2 * S(13) + 1;
    fill_round_rect_aa(it, x0 + 1, y0 + 1, s, s, static_cast<float>(d_r), kChromeShadow);
    fill_round_rect_aa(it, x0, y0, s, s, static_cast<float>(d_r), col_white);
    fast_filled_circle_aa(it, ix, iy, S(2), surf_bg);
    fast_filled_circle_aa(it, ix - S(7), iy - S(7), S(2), surf_bg);
    fast_filled_circle_aa(it, ix + S(7), iy - S(7), S(2), surf_bg);
    fast_filled_circle_aa(it, ix - S(7), iy + S(7), S(2), surf_bg);
    fast_filled_circle_aa(it, ix + S(7), iy + S(7), S(2), surf_bg);

  } else if (app_id == ToolsSubpage::FLASHLIGHT) {
    // Flashlight: Solid filled sculpted torch body & reflector head + 3 bold light beams
    fast_filled_triangle(it, ix - S(9), iy - S(7), ix + S(9), iy - S(7), ix - S(5), iy - S(1), col_white);
    fast_filled_triangle(it, ix + S(9), iy - S(7), ix - S(5), iy - S(1), ix + S(5), iy - S(1), col_white);
    fill_rect_row_major(it, ix - S(5), iy + S(1), 2 * S(5) + 1, S(14), col_white);
    fast_filled_circle_aa(it, ix, iy + S(14), S(5), col_white);
    fill_rect_row_major(it, ix - S(1), iy + S(5), 2 * S(1) + 1, S(5), surf_bg);
    for (int t = 0; t < thick; t++) {
      fast_line(it, ix + t, iy - S(10), ix + t, iy - S(16), col_white);
      fast_line(it, ix - S(6) + t, iy - S(10), ix - S(9) + t, iy - S(15), col_white);
      fast_line(it, ix + S(6) - t, iy - S(10), ix + S(9) - t, iy - S(15), col_white);
    }

  } else if (app_id == ToolsSubpage::CALCULATOR) {
    // Calculator: Solid rounded body with negative-space LCD readout & 3x2 keypad cutouts
    const int cw = 2 * S(12) + 1;
    const int ch = 2 * S(15) + 1;
    const int cx0 = ix - S(12);
    const int cy0 = iy - S(15);
    fill_round_rect_aa(it, cx0 + 1, cy0 + 1, cw, ch, static_cast<float>(S(4)), kChromeShadow);
    fill_round_rect_aa(it, cx0, cy0, cw, ch, static_cast<float>(S(4)), col_white);
    fill_rect_row_major(it, ix - S(8), iy - S(11), 2 * S(8) + 1, S(6), surf_bg);
    for (int r = 0; r < 2; r++) {
      const int ky = iy - S(1) + r * S(5);
      fill_rect_row_major(it, ix - S(8), ky, S(4), S(3), surf_bg);
      fill_rect_row_major(it, ix - S(2), ky, S(4), S(3), surf_bg);
      fill_rect_row_major(it, ix + S(4), ky, S(4), S(3), surf_bg);
    }
    fill_rect_row_major(it, ix - S(8), iy + S(9), S(4), std::max(2, S(3)), surf_bg);
    fill_rect_row_major(it, ix - S(2), iy + S(9), S(10), std::max(2, S(3)), surf_bg);

  } else if (app_id == ToolsSubpage::LEVEL) {
    // Bubble Level: Solid outer medallion + cardinal crosshairs + negative-space vial moat & solid center bubble
    fast_filled_circle_aa(it, ix + 1, iy + 1, S(13), kChromeShadow);
    fast_filled_circle_aa(it, ix, iy, S(13), col_white);
    fill_rect_row_major(it, ix - thick / 2, iy - S(16), thick, S(4), col_white);
    fill_rect_row_major(it, ix - thick / 2, iy + S(13), thick, S(4), col_white);
    fill_rect_row_major(it, ix - S(16), iy - thick / 2, S(4), thick, col_white);
    fill_rect_row_major(it, ix + S(13), iy - thick / 2, S(4), thick, col_white);
    fast_filled_circle_aa(it, ix, iy, S(7), surf_bg);
    fast_filled_circle_aa(it, ix, iy, S(4), col_white);

  } else if (app_id == ToolsSubpage::COIN_FLIP) {
    // Coin Flip: Solid medallion with negative-space inner ring & house cutout
    fast_filled_circle_aa(it, ix + 1, iy + 1, S(14), kChromeShadow);
    fast_filled_circle_aa(it, ix, iy, S(14), col_white);
    fast_circle_aa(it, ix, iy, static_cast<float>(S(10)), surf_bg, static_cast<float>(std::max(2, S(2))));
    fast_filled_triangle(it, ix - S(6), iy, ix + S(6), iy, ix, iy - S(6), surf_bg);
    fill_rect_row_major(it, ix - S(4), iy, 2 * S(4) + 1, S(6), surf_bg);

  } else if (app_id == ToolsSubpage::CALENDAR) {
    ui::draw_icon_calendar(it, ix, iy, ir, col_white, true);
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
  static constexpr ToolItemDef kItems[] = {
#if LATTER_ROUND_DISPLAY
      {ToolsSubpage::STOPWATCH, "Stopwatch"},
#endif
#if LATTER_HAS_IMU
      {ToolsSubpage::LEVEL, "Level"},
#endif
      {ToolsSubpage::FLASHLIGHT, "Flashlight"},
      {ToolsSubpage::CALCULATOR, "Calculator"},
      {ToolsSubpage::CALENDAR, "Calendar"},
      {ToolsSubpage::DICE, "Dice Roll"},
      {ToolsSubpage::COIN_FLIP, "Coin Flip"},
  };
  static constexpr int kTotalItems = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));
  static_assert(kTotalItems == tools_launcher_item_count(),
                "Tools launcher item table must match tools_launcher_item_count()");
  static constexpr int kTotalPages = tools_launcher_page_count(kTotalItems);

  const auto &ly = layout();
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
        if (item_idx >= kTotalItems) continue;
        const int app_id = kItems[item_idx].id;
        if (app_id == 0) continue;
        const int cx_card = col_cx[c];
        render_tool_launcher_card(it, cx_card, cy_card, app_id, kItems[item_idx].label, ctx);
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

  const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, kTotalPages - 1);
  if (rs.nav.menu_scroll_active && kTotalPages > 1) {
    const int full_span = scale_y(252);
    const int offset = menu_scroll_shift(full_span);
    const int out_dy = -rs.nav.menu_scroll_dir * offset;
    const int in_dy  = rs.nav.menu_scroll_dir * (full_span - offset);
    const int tgt_pg = std::clamp(rs.nav.menu_scroll_target_page, 0, kTotalPages - 1);
    draw_grid_for_page(cur_pg, out_dy, false);
    draw_grid_for_page(tgt_pg, in_dy, false);
  } else {
    const int bounce_y = edge_bounce().offset_px(
        PageId::TOOLS, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);
    draw_grid_for_page(cur_pg, bounce_y, true);
  }

  if (rs.nav.menu_scroll_active && kTotalPages > 1 && !backdrop_active()) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, scale_y(82), ctx.col_bg);
    fill_rect_row_major(it, 0, scale_y(388), SCREEN_W, SCREEN_H - scale_y(388), ctx.col_bg);
  }

  render_page_header(it, ctx.cx, "TOOLS", ctx.font_small, ctx.theme_primary);
  const int active_pip_pg = (rs.nav.menu_scroll_active && kTotalPages > 1)
                                ? std::clamp(rs.nav.menu_scroll_target_page, 0, kTotalPages - 1)
                                : cur_pg;
  render_vertical_page_pips(it, active_pip_pg > 0, (active_pip_pg + 1) < kTotalPages,
                            ctx.col_bg, ctx.col_white, kChromePipInactive);
}

#if LATTER_ROUND_DISPLAY
void render_stopwatch_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                              const StopwatchRuntimeState &sw) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const uint32_t total_ms = sw.running ? (ctx.now_ms - sw.start_time_ms) : sw.elapsed_ms;
  const int mins   = static_cast<int>((total_ms / 60000U) % 60U);
  const int secs   = static_cast<int>((total_ms / 1000U) % 60U);
  const int tenths = static_cast<int>((total_ms / 100U) % 10U);

  // Classic Round Analog Chronograph Dial (D-042: round displays only via Tools -> Stopwatch)
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const float cx_f = static_cast<float>(cx);
  const float cy_f = static_cast<float>(cy);

  fast_circle(it, cx, cy, scale_min(230), ctx.col_white);
  fast_circle(it, cx, cy, scale_min(228), ctx.theme_secondary);
  ui::render_analog_chapter_ring(it, cx, cy, scale_min(227),
                                 ctx.theme_secondary, ctx.col_white, ctx.col_slate, true);
  fast_circle(it, cx, cy, scale_min(196), ctx.col_dark_gray);

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

  // Split-Second (Rattrapante) Ghost Hand via sub-pixel SDF AA
  if (sw.lap_ms > 0) {
    const float lap_angle = (static_cast<float>(sw.lap_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
    float l_sin = 0.0f, l_cos = 1.0f;
    fast_sincos(lap_angle, l_sin, l_cos);
    ui::draw_aa_line(it, cx_f, cy_f,
                     cx_f + scale_min_f(154.0f) * l_sin,
                     cy_f - scale_min_f(154.0f) * l_cos,
                     2.0f, ctx.col_red);
    fast_circle(it,
                cx + static_cast<int>(scale_min_f(158.0f) * l_sin),
                cy - static_cast<int>(scale_min_f(158.0f) * l_cos),
                scale_min(4), ctx.col_red);
  }

  // Main Central Chronograph Sweep Hand via sub-pixel SDF AA
  const float chrono_angle = (static_cast<float>(total_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
  float s_sin = 0.0f, s_cos = 1.0f;
  fast_sincos(chrono_angle, s_sin, s_cos);

  ui::draw_aa_tapered_hand(it, cx_f, cy_f, chrono_angle,
                           scale_min_f(36.0f), scale_min_f(178.0f),
                           scale_min_f(2.4f), scale_min_f(0.9f),
                           ctx.col_white, ctx.theme_secondary, false, true);

  const int tail_x = cx - static_cast<int>(scale_min_f(36.0f) * s_sin);
  const int tail_y = cy + static_cast<int>(scale_min_f(36.0f) * s_cos);
  fast_filled_circle(it, tail_x, tail_y, scale_min(7), ctx.col_dark_gray);
  fast_circle(it, tail_x, tail_y, scale_min(7), ctx.theme_secondary);
  fast_filled_circle(it, tail_x, tail_y, scale_min(3), ctx.theme_secondary);

  ui::render_center_arbor_aa(it, cx, cy, ctx.theme_secondary, ctx.col_dark_gray, ctx.col_white);

  // Hit-table order: dial face start/pause -> LAP/RESET circles -> Back
  frame_hits().add_rect(scale_min(40), scale_min(69),
                        SCREEN_W - 2 * scale_min(40), scale_min(210),
                        WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_circle(btn_lap_x, btn_y, btn_r,
                          WidgetId::SW_LAP_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, 8);
  frame_hits().add_circle(btn_rst_x, btn_y, btn_r,
                          WidgetId::SW_RESET_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, 8);

  if (!tools_direct_home()) {
    render_glass_card(it, cx - scale_min(58), layout().back_y - scale_min(15), scale_min(116), scale_min(30),
                      scale_min(15), kChromeGlassTint, -1, GlassVariant::Clear, ctx.col_white);
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back", ctx.col_white);
  }
}
#endif

void render_dice_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &ds = dice_state();
  int cur_val_1 = ds.val_1;
  int cur_val_2 = ds.val_2;
  int hop_y1 = 0, hop_y2 = 0;
  int jostle_x1 = 0, jostle_x2 = 0;
  int shadow_extra = 0;
  float anim_p = 1.0f;

  if (ds.anim_active) {
    const uint32_t elapsed = ctx.now_ms - ds.anim_start_ms;
    static constexpr uint32_t kRollDurMs = 680U;
    if (elapsed >= kRollDurMs) {
      ds.anim_active = false;
    } else {
      anim_p = static_cast<float>(elapsed) / static_cast<float>(kRollDurMs);
      const float damp = 1.0f - anim_p;
      hop_y1 = -static_cast<int>(std::fabs(fast_sin(anim_p * PI_F * 2.2f)) * scale_min_f(34.0f) * damp);
      hop_y2 = -static_cast<int>(std::fabs(fast_sin(anim_p * PI_F * 2.2f + 0.7f)) * scale_min_f(32.0f) * damp);
      jostle_x1 = static_cast<int>(fast_sin(anim_p * PI_F * 6.0f) * scale_min_f(8.0f) * damp);
      jostle_x2 = -static_cast<int>(fast_cos(anim_p * PI_F * 6.0f) * scale_min_f(8.0f) * damp);
      shadow_extra = static_cast<int>(scale_min_f(6.0f) * damp);

      const uint32_t step = elapsed / 55U;
      cur_val_1 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step), 17) * 60.0f) % 6);
      cur_val_2 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step) + 31, 53) * 60.0f) % 6);
    }
  }

  render_page_header(it, ctx.cx, "DICE ROLL", ctx.font_small, ctx.theme_primary);

  const int dice_base_y = scale_y(196);
  const int die_off = scale_min(75);
  const int d1_cx = ctx.cx - die_off + jostle_x1;
  const int d2_cx = ctx.cx + die_off + jostle_x2;
  const int d1_cy = dice_base_y + hop_y1;
  const int d2_cy = dice_base_y + hop_y2;
  const int d_size = scale_min(132);
  const int cr = std::max(6, scale_min(16));

  // Opaque Back-Face Culled 3D Airborne Tumbling Cube Renderer
  auto draw_tumbling_cube_3d = [&](int center_x, int center_y, int hop_y, float rx, float ry, float rz) {
    static constexpr float kCubeV[8][3] = {
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, {1.0f, -1.0f,  1.0f}, {1.0f, 1.0f,  1.0f}, {-1.0f, 1.0f,  1.0f},
    };
    static constexpr uint8_t kCubeF[6][4] = {
        {0, 1, 2, 3},  // Face 0 (-Z): 1 pip
        {5, 4, 7, 6},  // Face 1 (+Z): 6 pips
        {4, 5, 1, 0},  // Face 2 (-Y): 2 pips
        {3, 2, 6, 7},  // Face 3 (+Y): 5 pips
        {4, 0, 3, 7},  // Face 4 (-X): 3 pips
        {1, 5, 6, 2},  // Face 5 (+X): 4 pips
    };

    const float half_s = scale_min_f(46.0f);
    const float fov    = scale_min_f(260.0f);
    const float z_dist = scale_min_f(280.0f);
    ProjectedPoint pts[8];
    Vec3 rv[8];
    for (int i = 0; i < 8; i++) {
      rv[i] = rotate_3d_xyz(
          {kCubeV[i][0] * half_s, kCubeV[i][1] * half_s, kCubeV[i][2] * half_s}, rx, ry, rz);
      pts[i] = project_3d(rv[i], center_x, center_y, fov, z_dist);
    }

    fast_filled_circle(it, center_x + scale_min(6), center_y + scale_min(18) - hop_y / 2,
                       scale_min(40), Color(16, 20, 28));

    const Color c_border(158, 150, 138);
    const Color c_pip(25, 28, 35);
    const int pip_r3d = std::max(2, scale_min(3));

    for (int f = 0; f < 6; f++) {
      const uint8_t i0 = kCubeF[f][0], i1 = kCubeF[f][1], i2 = kCubeF[f][2], i3 = kCubeF[f][3];
      const int cross = (pts[i1].x - pts[i0].x) * (pts[i2].y - pts[i0].y) -
                        (pts[i1].y - pts[i0].y) * (pts[i2].x - pts[i0].x);
      if (cross <= 0) continue;

      const float inv_s = 0.25f / std::max(1.0f, half_s);
      const float nx = (rv[i0].x + rv[i1].x + rv[i2].x + rv[i3].x) * inv_s;
      const float ny = (rv[i0].y + rv[i1].y + rv[i2].y + rv[i3].y) * inv_s;
      const float nz = (rv[i0].z + rv[i1].z + rv[i2].z + rv[i3].z) * inv_s;
      const float light = std::clamp(0.84f + (-nz * 0.12f - ny * 0.10f - nx * 0.06f), 0.74f, 1.0f);
      const Color face_col(static_cast<uint8_t>(248.0f * light),
                           static_cast<uint8_t>(244.0f * light),
                           static_cast<uint8_t>(236.0f * light));

      fast_filled_triangle(it, pts[i0].x, pts[i0].y, pts[i1].x, pts[i1].y, pts[i2].x, pts[i2].y, face_col);
      fast_filled_triangle(it, pts[i0].x, pts[i0].y, pts[i2].x, pts[i2].y, pts[i3].x, pts[i3].y, face_col);
      fast_line(it, pts[i0].x, pts[i0].y, pts[i1].x, pts[i1].y, c_border);
      fast_line(it, pts[i1].x, pts[i1].y, pts[i2].x, pts[i2].y, c_border);
      fast_line(it, pts[i2].x, pts[i2].y, pts[i3].x, pts[i3].y, c_border);
      fast_line(it, pts[i3].x, pts[i3].y, pts[i0].x, pts[i0].y, c_border);

      auto draw_face_pip = [&](float u, float v) {
        const float px = (1.0f - u) * (1.0f - v) * static_cast<float>(pts[i0].x) +
                         u * (1.0f - v) * static_cast<float>(pts[i1].x) +
                         u * v * static_cast<float>(pts[i2].x) +
                         (1.0f - u) * v * static_cast<float>(pts[i3].x);
        const float py = (1.0f - u) * (1.0f - v) * static_cast<float>(pts[i0].y) +
                         u * (1.0f - v) * static_cast<float>(pts[i1].y) +
                         u * v * static_cast<float>(pts[i2].y) +
                         (1.0f - u) * v * static_cast<float>(pts[i3].y);
        fast_filled_circle(it, static_cast<int>(px), static_cast<int>(py), pip_r3d, c_pip);
      };

      if (f == 0 || f == 4 || f == 3) {
        draw_face_pip(0.50f, 0.50f);
      }
      if (f >= 1) {
        draw_face_pip(0.26f, 0.26f);
        draw_face_pip(0.74f, 0.74f);
      }
      if (f == 1 || f == 3 || f == 5) {
        draw_face_pip(0.74f, 0.26f);
        draw_face_pip(0.26f, 0.74f);
      }
      if (f == 1) {
        draw_face_pip(0.26f, 0.50f);
        draw_face_pip(0.74f, 0.50f);
      }
    }
  };

  auto draw_rounded_d6 = [&](int center_x, int center_y, int val) {
    const int x0 = center_x - d_size / 2;
    const int y0 = center_y - d_size / 2;
    const int sx0 = x0 + scale_min(6) + shadow_extra;
    const int sy0 = y0 + scale_min(8) + shadow_extra;

    const Color sh_col(16, 20, 28);
    fill_rect_row_major(it, sx0 + cr, sy0, d_size - 2 * cr, d_size, sh_col);
    fill_rect_row_major(it, sx0, sy0 + cr, d_size, d_size - 2 * cr, sh_col);
    fast_filled_circle(it, sx0 + cr, sy0 + cr, cr, sh_col);
    fast_filled_circle(it, sx0 + d_size - cr, sy0 + cr, cr, sh_col);
    fast_filled_circle(it, sx0 + cr, sy0 + d_size - cr, cr, sh_col);
    fast_filled_circle(it, sx0 + d_size - cr, sy0 + d_size - cr, cr, sh_col);

    const Color body_col(248, 246, 240);
    const Color border_col(182, 176, 164);
    fill_rect_row_major(it, x0 + cr, y0, d_size - 2 * cr, d_size, body_col);
    fill_rect_row_major(it, x0, y0 + cr, d_size, d_size - 2 * cr, body_col);
    fast_filled_circle(it, x0 + cr, y0 + cr, cr, body_col);
    fast_filled_circle(it, x0 + d_size - cr, y0 + cr, cr, body_col);
    fast_filled_circle(it, x0 + cr, y0 + d_size - cr, cr, body_col);
    fast_filled_circle(it, x0 + d_size - cr, y0 + d_size - cr, cr, body_col);

    fast_line(it, x0 + cr, y0 + 1, x0 + d_size - cr, y0 + 1, ctx.col_white);
    fast_line(it, x0 + 1, y0 + cr, x0 + 1, y0 + d_size - cr, ctx.col_white);
    fast_line(it, x0 + cr, y0 + d_size - 1, x0 + d_size - cr, y0 + d_size - 1, border_col);
    fast_line(it, x0 + d_size - 1, y0 + cr, x0 + d_size - 1, y0 + d_size - cr, border_col);

    const int pr = std::max(5, scale_min(9));
    const int p_dist = scale_min(34);
    const Color pip_c(25, 28, 35);
    const Color pip_rim(85, 92, 105);
    auto draw_pip = [&](int px, int py) {
      fast_filled_circle(it, px, py, pr, pip_c);
      fast_pixel(it, px + pr / 2, py + pr / 2 + 1, pip_rim);
      fast_pixel(it, px + pr / 2 + 1, py + pr / 2, pip_rim);
    };

    if (val == 1 || val == 3 || val == 5) draw_pip(center_x, center_y);
    if (val >= 2) {
      draw_pip(center_x - p_dist, center_y - p_dist);
      draw_pip(center_x + p_dist, center_y + p_dist);
    }
    if (val >= 4) {
      draw_pip(center_x + p_dist, center_y - p_dist);
      draw_pip(center_x - p_dist, center_y + p_dist);
    }
    if (val == 6) {
      draw_pip(center_x - p_dist, center_y);
      draw_pip(center_x + p_dist, center_y);
    }
  };

  if (ds.anim_active && anim_p < 0.80f) {
    const float spin = anim_p * 7.8f;
    draw_tumbling_cube_3d(d1_cx, d1_cy, hop_y1, spin * 1.1f, spin * 1.4f + 0.5f, spin * 0.7f);
    draw_tumbling_cube_3d(d2_cx, d2_cy, hop_y2, -spin * 1.2f + 0.8f, spin * 1.0f, -spin * 0.9f);
  } else {
    draw_rounded_d6(d1_cx, d1_cy, ds.val_1);
    draw_rounded_d6(d2_cx, d2_cy, ds.val_2);
  }

  if (ds.anim_active) {
    if (backdrop_active()) {
      fast_print(it, ctx.cx + 1, scale_y(324) + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, "ROLLING...");
    }
    fast_print(it, ctx.cx, scale_y(324), ctx.font_date, ctx.theme_secondary, TextAlign::CENTER, "ROLLING...");
  } else {
    char total_buf[32];
    std::snprintf(total_buf, sizeof(total_buf), "TOTAL: %d", cur_val_1 + cur_val_2);
    if (backdrop_active()) {
      fast_print(it, ctx.cx + 1, scale_y(324) + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, total_buf);
    }
    fast_print(it, ctx.cx, scale_y(324), ctx.font_date, ctx.col_white, TextAlign::CENTER, total_buf);
  }

  frame_hits().add_rect(0, 0, SCREEN_W, layout().back_y - scale_y(24),
                        WidgetId::DICE_ROLL_BTN, kHitTap | kHitFocusSlot);
  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
}

void render_calculator_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &st = calc_state();
  const bool compact_w = (SCREEN_W < 380);
  const int hist_right_x  = std::min(SCREEN_W - 18, ctx.cx + scale_x(103));
  const int entry_right_x = std::min(SCREEN_W - 18, ctx.cx + scale_x(129));
  const int div_half_w    = std::min(SCREEN_W / 2 - 14, scale_x(151));

  // Plain bare history + entry readout directly on background (no non-interactive glass box)
  if (st.history[0] != '\0') {
    if (backdrop_active()) {
      fast_print(it, hist_right_x + 1, scale_y(42) + 1, ctx.font_small, kChromeShadow,
                 TextAlign::CENTER_RIGHT, st.history);
    }
    fast_print(it, hist_right_x, scale_y(42), ctx.font_small, ctx.col_slate,
               TextAlign::CENTER_RIGHT, st.history);
  }
  if (st.entry[0] != '\0') {
    auto *entry_font = (std::strlen(st.entry) > 10) ? ctx.font_btn_bold : ctx.font_date;
    const Color entry_col = st.error ? ctx.col_red : ctx.col_white;
    if (backdrop_active()) {
      fast_print(it, entry_right_x + 1, scale_y(74) + 1, entry_font, kChromeShadow,
                 TextAlign::CENTER_RIGHT, st.entry);
    }
    fast_print(it, entry_right_x, scale_y(74), entry_font, entry_col,
               TextAlign::CENTER_RIGHT, st.entry);
  }
  fast_line(it, ctx.cx - div_half_w, scale_y(98), ctx.cx + div_half_w, scale_y(98),
            kChromeCardBorder);

  static constexpr int kBaseRowsY[5] = {131, 187, 243, 299, 355};
  static constexpr const char *kKeyLabels[5][4] = {
      {"C", "±", "%", "÷"},
      {"7", "8", "9", "×"},
      {"4", "5", "6", "-"},
      {"1", "2", "3", "+"},
      {"0", ".", "DEL", "="},
  };

  const int raw_key_w = compact_w ? 62 : 68;
  const int eff_key_w = (SCREEN_W > 520) ? scale_x(raw_key_w) : raw_key_w;
  const int eff_key_h = (SCREEN_W > 520) ? scale_y(42) : 42;

  for (int r = 0; r < 5; r++) {
    const int ky_mid = scale_y(kBaseRowsY[r]);
    for (int c = 0; c < 4; c++) {
      const int kx_mid = layout().calc_col_x(c);

      Color bg_c     = kChromeGlassTint;
      Color border_c = kChromeCardBorder;
      Color text_c   = ctx.col_white;

      if (c == 3) {
        const bool op_selected = st.new_entry &&
                                 ((r == 0 && st.op == 4) || (r == 1 && st.op == 3) ||
                                  (r == 2 && st.op == 2) || (r == 3 && st.op == 1));
        if (op_selected) {
          bg_c     = ctx.col_orange;
          border_c = ctx.col_orange;
        } else {
          border_c = (r == 4) ? ctx.theme_primary : scale_color(ctx.col_orange, 0.72f);
        }
      } else if (r == 0 || (r == 4 && c == 2)) {
        border_c = kChromeCardBrdOn;
      }

      const bool is_minus_key = (r == 2 && c == 3);
      const bool is_del_key   = (r == 4 && c == 2);
      render_modern_card(it, kx_mid, ky_mid, raw_key_w, 42, 9, bg_c, border_c,
                         is_minus_key ? nullptr : kKeyLabels[r][c],
                         text_c, is_del_key ? ctx.font_small : ctx.font_btn_bold);
      if (is_minus_key) {
        const int bar_w = std::max(10, scale_s(14));
        const int bar_h = std::max(2, scale_s(3));
        fill_rect_row_major(it, kx_mid - bar_w / 2 + 1, ky_mid - bar_h / 2 + 1, bar_w, bar_h, kChromeShadow);
        fill_rect_row_major(it, kx_mid - bar_w / 2, ky_mid - bar_h / 2, bar_w, bar_h, text_c);
      }

      const uint16_t key_idx = static_cast<uint16_t>(r * 4 + c);
      frame_hits().add_centered_rect(kx_mid, ky_mid, eff_key_w + 4, eff_key_h + 6,
                                     WidgetId::CALC_KEY_BASE + key_idx,
                                     kHitTap | kHitFocusSlot,
                                     static_cast<int16_t>(key_idx));
    }
  }

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
}

void render_level_subpage(esphome::display::Display &it, const UiFrameContext &ctx,
                          const DeviceRuntimeState &rs) {
  using esphome::Color;
  using esphome::display::TextAlign;

  render_page_header(it, ctx.cx, "LEVEL", ctx.font_small, ctx.theme_primary);

  const float roll_deg = rs.hw_imu_roll;
  const float pitch_deg = rs.hw_imu_pitch;
  if (std::isnan(roll_deg) || std::isnan(pitch_deg) || std::isnan(rs.hw_accel_z)) {
    render_empty_state(it, ctx.cx, ctx.cy, "Waiting for the motion sensor...", ctx.font_small, ctx.col_slate);
    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  // Symmetric 2-axis planar tilt angles aligned to hardware-verified IMU axes (see page_games.cpp):
  //   +raw_x_deg => right edge raised (air bubble floats RIGHT, +x)
  //   +raw_y_deg => top/far edge raised (air bubble floats UP, -y)
  const float r_rad = roll_deg * (PI_F / 180.0f);
  const float p_rad = pitch_deg * (PI_F / 180.0f);
  const float sin_x = std::clamp(std::sin(r_rad) * std::cos(p_rad), -1.0f, 1.0f);
  const float raw_x_deg = -std::asin(sin_x) * (180.0f / PI_F);
  const float raw_y_deg = -pitch_deg;

  // Frame-rate-independent liquid EMA damping (snaps immediately when re-opening Level)
  static float smooth_x_deg = 0.0f;
  static float smooth_y_deg = 0.0f;
  static uint32_t last_level_ms = 0;
  static bool init_level = false;
  const uint32_t dt_ms = (ctx.now_ms >= last_level_ms) ? (ctx.now_ms - last_level_ms) : 401U;
  last_level_ms = ctx.now_ms;
  if (!init_level || dt_ms > 400U) {
    smooth_x_deg = raw_x_deg;
    smooth_y_deg = raw_y_deg;
    init_level = true;
  } else {
    const float alpha = std::clamp(static_cast<float>(dt_ms) / 115.0f, 0.20f, 0.65f);
    smooth_x_deg += (raw_x_deg - smooth_x_deg) * alpha;
    smooth_y_deg += (raw_y_deg - smooth_y_deg) * alpha;
  }

  const float total_tilt = std::sqrt(smooth_x_deg * smooth_x_deg + smooth_y_deg * smooth_y_deg);
  const bool is_level = (total_tilt <= 0.6f);
  const Color ring_c = is_level ? ctx.col_green : ctx.col_slate;

  // 1. Signed Two-Axis Tilt Readout directly on background below "LEVEL" header
  auto fmt_axis = [](char *dst, size_t cap, char axis, float deg) {
    if (std::fabs(deg) < 0.05f) {
      std::snprintf(dst, cap, "%c 0.0°", axis);
    } else {
      std::snprintf(dst, cap, "%c %+.1f°", axis, deg);
    }
  };
  char x_buf[20], y_buf[20], deg_buf[48];
  fmt_axis(x_buf, sizeof(x_buf), 'X', smooth_x_deg);
  fmt_axis(y_buf, sizeof(y_buf), 'Y', smooth_y_deg);
  std::snprintf(deg_buf, sizeof(deg_buf), "%s   %s", x_buf, y_buf);
  if (backdrop_active()) {
    fast_print(it, ctx.cx + 1, scale_y(82) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, deg_buf);
  }
  fast_print(it, ctx.cx, scale_y(82), ctx.font_small,
             is_level ? ctx.col_green : ctx.col_white, TextAlign::CENTER, deg_buf);

  // 2. Resolution-Adaptive Bulls-Eye Spirit Vial (~15° full-scale span; <= 0.6° centers inside inner ring)
  const int level_cy  = ctx.cy + scale_y(16);
  const int outer_r   = scale_min(136);
  const int mid_r     = scale_min(70);
  const int inner_r   = scale_min(27);
  const int cross_out = outer_r + scale_min(5);
  const int cross_in  = inner_r + scale_min(6);
  const float max_bub = static_cast<float>(outer_r - scale_min(22));
  const int bub_r     = std::max(12, scale_min(20));

  for (int i = 0; i < 60; i += 5) {
    const int x1 = ctx.cx + static_cast<int>(DIAL_SIN[i] * static_cast<float>(outer_r - scale_min(8)));
    const int y1 = level_cy - static_cast<int>(DIAL_COS[i] * static_cast<float>(outer_r - scale_min(8)));
    const int x2 = ctx.cx + static_cast<int>(DIAL_SIN[i] * static_cast<float>(outer_r));
    const int y2 = level_cy - static_cast<int>(DIAL_COS[i] * static_cast<float>(outer_r));
    fast_line(it, x1, y1, x2, y2, ctx.col_gray);
  }

  fast_circle(it, ctx.cx, level_cy, outer_r, ctx.col_slate);
  fast_circle(it, ctx.cx, level_cy, outer_r - 1, ctx.col_dark_gray);
  fast_circle(it, ctx.cx, level_cy, mid_r, ctx.col_gray);
  fast_circle(it, ctx.cx, level_cy, inner_r, ring_c);
  fast_circle(it, ctx.cx, level_cy, inner_r - 1, ring_c);

  fast_line(it, ctx.cx - cross_out, level_cy, ctx.cx - cross_in, level_cy, ring_c);
  fast_line(it, ctx.cx + cross_in, level_cy, ctx.cx + cross_out, level_cy, ring_c);
  fast_line(it, ctx.cx, level_cy - cross_out, ctx.cx, level_cy - cross_in, ring_c);
  fast_line(it, ctx.cx, level_cy + cross_in, ctx.cx, level_cy + cross_out, ring_c);
  fast_filled_circle(it, ctx.cx, level_cy, 2, ring_c);

  const float deg_px = scale_min_f(7.6f);
  float fbx = static_cast<float>(ctx.cx) + smooth_x_deg * deg_px;
  float fby = static_cast<float>(level_cy) - smooth_y_deg * deg_px;
  const float bdx = fbx - static_cast<float>(ctx.cx);
  const float bdy = fby - static_cast<float>(level_cy);
  const float b_dist_sq = bdx * bdx + bdy * bdy;
  if (b_dist_sq > max_bub * max_bub) {
    const float b_dist = std::sqrt(b_dist_sq);
    fbx = static_cast<float>(ctx.cx) + (bdx / b_dist) * max_bub;
    fby = static_cast<float>(level_cy) + (bdy / b_dist) * max_bub;
  }
  const int bx = static_cast<int>(std::lround(fbx));
  const int by = static_cast<int>(std::lround(fby));

  // 3D Glass Spirit Bubble
  fast_filled_circle(it, bx + 2, by + 3, bub_r, Color(12, 16, 22));
  const Color bub_fill = is_level ? ctx.col_green : ctx.theme_secondary;
  fast_filled_circle(it, bx, by, bub_r, bub_fill);
  fast_circle(it, bx, by, bub_r, ctx.col_white);
  fast_circle(it, bx, by, bub_r - 2, ctx.col_white);
  fast_filled_circle(it, bx - bub_r / 3, by - bub_r / 3, std::max(2, scale_min(4)), ctx.col_white);
  fast_filled_circle(it, bx + bub_r / 3, by + bub_r / 3, std::max(1, scale_min(2)), ctx.col_white);

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
}

void render_coin_flip_subpage(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &cs = coin_state();
  float coin_sy = 1.0f;
  float y_hop = 0.0f;
  float z_zoom = 1.0f;
  float raw_sin = 0.25f;
  bool show_heads = (cs.side == 0);
  bool is_edge_flash = false;

  if (cs.anim_active) {
    static constexpr uint32_t kTossDurationMs = 820U;
    const uint32_t el_ms = (ctx.now_ms >= cs.anim_start_ms)
                               ? (ctx.now_ms - cs.anim_start_ms)
                               : kTossDurationMs;
    if (el_ms >= kTossDurationMs) {
      cs.anim_active = false;
      coin_sy = 1.0f;
      y_hop = 0.0f;
      z_zoom = 1.0f;
      raw_sin = 0.25f;
      show_heads = (cs.side == 0);
    } else {
      const float p = static_cast<float>(el_ms) / static_cast<float>(kTossDurationMs);
      // Newtonian ballistic gravity arc: fast launch impulse, apex hang, accelerating gravity drop
      const float arc = 4.0f * p * (1.0f - p);
      y_hop = -scale_min_f(74.0f) * arc;
      z_zoom = 1.0f + 0.18f * arc;

      // Continuous 3D end-over-end somersault (5 half-turns = 2.5 full revolutions -> lands on cs.side at cos(5π) = -1)
      const float p_spin = p * (1.18f - 0.18f * p);
      const float flip_angle = p_spin * 5.0f * PI_F;
      float raw_cos = 1.0f;
      fast_sincos(flip_angle, raw_sin, raw_cos);

      coin_sy = std::max(0.05f, std::fabs(raw_cos));
      is_edge_flash = (coin_sy < 0.16f);
      const int cur_side = (raw_cos < 0.0f) ? cs.side : (1 - cs.side);
      show_heads = (cur_side == 0);
    }
  }

  render_page_header(it, ctx.cx, "COIN FLIP", ctx.font_small, ctx.theme_primary);

  const int cx = ctx.cx;
  const int coin_base_y = scale_y(220);
  const int coin_cy = coin_base_y + static_cast<int>(y_hop);
  const float s_zoom = scale_min_f(z_zoom);
  const int r_outer = std::max(16, static_cast<int>(108.0f * s_zoom));

  // 1. Dynamic 3D Floor Shadow (shrinks as coin rises toward viewer)
  const int sh_base_y = scale_y(316);
  const float shadow_scale = std::max(0.32f, 1.0f - (-y_hop / std::max(1.0f, scale_min_f(95.0f))));
  const int sh_rx = static_cast<int>(scale_min_f(88.0f) * shadow_scale);
  const int sh_ry = std::max(3, static_cast<int>(scale_min_f(16.0f + 10.0f * coin_sy) * shadow_scale));
  for (int sdy = -sh_ry; sdy <= sh_ry; sdy++) {
    const int lut_idx = std::clamp(233 + (sdy * 232) / sh_ry, 0, 465);
    const int sdx = (sh_rx * CIRCLE_HW[lut_idx]) / 233;
    fast_hline(it, cx - sdx, sh_base_y + sdy, 2 * sdx + 1, Color(12, 16, 22));
  }

  // All-Silver Struck Medallion Palette
  const Color col_rim_dark(64, 72, 84);
  const Color col_rim_mid(134, 144, 160);
  const Color col_bevel_hi(248, 252, 255);
  const Color col_groove_sh(82, 92, 108);
  const Color col_relief_shadow(52, 60, 72);
  const Color col_relief_hi(252, 254, 255);
  const Color col_engrave_recess(74, 84, 98);

  const float spec_center = cs.anim_active ? (0.68f * raw_sin) : -0.28f;
  const int b_outer = std::max(4, static_cast<int>(static_cast<float>(r_outer) * coin_sy));
  const float edge_signed = cs.anim_active ? (16.0f * raw_sin) : 4.0f;
  const bool edge_on_top = (edge_signed < 0.0f);
  const int edge_h = cs.anim_active
                         ? scale_min(static_cast<int>(std::lround(std::fabs(edge_signed))))
                         : std::max(3, scale_min(4));

  // 2. Orthographic 3D Cylindrical Milled Silver Edge Band (shifts bottom <-> top across full 360° somersault)
  if (edge_h > 0) {
    for (int dy = 1; dy <= b_outer + edge_h; dy++) {
      const int src_dy = (dy > edge_h) ? (dy - edge_h) : 0;
      const int lut_idx = std::clamp(233 + (src_dy * 232) / b_outer, 0, 465);
      const int dx_outer = (r_outer * CIRCLE_HW[lut_idx]) / 233;
      const Color ec = (dy >= b_outer + edge_h - 1)
                           ? col_rim_dark
                           : (is_edge_flash ? Color(202, 212, 228) : col_rim_mid);
      const int py = edge_on_top ? (coin_cy - dy) : (coin_cy + dy);
      fast_hline(it, cx - dx_outer, py, 2 * dx_outer + 1, ec);
    }
  }

  // 3. 3D Brushed-Silver Coin Field
  const int dish_in = std::max(5, scale_min(8));
  for (int dy = -b_outer; dy <= b_outer; dy++) {
    const int lut_idx = std::clamp(233 + (dy * 232) / b_outer, 0, 465);
    const int dx_outer = (r_outer * CIRCLE_HW[lut_idx]) / 233;

    const float u_y = static_cast<float>(dy) / static_cast<float>(b_outer);
    const float d_spec = std::fabs(u_y - spec_center);
    const float spec_boost = std::max(0.0f, 1.0f - d_spec * 2.1f);
    const float sheen = (is_edge_flash ? 215.0f : 156.0f) - u_y * 18.0f + spec_boost * 42.0f;
    const uint8_t sr = static_cast<uint8_t>(std::min(248.0f, sheen * 0.96f));
    const uint8_t sg = static_cast<uint8_t>(std::min(250.0f, sheen * 0.99f));
    const uint8_t sb = static_cast<uint8_t>(std::min(255.0f, sheen * 1.04f));
    fast_hline(it, cx - dx_outer, coin_cy + dy, 2 * dx_outer + 1, Color(sr, sg, sb));

    fast_pixel(it, cx - dx_outer, coin_cy + dy, col_rim_dark);
    fast_pixel(it, cx + dx_outer, coin_cy + dy, col_rim_dark);
    if (dx_outer > 4) {
      fast_pixel(it, cx - dx_outer + 1, coin_cy + dy, col_bevel_hi);
      fast_pixel(it, cx - dx_outer + 2, coin_cy + dy, col_bevel_hi);
      fast_pixel(it, cx + dx_outer - 1, coin_cy + dy, col_rim_mid);
      fast_pixel(it, cx + dx_outer - 2, coin_cy + dy, col_rim_mid);
    }
    if (dx_outer > dish_in + 3 && !is_edge_flash) {
      const Color dish_l = (dy < 0) ? col_groove_sh : col_bevel_hi;
      const Color dish_r = (dy < 0) ? col_bevel_hi : col_groove_sh;
      fast_pixel(it, cx - dx_outer + dish_in, coin_cy + dy, dish_l);
      fast_pixel(it, cx - dx_outer + dish_in + 1, coin_cy + dy, dish_l);
      fast_pixel(it, cx + dx_outer - dish_in, coin_cy + dy, dish_r);
      fast_pixel(it, cx + dx_outer - dish_in - 1, coin_cy + dy, dish_r);
    }
  }

  // Vertical milled reeding ticks along the visible 3D coin thickness band
  if (edge_h >= 2) {
    const int reed_step = std::max(4, scale_min(is_edge_flash ? 5 : 6));
    for (int rx_off = -r_outer + 6; rx_off <= r_outer - 6; rx_off += reed_step) {
      const int lut_x = std::clamp(233 + (rx_off * 232) / r_outer, 0, 465);
      const int y_off = (b_outer * CIRCLE_HW[lut_x]) / 233;
      const Color reed_c = ((rx_off / reed_step) & 1) ? col_bevel_hi : col_rim_dark;
      if (edge_on_top) {
        const int y_rim = coin_cy - y_off;
        fast_line(it, cx + rx_off, y_rim - edge_h + 1, cx + rx_off, y_rim - 1, reed_c);
      } else {
        const int y_rim = coin_cy + y_off;
        fast_line(it, cx + rx_off, y_rim + 1, cx + rx_off, y_rim + edge_h - 1, reed_c);
      }
    }
  }

  if (coin_sy >= 0.96f && !cs.anim_active) {
    fast_circle(it, cx, coin_cy, r_outer, col_rim_dark);
    fast_circle(it, cx, coin_cy, r_outer - 1, col_bevel_hi);
    fast_circle(it, cx, coin_cy, r_outer - dish_in, col_groove_sh);
    fast_circle(it, cx, coin_cy, r_outer - dish_in - 1, col_bevel_hi);
  }

  // 4. All-Silver 3D Struck Bas-Relief Home Assistant Emblem & Quarter Lettering
  if (coin_sy > 0.22f) {
    const int hx = cx;
    const int hy = coin_cy - static_cast<int>(18.0f * coin_sy * s_zoom);
    const int hw = static_cast<int>(62.0f * s_zoom);
    const int y_top  = hy - static_cast<int>(56.0f * coin_sy * s_zoom);
    const int y_eave = hy - static_cast<int>(2.0f * coin_sy * s_zoom);
    const int y_bot  = hy + static_cast<int>(54.0f * coin_sy * s_zoom);

    for (int y = y_top; y <= y_bot; y++) {
      int cur_hw = 0;
      if (y < y_eave) {
        if (y_eave > y_top) {
          cur_hw = static_cast<int>(
              static_cast<float>(y - y_top) / static_cast<float>(y_eave - y_top) * static_cast<float>(hw));
        }
      } else {
        const int dy_b = y_bot - y;
        cur_hw = (dy_b < 5 && coin_sy > 0.75f) ? (hw - (5 - dy_b)) : hw;
      }
      if (cur_hw > 0) {
        fast_hline(it, hx - cur_hw + 2, y + 2, 2 * cur_hw + 2, col_relief_shadow);
        fast_hline(it, hx - cur_hw - 2, y - 1, 2 * cur_hw + 1, col_relief_hi);
      }
    }

    for (int y = y_top; y <= y_bot; y++) {
      int cur_hw = 0;
      if (y < y_eave) {
        if (y_eave > y_top) {
          cur_hw = static_cast<int>(
              static_cast<float>(y - y_top) / static_cast<float>(y_eave - y_top) * static_cast<float>(hw));
        }
      } else {
        const int dy_b = y_bot - y;
        cur_hw = (dy_b < 5 && coin_sy > 0.75f) ? (hw - (5 - dy_b)) : hw;
      }
      if (cur_hw > 0) {
        const float rel_u = static_cast<float>(y - y_top) / static_cast<float>(y_bot - y_top + 1);
        const uint8_t rl = static_cast<uint8_t>(232.0f - rel_u * 26.0f);
        fast_hline(it, hx - cur_hw, y, 2 * cur_hw + 1,
                   Color(rl, static_cast<uint8_t>(rl + 2), static_cast<uint8_t>(std::min(255.0f, rl + 7.0f))));
        fast_pixel(it, hx - cur_hw, y, col_groove_sh);
        fast_pixel(it, hx + cur_hw, y, col_relief_shadow);
      }
    }

    auto *coin_lbl_font = (SCREEN_MIN_DIM < 380) ? ctx.font_small : ctx.font_date;

    if (show_heads) {
      const int eye_dx = static_cast<int>(26.0f * s_zoom);
      const int eye_y  = hy + static_cast<int>(7.0f * coin_sy * s_zoom);
      const int eye_r  = std::max(2, static_cast<int>(8.0f * coin_sy * s_zoom));
      fast_filled_circle(it, hx - eye_dx + 1, eye_y + 1, eye_r, col_relief_hi);
      fast_filled_circle(it, hx + eye_dx + 1, eye_y + 1, eye_r, col_relief_hi);
      fast_filled_circle(it, hx - eye_dx, eye_y, eye_r, col_relief_shadow);
      fast_filled_circle(it, hx + eye_dx, eye_y, eye_r, col_relief_shadow);
      fast_filled_circle(it, hx - eye_dx + 1, eye_y + 1, std::max(1, eye_r - 2), col_engrave_recess);
      fast_filled_circle(it, hx + eye_dx + 1, eye_y + 1, std::max(1, eye_r - 2), col_engrave_recess);

      const int smile_top_y = hy + static_cast<int>(22.0f * coin_sy * s_zoom);
      const int smile_mid_y = hy + static_cast<int>(28.0f * coin_sy * s_zoom);
      const int smile_bot_y = hy + static_cast<int>(40.0f * coin_sy * s_zoom);
      const int smile_hw    = static_cast<int>(20.0f * s_zoom);

      for (int y = smile_top_y; y <= smile_mid_y; y++) {
        for (int t = -2; t <= 1; t++) {
          fast_pixel(it, hx - smile_hw + t + 1, y + 1, col_relief_hi);
          fast_pixel(it, hx + smile_hw + t + 1, y + 1, col_relief_hi);
          fast_pixel(it, hx - smile_hw + t, y, col_relief_shadow);
          fast_pixel(it, hx + smile_hw + t, y, col_relief_shadow);
        }
      }
      for (int i = 0; i <= 32; i++) {
        const float sa = static_cast<float>(i) * (PI_F / 32.0f);
        const int sx = hx - static_cast<int>(static_cast<float>(smile_hw) * fast_cos(sa));
        const int sy = smile_mid_y + static_cast<int>(static_cast<float>(smile_bot_y - smile_mid_y) * fast_sin(sa));
        for (int t = -1; t <= 1; t++) {
          fast_pixel(it, sx + 1, sy + t + 1, col_relief_hi);
          fast_pixel(it, sx, sy + t, col_relief_shadow);
          fast_pixel(it, sx + t, sy, col_relief_shadow);
        }
      }

      if (coin_sy > 0.76f) {
        const int ty = coin_cy + static_cast<int>(74.0f * coin_sy * s_zoom);
        if (!cs.anim_active) {
          fast_print(it, cx + 2, ty + 2, coin_lbl_font, col_relief_shadow, TextAlign::CENTER, "HEADS");
          fast_print(it, cx - 1, ty - 1, coin_lbl_font, col_relief_hi, TextAlign::CENTER, "HEADS");
        }
        fast_print(it, cx, ty, coin_lbl_font, Color(220, 228, 240), TextAlign::CENTER, "HEADS");
      }
    } else {
      const int h_total      = y_bot - y_top;
      const int branch_x     = static_cast<int>(31.0f * s_zoom);
      const int top_node_y   = y_top + static_cast<int>(0.32f * static_cast<float>(h_total));
      const int right_node_x = hx + branch_x;
      const int right_node_y = y_top + static_cast<int>(0.59f * static_cast<float>(h_total));
      const int left_node_x  = hx - branch_x;
      const int left_node_y  = y_top + static_cast<int>(0.73f * static_cast<float>(h_total));

      const int node_r      = std::max(3, static_cast<int>(10.0f * coin_sy * s_zoom));
      const int line_hw     = std::max(1, static_cast<int>(3.0f * coin_sy * s_zoom));
      const int branch_drop = static_cast<int>(29.0f * coin_sy * s_zoom);

      for (int w = -line_hw; w <= line_hw; w++) {
        fast_line(it, hx + w + 2, top_node_y + 1, hx + w + 2, y_bot, col_relief_hi);
        fast_line(it, hx + w + 2, right_node_y + branch_drop + 1, right_node_x + w + 2, right_node_y + 1, col_relief_hi);
        fast_line(it, hx + w + 2, left_node_y + branch_drop + 1, left_node_x + w + 2, left_node_y + 1, col_relief_hi);
      }
      fast_filled_circle(it, hx + 2, top_node_y + 2, node_r, col_relief_hi);
      fast_filled_circle(it, right_node_x + 2, right_node_y + 2, node_r, col_relief_hi);
      fast_filled_circle(it, left_node_x + 2, left_node_y + 2, node_r, col_relief_hi);

      for (int w = -line_hw; w <= line_hw; w++) {
        fast_line(it, hx + w, top_node_y, hx + w, y_bot, col_relief_shadow);
        fast_line(it, hx + w, right_node_y + branch_drop, right_node_x + w, right_node_y, col_relief_shadow);
        fast_line(it, hx + w, left_node_y + branch_drop, left_node_x + w, left_node_y, col_relief_shadow);
      }
      fast_filled_circle(it, hx, top_node_y, node_r, col_relief_shadow);
      fast_filled_circle(it, right_node_x, right_node_y, node_r, col_relief_shadow);
      fast_filled_circle(it, left_node_x, left_node_y, node_r, col_relief_shadow);

      if (coin_sy > 0.76f) {
        const int ty = coin_cy + static_cast<int>(74.0f * coin_sy * s_zoom);
        if (!cs.anim_active) {
          fast_print(it, cx + 2, ty + 2, coin_lbl_font, col_relief_shadow, TextAlign::CENTER, "TAILS");
          fast_print(it, cx - 1, ty - 1, coin_lbl_font, col_relief_hi, TextAlign::CENTER, "TAILS");
        }
        fast_print(it, cx, ty, coin_lbl_font, Color(220, 228, 240), TextAlign::CENTER, "TAILS");
      }
    }
  }

  frame_hits().add_rect(0, 0, SCREEN_W, layout().back_y - scale_y(24),
                        WidgetId::COIN_FLIP_BTN, kHitTap | kHitFocusSlot);
  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
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
                             ctx.camera_snapshot_art.width > 16 &&
                             ctx.camera_snapshot_art.height > 16 &&
                             !rs.alert.loaded_camera_url.empty() &&
                             rs.alert.loaded_camera_url.find(base_map_u) != std::string::npos;

    if (has_map_img) {
      render_album_art_canvas(it, ctx.camera_snapshot_art, ctx.cx, false, false, false, false);

      const int map_hdr_w = std::min(scale_x(278), SCREEN_W - 24);
      char z_prefix[16];
      std::snprintf(z_prefix, sizeof(z_prefix), "z%d • ", cal_map_zoom());
      const std::string loc_hdr = fit_text_to_width_font(
          std::string(z_prefix) + cal_detail_location(), map_hdr_w - 16, ctx.font_small);
      render_card_box(it, ctx.cx - map_hdr_w / 2, scale_y(24), map_hdr_w, scale_y(34),
                      kChromeGlassTint, ctx.theme_primary, scale_s(10));
      fast_print(it, ctx.cx + 1, scale_y(41) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, loc_hdr.c_str());
      fast_print(it, ctx.cx, scale_y(41), ctx.font_small, ctx.col_white, TextAlign::CENTER, loc_hdr.c_str());

      const int z_by = ly.map_zoom_y;
      const int z_lx = ly.map_zoom_left_x();
      const int z_rx = ly.map_zoom_right_x();
      const int z_r  = scale_s(21);
      render_glass_circle(it, z_lx, z_by, z_r, kChromeGlassTint,
                          (cal_map_zoom() > 10) ? ctx.theme_primary : kChromeCardBorder);
      fill_rect_row_major(it, z_lx - scale_s(8), z_by - 1, scale_s(17), std::max(2, scale_s(3)),
                          (cal_map_zoom() > 10) ? ctx.col_white : ctx.col_slate);
      frame_hits().add_circle(z_lx, z_by, z_r, WidgetId::CAL_ZOOM_OUT_BTN,
                              kHitTap | kHitFocusCircle, 0, scale_s(16));

      const Color fg_in = (cal_map_zoom() < 18) ? ctx.col_white : ctx.col_slate;
      render_glass_circle(it, z_rx, z_by, z_r, kChromeGlassTint,
                          (cal_map_zoom() < 18) ? ctx.theme_primary : kChromeCardBorder);
      fill_rect_row_major(it, z_rx - scale_s(8), z_by - 1, scale_s(17), std::max(2, scale_s(3)), fg_in);
      fill_rect_row_major(it, z_rx - 1, z_by - scale_s(8), std::max(2, scale_s(3)), scale_s(17), fg_in);
      frame_hits().add_circle(z_rx, z_by, z_r, WidgetId::CAL_ZOOM_IN_BTN,
                              kHitTap | kHitFocusCircle, 0, scale_s(16));

      render_glass_card(it, ctx.cx - scale_x(64), ly.back_y - scale_y(16), scale_x(128), scale_y(32),
                        scale_s(16), kChromeGlassTint, -1, GlassVariant::Clear, ctx.col_white);
    } else {
      render_page_header(it, ctx.cx, "EVENT MAP", ctx.font_small, ctx.theme_primary);
      render_empty_state(it, ctx.cx, scale_y(222), "Loading OpenStreetMap...", ctx.font_small, ctx.theme_secondary);
    }

    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Back", ctx.col_white);
    return;
  }

  if (detail_mode == 1) {
    render_page_header(it, ctx.cx, "EVENT DETAILS", ctx.font_small, ctx.theme_primary);

    // Event details are non-interactive text -> draw directly on background (only "View Map" gets frosted glass)
    const int txt_max_w = (SCREEN_W < 380) ? (SCREEN_W - 36) : scale_x(316);
    const std::string fit_tm = fit_text_to_width_font(
        cal_detail_time().empty() ? "Upcoming" : cal_detail_time(), txt_max_w, ctx.font_small);
    if (backdrop_active()) {
      fast_print(it, ctx.cx + 1, scale_y(96) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, fit_tm.c_str());
    }
    fast_print(it, ctx.cx, scale_y(96), ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, fit_tm.c_str());

    std::string title_lines[3];
    const int t_cnt = wrap_text_lines(cal_detail_title(), txt_max_w, ctx.font_small, title_lines, 3);
    const int title_cy = scale_y(164);
    const int t_start_y = (t_cnt <= 1) ? title_cy
                          : ((t_cnt == 2) ? (title_cy - scale_y(14)) : (title_cy - scale_y(28)));
    for (int i = 0; i < t_cnt; i++) {
      const int ly_txt = t_start_y + i * scale_y(28);
      if (backdrop_active()) {
        fast_print(it, ctx.cx + 1, ly_txt + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, title_lines[i].c_str());
      }
      fast_print(it, ctx.cx, ly_txt, ctx.font_small, ctx.col_white, TextAlign::CENTER, title_lines[i].c_str());
    }

    fast_line(it, ctx.cx - scale_x(110), scale_y(218), ctx.cx + scale_x(110), scale_y(218), kChromeCardBorder);

    const bool has_loc = !cal_detail_location().empty();
    if (has_loc) {
      std::string loc_lines[2];
      const int l_cnt = wrap_text_lines(cal_detail_location(), txt_max_w, ctx.font_small, loc_lines, 2);
      const int loc_cy = scale_y(264);
      const int l_start_y = (l_cnt <= 1) ? loc_cy : (loc_cy - scale_y(13));
      for (int i = 0; i < l_cnt; i++) {
        const int ly_loc = l_start_y + i * scale_y(26);
        if (backdrop_active()) {
          fast_print(it, ctx.cx + 1, ly_loc + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, loc_lines[i].c_str());
        }
        fast_print(it, ctx.cx, ly_loc, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, loc_lines[i].c_str());
      }

      const int map_btn_y = scale_y(348);
      const int map_btn_w = (SCREEN_W > 520) ? scale_x(236) : 236;
      const int map_btn_h = (SCREEN_W > 520) ? scale_y(44) : 44;
      render_modern_card(it, ctx.cx, map_btn_y, 236, 44, 12,
                         kChromeGlassTint, ctx.theme_primary,
                         "View Map", ctx.col_white, ctx.font_small);
      const int pin_x = ctx.cx - scale_x(82);
      fast_filled_circle(it, pin_x, map_btn_y - scale_s(4), scale_s(5), Color(235, 52, 62));
      fast_filled_triangle(it, pin_x - scale_s(4), map_btn_y - scale_s(2),
                           pin_x + scale_s(4), map_btn_y - scale_s(2),
                           pin_x, map_btn_y + scale_s(6), Color(235, 52, 62));
      fast_filled_circle(it, pin_x, map_btn_y - scale_s(4), std::max(1, scale_s(2)), ctx.col_white);
      frame_hits().add_centered_rect(ctx.cx, map_btn_y, map_btn_w, map_btn_h,
                                     WidgetId::CAL_EVENT_BASE, kHitTap | kHitFocusSlot, 0, scale_y(8));
    } else {
      if (backdrop_active()) {
        fast_print(it, ctx.cx + 1, scale_y(278) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, "No location specified");
      }
      fast_print(it, ctx.cx, scale_y(278), ctx.font_small, ctx.col_slate,
                 TextAlign::CENTER, "No location specified");
    }

    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Back");
    return;
  }

  if (cal_quick_add_open()) {
    render_page_header(it, ctx.cx, "ADD REMINDER", ctx.font_small, ctx.theme_primary);

    static constexpr const char *kQuickLabels[5] = {
        "In 15m: Quick Reminder",
        "In 30m: Check-In / Task",
        "In 1h: Follow-Up Call",
        "In 3h: Errand / Pickup",
        "Tomorrow: Morning Reminder",
    };
    const int card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : ly.list_card_w;
    const int card_x = ctx.cx - card_w / 2;
    const int card_h = std::min(scale_y(44), ly.slot5_stride - 6);

    for (int i = 0; i < 5; i++) {
      const int ym = ly.slot5_y(i);
      render_card_box(it, card_x, ym - card_h / 2, card_w, card_h,
                      kChromeGlassTint, kChromeCardBrdOn, scale_s(12));
      fill_rect_row_major(it, card_x + scale_x(12), ym - 1, scale_s(9), std::max(2, scale_s(3)), ctx.theme_primary);
      fill_rect_row_major(it, card_x + scale_x(12) + scale_s(3), ym - scale_s(4), std::max(2, scale_s(3)), scale_s(9), ctx.theme_primary);
      const std::string fit_q = fit_text_to_width_font(
          kQuickLabels[i], card_w - scale_x(42), ctx.font_small);
      fast_print(it, card_x + scale_x(30), ym, ctx.font_small, ctx.col_white,
                 TextAlign::CENTER_LEFT, fit_q.c_str());
      frame_hits().add_rect(card_x, ym - card_h / 2, card_w, card_h,
                            WidgetId::CAL_PRESET_BASE + static_cast<uint16_t>(i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }

    render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Cancel");
    return;
  }

  const auto *slots = rs.slots_for(latter_rpc::SlotChannel::CALENDAR);
  const int cur_pg  = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::CALENDAR)];
  const int tot_pg  = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::CALENDAR)];

  const bool show_fb = (!cal_feedback_text().empty() && ctx.now_ms > 0 &&
                        (ctx.now_ms - cal_feedback_ms() < 2200U));
  if (show_fb) {
    const std::string fit_fb = fit_text_to_width_font(cal_feedback_text(), scale_x(260), ctx.font_small);
    render_page_header(it, ctx.cx, fit_fb, ctx.font_small, ctx.theme_secondary);
  } else if (tot_pg > 1) {
    char c_hdr[36];
    std::snprintf(c_hdr, sizeof(c_hdr), "CALENDAR %d/%d (+)", cur_pg, tot_pg);
    render_page_header(it, ctx.cx, c_hdr, ctx.font_small, ctx.theme_primary);
  } else {
    render_page_header(it, ctx.cx, "CALENDAR  (+)", ctx.font_small, ctx.theme_primary);
  }
  frame_hits().add_rect(ctx.cx - scale_x(120), 0, 2 * scale_x(120), scale_y(68),
                        WidgetId::CAL_QUICK_ADD_BTN, kHitTap | kHitFocusSlot);

  std::string names[5];
  bool acts[5] = {false, false, false, false, false};
  for (int i = 0; i < 5; i++) {
    names[i] = slots[i].name;
    acts[i]  = slots[i].active;
  }
  if (names[0].empty()) {
    CalendarRecord recs[5];
    const size_t n = parse_calendar_records(calendar_state().summary, recs, 5);
    for (size_t i = 0; i < n; i++) {
      names[i] = std::string(recs[i].time) + ": " + std::string(recs[i].title);
      acts[i]  = (i == 0);
    }
  }

  if (names[0].empty()) {
    render_empty_state(it, ctx.cx, scale_y(222), "No upcoming events (Tap +)", ctx.font_small, ctx.col_slate);
  } else {
    const int card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : ly.list_card_w;
    const int card_x = ctx.cx - card_w / 2;
    const int card_h = std::min(scale_y(44), ly.slot5_stride - 6);
    const int bounce_y = edge_bounce().offset_px(
        PageId::TOOLS, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);
    for (int i = 0; i < 5; i++) {
      if (names[i].empty()) continue;
      const int ym = ly.slot5_y(i) + bounce_y;
      const Color brd_c = acts[i] ? ctx.theme_primary : kChromeCardBorder;
      const Color txt_c = acts[i] ? ctx.col_white : ctx.col_slate;
      render_card_box(it, card_x, ym - card_h / 2, card_w, card_h,
                      kChromeGlassTint, brd_c, scale_s(12));
      if (acts[i]) {
        fill_rect_row_major(it, card_x + scale_x(8), ym - scale_y(12),
                            std::max(2, scale_x(3)), scale_y(24), ctx.theme_primary);
      }
      const std::string fit_ev = fit_text_to_width_font(names[i], card_w - scale_x(30), ctx.font_small);
      fast_print(it, card_x + scale_x(18), ym, ctx.font_small, txt_c, TextAlign::CENTER_LEFT, fit_ev.c_str());
      frame_hits().add_rect(card_x, ym - card_h / 2, card_w, card_h,
                            WidgetId::CAL_EVENT_BASE + static_cast<uint16_t>(i),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
  }

  if (tot_pg > 1) {
    render_vertical_page_pips(it, cur_pg > 1, cur_pg < tot_pg,
                              ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y,
                     tools_direct_home() ? "Home" : "< Back");
}

}  // namespace

void reset_calculator_state() {
  auto &st = calc_state();
  st.prev_val = 0.0;
  st.op = 0;
  std::snprintf(st.entry, sizeof(st.entry), "0");
  st.history[0] = '\0';
  st.new_entry = true;
  st.error = false;
}

void handle_calculator_keypad_tap(int tx, int ty) {
  static constexpr int kBaseRowsY[5] = {131, 187, 243, 299, 355};
  const int key_w_tol = (SCREEN_W < 380) ? 35 : scale_x(40);
  const int key_h_tol = scale_y(27);

  int r = -1;
  int c = -1;
  for (int i = 0; i < 5; i++) {
    if (std::abs(ty - scale_y(kBaseRowsY[i])) <= key_h_tol) {
      r = i;
      break;
    }
  }
  for (int j = 0; j < 4; j++) {
    if (std::abs(tx - layout().calc_col_x(j)) <= key_w_tol) {
      c = j;
      break;
    }
  }
  if (r < 0 || c < 0) return;

  auto &st = calc_state();
  if (st.error && !(r == 0 && c == 0)) {
    reset_calculator_state();
  }

  auto compute_op = [](double a, double b, int op_code) -> double {
    if (op_code == 1) return a + b;
    if (op_code == 2) return a - b;
    if (op_code == 3) return a * b;
    if (op_code == 4) return (std::fabs(b) > 1e-12) ? (a / b) : NAN;
    return b;
  };
  auto op_to_str = [](int op_code) -> const char * {
    if (op_code == 1) return "+";
    if (op_code == 2) return "-";
    if (op_code == 3) return "×";
    if (op_code == 4) return "÷";
    return "";
  };

  if (r == 0 && c == 0) {
    reset_calculator_state();
    return;
  }
  if (r == 0 && c == 1) {
    if (std::strcmp(st.entry, "0") != 0 && !st.error) {
      const size_t len = std::strlen(st.entry);
      if (st.entry[0] == '-') {
        std::memmove(st.entry, st.entry + 1, len);
      } else if (len + 1 < sizeof(st.entry)) {
        std::memmove(st.entry + 1, st.entry, len + 1);
        st.entry[0] = '-';
      }
    }
    return;
  }
  if (r == 0 && c == 2) {
    const double cur = std::strtod(st.entry, nullptr);
    if (st.op == 1 || st.op == 2) {
      format_calc_num_buf(st.prev_val * (cur / 100.0), st.entry, sizeof(st.entry));
      st.new_entry = false;
    } else {
      format_calc_num_buf(cur / 100.0, st.entry, sizeof(st.entry));
      st.new_entry = true;
    }
    return;
  }
  if (r == 4 && c == 2) {
    if (!st.new_entry && !st.error) {
      const size_t len = std::strlen(st.entry);
      if (len <= 1 || (len == 2 && st.entry[0] == '-')) {
        std::snprintf(st.entry, sizeof(st.entry), "0");
      } else {
        st.entry[len - 1] = '\0';
      }
    }
    return;
  }
  if (c == 3) {
    const int new_op = (r == 0) ? 4 : (r == 1) ? 3 : (r == 2) ? 2 : (r == 3) ? 1 : 5;
    const double cur = (st.entry[0] != '\0') ? std::strtod(st.entry, nullptr) : st.prev_val;

    if (new_op == 5) {
      if (st.op != 0) {
        char prev_b[20];
        format_calc_num_buf(st.prev_val, prev_b, sizeof(prev_b));
        std::snprintf(st.history, sizeof(st.history), "%s %s %s =", prev_b, op_to_str(st.op), st.entry);
        const double res = compute_op(st.prev_val, cur, st.op);
        format_calc_num_buf(res, st.entry, sizeof(st.entry));
        st.op = 0;
        st.new_entry = true;
      }
    } else {
      if (st.op != 0 && !st.new_entry && st.entry[0] != '\0') {
        const double res = compute_op(st.prev_val, cur, st.op);
        if (std::isnan(res) || std::isinf(res)) {
          format_calc_num_buf(res, st.entry, sizeof(st.entry));
          st.op = 0;
          st.new_entry = true;
          return;
        }
        st.prev_val = res;
      } else {
        st.prev_val = cur;
      }
      st.op = new_op;
      st.new_entry = true;
      char prev_b[20];
      format_calc_num_buf(st.prev_val, prev_b, sizeof(prev_b));
      std::snprintf(st.history, sizeof(st.history), "%s %s", prev_b, op_to_str(st.op));
      std::snprintf(st.entry, sizeof(st.entry), "%s", prev_b);
    }
    return;
  }
  if (r == 4 && c == 1) {
    if (st.new_entry || st.entry[0] == '\0') {
      std::snprintf(st.entry, sizeof(st.entry), "0.");
      st.new_entry = false;
    } else if (std::strchr(st.entry, '.') == nullptr && std::strlen(st.entry) < 11) {
      std::strcat(st.entry, ".");
    }
    return;
  }

  int digit = 0;
  if (r == 1) digit = 7 + c;
  else if (r == 2) digit = 4 + c;
  else if (r == 3) digit = 1 + c;
  else if (r == 4 && c == 0) digit = 0;

  if (st.new_entry || std::strcmp(st.entry, "0") == 0) {
    std::snprintf(st.entry, sizeof(st.entry), "%d", digit);
    st.new_entry = false;
  } else if (std::strlen(st.entry) < 10) {
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
  static int z = 15;
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
  out_title.clear();
  out_loc.clear();
  out_map_url.clear();

  const size_t p1 = slot_id.find('|');
  if (p1 != std::string::npos) {
    const size_t p2 = slot_id.find('|', p1 + 1);
    if (p2 != std::string::npos) {
      const size_t p3 = slot_id.find('|', p2 + 1);
      out_time  = slot_id.substr(0, p1);
      out_title = slot_id.substr(p1 + 1, p2 - p1 - 1);
      if (p3 != std::string::npos) {
        out_loc     = slot_id.substr(p2 + 1, p3 - p2 - 1);
        out_map_url = slot_id.substr(p3 + 1);
      } else {
        out_loc = slot_id.substr(p2 + 1);
      }
      return;
    }
  }

  const size_t col = slot_name.find(": ");
  if (col != std::string::npos) {
    out_time  = slot_name.substr(0, col);
    out_title = slot_name.substr(col + 2);
  } else {
    out_time  = "EVENT";
    out_title = slot_name;
  }
}

void render_alert_ringing_overlay(esphome::display::Display &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const bool pulse = ((st.now_ms / 250U) % 2U) != 0U;
  const Color alert_color = st.find_my_active ? st.col_white
                            : (st.alarm_ringing ? ctx.col_red : ctx.theme_secondary);
  const Color ring_c = pulse ? alert_color
                             : (st.find_my_active ? ctx.theme_secondary : Color(20, 20, 20));
  render_bezel_alert_border(it, st.cx, st.cy, ring_c);

  if (st.find_my_active) {
    const uint32_t max_span = static_cast<uint32_t>(std::max(60, SCREEN_MIN_R - 76));
    const int radar_r = 50 + static_cast<int>((st.now_ms / 8U) % max_span);
    fast_circle(it, st.cx, st.cy, radar_r, ctx.theme_primary);
    fast_circle(it, st.cx, st.cy, radar_r + 1, pulse ? st.col_white : ctx.theme_secondary);
    const int radar_r2 = 50 + static_cast<int>(((st.now_ms / 8U) + (max_span / 2U)) % max_span);
    fast_circle(it, st.cx, st.cy, radar_r2, ctx.theme_secondary);

    fast_print(it, st.cx, scale_y(75), ctx.font_small,
               pulse ? st.col_white : ctx.theme_secondary, TextAlign::CENTER, "LOCATING DEVICE");
    fast_print(it, st.cx, scale_y(156), ctx.font_date, st.col_white, TextAlign::CENTER, "LATTER");
    fast_print(it, st.cx, scale_y(196), ctx.font_date, ctx.theme_primary, TextAlign::CENTER, "COMPANION");
    fast_print(it, st.cx, scale_y(244), ctx.font_small, ctx.theme_secondary,
               TextAlign::CENTER, "Acoustic Beacon Active");

    const int btn_y = scale_y(330);
    const int btn_w = (SCREEN_W > 520) ? scale_x(240) : 240;
    const int btn_h = (SCREEN_W > 520) ? scale_y(44) : 44;
    render_modern_card(it, st.cx, btn_y, 240, 44, 12, kChromeGlassTint, ctx.theme_primary,
                       "Tap to Dismiss", st.col_white, ctx.font_small);
    frame_hits().add_centered_rect(st.cx, btn_y, btn_w, btn_h,
                                   WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 0, scale_y(8));
    return;
  }

  if (st.alarm_ringing) {
    fast_print(it, st.cx, scale_y(75), ctx.font_small,
               pulse ? ctx.theme_secondary : st.col_white, TextAlign::CENTER, "ALARM RINGING");

    char cur_t_buf[16];
    std::snprintf(cur_t_buf, sizeof(cur_t_buf), "%s", kUnknownGlyph);
    char ampm_buf[8] = "";
    if (ctx.now.is_valid()) {
      format_time_hhmm(ctx.now.hour, ctx.now.minute, st.is_24h_format,
                       cur_t_buf, sizeof(cur_t_buf), ampm_buf);
    }
    fast_print(it, st.cx, scale_y(160), ctx.font_time, st.col_white, TextAlign::CENTER, cur_t_buf);
    if (!st.is_24h_format && ampm_buf[0] != '\0') {
      fast_print(it, st.cx + scale_x(120), scale_y(175), ctx.font_small, ctx.theme_secondary,
                 TextAlign::CENTER_LEFT, ampm_buf);
    }

    const int r_idx = (st.alarm_ringing_idx == 3)
                          ? st.alarm3_recurrence
                          : ((st.alarm_ringing_idx == 2) ? st.alarm2_recurrence : st.alarm1_recurrence);
    static constexpr const char *kRecNames[4] = {"Daily", "Weekdays", "Weekends", "Once"};
    fast_print(it, st.cx, scale_y(225), ctx.font_small, ctx.theme_primary,
               TextAlign::CENTER, kRecNames[r_idx & 3]);

    const int snz_y = scale_y(295);
    const int snz_w = (SCREEN_W > 520) ? scale_x(224) : 224;
    const int snz_h = (SCREEN_W > 520) ? scale_y(44) : 44;
    render_modern_card(it, st.cx, snz_y, 224, 44, 12, kChromeGlassTint, ctx.theme_secondary,
                       "Snooze (+5m)", st.col_white, ctx.font_small);
    frame_hits().add_centered_rect(st.cx, snz_y, snz_w, snz_h,
                                   WidgetId::ALERT_SNOOZE_BTN, kHitTap | kHitFocusSlot, 0, scale_y(8));

    const int dis_y = scale_y(364);
    const int dis_w = (SCREEN_W > 520) ? scale_x(204) : 204;
    const int dis_h = (SCREEN_W > 520) ? scale_y(42) : 42;
    render_modern_card(it, st.cx, dis_y, 204, 42, 12, kChromeGlassTint, ctx.col_red,
                       "Dismiss", st.col_white, ctx.font_small);
    frame_hits().add_centered_rect(st.cx, dis_y, dis_w, dis_h,
                                   WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 1, scale_y(8));
    return;
  }

  fast_print(it, st.cx, scale_y(75), ctx.font_small,
             pulse ? ctx.theme_secondary : st.col_white, TextAlign::CENTER, "TIMER EXPIRED");
  fast_print(it, st.cx, scale_y(170), ctx.font_time, st.col_white, TextAlign::CENTER, "00:00");
  const std::string t_lbl = st.va_timer_name.empty()
                                ? std::string("Time is up!")
                                : fit_text_to_width_font(st.va_timer_name.str(), scale_x(260), ctx.font_small);
  fast_print(it, st.cx, scale_y(230), ctx.font_small, ctx.theme_secondary,
             TextAlign::CENTER, t_lbl.c_str());

  const int btn_y = scale_y(330);
  const int btn_w = (SCREEN_W > 520) ? scale_x(224) : 224;
  const int btn_h = (SCREEN_W > 520) ? scale_y(44) : 44;
  render_modern_card(it, st.cx, btn_y, 224, 44, 12, kChromeGlassTint, ctx.theme_primary,
                     "Dismiss", st.col_white, ctx.font_small);
  frame_hits().add_centered_rect(st.cx, btn_y, btn_w, btn_h,
                                 WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 0, scale_y(8));
}

void render_tools_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const int sub = rs.nav.tools_subpage;

  if (sub == ToolsSubpage::FLASHLIGHT) {
    if (!frame_base(it, ctx.col_white)) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, ctx.col_white);
    }
    return;
  }

  switch (sub) {
#if LATTER_ROUND_DISPLAY
    case ToolsSubpage::STOPWATCH:
      render_stopwatch_subpage(it, ctx, rs.stopwatch);
      break;
#endif
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
