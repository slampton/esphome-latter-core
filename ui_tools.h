#pragma once

// ==============================================================================
// Latter Core UI Module: 12-Tool Suite & Global Alert/Notification Overlays (ui_tools.h)
// ==============================================================================

#include "ui_layout.h"
#include "ui_ambient_va.h"

namespace pocket_ui {

inline bool &tools_direct_home() {
  static bool direct = false;
  return direct;
}

// ============================================================================
// SECTION 13: TOOLS SUITE PART 1 — LAUNCHER, STOPWATCH, DICE, LEVEL & COIN FLIP
// ============================================================================

template<typename DisplayT, typename FontSmallT>
inline void render_tool_launcher_card(DisplayT &it, int cx_card, int cy_card,
                                      int app_id, const char *label,
                                      FontSmallT *font_small,
                                      esphome::Color theme_secondary,
                                      esphome::Color col_white) {
  esphome::Color card_bg(22, 27, 36);
  esphome::Color card_border(48, 56, 70);
  // Sized for 2x3 (6-card) grid: w = 122, h = 86, r = 14 (19px vertical gap across y = 88..384!)
  render_modern_card(it, cx_card, cy_card, 122, 86, 14, card_bg, card_border, nullptr, col_white, font_small);

  int ix = cx_card;
  int iy = cy_card - 13;

  if (app_id == 1) {
    // 1. Timers: 2px-stroke hourglass with solid filled upper/lower sand bulbs (SF Symbol hourglass)
    it.filled_rectangle(ix - 11, iy - 14, 23, 2, col_white);
    it.filled_rectangle(ix - 11, iy + 13, 23, 2, col_white);
    // 2px tapered glass walls
    it.line(ix - 9, iy - 12, ix - 2, iy, col_white);
    it.line(ix - 8, iy - 12, ix - 1, iy, col_white);
    it.line(ix + 9, iy - 12, ix + 2, iy, col_white);
    it.line(ix + 8, iy - 12, ix + 1, iy, col_white);
    it.line(ix - 2, iy, ix - 9, iy + 12, col_white);
    it.line(ix - 1, iy, ix - 8, iy + 12, col_white);
    it.line(ix + 2, iy, ix + 9, iy + 12, col_white);
    it.line(ix + 1, iy, ix + 8, iy + 12, col_white);
    // Solid filled upper sand cone, falling stream & solid lower sand mound
    it.filled_triangle(ix - 5, iy - 7, ix + 5, iy - 7, ix, iy - 2, col_white);
    it.line(ix, iy - 2, ix, iy + 4, theme_secondary);
    it.filled_triangle(ix - 6, iy + 11, ix + 6, iy + 11, ix, iy + 4, col_white);

  } else if (app_id == 2) {
    // 2. Alarms: Twin solid filled bells, 2px clock bezel, angled legs & 2px hands (alarm.fill style)
    // Twin filled bells at upper-left & upper-right (carved cleanly by outer bezel gap)
    it.filled_circle(ix - 10, iy - 9, 4, col_white);
    it.filled_circle(ix + 10, iy - 9, 4, col_white);
    it.filled_circle(ix, iy + 2, 14, card_bg);
    // 2px clock case ring
    it.circle(ix, iy + 2, 12, col_white);
    it.circle(ix, iy + 2, 11, col_white);
    // 2px bottom legs
    it.line(ix - 8, iy + 12, ix - 11, iy + 16, col_white);
    it.line(ix - 7, iy + 12, ix - 10, iy + 16, col_white);
    it.line(ix + 8, iy + 12, ix + 11, iy + 16, col_white);
    it.line(ix + 7, iy + 12, ix + 10, iy + 16, col_white);
    // 2px hands + center cap
    it.line(ix,     iy + 2, ix,     iy - 5, col_white);
    it.line(ix + 1, iy + 2, ix + 1, iy - 5, col_white);
    it.line(ix,     iy + 2, ix + 5, iy + 5, col_white);
    it.line(ix,     iy + 3, ix + 5, iy + 6, col_white);
    it.filled_circle(ix, iy + 2, 2, col_white);

  } else if (app_id == 3) {
    // 3. Stopwatch: 2px chronometer bezel, bold top crown, angled pusher & 2px sweep hand
    // Top T-crown & angled 2 o'clock pusher
    it.filled_rectangle(ix - 4, iy - 16, 9, 2, col_white);
    it.filled_rectangle(ix - 1, iy - 14, 3, 4, col_white);
    it.line(ix + 9,  iy - 8, ix + 13, iy - 12, col_white);
    it.line(ix + 10, iy - 7, ix + 14, iy - 11, col_white);
    // 2px dial ring
    it.circle(ix, iy + 2, 12, col_white);
    it.circle(ix, iy + 2, 11, col_white);
    // 4 inner cardinal ticks
    it.line(ix, iy - 8, ix, iy - 6, theme_secondary);
    it.line(ix, iy + 10, ix, iy + 12, theme_secondary);
    it.line(ix - 10, iy + 2, ix - 8, iy + 2, theme_secondary);
    it.line(ix + 8, iy + 2, ix + 10, iy + 2, theme_secondary);
    // Center arbor & 2px diagonal chrono hand
    it.filled_circle(ix, iy + 2, 2, col_white);
    it.line(ix,     iy + 2, ix + 6, iy - 4, col_white);
    it.line(ix + 1, iy + 2, ix + 6, iy - 3, col_white);

  } else if (app_id == 4) {
    // 4. Dice Roll: Solid filled rounded cube with 5 crisp negative-space pips (die.face.5.fill)
    int d_r = 5;
    int x0 = ix - 13, y0 = iy - 13, s = 27;
    it.filled_rectangle(x0 + d_r, y0, s - 2 * d_r, s, col_white);
    it.filled_rectangle(x0, y0 + d_r, s, s - 2 * d_r, col_white);
    it.filled_circle(x0 + d_r, y0 + d_r, d_r, col_white);
    it.filled_circle(x0 + s - 1 - d_r, y0 + d_r, d_r, col_white);
    it.filled_circle(x0 + d_r, y0 + s - 1 - d_r, d_r, col_white);
    it.filled_circle(x0 + s - 1 - d_r, y0 + s - 1 - d_r, d_r, col_white);
    // 5 dark negative-space pips
    it.filled_circle(ix,     iy,     2, card_bg);
    it.filled_circle(ix - 7, iy - 7, 2, card_bg);
    it.filled_circle(ix + 7, iy - 7, 2, card_bg);
    it.filled_circle(ix - 7, iy + 7, 2, card_bg);
    it.filled_circle(ix + 7, iy + 7, 2, card_bg);

  } else if (app_id == 5) {
    // 5. Flashlight: Solid filled sculpted torch body & reflector head + 3 bold 2px light beams
    // Flared reflector head (solid filled trapezoid)
    it.filled_triangle(ix - 9, iy - 7, ix + 9, iy - 7, ix - 5, iy - 1, col_white);
    it.filled_triangle(ix + 9, iy - 7, ix - 5, iy - 1, ix + 5, iy - 1, col_white);
    // Solid filled handle barrel (separated by 2px card_bg collar groove at iy)
    it.filled_rectangle(ix - 5, iy + 1, 11, 14, col_white);
    it.filled_circle(ix, iy + 14, 5, col_white);
    // Negative-space power button slot on handle
    it.filled_rectangle(ix - 1, iy + 5, 3, 5, card_bg);
    // 3 bold 2px-thick radiating light beams
    it.line(ix,     iy - 10, ix,     iy - 16, col_white);
    it.line(ix + 1, iy - 10, ix + 1, iy - 16, col_white);
    it.line(ix - 6, iy - 10, ix - 9, iy - 15, col_white);
    it.line(ix - 5, iy - 10, ix - 8, iy - 15, col_white);
    it.line(ix + 6, iy - 10, ix + 9, iy - 15, col_white);
    it.line(ix + 5, iy - 10, ix + 8, iy - 15, col_white);

  } else if (app_id == 6) {
    // 6. Calculator: 2px rounded frame, solid filled display bar & 3x2 grid of solid keypad pills
    it.rectangle(ix - 12, iy - 15, 25, 31, col_white);
    it.rectangle(ix - 11, iy - 14, 23, 29, col_white);
    // Solid filled LCD display bar at top
    it.filled_rectangle(ix - 8, iy - 11, 17, 6, col_white);
    // 3 columns x 2 rows of solid filled keypad buttons
    for (int r = 0; r < 2; r++) {
      int ky = iy - 1 + r * 6;
      it.filled_rectangle(ix - 8, ky, 4, 3, col_white);
      it.filled_rectangle(ix - 2, ky, 4, 3, col_white);
      it.filled_rectangle(ix + 4, ky, 4, 3, col_white);
    }
    // Bottom row: standard key + wide [=] bar
    it.filled_rectangle(ix - 8, iy + 10, 4, 2, col_white);
    it.filled_rectangle(ix - 2, iy + 10, 10, 2, col_white);

  } else if (app_id == 7) {
    // 7. Bubble Level: 2px outer ring, 2px cardinal crosshairs, inner ring & solid center bubble
    it.circle(ix, iy, 14, col_white);
    it.circle(ix, iy, 13, col_white);
    it.circle(ix, iy, 7, theme_secondary);
    it.filled_circle(ix, iy, 4, col_white);
    // 4 bold 2px outer cardinal crosshair ticks
    it.line(ix,     iy - 17, ix,     iy - 13, col_white);
    it.line(ix + 1, iy - 17, ix + 1, iy - 13, col_white);
    it.line(ix,     iy + 13, ix,     iy + 17, col_white);
    it.line(ix + 1, iy + 13, ix + 1, iy + 17, col_white);
    it.line(ix - 17, iy,     ix - 13, iy,     col_white);
    it.line(ix - 17, iy + 1, ix - 13, iy + 1, col_white);
    it.line(ix + 13, iy,     ix + 17, iy,     col_white);
    it.line(ix + 13, iy + 1, ix + 17, iy + 1, col_white);

  } else if (app_id == 8) {
    // 8. Coin Flip: 2px outer medallion rim, inner struck ring & solid filled Home Assistant house
    it.circle(ix, iy, 15, col_white);
    it.circle(ix, iy, 14, col_white);
    it.circle(ix, iy, 11, theme_secondary);
    // Solid filled house emblem with negative-space center node/stem
    it.filled_triangle(ix - 7, iy, ix + 7, iy, ix, iy - 7, col_white);
    it.filled_rectangle(ix - 5, iy, 11, 7, col_white);
    it.filled_circle(ix, iy + 1, 2, card_bg);
    it.line(ix, iy + 3, ix, iy + 6, card_bg);
  } else if (app_id == 9) {
    // 9. Calendar: 2px rounded page frame, solid header band, twin binder rings & 3x2 date pips
    it.rectangle(ix - 13, iy - 12, 27, 26, col_white);
    it.rectangle(ix - 12, iy - 11, 25, 24, col_white);
    it.filled_rectangle(ix - 12, iy - 11, 25, 6, col_white);
    it.filled_rectangle(ix - 7, iy - 15, 3, 5, col_white);
    it.filled_rectangle(ix + 5, iy - 15, 3, 5, col_white);
    for (int r = 0; r < 2; r++) {
      for (int c = 0; c < 3; c++) {
        it.filled_rectangle(ix - 8 + c * 7, iy - 1 + r * 6, 3, 3, (r == 0 && c == 1) ? theme_secondary : col_white);
      }
    }
  } else if (app_id == 10) {
    // 10. Intercom: Solid filled megaphone horn + handle & 2 radiating acoustic wave arcs
    it.filled_rectangle(ix - 12, iy - 3, 5, 7, col_white);
    it.filled_triangle(ix - 7, iy - 3, ix - 7, iy + 3, ix + 4, iy + 10, col_white);
    it.filled_triangle(ix - 7, iy - 3, ix + 4, iy + 10, ix + 4, iy - 10, col_white);
    it.filled_rectangle(ix - 8, iy + 4, 3, 6, theme_secondary);
    it.line(ix + 8, iy - 5, ix + 10, iy, col_white);
    it.line(ix + 10, iy, ix + 8, iy + 5, col_white);
    it.line(ix + 11, iy - 8, ix + 14, iy, theme_secondary);
    it.line(ix + 14, iy, ix + 11, iy + 8, theme_secondary);
  } else if (app_id == 11) {
    // 11. Macros (Quick Actions): Sculpted High-Voltage Lightning Bolt inside 2px Badge Ring
    it.circle(ix, iy, 14, theme_secondary);
    it.filled_triangle(ix + 2, iy - 10, ix - 7, iy + 1, ix + 1, iy + 1, col_white);
    it.filled_triangle(ix - 1, iy - 1, ix + 7, iy - 1, ix - 2, iy + 10, col_white);
  } else if (app_id == 12) {
    // 12. Camera (Doorbell / Security Peephole): Sculpted Camera Body, Top Prism & Dual-Ring Lens
    it.filled_rectangle(ix - 13, iy - 8, 26, 19, col_white);
    it.filled_rectangle(ix - 5, iy - 12, 10, 4, col_white);
    it.filled_circle(ix, iy + 1, 7, card_bg);
    it.circle(ix, iy + 1, 5, theme_secondary);
    it.filled_circle(ix, iy + 1, 2, col_white);
    it.filled_circle(ix + 9, iy - 5, 1, card_bg);
  }

  it.print(cx_card, cy_card + 22, font_small, col_white,
           esphome::display::TextAlign::CENTER, label);
}

template<typename DisplayT, typename FontBigT, typename FontSmallT>
inline void render_tools_launcher(DisplayT &it, int cx, int stuff_menu_page,
                                  bool menu_scroll_active, int menu_scroll_step,
                                  int menu_scroll_dir, int menu_scroll_target_page,
                                  FontBigT *font_date, FontSmallT *font_small,
                                  esphome::Color theme_primary, esphome::Color theme_secondary,
                                  esphome::Color col_bg, esphome::Color col_black,
                                  esphome::Color col_white) {
  (void)font_date;
  // 2x3 Grid (6 cards per page):
  //   - Row 0: y_mid = 131 (y = 88..174, 30px below title at y=58)
  //   - Row 1: y_mid = 236 (y = 193..279, 19px vertical gap)
  //   - Row 2: y_mid = 341 (y = 298..384, 19px vertical gap, 31px above < Back at y=415)
  const int col_left_x  = cx - 68;
  const int col_right_x = cx + 68;
  const int row_0_y = 131;
  const int row_1_y = 236;
  const int row_2_y = 341;

  auto draw_page_cards = [&](int page_idx, int y_shift) {
    int y0 = row_0_y + y_shift;
    int y1 = row_1_y + y_shift;
    int y2 = row_2_y + y_shift;
    if (page_idx == 0) {
      if (y0 >= 75 && y0 <= 390) {
        render_tool_launcher_card(it, col_left_x,  y0, 1, "Timers",     font_small, theme_secondary, col_white);
        render_tool_launcher_card(it, col_right_x, y0, 2, "Alarms",     font_small, theme_secondary, col_white);
      }
      if (y1 >= 75 && y1 <= 390) {
        render_tool_launcher_card(it, col_left_x,  y1, 3, "Stopwatch",  font_small, theme_secondary, col_white);
        render_tool_launcher_card(it, col_right_x, y1, 7, "Level",      font_small, theme_secondary, col_white);
      }
      if (y2 >= 75 && y2 <= 390) {
        render_tool_launcher_card(it, col_left_x,  y2, 5, "Flashlight", font_small, theme_secondary, col_white);
        render_tool_launcher_card(it, col_right_x, y2, 6, "Calculator", font_small, theme_secondary, col_white);
      }
    } else {
      if (y0 >= 75 && y0 <= 390) {
        render_tool_launcher_card(it, col_left_x,  y0, 4,  "Dice Roll", font_small, theme_secondary, col_white);
        render_tool_launcher_card(it, col_right_x, y0, 8,  "Coin Flip", font_small, theme_secondary, col_white);
      }
      if (y1 >= 75 && y1 <= 390) {
        render_tool_launcher_card(it, col_left_x,  y1, 9,  "Calendar",  font_small, theme_secondary, col_white);
        render_tool_launcher_card(it, col_right_x, y1, 10, "Intercom",  font_small, theme_secondary, col_white);
      }
      if (y2 >= 75 && y2 <= 390) {
        render_tool_launcher_card(it, col_left_x,  y2, 11, "Macros",    font_small, theme_secondary, col_white);
        render_tool_launcher_card(it, col_right_x, y2, 12, "Camera",    font_small, theme_secondary, col_white);
      }
    }
  };

  if (menu_scroll_active) {
    int step = std::min(4, menu_scroll_step);
    int offset = SHARED_SCROLL_EASE_252[step];
    draw_page_cards(stuff_menu_page, -menu_scroll_dir * offset);
    draw_page_cards(menu_scroll_target_page, menu_scroll_dir * (252 - offset));
  } else {
    draw_page_cards(stuff_menu_page, 0);
  }

  // Viewport Masks (only needed during active 2-frame scroll transition!), Header & Right-Flank Scroll Chevrons
  if (menu_scroll_active) {
    it.filled_rectangle(0, 0, 466, 82, col_bg);
    it.filled_rectangle(0, 388, 466, 78, col_bg);
  }
  it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "TOOLS");
  int active_page = menu_scroll_active ? menu_scroll_target_page : stuff_menu_page;
  render_vertical_page_pips(it, active_page == 1, active_page == 0, col_bg, col_white, esphome::Color(95, 112, 134));
}

