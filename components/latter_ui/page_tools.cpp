#include "tools_internal.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace latter_ui {

namespace {

RouteId tool_subpage_to_route(int app_id) {
#if LATTER_ROUND_DISPLAY
  if (app_id == ToolsSubpage::STOPWATCH) return RouteId::TOOLS_STOPWATCH;
#endif
  if (app_id == ToolsSubpage::DICE) return RouteId::TOOLS_DICE;
  if (app_id == ToolsSubpage::FLASHLIGHT) return RouteId::TOOLS_FLASHLIGHT;
  if (app_id == ToolsSubpage::CALCULATOR) return RouteId::TOOLS_CALCULATOR;
  if (app_id == ToolsSubpage::LEVEL) return RouteId::TOOLS_LEVEL;
  if (app_id == ToolsSubpage::COIN_FLIP) return RouteId::TOOLS_COIN_FLIP;
  return RouteId::APP_TOOLS;
}

void draw_tool_tile_icon(Rgb565Surface &it, int ix, int iy, int ir,
                         UiColor col, bool shadow, void *user_ctx) {
  const int app_id = static_cast<int>(reinterpret_cast<intptr_t>(user_ctx));
  ui::draw_route_icon(it, tool_subpage_to_route(app_id), ix, iy, ir, col, kChromeGlassTint, shadow);
}

void render_tools_launcher(Rgb565Surface &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs) {
  ToolsLauncherItemSpec items[kMaxToolsLauncherItems]{};
  const int total_items = collect_tools_launcher_items(items, device_prefs());
  const int total_pages = tools_launcher_page_count(total_items);

  if (total_items <= 0) {
    runtime_state().nav.tools_menu_page = 0;
    render_page_header(it, ctx.cx, "TOOLS", ctx.font_small, ctx.col_white);
    render_empty_state(it, ctx.cx, ctx.cy, "No tools enabled", ctx.font_small, ctx.col_slate);
    return;
  }

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
        if (item_idx >= total_items) continue;
        const int app_id = items[item_idx].id;
        if (app_id == 0) continue;
        const int cx_card = col_cx[c];
        const uint16_t wid = register_hits
                                 ? static_cast<uint16_t>(WidgetId::TOOLS_CARD_BASE + (r * 2 + c))
                                 : WidgetId::NONE;
        ui::grid_tile(it, cx_card, cy_card, eff_w, eff_h, items[item_idx].label,
                      false, false, ctx.theme_primary, ctx.col_white, ctx.font_small,
                      draw_tool_tile_icon, reinterpret_cast<void *>(static_cast<intptr_t>(app_id)),
                      wid, static_cast<int16_t>(app_id));
      }
    }
  };

  const int cur_pg = std::clamp(rs.nav.tools_menu_page, 0, total_pages - 1);
  if (rs.nav.tools_menu_page != cur_pg) {
    runtime_state().nav.tools_menu_page = cur_pg;
  }
  if (rs.nav.menu_scroll_active && total_pages > 1) {
    const int full_span = scale_y(252);
    const int offset = menu_scroll_shift(full_span, ctx.now_ms);
    const int out_dy = -rs.nav.menu_scroll_dir * offset;
    const int in_dy  = rs.nav.menu_scroll_dir * (full_span - offset);
    const int tgt_pg = std::clamp(rs.nav.menu_scroll_target_page, 0, total_pages - 1);
    draw_grid_for_page(cur_pg, out_dy, false);
    draw_grid_for_page(tgt_pg, in_dy, false);
  } else {
    const int bounce_y = edge_bounce().offset_px(
        PageId::TOOLS, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);
    draw_grid_for_page(cur_pg, bounce_y, true);
  }

  if (rs.nav.menu_scroll_active && total_pages > 1 && !backdrop_active()) {
    fill_rect_row_major(it, 0, 0, SCREEN_W, scale_y(82), ctx.col_bg);
    fill_rect_row_major(it, 0, scale_y(388), SCREEN_W, SCREEN_H - scale_y(388), ctx.col_bg);
  }

  render_page_header(it, ctx.cx, "TOOLS", ctx.font_small, ctx.col_white);
  const int active_pip_pg = (rs.nav.menu_scroll_active && total_pages > 1)
                                ? std::clamp(rs.nav.menu_scroll_target_page, 0, total_pages - 1)
                                : cur_pg;
  render_vertical_page_pips(it, active_pip_pg > 0, (active_pip_pg + 1) < total_pages,
                            ctx.col_bg, ctx.col_white, kChromePipInactive);
}

