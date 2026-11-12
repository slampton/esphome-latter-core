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
  static bool modern = true;
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

void draw_tools_card_icon(esphome::display::Display &it, int app_id, int ix, int iy,
                          const UiFrameContext &ctx) {
  using esphome::Color;
  const Color surf_bg = ui::chrome_card_bg_off();
  if (app_id == ToolsSubpage::TIMERS) {
    ui::draw_icon_hourglass(it, ix, iy, ctx.col_gold, ctx.col_white, 13);
  } else if (app_id == ToolsSubpage::ALARMS) {
    ui::draw_icon_alarm_clock(it, ix, iy + 2, ctx.theme_secondary, surf_bg, 11);
  } else if (app_id == ToolsSubpage::STOPWATCH) {
    ui::draw_icon_stopwatch(it, ix, iy + 2, ctx.col_cyan, ctx.col_gold, 11);
  } else if (app_id == ToolsSubpage::LEVEL) {
    fast_circle(it, ix, iy, 12, ctx.col_green);
    fast_circle(it, ix, iy, 11, ctx.col_green);
    fast_hline(it, ix - 14, iy, 29, scale_color(ctx.col_slate, 0.7f));
    fast_vline(it, ix, iy - 14, 29, scale_color(ctx.col_slate, 0.7f));
    fast_filled_circle(it, ix + 2, iy - 2, 4, ctx.col_gold);
    fast_pixel(it, ix + 1, iy - 3, ctx.col_white);
  } else if (app_id == ToolsSubpage::FLASHLIGHT) {
    fill_rect_row_major(it, ix - 4, iy - 2, 9, 13, ctx.col_slate);
    fast_filled_triangle(it, ix - 8, iy - 7, ix + 8, iy - 7, ix + 4, iy - 2, ctx.col_gold);
    fast_filled_triangle(it, ix - 8, iy - 7, ix - 4, iy - 2, ix + 4, iy - 2, ctx.col_gold);
    fast_hline(it, ix - 8, iy - 8, 17, ctx.col_white);
    fast_line(it, ix - 5, iy - 11, ix - 9, iy - 15, ctx.col_gold);
    fast_vline(it, ix, iy - 16, 5, ctx.col_gold);
    fast_line(it, ix + 5, iy - 11, ix + 9, iy - 15, ctx.col_gold);
  } else if (app_id == ToolsSubpage::CALCULATOR) {
    render_rounded_rect(it, ix - 11, iy - 13, 22, 26, 4, ctx.col_slate);
    fill_rect_row_major(it, ix - 8, iy - 10, 16, 6, surf_bg);
    for (int gr = 0; gr < 2; gr++) {
      for (int gc = 0; gc < 3; gc++) {
        const Color kc = (gc == 2) ? ctx.col_orange : ctx.col_white;
        fill_rect_row_major(it, ix - 8 + gc * 6, iy - 1 + gr * 6, 4, 4, kc);
      }
    }
  } else if (app_id == ToolsSubpage::DICE) {
    render_rounded_rect(it, ix - 12, iy - 12, 24, 24, 5, ctx.col_white);
    fast_filled_circle(it, ix - 6, iy - 6, 2, surf_bg);
    fast_filled_circle(it, ix + 6, iy - 6, 2, surf_bg);
    fast_filled_circle(it, ix, iy, 2, ctx.col_red);
    fast_filled_circle(it, ix - 6, iy + 6, 2, surf_bg);
    fast_filled_circle(it, ix + 6, iy + 6, 2, surf_bg);
  } else if (app_id == ToolsSubpage::COIN_FLIP) {
    fast_filled_circle(it, ix, iy, 12, ctx.col_gold);
    fast_circle(it, ix, iy, 12, ctx.col_white);
    fast_circle(it, ix, iy, 9, Color(185, 135, 18));
    fast_vline(it, ix, iy - 6, 13, ctx.col_white);
    fast_hline(it, ix - 3, iy - 4, 7, ctx.col_white);
    fast_hline(it, ix - 3, iy, 7, ctx.col_white);
    fast_hline(it, ix - 3, iy + 4, 7, ctx.col_white);
  } else if (app_id == ToolsSubpage::CALENDAR) {
    ui::draw_icon_calendar(it, ix, iy, ctx.col_white, ctx.col_red, 11);
  } else if (app_id == ToolsSubpage::INTERCOM) {
    ui::draw_icon_speaker(it, ix - 2, iy, ctx.col_cyan, true);
  } else if (app_id == ToolsSubpage::CAMERAS) {
    ui::draw_icon_camera(it, ix, iy, ctx.col_green, surf_bg, 11);
  }
}