template<typename DisplayT, typename FontBigT, typename FontSmallT>
inline void render_tool_stopwatch(DisplayT &it, int cx, int cy, uint32_t now_ms,
                                  bool stopwatch_running, uint32_t stopwatch_start_time,
                                  uint32_t stopwatch_elapsed_ms, uint32_t stopwatch_lap_ms,
                                  FontBigT *font_date, FontSmallT *font_small,
                                  esphome::Color col_white, esphome::Color col_silver,
                                  esphome::Color col_slate, esphome::Color col_dark_gray,
                                  esphome::Color col_red) {
  // Dual Perimeter Rings & Inner Rehaut Flange
  it.circle(cx, cy, 230, col_white);
  it.circle(cx, cy, 228, col_silver);
  it.circle(cx, cy, 197, col_dark_gray);
  it.circle(cx, cy, 196, col_silver);

  // Precision Perimeter Chapter Ring via .rodata DIAL_SIN / DIAL_COS
  for (int s = 0; s < 60; s++) {
    float t_sin = DIAL_SIN[s];
    float t_cos = DIAL_COS[s];
    if (s % 5 == 0) {
      float perp_x = -t_cos;
      float perp_y = t_sin;
      int x1 = cx + (int)(196.0f * t_sin);
      int y1 = cy - (int)(196.0f * t_cos);
      int x2 = cx + (int)(227.0f * t_sin);
      int y2 = cy - (int)(227.0f * t_cos);
      it.line(x1 + (int)perp_x, y1 + (int)perp_y, x2 + (int)perp_x, y2 + (int)perp_y, col_silver);
      it.line(x1 - (int)perp_x, y1 - (int)perp_y, x2 - (int)perp_x, y2 - (int)perp_y, col_slate);
      it.line(x1, y1, x2, y2, col_white);
    } else {
      int x1 = cx + (int)(210.0f * t_sin);
      int y1 = cy - (int)(210.0f * t_cos);
      int x2 = cx + (int)(227.0f * t_sin);
      int y2 = cy - (int)(227.0f * t_cos);
      it.line(x1, y1, x2, y2, col_slate);
      it.line(x1 + 1, y1, x2 + 1, y2, col_slate);
    }
  }

  static constexpr int CHRONO_TICKS[11] = {0, 5, 10, 15, 20, 25, 35, 40, 45, 50, 55};
  static constexpr const char *CHRONO_STRS[11] = {"0", "5", "10", "15", "20", "25", "35", "40", "45", "50", "55"};
  for (int i = 0; i < 11; i++) {
    int t_idx = CHRONO_TICKS[i];
    int nx = cx + static_cast<int>(172.0f * DIAL_SIN[t_idx]);
    int ny = cy - static_cast<int>(172.0f * DIAL_COS[t_idx]);
    it.print(nx, ny, font_small, col_white, esphome::display::TextAlign::CENTER, CHRONO_STRS[i]);
  }

  uint32_t total_ms = stopwatch_running ? (now_ms - stopwatch_start_time) : stopwatch_elapsed_ms;
  int mins = (total_ms / 60000U) % 60U;
  int secs = (total_ms / 1000U) % 60U;
  int tenths = (total_ms / 100U) % 10U;

  it.print(cx, 132, font_small, col_silver, esphome::display::TextAlign::CENTER, "CHRONOGRAPH");

  char main_buf[32];
  snprintf(main_buf, sizeof(main_buf), "%02d:%02d.%d", mins, secs, tenths);
  it.print(cx, 238, font_date, col_white, esphome::display::TextAlign::CENTER, main_buf);

  if (stopwatch_lap_ms > 0) {
    int l_mins = (stopwatch_lap_ms / 60000U) % 60U;
    int l_secs = (stopwatch_lap_ms / 1000U) % 60U;
    int l_tenths = (stopwatch_lap_ms / 100U) % 10U;
    char lap_buf[32];
    snprintf(lap_buf, sizeof(lap_buf), "SPLIT  %02d:%02d.%d", l_mins, l_secs, l_tenths);
    it.print(cx, 274, font_small, col_red, esphome::display::TextAlign::CENTER, lap_buf);
  }

  const int btn_y = cy + 99;
  const int btn_lap_x = cx - 57;
  const int btn_rst_x = cx + 57;
  const int btn_r = 36;

  it.filled_circle(btn_lap_x, btn_y, btn_r - 2, col_dark_gray);
  it.circle(btn_lap_x, btn_y, btn_r, col_silver);
  it.circle(btn_lap_x, btn_y, btn_r - 1, col_slate);
  it.circle(btn_lap_x, btn_y, btn_r - 4, (stopwatch_lap_ms > 0) ? col_red : col_slate);
  it.print(btn_lap_x, btn_y, font_small, col_silver, esphome::display::TextAlign::CENTER, "LAP");

  it.filled_circle(btn_rst_x, btn_y, btn_r - 2, col_dark_gray);
  it.circle(btn_rst_x, btn_y, btn_r, col_silver);
  it.circle(btn_rst_x, btn_y, btn_r - 1, col_slate);
  it.circle(btn_rst_x, btn_y, btn_r - 4, col_slate);
  it.print(btn_rst_x, btn_y, font_small, col_silver, esphome::display::TextAlign::CENTER, "RESET");

  // Split-Second (Rattrapante) Ghost Hand via fast_sincos
  if (stopwatch_lap_ms > 0) {
    float lap_angle = ((stopwatch_lap_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
    float l_sin = 0.0f, l_cos = 1.0f;
    fast_sincos(lap_angle, l_sin, l_cos);
    it.line(cx, cy, cx + (int)(154.0f * l_sin), cy - (int)(154.0f * l_cos), col_red);
    it.line(cx + 1, cy, cx + (int)(154.0f * l_sin) + 1, cy - (int)(154.0f * l_cos), col_red);
    it.circle(cx + (int)(158.0f * l_sin), cy - (int)(158.0f * l_cos), 4, col_red);
  }

  // Main Central Arrow Hand (20 FPS High-Beat Sweep via fast_sincos)
  float chrono_angle = ((total_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
  float s_sin = 0.0f, s_cos = 1.0f;
  fast_sincos(chrono_angle, s_sin, s_cos);

  int tip_x = cx + (int)(178.0f * s_sin);
  int tip_y = cy - (int)(178.0f * s_cos);
  int arr_base_x = cx + (int)(152.0f * s_sin);
  int arr_base_y = cy - (int)(152.0f * s_cos);
  int tail_x = cx - (int)(36.0f * s_sin);
  int tail_y = cy + (int)(36.0f * s_cos);

  float perp_x = s_cos;
  float perp_y = s_sin;
  int left_x = arr_base_x + (int)(8.0f * perp_x);
  int left_y = arr_base_y + (int)(8.0f * perp_y);
  int right_x = arr_base_x - (int)(8.0f * perp_x);
  int right_y = arr_base_y - (int)(8.0f * perp_y);

  it.line(tail_x + (int)perp_x, tail_y + (int)perp_y, arr_base_x + (int)perp_x, arr_base_y + (int)perp_y, col_silver);
  it.line(tail_x - (int)perp_x, tail_y - (int)perp_y, arr_base_x - (int)perp_x, arr_base_y - (int)perp_y, col_slate);
  it.line(tail_x, tail_y, arr_base_x, arr_base_y, col_white);

  it.filled_triangle(tip_x, tip_y, left_x, left_y, right_x, right_y, col_white);
  it.line(tip_x, tip_y, left_x, left_y, col_silver);
  it.line(tip_x, tip_y, right_x, right_y, col_slate);
  it.line(left_x, left_y, right_x, right_y, col_silver);

  it.filled_circle(tail_x, tail_y, 8, col_dark_gray);
  it.circle(tail_x, tail_y, 8, col_silver);
  it.filled_circle(tail_x, tail_y, 3, col_silver);

  it.filled_circle(cx, cy, 9, col_silver);
  it.circle(cx, cy, 9, col_slate);
  it.filled_circle(cx, cy, 6, col_dark_gray);
  it.filled_circle(cx, cy, 2, col_white);

  it.print(cx, 408, font_small, col_slate, esphome::display::TextAlign::CENTER, "< Back");
}

template<typename DisplayT, typename FontBigT, typename FontSmallT>
inline void render_tool_dice_roll(DisplayT &it, int cx, uint32_t now_ms,
                                  bool &dice_anim_active, uint32_t dice_anim_start_ms,
                                  int dice_val_d6, int dice_val_d6_2,
                                  FontBigT *font_date, FontSmallT *font_small,
                                  esphome::Color theme_primary, esphome::Color theme_secondary,
                                  esphome::Color col_white, esphome::Color col_dark_gray) {
  int cur_val_1 = dice_val_d6;
  int cur_val_2 = dice_val_d6_2;
  int hop_y1 = 0, hop_y2 = 0;
  int jostle_x1 = 0, jostle_x2 = 0;
  int shadow_extra = 0;
  float anim_p = 1.0f;

  if (dice_anim_active) {
    uint32_t elapsed = now_ms - dice_anim_start_ms;
    const uint32_t roll_dur = 680U;
    if (elapsed >= roll_dur) {
      dice_anim_active = false;
    } else {
      anim_p = static_cast<float>(elapsed) / static_cast<float>(roll_dur);
      float damp = 1.0f - anim_p;
      hop_y1 = -static_cast<int>(std::abs(fast_sin(anim_p * PI_F * 2.2f)) * 34.0f * damp);
      hop_y2 = -static_cast<int>(std::abs(fast_sin(anim_p * PI_F * 2.2f + 0.7f)) * 32.0f * damp);
      jostle_x1 = static_cast<int>(fast_sin(anim_p * PI_F * 6.0f) * 8.0f * damp);
      jostle_x2 = -static_cast<int>(fast_cos(anim_p * PI_F * 6.0f) * 8.0f * damp);
      shadow_extra = static_cast<int>(6.0f * damp);

      uint32_t step = elapsed / 55U;
      cur_val_1 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step), 17) * 60.0f) % 6);
      cur_val_2 = 1 + (static_cast<int>(hash2d_01(static_cast<int>(step) + 31, 53) * 60.0f) % 6);
    }
  }

  const int dice_base_y = 192;
  const int d1_cx = cx - 75 + jostle_x1;
  const int d2_cx = cx + 75 + jostle_x2;
  const int d1_cy = dice_base_y + hop_y1;
  const int d2_cy = dice_base_y + hop_y2;
  const int d_size = 132;
  const int cr = 16;

  // Opaque Back-Face Culled 3D Airborne Tumbling Cube Renderer (Zero see-through rear lines!)
  auto draw_tumbling_cube_3d = [&](int center_x, int center_y, float rx, float ry, float rz) {
    static constexpr float CUBE_V[8][3] = {
      {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
      {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
    };
    // 6 outward-oriented faces (CCW when viewed from outside the cube)
    static constexpr uint8_t CUBE_F[6][4] = {
      {0, 1, 2, 3}, // Face 0 (-Z): 1 pip
      {5, 4, 7, 6}, // Face 1 (+Z): 6 pips
      {4, 5, 1, 0}, // Face 2 (-Y): 2 pips
      {3, 2, 6, 7}, // Face 3 (+Y): 5 pips
      {4, 0, 3, 7}, // Face 4 (-X): 3 pips
      {1, 5, 6, 2}  // Face 5 (+X): 4 pips
    };

    const float half_s = 46.0f;
    ProjectedPoint pts[8];
    Vec3 rv[8];
    for (int i = 0; i < 8; i++) {
      rv[i] = rotate_3d_xyz({CUBE_V[i][0] * half_s, CUBE_V[i][1] * half_s, CUBE_V[i][2] * half_s}, rx, ry, rz);
      pts[i] = project_3d(rv[i], center_x, center_y, 260.0f, 280.0f);
    }

    // Soft floor shadow beneath airborne cube
    it.filled_circle(center_x + 6, center_y + 18 - hop_y1 / 2, 40, esphome::Color(16, 20, 28));

    esphome::Color c_border(158, 150, 138);
    esphome::Color c_pip(25, 28, 35);

    // Render only front-facing cube faces (screen-space cross > 0 -> 100% solid, opaque cube)
    for (int f = 0; f < 6; f++) {
      uint8_t i0 = CUBE_F[f][0], i1 = CUBE_F[f][1], i2 = CUBE_F[f][2], i3 = CUBE_F[f][3];
      int cross = (pts[i1].x - pts[i0].x) * (pts[i2].y - pts[i0].y) -
                  (pts[i1].y - pts[i0].y) * (pts[i2].x - pts[i0].x);
      if (cross <= 0) continue; // Cull back-facing faces completely

      // Directional 3D shading per face based on face center Z/Y normal orientation
      float nx = (rv[i0].x + rv[i1].x + rv[i2].x + rv[i3].x) * (0.25f / half_s);
      float ny = (rv[i0].y + rv[i1].y + rv[i2].y + rv[i3].y) * (0.25f / half_s);
      float nz = (rv[i0].z + rv[i1].z + rv[i2].z + rv[i3].z) * (0.25f / half_s);
      float light = fmaxf(0.74f, fminf(1.0f, 0.84f + (-nz * 0.12f - ny * 0.10f - nx * 0.06f)));
      esphome::Color face_col(
          static_cast<uint8_t>(248.0f * light),
          static_cast<uint8_t>(244.0f * light),
          static_cast<uint8_t>(236.0f * light));

      // Fill opaque quad (2 triangles) + draw crisp perimeter bevel edges
      it.filled_triangle(pts[i0].x, pts[i0].y, pts[i1].x, pts[i1].y, pts[i2].x, pts[i2].y, face_col);
      it.filled_triangle(pts[i0].x, pts[i0].y, pts[i2].x, pts[i2].y, pts[i3].x, pts[i3].y, face_col);
      it.line(pts[i0].x, pts[i0].y, pts[i1].x, pts[i1].y, c_border);
      it.line(pts[i1].x, pts[i1].y, pts[i2].x, pts[i2].y, c_border);
      it.line(pts[i2].x, pts[i2].y, pts[i3].x, pts[i3].y, c_border);
      it.line(pts[i3].x, pts[i3].y, pts[i0].x, pts[i0].y, c_border);

      // Bilinear helper to project pips directly onto the 3D quad face (u, v in [0..1])
      auto draw_face_pip = [&](float u, float v) {
        float px = (1.0f - u) * (1.0f - v) * pts[i0].x + u * (1.0f - v) * pts[i1].x +
                   u * v * pts[i2].x + (1.0f - u) * v * pts[i3].x;
        float py = (1.0f - u) * (1.0f - v) * pts[i0].y + u * (1.0f - v) * pts[i1].y +
                   u * v * pts[i2].y + (1.0f - u) * v * pts[i3].y;
        it.filled_circle(static_cast<int>(px), static_cast<int>(py), 3, c_pip);
      };

      // Standard D6 pip patterns per face (f = 0..5 -> 1..6 pips)
      if (f == 0 || f == 4 || f == 3) { // 1, 3, 5 center pip
        draw_face_pip(0.50f, 0.50f);
      }
      if (f >= 1) { // 2, 3, 4, 5, 6 diagonal pair
        draw_face_pip(0.26f, 0.26f);
        draw_face_pip(0.74f, 0.74f);
      }
      if (f == 1 || f == 3 || f == 5) { // 4, 5, 6 second diagonal pair
        draw_face_pip(0.74f, 0.26f);
        draw_face_pip(0.26f, 0.74f);
      }
      if (f == 1) { // 6 middle pair
        draw_face_pip(0.26f, 0.50f);
        draw_face_pip(0.74f, 0.50f);
      }
    }
  };
  auto draw_rounded_d6 = [&](int center_x, int center_y, int val) {
    int x0 = center_x - d_size / 2;
    int y0 = center_y - d_size / 2;
    int sx0 = x0 + 6 + shadow_extra;
    int sy0 = y0 + 8 + shadow_extra;

    esphome::Color sh_col(16, 20, 28);
    it.filled_rectangle(sx0 + cr, sy0, d_size - 2 * cr, d_size, sh_col);
    it.filled_rectangle(sx0, sy0 + cr, d_size, d_size - 2 * cr, sh_col);
    it.filled_circle(sx0 + cr, sy0 + cr, cr, sh_col);
    it.filled_circle(sx0 + d_size - cr, sy0 + cr, cr, sh_col);
    it.filled_circle(sx0 + cr, sy0 + d_size - cr, cr, sh_col);
    it.filled_circle(sx0 + d_size - cr, sy0 + d_size - cr, cr, sh_col);

    esphome::Color body_col(248, 246, 240);
    esphome::Color border_col(182, 176, 164);
    it.filled_rectangle(x0 + cr, y0, d_size - 2 * cr, d_size, body_col);
    it.filled_rectangle(x0, y0 + cr, d_size, d_size - 2 * cr, body_col);
    it.filled_circle(x0 + cr, y0 + cr, cr, body_col);
    it.filled_circle(x0 + d_size - cr, y0 + cr, cr, body_col);
    it.filled_circle(x0 + cr, y0 + d_size - cr, cr, body_col);
    it.filled_circle(x0 + d_size - cr, y0 + d_size - cr, cr, body_col);

    it.line(x0 + cr, y0 + 1, x0 + d_size - cr, y0 + 1, col_white);
    it.line(x0 + 1, y0 + cr, x0 + 1, y0 + d_size - cr, col_white);
    it.line(x0 + cr, y0 + d_size - 1, x0 + d_size - cr, y0 + d_size - 1, border_col);
    it.line(x0 + d_size - 1, y0 + cr, x0 + d_size - 1, y0 + d_size - cr, border_col);

    const int pr = 9;
    const int p_dist = 34;
    esphome::Color pip_c(25, 28, 35);
    esphome::Color pip_rim(85, 92, 105);
    auto draw_pip = [&](int px, int py) {
      it.filled_circle(px, py, pr, pip_c);
      it.draw_pixel_at(px + 4, py + 5, pip_rim);
      it.draw_pixel_at(px + 5, py + 4, pip_rim);
      it.draw_pixel_at(px + 3, py + 6, pip_rim);
    };

    bool p_center = (val == 1 || val == 3 || val == 5);
    bool p_tl_br  = (val >= 2);
    bool p_tr_bl  = (val >= 4);
    bool p_mid_lr = (val == 6);

    if (p_center) draw_pip(center_x, center_y);
    if (p_tl_br)  { draw_pip(center_x - p_dist, center_y - p_dist); draw_pip(center_x + p_dist, center_y + p_dist); }
    if (p_tr_bl)  { draw_pip(center_x + p_dist, center_y - p_dist); draw_pip(center_x - p_dist, center_y + p_dist); }
    if (p_mid_lr) { draw_pip(center_x - p_dist, center_y); draw_pip(center_x + p_dist, center_y); }
  };

  if (dice_anim_active && anim_p < 0.80f) {
    float spin = anim_p * 7.8f;
    draw_tumbling_cube_3d(d1_cx, d1_cy, spin * 1.1f, spin * 1.4f + 0.5f, spin * 0.7f);
    draw_tumbling_cube_3d(d2_cx, d2_cy, -spin * 1.2f + 0.8f, spin * 1.0f, -spin * 0.9f);
  } else {
    draw_rounded_d6(d1_cx, d1_cy, dice_val_d6);
    draw_rounded_d6(d2_cx, d2_cy, dice_val_d6_2);
  }

  if (dice_anim_active) {
    it.print(cx, 324, font_date, theme_secondary, esphome::display::TextAlign::CENTER, "ROLLING...");
  } else {
    char total_buf[32];
    snprintf(total_buf, sizeof(total_buf), "TOTAL: %d", cur_val_1 + cur_val_2);
    it.print(cx, 324, font_date, col_white, esphome::display::TextAlign::CENTER, total_buf);
  }

  render_back_button(it, cx, font_small, 426, "< BACK");
}
template<typename DisplayT, typename FontBigT, typename FontSmallT>
inline void render_tool_bubble_level(DisplayT &it, int cx, float ax, float ay, float az,
                                     FontBigT *font_date, FontSmallT *font_small,
                                     esphome::Color theme_primary, esphome::Color theme_secondary,
                                     esphome::Color col_white, esphome::Color col_gray,
                                     esphome::Color col_slate, esphome::Color col_dark_gray,
                                     esphome::Color col_green) {
  if (std::isnan(ax)) ax = 0.0f;
  if (std::isnan(ay)) ay = 0.0f;
  if (std::isnan(az)) az = 9.8f;

  float p_rad = std::atan2(ay, std::sqrt(ax * ax + az * az));
  float r_rad = std::atan2(ax, std::sqrt(ay * ay + az * az));
  float raw_p_deg = p_rad * (180.0f / PI_F);
  float raw_r_deg = r_rad * (180.0f / PI_F);

  // 20 FPS EMA Liquid Damping Filter (eliminates 5 Hz accelerometer step-jumping)
  static float smooth_p_deg = 0.0f;
  static float smooth_r_deg = 0.0f;
  static bool init_level = false;
  if (!init_level) {
    smooth_p_deg = raw_p_deg;
    smooth_r_deg = raw_r_deg;
    init_level = true;
  } else {
    smooth_p_deg += (raw_p_deg - smooth_p_deg) * 0.28f;
    smooth_r_deg += (raw_r_deg - smooth_r_deg) * 0.28f;
  }

  float total_tilt = std::sqrt(smooth_p_deg * smooth_p_deg + smooth_r_deg * smooth_r_deg);
  bool is_level = (total_tilt <= 0.5f);
  esphome::Color ring_c = is_level ? col_green : col_slate;

  // 1. Top Total-Tilt Degree Readout at y = 40
  char deg_buf[16];
  snprintf(deg_buf, sizeof(deg_buf), "%.1f°", total_tilt);
  it.print(cx, 40, font_date, is_level ? col_green : col_white,
           esphome::display::TextAlign::CENTER, deg_buf);

  // 2. Enlarged 324px-Diameter Bulls-Eye Spirit Vial (outer_r = 162 at level_cy = 232)
  //    Spans y = 70..394 with a clean 17px buffer below top degree text (y = 53)
  //    and a 21px buffer above the bottom < Back button (y = 415).
  const int level_cy = 232;
  const int outer_r = 162;

  // 12-point 30-degree radial bezel ticks via .rodata DIAL_SIN / DIAL_COS
  for (int i = 0; i < 60; i += 5) {
    int x1 = cx + (int)(DIAL_SIN[i] * (float)(outer_r - 8));
    int y1 = level_cy - (int)(DIAL_COS[i] * (float)(outer_r - 8));
    int x2 = cx + (int)(DIAL_SIN[i] * (float)outer_r);
    int y2 = level_cy - (int)(DIAL_COS[i] * (float)outer_r);
    it.line(x1, y1, x2, y2, col_gray);
  }

  it.circle(cx, level_cy, outer_r, col_slate);
  it.circle(cx, level_cy, outer_r - 1, col_dark_gray);
  it.circle(cx, level_cy, 82, col_gray);
  it.circle(cx, level_cy, 30, ring_c);
  it.circle(cx, level_cy, 29, ring_c);

  // Cardinal Crosshairs
  it.line(cx - 168, level_cy, cx - 36, level_cy, ring_c);
  it.line(cx + 36, level_cy, cx + 168, level_cy, ring_c);
  it.line(cx, level_cy - 168, cx, level_cy - 36, ring_c);
  it.line(cx, level_cy + 36, cx, level_cy + 168, ring_c);
  it.filled_circle(cx, level_cy, 2, ring_c);

  // Spirit Level Bubble Position (clamped inside outer vial r = 138)
  float fbx = (float)cx - smooth_r_deg * 15.0f;
  float fby = (float)level_cy - smooth_p_deg * 15.0f;
  float bdx = fbx - (float)cx;
  float bdy = fby - (float)level_cy;
  float b_dist_sq = bdx * bdx + bdy * bdy;
  if (b_dist_sq > 138.0f * 138.0f) {
    float b_dist = std::sqrt(b_dist_sq);
    fbx = (float)cx + (bdx / b_dist) * 138.0f;
    fby = (float)level_cy + (bdy / b_dist) * 138.0f;
  }
  int bx = (int)fbx;
  int by = (int)fby;

  // 3D Glass Spirit Bubble (r = 22: Shadow, Meniscus Rim, Dual Specular Highlights)
  it.filled_circle(bx + 2, by + 3, 22, esphome::Color(12, 16, 22));
  esphome::Color bub_fill = is_level ? col_green : theme_secondary;
  it.filled_circle(bx, by, 22, bub_fill);
  it.circle(bx, by, 22, col_white);
  it.circle(bx, by, 20, col_white);
  it.filled_circle(bx - 7, by - 7, 4, col_white);
  it.filled_circle(bx + 8, by + 8, 2, col_white);

  render_back_button(it, cx, font_small, 426, "< BACK");
}

template<typename DisplayT, typename FontBigT, typename FontSmallT>
inline void render_tool_coin_flip(DisplayT &it, int cx, uint32_t now_ms,
                                  bool &coin_anim_active, uint32_t coin_anim_start_ms,
                                  int coin_side,
                                  FontBigT *font_date, FontSmallT *font_small,
                                  esphome::Color theme_primary, esphome::Color /*theme_secondary*/,
                                  esphome::Color /*col_white*/, esphome::Color col_dark_gray) {
  (void)now_ms;
  static uint32_t last_start_ms = 0;
  static int flip_frame = 0;
  static int start_side = 0;

  float scale_y = 1.0f;
  float y_hop = 0.0f;
  float z_zoom = 1.0f;
  float theta = (coin_side == 0) ? 0.0f : PI_F;
  bool show_heads = (coin_side == 0);
  bool is_edge_flash = false;

  if (coin_anim_active) {
    if (coin_anim_start_ms != last_start_ms) {
      last_start_ms = coin_anim_start_ms;
      flip_frame = 0;
      // Always start opposite to final side if needed so the toss visibly turns over 4 or 5 times!
      start_side = (coin_side == 0) ? 0 : 1;
    }

    // 11 guaranteed rendered frames per toss (~31% shorter duration, faster spin cadence):
    // Packs 4 razor-thin milled-edge flashes and 4 full face reversals into 11 crisp frames!
    static constexpr int TOTAL_FRAMES = 11;
    if (flip_frame >= TOTAL_FRAMES) {
      coin_anim_active = false;
      scale_y = 1.0f;
      y_hop = 0.0f;
      z_zoom = 1.0f;
      show_heads = (coin_side == 0);
    } else {
      static constexpr float STEP_SCALES[11] = {
        0.84f, 0.06f, 0.56f, 0.90f, 0.06f, 0.58f, 0.90f, 0.06f, 0.86f, 0.06f, 0.58f
      };
      // 0 = coin_side, 1 = opposite side (alternates on every edge crossing at frames 1, 4, 7, 9)
      static constexpr uint8_t STEP_OPP[11] = {
        0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0
      };

      float p = static_cast<float>(flip_frame + 1) / static_cast<float>(TOTAL_FRAMES + 1);
      float arc = fast_sin(p * PI_F);
      y_hop = -68.0f * arc;
      z_zoom = 0.96f + 0.20f * arc;

      scale_y = STEP_SCALES[flip_frame];
      is_edge_flash = (scale_y < 0.10f);
      int cur_side = STEP_OPP[flip_frame] ? (1 - coin_side) : coin_side;
      show_heads = (cur_side == 0);
      theta = static_cast<float>(flip_frame) * (PI_F * 0.36f);
      flip_frame++;
    }
  } else {
    last_start_ms = 0;
  }
  const int coin_base_y = 216;
  int coin_cy = coin_base_y + static_cast<int>(y_hop);
  const int r_outer = static_cast<int>(116.0f * z_zoom);

  // 1. Dynamic 3D Floor Shadow (shrinks as coin rises toward the viewer)
  int sh_base_y = 312;
  float shadow_scale = fmaxf(0.32f, 1.0f - (-y_hop / 95.0f));
  int sh_rx = static_cast<int>(94.0f * shadow_scale);
  int sh_ry = std::max(3, static_cast<int>((16.0f + 10.0f * scale_y) * shadow_scale));
  for (int sdy = -sh_ry; sdy <= sh_ry; sdy++) {
    int lut_idx = 233 + (sdy * 232) / sh_ry;
    if (lut_idx < 0) lut_idx = 0;
    if (lut_idx > 465) lut_idx = 465;
    int sdx = (sh_rx * CIRCLE_HW[lut_idx]) / 233;
    it.horizontal_line(cx - sdx, sh_base_y + sdy, 2 * sdx + 1, esphome::Color(12, 16, 22));
  }

  // All-Silver Struck Medallion Palette (Zero Blue — Pure 3D Engraved Silver Quarter / Dollar)
  esphome::Color col_rim_dark(64, 72, 84);
  esphome::Color col_rim_mid(134, 144, 160);
  esphome::Color col_bevel_hi(248, 252, 255);
  esphome::Color col_groove_sh(82, 92, 108);
  esphome::Color col_relief_shadow(52, 60, 72);
  esphome::Color col_relief_hi(252, 254, 255);
  esphome::Color col_engrave_recess(74, 84, 98);

  float spec_center = -0.35f + 0.65f * fast_sin(theta * 2.0f);
  int b_outer = std::max(4, static_cast<int>(r_outer * scale_y));
  int edge_h = is_edge_flash ? 18 : std::max(3, static_cast<int>(15.0f * (1.0f - scale_y * 0.78f)));

  // 2. O(1) Crescent 3D Milled Silver Edge Band (prominent 18px reeded silver bar on edge-flash frames!)
  for (int dy = 1; dy <= b_outer + edge_h; dy++) {
    int src_dy = (dy > edge_h) ? (dy - edge_h) : 0;
    int lut_idx = 233 + (src_dy * 232) / b_outer;
    if (lut_idx < 0) lut_idx = 0;
    if (lut_idx > 465) lut_idx = 465;
    int dx_outer = (r_outer * CIRCLE_HW[lut_idx]) / 233;
    esphome::Color ec = (dy >= b_outer + edge_h - 1)
                            ? col_rim_dark
                            : (is_edge_flash ? esphome::Color(195, 206, 222) : col_rim_mid);
    it.horizontal_line(cx - dx_outer, coin_cy + dy, 2 * dx_outer + 1, ec);
  }

  // 3. 3D Brushed-Silver Coin Field (Recessed Satin Pewter-Silver Field so Raised Relief Pops!)
  for (int dy = -b_outer; dy <= b_outer; dy++) {
    int lut_idx = 233 + (dy * 232) / b_outer;
    if (lut_idx < 0) lut_idx = 0;
    if (lut_idx > 465) lut_idx = 465;
    int dx_outer = (r_outer * CIRCLE_HW[lut_idx]) / 233;

    float u_y = static_cast<float>(dy) / static_cast<float>(b_outer);
    float d_spec = std::fabs(u_y - spec_center);
    float spec_boost = fmaxf(0.0f, 1.0f - d_spec * 2.1f);
    float sheen = (is_edge_flash ? 215.0f : 156.0f) - u_y * 18.0f + spec_boost * 42.0f;
    uint8_t sr = static_cast<uint8_t>(fminf(248.0f, sheen * 0.96f));
    uint8_t sg = static_cast<uint8_t>(fminf(250.0f, sheen * 0.99f));
    uint8_t sb = static_cast<uint8_t>(fminf(255.0f, sheen * 1.04f));
    it.horizontal_line(cx - dx_outer, coin_cy + dy, 2 * dx_outer + 1, esphome::Color(sr, sg, sb));

    it.draw_pixel_at(cx - dx_outer, coin_cy + dy, col_rim_dark);
    it.draw_pixel_at(cx + dx_outer, coin_cy + dy, col_rim_dark);
    if (dx_outer > 4) {
      it.draw_pixel_at(cx - dx_outer + 1, coin_cy + dy, col_bevel_hi);
      it.draw_pixel_at(cx - dx_outer + 2, coin_cy + dy, col_bevel_hi);
      it.draw_pixel_at(cx + dx_outer - 1, coin_cy + dy, col_rim_mid);
      it.draw_pixel_at(cx + dx_outer - 2, coin_cy + dy, col_rim_mid);
    }
    if (dx_outer > 11 && !is_edge_flash) {
      esphome::Color dish_l = (dy < 0) ? col_groove_sh : col_bevel_hi;
      esphome::Color dish_r = (dy < 0) ? col_bevel_hi  : col_groove_sh;
      it.draw_pixel_at(cx - dx_outer + 8, coin_cy + dy, dish_l);
      it.draw_pixel_at(cx - dx_outer + 9, coin_cy + dy, dish_l);
      it.draw_pixel_at(cx + dx_outer - 8, coin_cy + dy, dish_r);
      it.draw_pixel_at(cx + dx_outer - 9, coin_cy + dy, dish_r);
    }
  }

  // Vertical milled reeding ticks along the front 3D coin thickness band
  int reed_step = is_edge_flash ? 5 : 6;
  for (int rx_off = -r_outer + 6; rx_off <= r_outer - 6; rx_off += reed_step) {
    int lut_x = 233 + (rx_off * 232) / r_outer;
    if (lut_x < 0) lut_x = 0;
    if (lut_x > 465) lut_x = 465;
    int y_rim = coin_cy + (b_outer * CIRCLE_HW[lut_x]) / 233;
    esphome::Color reed_c = ((rx_off / reed_step) & 1) ? col_bevel_hi : col_rim_dark;
    it.line(cx + rx_off, y_rim + 1, cx + rx_off, y_rim + edge_h - 1, reed_c);
  }

  if (scale_y >= 0.96f && !coin_anim_active) {
    it.circle(cx, coin_cy, r_outer, col_rim_dark);
    it.circle(cx, coin_cy, r_outer - 1, col_bevel_hi);
    it.circle(cx, coin_cy, r_outer - 8, col_groove_sh);
    it.circle(cx, coin_cy, r_outer - 9, col_bevel_hi);
  }

  // 4. Enlarged (+30%) All-Silver 3D Struck Bas-Relief Home Assistant Emblem & Quarter Lettering
  if (scale_y > 0.22f) {
    int hx = cx;
    int hy = coin_cy - static_cast<int>(18.0f * scale_y * z_zoom);
    int hw = static_cast<int>(62.0f * z_zoom);
    int y_top = hy - static_cast<int>(56.0f * scale_y * z_zoom);
    int y_eave = hy - static_cast<int>(2.0f * scale_y * z_zoom);
    int y_bot = hy + static_cast<int>(54.0f * scale_y * z_zoom);

    // Pass 1: Deep 3D struck relief shadow (+3px lower-right) & gleaming upper-left bevel (-2px)
    for (int y = y_top; y <= y_bot; y++) {
      int cur_hw = 0;
      if (y < y_eave) {
        if (y_eave > y_top) {
          cur_hw = static_cast<int>(static_cast<float>(y - y_top) / static_cast<float>(y_eave - y_top) * static_cast<float>(hw));
        }
      } else {
        int dy_b = y_bot - y;
        cur_hw = (dy_b < 5 && scale_y > 0.75f) ? (hw - (5 - dy_b)) : hw;
      }
      if (cur_hw > 0) {
        it.horizontal_line(hx - cur_hw + 2, y + 2, 2 * cur_hw + 2, col_relief_shadow);
        it.horizontal_line(hx - cur_hw - 2, y - 1, 2 * cur_hw + 1, col_relief_hi);
      }
    }

    // Pass 2: Raised Brushed-Silver Bas-Relief House Plateau
    for (int y = y_top; y <= y_bot; y++) {
      int cur_hw = 0;
      if (y < y_eave) {
        if (y_eave > y_top) {
          cur_hw = static_cast<int>(static_cast<float>(y - y_top) / static_cast<float>(y_eave - y_top) * static_cast<float>(hw));
        }
      } else {
        int dy_b = y_bot - y;
        cur_hw = (dy_b < 5 && scale_y > 0.75f) ? (hw - (5 - dy_b)) : hw;
      }
      if (cur_hw > 0) {
        float rel_u = static_cast<float>(y - y_top) / static_cast<float>(y_bot - y_top + 1);
        uint8_t rl = static_cast<uint8_t>(232.0f - rel_u * 26.0f);
        it.horizontal_line(hx - cur_hw, y, 2 * cur_hw + 1, esphome::Color(rl, static_cast<uint8_t>(rl + 2), static_cast<uint8_t>(fminf(255.0f, rl + 7.0f))));
        it.draw_pixel_at(hx - cur_hw, y, col_groove_sh);
        it.draw_pixel_at(hx + cur_hw, y, col_relief_shadow);
      }
    }

    if (show_heads) {
      int eye_dx = static_cast<int>(26.0f * z_zoom);
      int eye_y = hy + static_cast<int>(7.0f * scale_y * z_zoom);
      int eye_r = std::max(2, static_cast<int>(8.0f * scale_y * z_zoom));
      it.filled_circle(hx - eye_dx + 1, eye_y + 1, eye_r, col_relief_hi);
      it.filled_circle(hx + eye_dx + 1, eye_y + 1, eye_r, col_relief_hi);
      it.filled_circle(hx - eye_dx, eye_y, eye_r, col_relief_shadow);
      it.filled_circle(hx + eye_dx, eye_y, eye_r, col_relief_shadow);
      it.filled_circle(hx - eye_dx + 1, eye_y + 1, std::max(1, eye_r - 2), col_engrave_recess);
      it.filled_circle(hx + eye_dx + 1, eye_y + 1, std::max(1, eye_r - 2), col_engrave_recess);

      int smile_top_y = hy + static_cast<int>(22.0f * scale_y * z_zoom);
      int smile_mid_y = hy + static_cast<int>(28.0f * scale_y * z_zoom);
      int smile_bot_y = hy + static_cast<int>(40.0f * scale_y * z_zoom);
      int smile_hw = static_cast<int>(20.0f * z_zoom);

      for (int y = smile_top_y; y <= smile_mid_y; y++) {
        for (int t = -2; t <= 1; t++) {
          it.draw_pixel_at(hx - smile_hw + t + 1, y + 1, col_relief_hi);
          it.draw_pixel_at(hx + smile_hw + t + 1, y + 1, col_relief_hi);
          it.draw_pixel_at(hx - smile_hw + t, y, col_relief_shadow);
          it.draw_pixel_at(hx + smile_hw + t, y, col_relief_shadow);
        }
      }
      for (int i = 0; i <= 32; i++) {
        float sa = static_cast<float>(i) * (PI_F / 32.0f);
        int sx = hx - static_cast<int>(smile_hw * fast_cos(sa));
        int sy = smile_mid_y + static_cast<int>((smile_bot_y - smile_mid_y) * fast_sin(sa));
        for (int t = -1; t <= 1; t++) {
          it.draw_pixel_at(sx + 1, sy + t + 1, col_relief_hi);
          it.draw_pixel_at(sx, sy + t, col_relief_shadow);
          it.draw_pixel_at(sx + t, sy, col_relief_shadow);
        }
      }

      if (scale_y > 0.76f) {
        int ty = coin_cy + static_cast<int>(74.0f * scale_y * z_zoom);
        if (!coin_anim_active) {
          it.print(cx + 2, ty + 2, font_date, col_relief_shadow, esphome::display::TextAlign::CENTER, "HEADS");
          it.print(cx - 1, ty - 1, font_date, col_relief_hi,     esphome::display::TextAlign::CENTER, "HEADS");
        }
        it.print(cx, ty, font_date, esphome::Color(220, 228, 240), esphome::display::TextAlign::CENTER, "HEADS");
      }
    } else {
      int H_total = y_bot - y_top;
      int branch_x     = static_cast<int>(31.0f * z_zoom);
      int top_node_y   = y_top + static_cast<int>(0.32f * static_cast<float>(H_total));
      int right_node_x = hx + branch_x;
      int right_node_y = y_top + static_cast<int>(0.59f * static_cast<float>(H_total));
      int left_node_x  = hx - branch_x;
      int left_node_y  = y_top + static_cast<int>(0.73f * static_cast<float>(H_total));

      int node_r = std::max(3, static_cast<int>(10.0f * scale_y * z_zoom));
      int line_hw = std::max(1, static_cast<int>(3.0f * scale_y * z_zoom));
      int branch_drop = static_cast<int>(29.0f * scale_y * z_zoom);

      for (int w = -line_hw; w <= line_hw; w++) {
        it.line(hx + w + 2, top_node_y + 1, hx + w + 2, y_bot, col_relief_hi);
        it.line(hx + w + 2, right_node_y + branch_drop + 1, right_node_x + w + 2, right_node_y + 1, col_relief_hi);
        it.line(hx + w + 2, left_node_y + branch_drop + 1, left_node_x + w + 2, left_node_y + 1, col_relief_hi);
      }
      it.filled_circle(hx + 2, top_node_y + 2, node_r, col_relief_hi);
      it.filled_circle(right_node_x + 2, right_node_y + 2, node_r, col_relief_hi);
      it.filled_circle(left_node_x + 2, left_node_y + 2, node_r, col_relief_hi);

      for (int w = -line_hw; w <= line_hw; w++) {
        it.line(hx + w, top_node_y, hx + w, y_bot, col_relief_shadow);
        it.line(hx + w, right_node_y + branch_drop, right_node_x + w, right_node_y, col_relief_shadow);
        it.line(hx + w, left_node_y + branch_drop, left_node_x + w, left_node_y, col_relief_shadow);
      }
      it.filled_circle(hx, top_node_y, node_r, col_relief_shadow);
      it.filled_circle(right_node_x, right_node_y, node_r, col_relief_shadow);
      it.filled_circle(left_node_x, left_node_y, node_r, col_relief_shadow);

      if (scale_y > 0.76f) {
        int ty = coin_cy + static_cast<int>(74.0f * scale_y * z_zoom);
        if (!coin_anim_active) {
          it.print(cx + 2, ty + 2, font_date, col_relief_shadow, esphome::display::TextAlign::CENTER, "TAILS");
          it.print(cx - 1, ty - 1, font_date, col_relief_hi,     esphome::display::TextAlign::CENTER, "TAILS");
        }
        it.print(cx, ty, font_date, esphome::Color(220, 228, 240), esphome::display::TextAlign::CENTER, "TAILS");
      }
    }
  }

  render_back_button(it, cx, font_small, 426, "< BACK");
}
// ============================================================================
// SECTION 14: TOOLS SUITE PART 2 — GLOBAL ALERT OVERLAY, TIMER, ALARMS & CALC
// ============================================================================

template<typename DisplayT, typename TimeT, typename FontTimeT, typename FontDateT,
         typename FontSmallT, typename FontMicroT>
inline void render_alert_ringing_overlay(DisplayT &it, int cx, int cy, uint32_t now_ms,
                                         bool alarm_ringing, int alarm_ringing_idx,
                                         int alarm1_recurrence, int alarm2_recurrence,
                                         const TimeT &now, bool is_24h_format,
                                         FontTimeT *font_time, FontDateT *font_date,
                                         FontSmallT *font_small, FontMicroT *font_micro,
                                         esphome::Color theme_primary,
                                         esphome::Color theme_secondary,
                                         esphome::Color col_bg, esphome::Color col_black,
                                         esphome::Color col_white, esphome::Color col_gray,
                                         esphome::Color col_red,
                                         bool find_my_active = false,
                                         const std::string &va_timer_name = "") {
  it.fill(col_bg);
  uint8_t pulse = (now_ms / 250U) % 2U;
  esphome::Color alert_color = find_my_active ? col_white : (alarm_ringing ? col_red : theme_secondary);
  esphome::Color ring_c = pulse ? alert_color : (find_my_active ? theme_secondary : esphome::Color(20, 20, 20));
  it.circle(cx, cy, 223, ring_c);
  it.circle(cx, cy, 224, ring_c);
  it.circle(cx, cy, 225, ring_c);
  it.circle(cx, cy, 226, ring_c);

  if (find_my_active) {
    // High-visibility expanding radar rings for Find My Pocket Assistant
    int radar_r = 60 + static_cast<int>((now_ms / 8U) % 145U);
    it.circle(cx, cy, radar_r, theme_primary);
    it.circle(cx, cy, radar_r + 1, pulse ? col_white : theme_secondary);
    int radar_r2 = 60 + static_cast<int>(((now_ms / 8U) + 72U) % 145U);
    it.circle(cx, cy, radar_r2, theme_secondary);

    it.print(cx, 75, font_small, pulse ? col_white : theme_secondary,
             esphome::display::TextAlign::CENTER, "LOCATING DEVICE");
    it.print(cx, 156, font_date, col_white,
             esphome::display::TextAlign::CENTER, "POCKET");
    it.print(cx, 196, font_date, theme_primary,
             esphome::display::TextAlign::CENTER, "ASSISTANT");
    it.print(cx, 244, font_small, theme_secondary,
             esphome::display::TextAlign::CENTER, "Acoustic Beacon Active");

    (void)font_micro; (void)col_gray;
    render_modern_card(it, cx, 330, 240, 44, 12, esphome::Color(22, 27, 36), theme_primary,
                       "Tap to Dismiss", col_white, font_small);
  } else if (alarm_ringing) {
    it.print(cx, 75, font_small, pulse ? col_red : col_white,
             esphome::display::TextAlign::CENTER, "ALARM RINGING");

    char cur_t_buf[16] = "--:--";
    char ampm_buf[8] = "";
    if (now.is_valid()) {
      format_time_hhmm(now.hour, now.minute, is_24h_format, cur_t_buf, sizeof(cur_t_buf), ampm_buf);
    }
    it.print(cx, 160, font_time, col_white, esphome::display::TextAlign::CENTER, cur_t_buf);
    if (!is_24h_format && ampm_buf[0] != 0) {
      it.print(cx + 120, 175, font_date, theme_secondary,
               esphome::display::TextAlign::LEFT, ampm_buf);
    }

    int r_idx = (alarm_ringing_idx == 2) ? alarm2_recurrence : alarm1_recurrence;
    static constexpr const char *REC_NAMES[4] = {"Daily", "Weekdays", "Weekends", "Once"};
    it.print(cx, 225, font_small, theme_primary,
             esphome::display::TextAlign::CENTER, REC_NAMES[r_idx & 3]);

    render_modern_card(it, cx, 295, 224, 44, 12, esphome::Color(22, 27, 36), theme_secondary,
                       "Snooze (+5m)", col_white, font_small);

    render_modern_card(it, cx, 364, 204, 42, 12, esphome::Color(28, 18, 22), col_red,
                       "Dismiss", col_white, font_small);
  } else {
    it.print(cx, 75, font_small, pulse ? theme_secondary : col_white,
             esphome::display::TextAlign::CENTER, "TIMER EXPIRED");
    it.print(cx, 170, font_time, col_white, esphome::display::TextAlign::CENTER, "00:00");
    std::string t_lbl = va_timer_name.empty() ? "Time is up!" : fit_text_to_width_font(va_timer_name, 260, font_small);
    it.print(cx, 230, font_small, theme_secondary,
             esphome::display::TextAlign::CENTER, t_lbl.c_str());

    render_modern_card(it, cx, 330, 224, 44, 12, esphome::Color(22, 27, 36), theme_primary,
                       "Dismiss", col_white, font_small);
  }
}

template<typename DisplayT, typename CamImgT, typename FontDateT, typename FontSmallT>
inline void render_notification_overlay(
    DisplayT &it, int cx, int cy, uint32_t now_ms,
    const std::string &title, const std::string &message, const std::string &priority,
    CamImgT *cam_art, const std::string &cam_url, const std::string &loaded_cam_url,
    FontDateT *font_date, FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black, esphome::Color col_white,
    esphome::Color col_slate, esphome::Color col_gold, esphome::Color col_red) {
  using esphome::Color;
  using esphome::display::TextAlign;

  it.fill(col_bg);

  const bool is_crit = (priority == "critical");
  const bool is_warn = (priority == "warning");
  const bool pulse   = ((now_ms / 250U) % 2U) != 0;
  Color accent_c = is_crit ? col_red : (is_warn ? col_gold : theme_primary);

  const bool has_cam_url = (!cam_url.empty() && cam_url != "none" &&
                            cam_url != "unknown" && cam_url != "unavailable");
  const bool cam_ready   = (has_cam_url && cam_url == loaded_cam_url &&
                            cam_art != nullptr && cam_art->get_width() > 0);

  if (cam_ready) {
    // Full-bleed 466x466 circular camera snapshot with bottom metadata scrim
    render_album_art_canvas(it, cam_art, cx, false, false, false, true);
  }

  // Perimeter Priority Ring (r = 224..227)
  Color ring_c = (is_crit && !pulse) ? Color(40, 14, 18) : accent_c;
  for (int r = 224; r <= 227; r++) {
    it.circle(cx, cy, r, ring_c);
  }

  // Word-wrap message into up to 2 lines (max 280px each)
  std::string line1 = "";
  std::string line2 = "";
  std::string clean_msg = sanitize_utf8_for_font(message);
  if (!clean_msg.empty() && font_small != nullptr) {
    int w = 0, xo = 0, b = 0, th = 0;
    font_small->measure(clean_msg.c_str(), &w, &xo, &b, &th);
    if (w <= 280) {
      line1 = clean_msg;
    } else {
      size_t split_pos = std::string::npos;
      size_t space = 0;
      while ((space = clean_msg.find(' ', space)) != std::string::npos) {
        std::string sub = clean_msg.substr(0, space);
        font_small->measure(sub.c_str(), &w, &xo, &b, &th);
        if (w <= 280) {
          split_pos = space;
          space++;
        } else {
          break;
        }
      }
      if (split_pos != std::string::npos) {
        line1 = clean_msg.substr(0, split_pos);
        line2 = fit_text_to_width_font(clean_msg.substr(split_pos + 1), 280, font_small);
      } else {
        line1 = fit_text_to_width_font(clean_msg, 280, font_small);
      }
    }
  }

  std::string fit_title = fit_text_to_width_font(title.empty() ? "Notification" : title, 280, font_date);

  if (cam_ready) {
    // Camera Snapshot HUD: Top title pill + bottom scrim text + Dismiss button
    it.print(cx + 1, 53, font_date, col_black, TextAlign::CENTER, fit_title.c_str());
    it.print(cx,     52, font_date, col_white, TextAlign::CENTER, fit_title.c_str());

    if (!line1.empty()) {
      int y1 = line2.empty() ? 356 : 344;
      it.print(cx + 1, y1 + 1, font_small, col_black, TextAlign::CENTER, line1.c_str());
      it.print(cx,     y1,     font_small, col_white, TextAlign::CENTER, line1.c_str());
    }
    if (!line2.empty()) {
      it.print(cx + 1, 369, font_small, col_black, TextAlign::CENTER, line2.c_str());
      it.print(cx,     368, font_small, theme_secondary, TextAlign::CENTER, line2.c_str());
    }
    render_modern_card(it, cx, 412, 168, 36, 10, Color(22, 27, 36), accent_c,
                       "Dismiss", col_white, font_small);
  } else {
    // Text Notification / Loading Camera View
    const char *hdr_lbl = is_crit ? "CRITICAL ALERT" : (is_warn ? "WARNING" : "NOTIFICATION");
    it.print(cx, 72, font_small, accent_c, TextAlign::CENTER, hdr_lbl);

    // Bell / Camera badge icon at y = 122
    if (has_cam_url) {
      it.rectangle(cx - 14, 112, 22, 18, col_white);
      it.filled_triangle(cx + 8, 121, cx + 15, 115, cx + 15, 127, theme_secondary);
      it.circle(cx - 3, 121, 4, accent_c);
    } else {
      it.filled_circle(cx, 118, 9, accent_c);
      it.filled_rectangle(cx - 9, 118, 19, 8, accent_c);
      it.filled_rectangle(cx - 12, 126, 25, 2, col_white);
      it.filled_circle(cx, 130, 3, col_white);
    }

    it.print(cx, 168, font_date, col_white, TextAlign::CENTER, fit_title.c_str());

    if (!line1.empty()) {
      it.print(cx, line2.empty() ? 222 : 210, font_small, theme_secondary, TextAlign::CENTER, line1.c_str());
    }
    if (!line2.empty()) {
      it.print(cx, 238, font_small, theme_secondary, TextAlign::CENTER, line2.c_str());
    }
    if (has_cam_url && !cam_ready) {
      it.print(cx, 274, font_small, col_slate, TextAlign::CENTER, "Loading camera snapshot...");
    }

    render_modern_card(it, cx, 336, 216, 44, 12, Color(22, 27, 36), accent_c,
                       "Dismiss", col_white, font_small);
  }
}

// ==============================================================================
// 3-SLOT MULTI-TIMER & SAVED PRESET ENGINE
// ==============================================================================
struct MultiTimerSlot {
  std::string id;
  std::string name;
  int duration_sec;
  int remaining_sec;
  uint32_t target_ms;
  bool running;
  bool paused;
};

inline MultiTimerSlot *timer_slots() {
  static MultiTimerSlot slots[3] = {
    {"", "", 300, 300, 0, false, false},
    {"", "", 600, 600, 0, false, false},
    {"", "", 900, 900, 0, false, false}
  };
  return slots;
}

// 0 = 3-Timer Landing Page, 1..3 = Single-Timer Detail/Editor View for Slot 0..2
inline int &timer_view_slot() {
  static int idx = 0;
  return idx;
}



inline std::string format_timer_slot_label(int slot_idx) {
  const auto &s = timer_slots()[slot_idx];
  if (!s.name.empty()) return s.name;
  char buf[24];
  int mins = s.duration_sec / 60;
  int secs = s.duration_sec % 60;
  if (secs == 0) snprintf(buf, sizeof(buf), "%d Min Timer", mins);
  else snprintf(buf, sizeof(buf), "%dm %ds Timer", mins, secs);
  return std::string(buf);
}

inline std::string format_timer_header_upper(int slot_idx) {
  const auto &s = timer_slots()[slot_idx];
  if (!s.name.empty()) {
    std::string up = s.name;
    for (char &c : up) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    return up;
  }
  char buf[20];
  snprintf(buf, sizeof(buf), "TIMER %d", slot_idx + 1);
  return std::string(buf);
}

// Syncs the 3-slot pool with global summary variables (for power inhibition & HA sensors)
inline void sync_multi_timer_globals(bool &out_any_running, bool &out_any_paused,
                                     int &out_min_rem_sec, int &out_active_dur_sec,
                                     uint32_t &out_min_target_ms) {
  auto *slots = timer_slots();
  bool any_run = false;
  bool any_pau = false;
  int min_rem = 0;
  int act_dur = slots[0].duration_sec;
  uint32_t min_t = 0;

  for (int i = 0; i < 3; i++) {
    if (slots[i].running) {
      if (!any_run || slots[i].remaining_sec < min_rem) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
        min_t   = slots[i].target_ms;
      }
      any_run = true;
    } else if (slots[i].paused && !any_run) {
      if (!any_pau) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
      }
      any_pau = true;
    }
  }
  if (!any_run && !any_pau) {
    int v_idx = (timer_view_slot() >= 1 && timer_view_slot() <= 3) ? (timer_view_slot() - 1) : 0;
    min_rem = slots[v_idx].duration_sec;
    act_dur = slots[v_idx].duration_sec;
  }
  out_any_running    = any_run;
  out_any_paused     = any_pau;
  out_min_rem_sec    = min_rem;
  out_active_dur_sec = act_dur;
  out_min_target_ms  = min_t;
}