#if LATTER_ROUND_DISPLAY
void render_stopwatch_subpage(Rgb565Surface &it, const UiFrameContext &ctx,
                              const StopwatchRuntimeState &sw) {
  const uint32_t total_ms = sw.running ? (ctx.now_ms - sw.start_time_ms) : sw.elapsed_ms;
  const int mins   = static_cast<int>((total_ms / 60000U) % 60U);
  const int secs   = static_cast<int>((total_ms / 1000U) % 60U);
  const int tenths = static_cast<int>((total_ms / 100U) % 10U);

  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const float cx_f = static_cast<float>(cx) + 0.5f;
  const float cy_f = static_cast<float>(cy) + 0.5f;

  fast_circle(it, cx, cy, scale_min(230), ctx.col_white);
  fast_circle(it, cx, cy, scale_min(228), ctx.theme_secondary);
  ui::render_analog_chapter_ring(it, cx, cy, scale_min(227),
                                 ctx.theme_secondary, ctx.col_white, ctx.col_slate, false);
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

  if (sw.lap_ms > 0) {
    const float lap_angle = (static_cast<float>(sw.lap_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
    const float l_sin = std::sin(lap_angle);
    const float l_cos = std::cos(lap_angle);
    ui::draw_aa_line(it, cx_f, cy_f,
                     cx_f + scale_min_f(154.0f) * l_sin,
                     cy_f - scale_min_f(154.0f) * l_cos,
                     std::max(1.8f, scale_min_f(2.0f)), ctx.col_red);
    ui::draw_aa_ring(it,
                     cx_f + scale_min_f(158.0f) * l_sin,
                     cy_f - scale_min_f(158.0f) * l_cos,
                     scale_min_f(4.0f), std::max(1.4f, scale_min_f(1.6f)), ctx.col_red);
  }

  const float chrono_angle = (static_cast<float>(total_ms % 60000U) / 60000.0f) * (2.0f * PI_F);
  const float s_sin = std::sin(chrono_angle);
  const float s_cos = std::cos(chrono_angle);
  const float tail_len = scale_min_f(36.0f);
  const float tip_len  = scale_min_f(178.0f);
  const float tail_xf  = cx_f - tail_len * s_sin;
  const float tail_yf  = cy_f + tail_len * s_cos;
  const float tip_xf   = cx_f + tip_len * s_sin;
  const float tip_yf   = cy_f - tip_len * s_cos;
  const float needle_w = std::max(2.0f, scale_min_f(2.2f));
  const float cw_outer = scale_min_f(7.0f);
  const float cw_inner = scale_min_f(5.6f);
  const float cw_pip   = scale_min_f(3.0f);

  ui::draw_aa_line(it, tail_xf + 1.0f, tail_yf + 1.2f, tip_xf + 1.0f, tip_yf + 1.2f,
                   needle_w, kChromeShadow, 96);
  ui::draw_aa_disc(it, tail_xf + 1.0f, tail_yf + 1.2f, cw_outer, kChromeShadow, 96);
  ui::draw_aa_line(it, tail_xf, tail_yf, tip_xf, tip_yf, needle_w, ctx.col_white);
  ui::draw_aa_disc(it, tail_xf, tail_yf, cw_outer, ctx.theme_secondary);
  ui::draw_aa_disc(it, tail_xf, tail_yf, cw_inner, ctx.col_dark_gray);
  ui::draw_aa_disc(it, tail_xf, tail_yf, cw_pip, ctx.theme_secondary);

  ui::render_center_arbor_aa(it, cx, cy, ctx.theme_secondary, ctx.col_dark_gray, ctx.col_white);

  const int sw_min_r = (layout().min_hit_px() + 1) / 2;
  const int sw_pad = std::max(8, sw_min_r - btn_r);
  frame_hits().add_rect(scale_min(40), scale_min(69),
                        SCREEN_W - 2 * scale_min(40), scale_min(210),
                        WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_circle(btn_lap_x, btn_y, btn_r,
                          WidgetId::SW_LAP_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, sw_pad);
  frame_hits().add_circle(btn_rst_x, btn_y, btn_r,
                          WidgetId::SW_RESET_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, sw_pad);

  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back", ctx.col_white);
}
#endif

void render_level_subpage(Rgb565Surface &it, const UiFrameContext &ctx,
                          const DeviceRuntimeState &rs) {
  render_page_header(it, ctx.cx, "LEVEL", ctx.font_small, ctx.col_white);

  const float roll_deg = rs.hw_imu_roll;
  const float pitch_deg = rs.hw_imu_pitch;
  if (std::isnan(roll_deg) || std::isnan(pitch_deg) || std::isnan(rs.hw_accel_z)) {
    render_empty_state(it, ctx.cx, ctx.cy, "Waiting for the motion sensor...", ctx.font_small, ctx.col_slate);
    render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  const float r_rad = roll_deg * (PI_F / 180.0f);
  const float p_rad = pitch_deg * (PI_F / 180.0f);
  const float sin_x = std::clamp(std::sin(r_rad) * std::cos(p_rad), -1.0f, 1.0f);
  const float raw_x_deg = -std::asin(sin_x) * (180.0f / PI_F);
  const float raw_y_deg = -pitch_deg;

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
  const UiColor ring_c = is_level ? ctx.col_green : ctx.col_slate;

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
  render_sub_header(it, ctx.cx, deg_buf, ctx.font_small,
                    is_level ? ctx.col_green : ctx.col_white);

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

  fast_filled_circle(it, bx + 2, by + 3, bub_r, kChromePauseGlyph);
  const UiColor bub_fill = is_level ? ctx.col_green : ctx.theme_secondary;
  fast_filled_circle(it, bx, by, bub_r, bub_fill);
  fast_circle(it, bx, by, bub_r, ctx.col_white);
  fast_circle(it, bx, by, bub_r - 2, ctx.col_white);
  fast_filled_circle(it, bx - bub_r / 3, by - bub_r / 3, std::max(2, scale_min(4)), ctx.col_white);
  fast_filled_circle(it, bx + bub_r / 3, by + bub_r / 3, std::max(1, scale_min(2)), ctx.col_white);

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
}

}  // namespace

void render_tools_page(Rgb565Surface &it, const UiFrameContext &ctx) {
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
    case ToolsSubpage::LAUNCHER:
    default:
      render_tools_launcher(it, ctx, rs);
      break;
  }
}

}  // namespace latter_ui