void render_tools_launcher(esphome::display::Display &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs) {
  using esphome::display::TextAlign;

  struct ToolItemDef {
    int id;
    const char *label;
  };
#if LATTER_HAS_IMU
  static constexpr ToolItemDef kPage0[3][2] = {
      {{ToolsSubpage::TIMERS, "Timers"},     {ToolsSubpage::ALARMS, "Alarms"}},
      {{ToolsSubpage::STOPWATCH, "Chrono"},  {ToolsSubpage::LEVEL, "Level"}},
      {{ToolsSubpage::FLASHLIGHT, "Light"},  {ToolsSubpage::CALCULATOR, "Calc"}},
  };
  static constexpr ToolItemDef kPage1[3][2] = {
      {{ToolsSubpage::DICE, "Dice"},         {ToolsSubpage::COIN_FLIP, "Flip"}},
      {{ToolsSubpage::CALENDAR, "Agenda"},   {ToolsSubpage::INTERCOM, "Intercom"}},
      {{ToolsSubpage::CAMERAS, "Cameras"},   {0, ""}},
  };
#else
  static constexpr ToolItemDef kPage0[3][2] = {
      {{ToolsSubpage::TIMERS, "Timers"},     {ToolsSubpage::ALARMS, "Alarms"}},
      {{ToolsSubpage::STOPWATCH, "Chrono"},  {ToolsSubpage::FLASHLIGHT, "Light"}},
      {{ToolsSubpage::CALCULATOR, "Calc"},   {ToolsSubpage::CALENDAR, "Agenda"}},
  };
  static constexpr ToolItemDef kPage1[3][2] = {
      {{ToolsSubpage::INTERCOM, "Intercom"}, {ToolsSubpage::CAMERAS, "Cameras"}},
      {{ToolsSubpage::DICE, "Dice"},         {ToolsSubpage::COIN_FLIP, "Flip"}},
      {{0, ""},                              {0, ""}},
  };
#endif

  const auto &ly = layout();
  const bool compact_w = (SCREEN_W < 380);
  const int card_w = compact_w ? ((SCREEN_W - 30) / 2) : scale_x(138);
  const int card_h = std::min(78, ly.tools_row_stride - 10);
  const int col_cx[2] = {ctx.cx - ly.tools_col_off, ctx.cx + ly.tools_col_off};
  const int row_cy[3] = {ly.tools_row_y(0), ly.tools_row_y(1), ly.tools_row_y(2)};

  auto draw_grid_for_page = [&](int pg_idx, int y_shift, bool register_hits) {
    const auto &grid = (pg_idx == 0) ? kPage0 : kPage1;
    for (int r = 0; r < 3; r++) {
      const int cy_card = row_cy[r] + y_shift;
      if (cy_card < scale_y(78) || cy_card > scale_y(412)) continue;
      for (int c = 0; c < 2; c++) {
        const int app_id = grid[r][c].id;
        if (app_id == 0) continue;
        const int cx_card = col_cx[c];
        const int rx = cx_card - card_w / 2;
        const int ry = cy_card - card_h / 2;

        render_rounded_rect(it, rx + 2, ry + 3, card_w, card_h, 16, kChromeShadow);
        render_card_box(it, rx, ry, card_w, card_h,
                           ui::chrome_card_bg_off(), ui::chrome_card_brd_on(), 16);

        const int ix = compact_w ? (rx + 22) : (cx_card - 40);
        const int tx = compact_w ? (rx + 42) : (cx_card - 16);
        draw_tools_card_icon(it, app_id, ix, cy_card, ctx);
        fast_print(it, tx, cy_card, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT,
                   grid[r][c].label);

        if (register_hits) {
          const uint16_t slot = static_cast<uint16_t>(r * 2 + c);
          frame_hits().add_rect(rx, ry, card_w, card_h,
                                WidgetId::TOOLS_CARD_BASE + slot,
                                kHitTap | kHitFocusSlot,
                                static_cast<int16_t>(app_id));
        }
      }
    }
  };

  const int cur_pg = (rs.nav.tools_menu_page == 0) ? 0 : 1;
  if (rs.nav.menu_scroll_active) {
    const int full_span = scale_y(264);
    const int offset = menu_scroll_shift(full_span);
    const int out_dy = -rs.nav.menu_scroll_dir * offset;
    const int in_dy  = rs.nav.menu_scroll_dir * (full_span - offset);
    const int tgt_pg = (rs.nav.menu_scroll_target_page == 0) ? 0 : 1;
    draw_grid_for_page(cur_pg, out_dy, false);
    draw_grid_for_page(tgt_pg, in_dy, false);
  } else {
    draw_grid_for_page(cur_pg, 0, true);
  }

  render_page_header(it, ctx.cx, "TOOLS", ctx.font_btn_bold, ctx.theme_primary, true);
  const int active_pip_pg = rs.nav.menu_scroll_active
                                ? ((rs.nav.menu_scroll_target_page == 0) ? 0 : 1)
                                : cur_pg;
  render_vertical_page_pips(it, active_pip_pg == 1, active_pip_pg == 0,
                            ctx.col_bg, ctx.col_white, kChromePipInactive);
  render_home_indicator_bar(it, ctx.cx);
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
  render_card_box(it, ctx.cx - rec_w / 2, scale_y(242), rec_w, 34,
                     ui::chrome_card_bg_on(), ctx.theme_primary, 17);
  frame_hits().add_rect(ctx.cx - rec_w / 2, scale_y(242), rec_w, 34,
                        WidgetId::ALARM_RECUR_BTN, kHitTap | kHitFocusSlot);
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

  const uint32_t cur_ms = sw.running
                              ? (sw.elapsed_ms + (ctx.now_ms - sw.start_time_ms))
                              : sw.elapsed_ms;
  const uint32_t total_sec = cur_ms / 1000U;
  const uint32_t mins      = total_sec / 60U;
  const uint32_t secs      = total_sec % 60U;
  const uint32_t tenths    = (cur_ms % 1000U) / 100U;
  const uint32_t centis    = (cur_ms % 1000U) / 10U;

  const bool modern_mode = stopwatch_modern_digital();
  render_page_header(it, ctx.cx, "STOPWATCH", ctx.font_btn_bold, ctx.theme_primary);
  frame_hits().add_rect(ctx.cx - scale_x(95), scale_y(44), scale_x(190), scale_y(42),
                        WidgetId::SW_STYLE_BTN, kHitTap | kHitFocusSlot);

  if (modern_mode) {
    const float sec_prog = static_cast<float>(cur_ms % 60000U) / 60000.0f;
    if (cur_ms > 0 || sw.running) {
      render_progress_arc_r224(it, ctx.cx, ctx.cy, sec_prog,
                               sw.running ? ctx.col_cyan : ctx.theme_primary);
    }

    fast_print(it, ctx.cx, scale_y(116), ctx.font_small,
               sw.running ? ctx.col_cyan : ctx.col_slate, TextAlign::CENTER,
               sw.running ? "RUNNING • TAP HEADER FOR ANALOG" : "DIGITAL • TAP HEADER FOR ANALOG");

    const Color card_brd = sw.running ? ctx.col_cyan
                                      : (cur_ms > 0 ? ctx.theme_primary : ui::chrome_card_brd_on());
    render_card_box(it, ctx.cx - scale_x(122), scale_y(158), scale_x(244), scale_y(94),
                       sw.running ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                       card_brd, 20);

    char mmss_buf[16];
    std::snprintf(mmss_buf, sizeof(mmss_buf), "%02lu:%02lu",
                  static_cast<unsigned long>(mins), static_cast<unsigned long>(secs));
    char cs_buf[8];
    std::snprintf(cs_buf, sizeof(cs_buf), ".%02lu", static_cast<unsigned long>(centis));

    fast_print(it, ctx.cx - scale_x(22), scale_y(205), ctx.font_chrono_time,
               sw.running ? ctx.col_white : (cur_ms > 0 ? ctx.col_gold : ctx.col_slate),
               TextAlign::CENTER, mmss_buf);
    fast_print(it, ctx.cx + scale_x(82), scale_y(214), ctx.font_date,
               sw.running ? ctx.col_cyan : ctx.col_slate, TextAlign::CENTER, cs_buf);

    if (sw.lap_ms > 0) {
      const uint32_t l_sec = sw.lap_ms / 1000U;
      const uint32_t l_cs  = (sw.lap_ms % 1000U) / 10U;
      char lap_buf[32];
      std::snprintf(lap_buf, sizeof(lap_buf), "LAP  %02lu:%02lu.%02lu",
                    static_cast<unsigned long>(l_sec / 60U),
                    static_cast<unsigned long>(l_sec % 60U),
                    static_cast<unsigned long>(l_cs));
      fast_print(it, ctx.cx, scale_y(278), ctx.font_small, ctx.col_gold,
                 TextAlign::CENTER, lap_buf);
    }
  } else {
    const int d_cy = scale_y(208);
    const int d_r  = scale_min(104);
    fast_filled_circle(it, ctx.cx, d_cy, d_r, ui::chrome_card_bg_off());
    fast_circle(it, ctx.cx, d_cy, d_r, ui::chrome_card_brd_on());
    fast_circle(it, ctx.cx, d_cy, d_r - 1, ui::chrome_card_brd_off());

    for (int i = 0; i < 60; i++) {
      const float s_a = DIAL_SIN[i];
      const float c_a = DIAL_COS[i];
      const bool major = (i % 5 == 0);
      const int r_in  = d_r - (major ? scale_min(12) : scale_min(6));
      const int r_out = d_r - 3;
      fast_line(it,
                ctx.cx + static_cast<int>(s_a * r_in), d_cy - static_cast<int>(c_a * r_in),
                ctx.cx + static_cast<int>(s_a * r_out), d_cy - static_cast<int>(c_a * r_out),
                major ? ctx.col_white : ctx.col_slate);
    }

    const int sub_cy = d_cy - scale_min(40);
    const int sub_r  = scale_min(26);
    fast_filled_circle(it, ctx.cx, sub_cy, sub_r, ui::chrome_card_bg_on());
    fast_circle(it, ctx.cx, sub_cy, sub_r, ui::chrome_card_brd_on());
    const float m_ang = (static_cast<float>(mins % 30U) + static_cast<float>(secs) / 60.0f) *
                        (2.0f * PI_F / 30.0f);
    fast_line(it, ctx.cx, sub_cy,
              ctx.cx + static_cast<int>(fast_sin(m_ang) * (sub_r - 5)),
              sub_cy - static_cast<int>(fast_cos(m_ang) * (sub_r - 5)), ctx.col_gold);

    char dig_b[20];
    std::snprintf(dig_b, sizeof(dig_b), "%02lu:%02lu.%lu",
                  static_cast<unsigned long>(mins),
                  static_cast<unsigned long>(secs),
                  static_cast<unsigned long>(tenths));
    fast_print(it, ctx.cx, d_cy + scale_min(44), ctx.font_small, ctx.col_white,
               TextAlign::CENTER, dig_b);

    if (sw.lap_ms > 0) {
      const float l_ang = (static_cast<float>(sw.lap_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
      fast_line(it, ctx.cx, d_cy,
                ctx.cx + static_cast<int>(fast_sin(l_ang) * (d_r - 12)),
                d_cy - static_cast<int>(fast_cos(l_ang) * (d_r - 12)), ctx.col_gold);
    }

    const float s_ang = (static_cast<float>(cur_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
    const float ss = fast_sin(s_ang);
    const float cs = fast_cos(s_ang);
    draw_glow_line(it,
                   static_cast<int>(std::round(static_cast<float>(ctx.cx) - ss * scale_min_f(18.0f))),
                   static_cast<int>(std::round(static_cast<float>(d_cy) + cs * scale_min_f(18.0f))),
                   static_cast<int>(std::round(static_cast<float>(ctx.cx) + ss * static_cast<float>(d_r - 10))),
                   static_cast<int>(std::round(static_cast<float>(d_cy) - cs * static_cast<float>(d_r - 10))),
                   ctx.col_cyan, scale_color(ctx.col_cyan, 0.35f));
    fast_filled_circle(it, ctx.cx, d_cy, 4, ctx.col_white);
    fast_filled_circle(it, ctx.cx, d_cy, 2, ctx.col_cyan);
  }

  const int btn_y = scale_y(348);
  const Color start_brd = sw.running ? ctx.col_orange : ctx.col_green;
  const Color start_bg  = sw.running ? scale_color(ctx.col_orange, 0.22f)
                                     : scale_color(ctx.col_green, 0.22f);
  const char *start_lbl = sw.running ? "STOP" : (cur_ms > 0 ? "RESUME" : "START");

  if (sw.running || cur_ms > 0) {
    ui::pill_button(it, ctx.cx - scale_x(132), ctx.cx - scale_x(10), btn_y, 23,
                    start_brd, start_bg, start_lbl, ctx.col_white, ctx.font_btn_bold,
                    WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
    ui::pill_button(it, ctx.cx + scale_x(10), ctx.cx + scale_x(132), btn_y, 23,
                    sw.running ? ctx.col_gold : ui::chrome_card_brd_on(),
                    ui::chrome_card_bg_on(),
                    sw.running ? "LAP" : "RESET",
                    sw.running ? ctx.col_gold : ctx.col_slate, ctx.font_btn_bold,
                    WidgetId::SW_RESET_BTN, kHitTap | kHitFocusSlot);
  } else {
    ui::pill_button(it, ctx.cx - scale_x(82), ctx.cx + scale_x(82), btn_y, 23,
                    start_brd, start_bg, start_lbl, ctx.col_white, ctx.font_btn_bold,
                    WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y,
                     tools_direct_home() ? "< Home" : "< Tools");
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
    render_glass_circle(it, zl_x, zy, 22, kChromeGlassTint, ui::chrome_card_brd_on());
    fast_hline(it, zl_x - 7, zy, 15, (cal_map_zoom() > 10) ? ctx.col_white : ctx.col_slate);
    fast_hline(it, zl_x - 7, zy + 1, 15, (cal_map_zoom() > 10) ? ctx.col_white : ctx.col_slate);
    frame_hits().add_circle(zl_x, zy, 28, WidgetId::CAL_ZOOM_OUT_BTN,
                            kHitTap | kHitFocusCircle);

    render_glass_circle(it, zr_x, zy, 22, kChromeGlassTint, ui::chrome_card_brd_on());
    const Color zi_c = (cal_map_zoom() < 18) ? ctx.col_white : ctx.col_slate;
    fast_hline(it, zr_x - 7, zy, 15, zi_c);
    fast_hline(it, zr_x - 7, zy + 1, 15, zi_c);
    fast_vline(it, zr_x, zy - 7, 15, zi_c);
    fast_vline(it, zr_x + 1, zy - 7, 15, zi_c);
    frame_hits().add_circle(zr_x, zy, 28, WidgetId::CAL_ZOOM_IN_BTN,
                            kHitTap | kHitFocusCircle);

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
      render_card_box(it, card_x, ym - 22, card_w, 44,
                         ui::chrome_card_bg_on(), ui::chrome_card_brd_on(), 14);
      fast_print(it, card_x + 18, ym, ctx.font_small, ctx.col_white,
                 TextAlign::CENTER_LEFT, kQuickLabels[i]);
      fast_print(it, card_x + card_w - 16, ym, ctx.font_small, ctx.col_gold,
                 TextAlign::CENTER_RIGHT, "+");
      frame_hits().add_rect(card_x, ym - 22, card_w, 44,
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
    ctx.now.strftime(clk_buf, sizeof(clk_buf), st.is_24h_format ? "%H:%M" : "%I:%M");
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