// Upserts a voice/HA timer into the 3-slot pool, rolling off the oldest idle preset if needed
inline int upsert_voice_timer_slot(const std::string &id_str, const std::string &name_str,
                                   int total_sec, int left_sec, bool is_act, uint32_t now_ms) {
  auto *slots = timer_slots();
  int target = -1;

  // 1. Match by existing UUID or same case-insensitive name
  for (int i = 0; i < 3; i++) {
    if (!id_str.empty() && slots[i].id == id_str) { target = i; break; }
    if (!name_str.empty() && !slots[i].name.empty()) {
      std::string a = slots[i].name, b = name_str;
      for (char &c : a) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
      for (char &c : b) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
      if (a == b) { target = i; break; }
    }
  }

  // 2. Otherwise insert into first non-running slot (rolling idle presets down so recent ones stay!)
  if (target < 0) {
    if (!slots[0].running && !slots[0].paused) {
      if (!slots[1].running && !slots[1].paused && !slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
        slots[1] = slots[0];
      }
      target = 0;
    } else if (!slots[1].running && !slots[1].paused) {
      if (!slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
      }
      target = 1;
    } else if (!slots[2].running && !slots[2].paused) {
      target = 2;
    } else {
      target = 2;
    }
  }

  if (total_sec <= 0) total_sec = std::max(60, left_sec);
  bool same_id = (!id_str.empty() && slots[target].id == id_str);
  slots[target].id = id_str;
  if (!name_str.empty() || !same_id) {
    slots[target].name = name_str;
  }
  slots[target].duration_sec  = total_sec;
  slots[target].remaining_sec = left_sec;
  slots[target].target_ms     = now_ms + static_cast<uint32_t>(left_sec) * 1000U;
  slots[target].running       = is_act;
  slots[target].paused        = !is_act;
  return target;
}

template<typename DisplayT, typename FontTimeT, typename FontDateT,
         typename FontBoldT, typename FontSmallT, typename FontChronoT>
inline void render_tool_timer(DisplayT &it, int cx, int cy,
                              bool /*timer_running*/, bool /*timer_paused*/,
                              int /*timer_remaining_sec*/, int /*timer_duration_sec*/,
                              FontTimeT *font_time, FontDateT *font_date,
                              FontBoldT *font_btn_bold, FontSmallT *font_small,
                              esphome::Color theme_primary, esphome::Color theme_secondary,
                              esphome::Color col_black, esphome::Color col_white,
                              esphome::Color col_slate, esphome::Color col_dark_gray,
                              esphome::Color col_red,
                              const std::string & /*va_timer_name*/,
                              FontChronoT *font_chrono_time,
                              esphome::Color col_gray) {
  auto *slots = timer_slots();
  int view_idx = timer_view_slot();

  if (view_idx == 0) {
    // -------------------------------------------------------------------------
    // TIMERS LANDING PAGE (3 Modern Rounded Timer Cards at y = 122, 222, 322)
    // -------------------------------------------------------------------------
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "TIMERS");

    static constexpr int CARD_YS[3] = {122, 222, 322};
    for (int i = 0; i < 3; i++) {
      const auto &s = slots[i];
      int y_mid = CARD_YS[i];
      bool is_act = (s.running || s.paused);

      esphome::Color card_bg  = is_act ? esphome::Color(22, 28, 38) : esphome::Color(16, 20, 26);
      esphome::Color border_c = s.running ? theme_primary : (s.paused ? theme_secondary : esphome::Color(44, 52, 66));

      render_modern_card(it, cx, y_mid, 316, 74, 16, card_bg, border_c, nullptr, col_white, font_small);

      // Left vertical status accent stripe (4px wide)
      esphome::Color bar_c = s.running ? theme_primary : (s.paused ? theme_secondary : esphome::Color(52, 62, 76));
      it.filled_rectangle(86, y_mid - 21, 4, 42, bar_c);

      // Top Row: Bold MM:SS Time
      int disp_sec = is_act ? s.remaining_sec : s.duration_sec;
      char t_buf[16];
      snprintf(t_buf, sizeof(t_buf), "%02d:%02d", disp_sec / 60, disp_sec % 60);
      it.print(100, y_mid - 2, font_chrono_time, is_act ? col_white : col_slate,
               esphome::display::TextAlign::BASELINE_LEFT, t_buf);

      // Bottom Row: Timer Name + Status ("Chicken • Running", "Pasta • Saved", "5 Min Timer")
      std::string lbl = format_timer_slot_label(i);
      if (s.running) lbl += " • Running";
      else if (s.paused) lbl += " • Paused";
      else if (!s.name.empty()) lbl += " • Saved";
      lbl = fit_text_to_width_font(lbl, 186, font_small);
      esphome::Color sub_c = s.running ? theme_primary : (s.paused ? theme_secondary : col_gray);
      it.print(102, y_mid + 18, font_small, sub_c, esphome::display::TextAlign::CENTER_LEFT, lbl.c_str());

      // Subtle vertical separator at x = 298
      it.line(298, y_mid - 22, 298, y_mid + 22, esphome::Color(42, 50, 64));

      // Right Action Button: 1-Tap Play (▶) or Pause (||) Circle at (342, y_mid)
      int bx = 342;
      it.filled_circle(bx, y_mid, 18, is_act ? theme_primary : esphome::Color(30, 36, 48));
      it.circle(bx, y_mid, 18, is_act ? col_white : theme_secondary);
      if (s.running) {
        it.filled_rectangle(bx - 5, y_mid - 6, 3, 13, col_black);
        it.filled_rectangle(bx + 2, y_mid - 6, 3, 13, col_black);
      } else {
        esphome::Color tri_c = s.paused ? col_black : theme_primary;
        it.filled_triangle(bx - 4, y_mid - 7, bx - 4, y_mid + 7, bx + 7, y_mid, tri_c);
      }
    }

    render_back_button(it, cx, font_small, 426, tools_direct_home() ? "Home" : "< Back");
    return;
  }

  // ---------------------------------------------------------------------------
  // SINGLE-TIMER DETAIL & EDITOR VIEW (view_idx == 1..3)
  // ---------------------------------------------------------------------------
  int s_idx = (view_idx >= 1 && view_idx <= 3) ? (view_idx - 1) : 0;
  const auto &s = slots[s_idx];

  // 1. Continuous 5px Outer Bezel Progress Ring when running or paused
  if (s.running || s.paused) {
    float frac = (s.duration_sec > 0)
                     ? (static_cast<float>(s.remaining_sec) / static_cast<float>(s.duration_sec))
                     : 0.0f;
    render_progress_arc_r224(it, cx, cy, frac, s.paused ? theme_secondary : theme_primary);
  }

  // 2. Dynamic Uppercase Timer Title ("CHICKEN", "PASTA", or "TIMER 1") in 21pt font_small at y = 48
  std::string hdr = fit_text_to_width_font(format_timer_header_upper(s_idx), 280, font_small);
  it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, hdr.c_str());

  int disp_sec = (s.running || s.paused) ? s.remaining_sec : s.duration_sec;
  char t_str[16];
  snprintf(t_str, sizeof(t_str), "%02d:%02d", disp_sec / 60, disp_sec % 60);
  it.print(cx, 142, font_time, col_white, esphome::display::TextAlign::CENTER, t_str);

  if (s.running) {
    it.print(cx, 192, font_small, theme_secondary, esphome::display::TextAlign::CENTER, "Running");
  } else if (s.paused) {
    it.print(cx, 192, font_small, theme_primary, esphome::display::TextAlign::CENTER, "Paused");
  } else {
    it.print(cx, 192, font_small, col_slate, esphome::display::TextAlign::CENTER, s.name.empty() ? "Ready" : "Saved Preset");
  }

  // 3. Hero START / PAUSE / RESUME Modern Card at y = 248 (matches Edit Alarm / Stepper card aesthetic!)
  const char *hero_lbl = (!s.running && !s.paused) ? "START" : (s.running ? "PAUSE" : "RESUME");
  render_modern_card(it, cx, 248, 188, 44, 12,
                     s.running ? esphome::Color(28, 36, 48) : esphome::Color(22, 27, 36),
                     s.running ? theme_secondary : theme_primary,
                     hero_lbl, col_white, font_btn_bold);

  // 4. Modern Stepper Cards (-5m, -1m, +1m, +5m) or RESET Card at y = 324
  esphome::Color card_bg(22, 27, 36);
  esphome::Color card_brd(52, 62, 78);
  if (!s.running && !s.paused) {
    const int STEP_XS[4] = {cx - 135, cx - 45, cx + 45, cx + 135};
    static constexpr const char *STEP_LBLS[4] = {"-5m", "-1m", "+1m", "+5m"};
    for (int i = 0; i < 4; i++) {
      render_modern_card(it, STEP_XS[i], 324, 68, 46, 12, card_bg, card_brd,
                         STEP_LBLS[i], col_white, font_small);
    }
  } else {
    render_modern_card(it, cx, 324, 136, 46, 12, esphome::Color(28, 20, 24), col_red,
                       "RESET", col_red, font_small);
  }

  render_back_button(it, cx, font_small, 426, "< Back");
}

template<typename DisplayT, typename TimeT, typename FontTimeT, typename FontChronoT,
         typename FontDateT, typename FontSmallT>
inline void render_tool_alarms(DisplayT &it, int cx, const TimeT &now, bool is_24h_format,
                               int alarm_edit_idx, uint32_t alarm_snooze_until,
                               int alarm_ringing_idx,
                               int alarm1_hour, int alarm1_minute, bool alarm1_enabled, int alarm1_recurrence,
                               int alarm2_hour, int alarm2_minute, bool alarm2_enabled, int alarm2_recurrence,
                               int alarm3_hour, int alarm3_minute, bool alarm3_enabled, int alarm3_recurrence,
                               FontTimeT *font_time, FontChronoT *font_chrono_time,
                               FontDateT *font_date, FontSmallT *font_small,
                               esphome::Color theme_primary, esphome::Color theme_secondary,
                               esphome::Color col_black, esphome::Color col_white,
                               esphome::Color col_gray, esphome::Color col_dark_gray,
                               esphome::Color col_gold) {
  (void)now; (void)col_gold;
  if (alarm_edit_idx == 0) {
    // -------------------------------------------------------------------------
    // ALARMS LANDING PAGE (3 Modern Rounded Alarm Cards at y = 122, 210, 298)
    // -------------------------------------------------------------------------
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "ALARMS");

    static constexpr const char *REC_LABELS[4] = {"Daily", "Weekdays", "Weekends", "Once"};

    auto draw_modern_alarm_card = [&](int y_mid, int h, int m, bool en, int rec, int idx) {
      bool is_snoozed = (alarm_snooze_until > 0 && (alarm_ringing_idx == idx || alarm_ringing_idx == 0));

      esphome::Color card_bg  = en ? esphome::Color(22, 28, 38) : esphome::Color(14, 17, 22);
      esphome::Color border_c = en ? esphome::Color(58, 70, 88) : esphome::Color(34, 40, 50);

      render_modern_card(it, cx, y_mid, 316, 74, 16, card_bg, border_c, nullptr, col_white, font_small);

      if (is_snoozed) {
        render_modern_card(it, cx, y_mid, 264, 44, 12, esphome::Color(28, 36, 48), theme_secondary,
                           "DISMISS SNOOZE", col_white, font_small);
        return;
      }

      // Left vertical status accent stripe (4px wide)
      esphome::Color bar_c = en ? theme_primary : esphome::Color(38, 44, 54);
      it.filled_rectangle(86, y_mid - 21, 4, 42, bar_c);

      // Top Row: Bold Time + AM/PM
      char t_buf[16];
      char ap_buf[8] = "";
      format_time_hhmm(h, m, is_24h_format, t_buf, sizeof(t_buf), ap_buf, true);
      esphome::Color time_col = en ? col_white : col_gray;
      it.print(100, y_mid - 2, font_chrono_time, time_col,
               esphome::display::TextAlign::BASELINE_LEFT, t_buf);
      if (!is_24h_format && ap_buf[0] != '\0') {
        int h12 = h % 12;
        if (h12 == 0) h12 = 12;
        int ap_x = (h12 >= 10) ? 214 : 190;
        it.print(ap_x, y_mid - 2, font_small, en ? theme_secondary : col_gray,
                 esphome::display::TextAlign::BASELINE_LEFT, ap_buf);
      }

      // Bottom Row: Clean Schedule Subtitle
      it.print(102, y_mid + 18, font_small, en ? theme_primary : col_gray,
               esphome::display::TextAlign::CENTER_LEFT, REC_LABELS[rec & 3]);

      // Subtle vertical separator between Edit Zone (left) and Toggle Switch (right)
      it.line(298, y_mid - 22, 298, y_mid + 22, border_c);

      // Centered Modern Toggle Switch (x = 330..356, y = y_mid)
      esphome::Color sw_bg = en ? theme_primary : esphome::Color(42, 48, 58);
      it.filled_circle(330, y_mid, 12, sw_bg);
      it.filled_circle(356, y_mid, 12, sw_bg);
      it.filled_rectangle(330, y_mid - 12, 26, 25, sw_bg);
      int knob_x = en ? 356 : 330;
      it.filled_circle(knob_x, y_mid, 9, en ? col_white : esphome::Color(180, 186, 196));
    };

    draw_modern_alarm_card(122, alarm1_hour, alarm1_minute, alarm1_enabled, alarm1_recurrence, 1);
    draw_modern_alarm_card(222, alarm2_hour, alarm2_minute, alarm2_enabled, alarm2_recurrence, 2);
    draw_modern_alarm_card(322, alarm3_hour, alarm3_minute, alarm3_enabled, alarm3_recurrence, 3);
  } else {
    it.print(cx, 48, font_small, theme_primary, esphome::display::TextAlign::CENTER, "EDIT ALARM");

    int cur_h = (alarm_edit_idx == 3) ? alarm3_hour : ((alarm_edit_idx == 2) ? alarm2_hour : alarm1_hour);
    int cur_m = (alarm_edit_idx == 3) ? alarm3_minute : ((alarm_edit_idx == 2) ? alarm2_minute : alarm1_minute);
    int cur_rec = (alarm_edit_idx == 3) ? alarm3_recurrence : ((alarm_edit_idx == 2) ? alarm2_recurrence : alarm1_recurrence);

    char t_buf[16];
    char ap_buf[8] = "";
    format_time_hhmm(cur_h, cur_m, is_24h_format, t_buf, sizeof(t_buf), ap_buf);
    it.print(cx, 142, font_time, col_white, esphome::display::TextAlign::CENTER, t_buf);
    if (!is_24h_format) {
      int h12 = cur_h % 12;
      if (h12 == 0) h12 = 12;
      int ap_x = (h12 >= 10) ? (cx + 107) : (cx + 84);
      it.print(ap_x, 151, font_small, theme_secondary, esphome::display::TextAlign::LEFT, ap_buf);
    }

    esphome::Color card_bg(22, 27, 36);
    esphome::Color card_brd(52, 62, 78);
    const int STEP_XS[4] = {cx - 135, cx - 45, cx + 45, cx + 135};
    static constexpr const char *STEP_LBLS[4] = {"-1h", "+1h", "-5m", "+5m"};
    for (int i = 0; i < 4; i++) {
      render_modern_card(it, STEP_XS[i], 236, 68, 46, 12, card_bg, card_brd,
                         STEP_LBLS[i], col_white, font_small);
    }

    static constexpr const char *REC_NAMES[4] = {
        "Repeat: Daily", "Repeat: Weekdays", "Repeat: Weekends", "Repeat: Once"};
    render_modern_card(it, cx, 316, 264, 46, 12, card_bg, card_brd,
                       REC_NAMES[cur_rec & 3], col_white, font_small);
    it.line(118, 316, 124, 310, theme_secondary);
    it.line(118, 316, 124, 322, theme_secondary);
    it.line(348, 316, 342, 310, theme_secondary);
    it.line(348, 316, 342, 322, theme_secondary);
  }

  render_back_button(it, cx, font_small, 426, "< Back");
}
template<typename DisplayT, typename FontDateT, typename FontBoldT, typename FontSmallT>
inline void render_tool_calculator(DisplayT &it, int cx, const std::string &calc_history,
                                   const std::string &calc_entry, int calc_op,
                                   bool calc_new_entry,
                                   FontDateT *font_date, FontBoldT *font_btn_bold,
                                   FontSmallT *font_small,
                                   esphome::Color theme_primary, esphome::Color col_white,
                                   esphome::Color col_slate, esphome::Color col_dark_gray) {
  (void)theme_primary; (void)col_dark_gray;
  // Raised display readout: History at y = 42, Entry at y = 74 (bottom at y = 87),
  // Divider at y = 98, and Row 0 top at y = 110 -> 23px clear buffer below output!
  if (!calc_history.empty()) {
    it.print(cx + 103, 42, font_small, col_slate, esphome::display::TextAlign::RIGHT, calc_history.c_str());
  }
  if (!calc_entry.empty()) {
    it.print(cx + 129, 74, font_date, col_white, esphome::display::TextAlign::RIGHT, calc_entry.c_str());
  }

  it.line(cx - 151, 98, cx + 151, 98, esphome::Color(50, 58, 72));

  // Expanded 5x4 keypad grid (82px horizontal stride centered on cx, 56px vertical stride)
  static constexpr int ROWS_Y[5] = {131, 187, 243, 299, 355};
  const int COLS_X[4] = {cx - 123, cx - 41, cx + 41, cx + 123};
  static constexpr const char *KEY_LABELS[5][4] = {
      {"C", "±", "%", "/"},
      {"7", "8", "9", "x"},
      {"4", "5", "6", "-"},
      {"1", "2", "3", "+"},
      {"0", ".", "DEL", "="}};

  auto draw_calc_key = [&](int kx, int ky, const char *label,
                           esphome::Color bg_c, esphome::Color text_c, esphome::Color border_c) {
    const int kw = 68;
    const int kh = 42;
    const int kr = 9;
    int x0 = kx - kw / 2;
    int y0 = ky - kh / 2;
    it.filled_rectangle(x0 + kr, y0, kw - 2 * kr, kh, bg_c);
    it.filled_rectangle(x0, y0 + kr, kw, kh - 2 * kr, bg_c);
    it.filled_circle(x0 + kr, y0 + kr, kr, bg_c);
    it.filled_circle(x0 + kw - kr, y0 + kr, kr, bg_c);
    it.filled_circle(x0 + kr, y0 + kh - kr, kr, bg_c);
    it.filled_circle(x0 + kw - kr, y0 + kh - kr, kr, bg_c);

    it.line(x0 + kr, y0, x0 + kw - kr, y0, border_c);
    it.line(x0 + kr, y0 + kh - 1, x0 + kw - kr, y0 + kh - 1, border_c);
    it.line(x0, y0 + kr, x0, y0 + kh - kr, border_c);
    it.line(x0 + kw - 1, y0 + kr, x0 + kw - 1, y0 + kh - kr, border_c);

    it.print(kx, ky, font_btn_bold, text_c, esphome::display::TextAlign::CENTER, label);
  };

  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 4; c++) {
      esphome::Color bg_c;
      esphome::Color text_c = col_white;
      esphome::Color border_c;

      if (c == 3) {
        // Highlight active pending operator (+, -, x, /) with inverted white pill & amber text
        bool op_selected = calc_new_entry &&
                           ((r == 0 && calc_op == 4) || (r == 1 && calc_op == 3) ||
                            (r == 2 && calc_op == 2) || (r == 3 && calc_op == 1));
        if (op_selected) {
          bg_c = col_white;
          border_c = esphome::Color(255, 180, 80);
          text_c = esphome::Color(230, 115, 0);
        } else {
          bg_c = esphome::Color(230, 115, 0);
          border_c = esphome::Color(255, 145, 30);
        }
      } else if (r == 0) {
        bg_c = esphome::Color(65, 75, 90);
        border_c = esphome::Color(90, 105, 125);
      } else if (r == 4 && c == 2) {
        bg_c = esphome::Color(60, 50, 60);
        border_c = esphome::Color(85, 70, 85);
      } else {
        bg_c = esphome::Color(35, 40, 52);
        border_c = esphome::Color(55, 62, 78);
      }

      draw_calc_key(COLS_X[c], ROWS_Y[r], KEY_LABELS[r][c], bg_c, text_c, border_c);
    }
  }

  render_back_button(it, cx, font_small, 426, "< BACK");
}

inline bool &cal_quick_add_open() {
  static bool open = false;
  return open;
}

// 0 = Agenda List, 1 = Event Detail Inspector, 2 = Full-Screen Location Map Snapshot
inline int &cal_detail_mode() {
  static int mode = 0;
  return mode;
}

inline std::string &cal_detail_time() {
  static std::string s;
  return s;
}

inline std::string &cal_detail_title() {
  static std::string s;
  return s;
}

inline std::string &cal_detail_location() {
  static std::string s;
  return s;
}

inline std::string &cal_detail_map_url() {
  static std::string s;
  return s;
}

inline int &cal_map_zoom() {
  static int z = 15;
  return z;
}

inline int &cal_map_pan_x() {
  static int px = 0;
  return px;
}

inline int &cal_map_pan_y() {
  static int py = 0;
  return py;
}

inline std::string &cal_feedback_text() {
  static std::string s;
  return s;
}

inline uint32_t &cal_feedback_ms() {
  static uint32_t ms = 0;
  return ms;
}

inline void parse_calendar_slot_meta(const std::string &slot_name, const std::string &slot_id,
                                     std::string &out_time, std::string &out_title,
                                     std::string &out_loc, std::string &out_map_url) {
  out_time.clear();
  out_title.clear();
  out_loc.clear();
  out_map_url.clear();

  size_t p1 = slot_id.find('|');
  if (p1 != std::string::npos) {
    size_t p2 = slot_id.find('|', p1 + 1);
    if (p2 != std::string::npos) {
      size_t p3 = slot_id.find('|', p2 + 1);
      out_time = slot_id.substr(0, p1);
      out_title = slot_id.substr(p1 + 1, p2 - p1 - 1);
      if (p3 != std::string::npos) {
        out_loc = slot_id.substr(p2 + 1, p3 - p2 - 1);
        out_map_url = slot_id.substr(p3 + 1);
        if (out_map_url == "none") out_map_url.clear();
      } else {
        out_loc = slot_id.substr(p2 + 1);
      }
      return;
    }
  }
  size_t col = slot_name.find(": ");
  if (col != std::string::npos) {
    out_time = slot_name.substr(0, col);
    out_title = slot_name.substr(col + 2);
  } else {
    out_time = "EVENT";
    out_title = slot_name;
  }
}

inline std::string url_encode_query(const std::string &raw) {
  std::string out;
  char hex[8];
  for (unsigned char c : raw) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned int>(c));
      out += hex;
    }
  }
  return out;
}

template <typename FontT>
inline int wrap_text_lines(const std::string &text, int max_w, FontT *font,
                           std::string out_lines[], int max_lines) {
  std::string clean = sanitize_utf8_for_font(text);
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
      size_t nxt_space = clean.find(' ', scan);
      size_t word_end = (nxt_space == std::string::npos) ? clean.size() : nxt_space;
      std::string cand = clean.substr(pos, word_end - pos);
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

template<typename DisplayT, typename FontSmallT, typename MapImgT = void>
inline void render_tool_calendar(
    DisplayT &it, int cx, const std::string &raw_agenda,
    int cal_page, int cal_total_pages,
    const std::string &s1_name, bool s1_act,
    const std::string &s2_name, bool s2_act,
    const std::string &s3_name, bool s3_act,
    const std::string &s4_name, bool s4_act,
    const std::string &s5_name, bool s5_act,
    FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black,
    esphome::Color col_white, esphome::Color col_slate,
    uint32_t now_ms = 0,
    MapImgT *map_art = nullptr) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const Color card_bg(22, 27, 36);
  const Color card_brd(52, 62, 78);

  if (cal_detail_mode() == 2) {
    // MODE 2: INTERACTIVE FULL-BLEED OPENSTREETMAP VIEWER (Zoom z=10..18 + Swipe-to-Pan)
    if (map_art != nullptr && map_art->get_width() > 16 && map_art->get_height() > 16) {
      int iw = map_art->get_width();
      int ih = map_art->get_height();
      it.image(cx - iw / 2, cx - ih / 2, map_art);

      // 1. Top Address & Zoom Header Pill (Tap to Re-Center on Event Pin)
      it.filled_rectangle(94, 24, 278, 34, Color(10, 14, 20));
      it.rectangle(94, 24, 278, 34, theme_primary);
      char z_prefix[16];
      snprintf(z_prefix, sizeof(z_prefix), "z%d • ", cal_map_zoom());
      std::string loc_hdr = fit_text_to_width_font(std::string(z_prefix) + cal_detail_location(), 262, font_small);
      it.print(cx, 41, font_small, col_white, TextAlign::CENTER, loc_hdr.c_str());

      // 2. Left/Right Floating Zoom Buttons (adaptive to screen width)
      const int z_by = 233;
      const int z_lx = POCKET_UI_ROUND_DISPLAY ? 44 : 34;
      const int z_rx = SCREEN_W - z_lx;
      Color btn_bg(12, 16, 24);
      Color btn_ring = (cal_map_zoom() > 10) ? theme_primary : col_slate;
      it.filled_circle(z_lx, z_by, 21, btn_bg);
      it.circle(z_lx, z_by, 21, btn_ring);
      it.circle(z_lx, z_by, 20, btn_ring);
      it.filled_rectangle(z_lx - 8, z_by - 1, 17, 3, (cal_map_zoom() > 10) ? col_white : col_slate);

      // 3. Right Floating Zoom-In (+) Button at (422, 233, r = 21)
      Color btn_ring_in = (cal_map_zoom() < 18) ? theme_primary : col_slate;
      Color fg_in = (cal_map_zoom() < 18) ? col_white : col_slate;
      it.filled_circle(z_rx, z_by, 21, btn_bg);
      it.circle(z_rx, z_by, 21, btn_ring_in);
      it.circle(z_rx, z_by, 20, btn_ring_in);
      it.filled_rectangle(z_rx - 8, z_by - 1, 17, 3, fg_in);
      it.filled_rectangle(z_rx - 1, z_by - 8, 3, 17, fg_in);

      // 4. Bottom Translucent Back Scrim
      it.filled_rectangle(148, 406, 170, 38, Color(10, 14, 20));
    } else {
      it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "EVENT MAP");
      render_modern_card(it, cx, 222, 304, 60, 14, Color(16, 20, 27), Color(44, 52, 66),
                         "Loading OpenStreetMap...", theme_secondary, font_small);
    }
    render_back_button(it, cx, font_small, 426, "< Back", col_white);
    return;
  }

  if (cal_detail_mode() == 1) {
    // MODE 1: EVENT DETAIL INSPECTOR (Wrapped Multi-Line Title + Time + Wrapped Location + View Map Button)
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "EVENT DETAILS");

    // Time Badge Pill at y = 92
    std::string fit_tm = fit_text_to_width_font(cal_detail_time(), 260, font_small);
    render_modern_card(it, cx, 92, 280, 36, 10, Color(18, 24, 34), theme_primary,
                       fit_tm.c_str(), theme_secondary, font_small);

    // Wrapped Event Title Card (y_mid = 176, h = 96, up to 3 lines)
    render_modern_card(it, cx, 176, 324, 96, 14, card_bg, card_brd, nullptr, col_white, font_small);
    std::string title_lines[3];
    int t_cnt = wrap_text_lines(cal_detail_title(), 296, font_small, title_lines, 3);
    int t_start_y = (t_cnt <= 1) ? 176 : ((t_cnt == 2) ? 163 : 150);
    for (int i = 0; i < t_cnt; i++) {
      it.print(cx, t_start_y + i * 26, font_small, col_white, TextAlign::CENTER, title_lines[i].c_str());
    }

    const bool has_loc = !cal_detail_location().empty();
    if (has_loc) {
      // Location Address Card (y_mid = 278, h = 68, up to 2 lines)
      render_modern_card(it, cx, 278, 324, 68, 14, Color(16, 20, 28), card_brd, nullptr, col_slate, font_small);
      std::string loc_lines[2];
      int l_cnt = wrap_text_lines(cal_detail_location(), 296, font_small, loc_lines, 2);
      int l_start_y = (l_cnt <= 1) ? 278 : 266;
      for (int i = 0; i < l_cnt; i++) {
        it.print(cx, l_start_y + i * 24, font_small, theme_secondary, TextAlign::CENTER, loc_lines[i].c_str());
      }

      // Prominent "View Map" Button at y = 359
      render_modern_card(it, cx, 359, 236, 44, 12, Color(24, 38, 56), theme_primary,
                         "View Map", col_white, font_small);
      // Map Pin Icon on left of button
      it.filled_circle(148, 355, 5, Color(235, 52, 62));
      it.filled_triangle(144, 357, 152, 357, 148, 365, Color(235, 52, 62));
      it.filled_circle(148, 355, 2, col_white);
    } else {
      render_modern_card(it, cx, 288, 296, 48, 12, Color(15, 18, 24), Color(34, 40, 50),
                         "No location specified", col_slate, font_small);
    }

    render_back_button(it, cx, font_small, 426, "< Back");
    return;
  }

  if (cal_quick_add_open()) {
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "ADD REMINDER");
    static constexpr int ROW_YS[5] = {110, 173, 236, 299, 362};
    static constexpr const char *kQuickLabels[5] = {
      "In 15m: Quick Reminder",
      "In 30m: Check-In / Task",
      "In 1h: Follow-Up Call",
      "In 3h: Errand / Pickup",
      "Tomorrow: Morning Reminder"
    };
    for (int i = 0; i < 5; i++) {
      int y_mid = ROW_YS[i];
      render_modern_card(it, cx, y_mid, 296, 44, 12, card_bg, card_brd, nullptr, col_white, font_small);
      it.filled_rectangle(102, y_mid - 1, 9, 3, theme_primary);
      it.filled_rectangle(105, y_mid - 4, 3, 9, theme_primary);
      it.print(120, y_mid, font_small, col_white, TextAlign::CENTER_LEFT, kQuickLabels[i]);
    }
    render_back_button(it, cx, font_small, 426, "< Cancel");
    return;
  }

  const bool show_fb = (!cal_feedback_text().empty() && now_ms > 0 && (now_ms - cal_feedback_ms() < 2200U));
  if (show_fb) {
    std::string fit_fb = fit_text_to_width_font(cal_feedback_text(), 260, font_small);
    it.print(cx, 48, font_small, theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  } else if (cal_total_pages > 1) {
    char c_hdr[36];
    snprintf(c_hdr, sizeof(c_hdr), "CALENDAR %d/%d (+)", cal_page, cal_total_pages);
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, c_hdr);
  } else {
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "CALENDAR  (+)");
  }

  std::string names[5] = {s1_name, s2_name, s3_name, s4_name, s5_name};
  bool acts[5] = {s1_act, s2_act, s3_act, s4_act, s5_act};

  // Fallback to parsing sensor.pocket_assistant_calendar_agenda ("TODAY 14:30|Dentist;...") if browse slots haven't arrived yet
  if (names[0].empty() && !raw_agenda.empty() && raw_agenda != "NONE" &&
      raw_agenda != "unknown" && raw_agenda != "unavailable") {
    size_t pos = 0;
    int idx = 0;
    while (pos < raw_agenda.size() && idx < 5) {
      size_t semi = raw_agenda.find(';', pos);
      std::string tok = (semi == std::string::npos) ? raw_agenda.substr(pos) : raw_agenda.substr(pos, semi - pos);
      pos = (semi == std::string::npos) ? raw_agenda.size() : (semi + 1);
      if (tok.empty()) continue;
      size_t bar = tok.find('|');
      if (bar != std::string::npos) {
        tok = tok.substr(0, bar) + ": " + tok.substr(bar + 1);
      }
      names[idx] = tok;
      acts[idx] = (idx == 0);
      idx++;
    }
  }

  if (names[0].empty()) {
    render_modern_card(it, cx, 222, 296, 54, 14, Color(16, 20, 27), Color(38, 46, 58),
                       "No upcoming events (Tap +)", col_slate, font_small);
  } else {
    static constexpr int ROW_YS[5] = {110, 173, 236, 299, 362};
    for (int i = 0; i < 5; i++) {
      if (names[i].empty()) continue;
      int y_mid = ROW_YS[i];
      Color brd_c = acts[i] ? theme_primary : card_brd;
      Color txt_c = acts[i] ? col_white : col_slate;
      render_modern_card(it, cx, y_mid, 296, 44, 12, card_bg, brd_c, nullptr, txt_c, font_small);
      if (acts[i]) {
        it.filled_rectangle(94, y_mid - 12, 3, 24, theme_primary);
      }
      std::string fit_ev = fit_text_to_width_font(names[i], 256, font_small);
      it.print(106, y_mid, font_small, txt_c, TextAlign::CENTER_LEFT, fit_ev.c_str());
    }
  }

  if (cal_total_pages > 1) {
    render_vertical_page_pips(it, cal_page > 1, cal_page < cal_total_pages, col_bg, theme_secondary, col_slate);
  }

  render_back_button(it, cx, font_small, 426, "< Back");
}

template<typename DisplayT, typename FontSmallT>
inline void render_tool_intercom(
    DisplayT &it, int cx, uint32_t now_ms,
    const std::string &feedback_str, uint32_t feedback_ms,
    FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_white, esphome::Color col_slate) {
  using esphome::Color;
  using esphome::display::TextAlign;
  (void)col_slate;

  const bool show_fb = (!feedback_str.empty() && (now_ms - feedback_ms < 2200U));
  if (show_fb) {
    std::string fit_fb = fit_text_to_width_font(feedback_str, 280, font_small);
    it.print(cx, 48, font_small, theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  } else {
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "INTERCOM");
  }

  const Color card_bg(22, 27, 36);
  const Color card_brd(52, 62, 78);
  static constexpr int ROW_YS[5] = {110, 173, 236, 299, 362};
  static constexpr const char *kPresets[5] = {
    "Dinner is ready!",
    "Time to head out!",
    "Kids, come downstairs!",
    "Need a hand in here!",
    "Movie time in Living Room!"
  };

  for (int i = 0; i < 5; i++) {
    int y_mid = ROW_YS[i];
    render_modern_card(it, cx, y_mid, 290, 44, 12, card_bg, card_brd, nullptr, col_white, font_small);
    // Broadcast wave icon on left
    it.filled_triangle(104, y_mid - 4, 104, y_mid + 4, 111, y_mid, theme_primary);
    it.line(114, y_mid - 5, 116, y_mid, theme_secondary);
    it.line(116, y_mid, 114, y_mid + 5, theme_secondary);
    it.print(124, y_mid, font_small, col_white, TextAlign::CENTER_LEFT, kPresets[i]);
  }

  render_back_button(it, cx, font_small, 426, "< Back");
}

inline void parse_custom_bindings_summary(
    const std::string &raw,
    std::string out_slots[5],
    std::string out_names[5],
    std::string out_actions[5]) {
  static constexpr const char *kDefaultSlots[5] = {
      "macro_1", "macro_2", "macro_3", "macro_4", "macro_5"};
  static constexpr const char *kDefaultNames[5] = {
      "Macro Slot 1", "Macro Slot 2", "Macro Slot 3", "Macro Slot 4", "Macro Slot 5"};
  for (int i = 0; i < 5; i++) {
    out_slots[i] = kDefaultSlots[i];
    out_names[i] = kDefaultNames[i];
    out_actions[i] = "toggle";
  }
  if (raw.empty() || raw == "NONE" || raw == "unknown" || raw == "unavailable") {
    return;
  }
  size_t pos = 0;
  int idx = 0;
  while (pos < raw.size() && idx < 5) {
    size_t semi = raw.find(';', pos);
    std::string tok = (semi == std::string::npos) ? raw.substr(pos) : raw.substr(pos, semi - pos);
    pos = (semi == std::string::npos) ? raw.size() : (semi + 1);
    if (tok.empty()) continue;
    size_t p1 = tok.find('|');
    if (p1 == std::string::npos) continue;
    size_t p2 = tok.find('|', p1 + 1);
    std::string s_id = tok.substr(0, p1);
    std::string s_nm = (p2 == std::string::npos) ? tok.substr(p1 + 1) : tok.substr(p1 + 1, p2 - p1 - 1);
    std::string s_ac = (p2 == std::string::npos) ? "toggle" : tok.substr(p2 + 1);
    if (!s_id.empty()) out_slots[idx] = s_id;
    if (!s_nm.empty()) out_names[idx] = s_nm;
    if (!s_ac.empty()) out_actions[idx] = s_ac;
    idx++;
  }
}

template<typename DisplayT, typename FontSmallT>
inline void render_tool_macros(
    DisplayT &it, int cx, uint32_t now_ms,
    const std::string &bindings_summary,
    const std::string &feedback_str, uint32_t feedback_ms,
    FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_white, esphome::Color col_slate) {
  using esphome::Color;
  using esphome::display::TextAlign;
  (void)col_slate;

  const bool show_fb = (!feedback_str.empty() && (now_ms - feedback_ms < 2200U));
  if (show_fb) {
    std::string fit_fb = fit_text_to_width_font(feedback_str, 280, font_small);
    it.print(cx, 48, font_small, theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  } else {
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "MACROS");
  }

  std::string slots[5], names[5], acts[5];
  parse_custom_bindings_summary(bindings_summary, slots, names, acts);

  const Color card_bg(22, 27, 36);
  const Color card_brd(52, 62, 78);
  static constexpr int ROW_YS[5] = {110, 173, 236, 299, 362};

  for (int i = 0; i < 5; i++) {
    int y_mid = ROW_YS[i];
    render_modern_card(it, cx, y_mid, 290, 44, 12, card_bg, card_brd, nullptr, col_white, font_small);
    // Lightning bolt badge on left
    it.filled_triangle(108, y_mid - 7, 101, y_mid + 1, 107, y_mid + 1, theme_primary);
    it.filled_triangle(105, y_mid - 1, 112, y_mid - 1, 104, y_mid + 7, theme_secondary);
    std::string fit_lbl = fit_text_to_width_font(names[i], 230, font_small);
    it.print(124, y_mid, font_small, col_white, TextAlign::CENTER_LEFT, fit_lbl.c_str());
  }

  render_back_button(it, cx, font_small, 426, "< Back");
}

template<typename DisplayT, typename CamImgT, typename FontSmallT>
inline void render_tool_camera(
    DisplayT &it, int cx, int cy,
    CamImgT *cam_art, const std::string &cam_url,
    FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_white, esphome::Color col_slate) {
  using esphome::Color;
  using esphome::display::TextAlign;

  bool has_url = (!cam_url.empty() && cam_url != "none" && cam_url != "unknown" && cam_url != "unavailable");
  if (has_url && cam_art != nullptr && cam_art->get_width() > 16 && cam_art->get_height() > 16) {
    int iw = cam_art->get_width();
    int ih = cam_art->get_height();
    it.image(cx - iw / 2, cy - ih / 2, cam_art);
    // Top & bottom translucent HUD bars for title + back button legibility
    it.filled_rectangle(116, 26, 234, 34, Color(10, 14, 20));
    it.rectangle(116, 26, 234, 34, theme_primary);
    it.print(cx, 43, font_small, col_white, TextAlign::CENTER, "LIVE CAMERA (TAP REFRESH)");
    it.filled_rectangle(148, 406, 170, 38, Color(10, 14, 20));
  } else {
    it.print(cx, 48, font_small, theme_primary, TextAlign::CENTER, "CAMERA");
    const char *msg = has_url ? "Loading camera snapshot..." : "Configure camera in Latter Studio";
    render_modern_card(it, cx, 222, 308, 60, 14, Color(16, 20, 27), Color(44, 52, 66),
                       msg, has_url ? theme_secondary : col_slate, font_small);
  }
  (void)col_bg;
  render_back_button(it, cx, font_small, 426, "< Back", col_white);
}

}  // namespace pocket_ui
