#include "esphome/core/defines.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

void draw_rem_power_icon(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow, void *) {
  ui::draw_icon_power(it, cx, cy, r, col, shadow);
}

void draw_rem_home_icon(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow, void *) {
  ui::draw_icon_home(it, cx, cy, r, col, shadow);
}

void draw_rem_plus_icon(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow, void *) {
  ui::draw_icon_plus(it, cx, cy, r, col, shadow);
}

void draw_rem_back_icon(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow, void *) {
  ui::draw_icon_chevron(it, cx, cy, r, false, col, shadow);
}

void draw_rem_minus_icon(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow, void *) {
  ui::draw_icon_minus(it, cx, cy, r, col, shadow);
}

}  // namespace

void render_tv_remote_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const int remote_subpage = rs.nav.remote_subpage;
  const std::string &remote_action_feedback = rs.remote.action_feedback;
  const uint32_t remote_feedback_timer = rs.remote.feedback_timer;

  auto *font_btn_bold = ctx.font_btn_bold;
  auto *font_small = ctx.font_small;
  const Color &theme_primary = ctx.theme_primary;
  const Color &theme_secondary = ctx.theme_secondary;
  const Color &col_white = ctx.col_white;
  const Color &col_slate = ctx.col_slate;
  const Color &col_red = ctx.col_red;

  const std::string *t_ents = remote_slots_ent();
  const std::string *t_names = remote_slots_name();

  const int slot = resolve_active_remote_slot(rs.remote.active_slot);
  rs.remote.active_slot = slot;

  std::string rem_name = t_names[slot];
  if (rem_name.empty()) {
    rem_name = format_remote_entity_fallback(t_ents[slot]);
    if (rem_name.empty()) rem_name = "TV Remote";
  }

  const bool is_fb = (now_ms - remote_feedback_timer < 1500U && !remote_action_feedback.empty());

  if (remote_subpage == 1) {
    // -------------------------------------------------------------------------
    // SUBPAGE 1: CONTROL (PINNED MASTER POWER + UP TO 10 SCROLLABLE DEVICES)
    // -------------------------------------------------------------------------
    int valid_slots[MAX_REMOTE_SLOTS] = {0};
    const int valid_count = collect_configured_remote_slots(valid_slots);
    const int total_pages = std::max(1, (valid_count + 3) / 4);
    if (remote_control_page() < 0 || remote_control_page() >= total_pages) {
      remote_control_page() = 0;
    }
    const int cur_page = remote_control_page();

    if (total_pages > 1) {
      char hdr_buf[32];
      std::snprintf(hdr_buf, sizeof(hdr_buf), "CONTROL  %d/%d", cur_page + 1, total_pages);
      render_page_header(it, cx, hdr_buf, font_small, col_white);
    } else {
      render_page_header(it, cx, "CONTROL", font_small, col_white);
    }
    if (is_fb) {
      std::string fit_fb = fit_text_to_width_font(remote_action_feedback, safe_span(layout().sub_hdr_y, 18), font_small);
      render_sub_header(it, cx, fit_fb.c_str(), font_small, theme_secondary);
    }

    const Color card_bg = kChromeGlassTint;
    const Color card_brd = kChromeCardBrdOn;
    const int full_w = layout().list_card_w;
    const int row_h = layout().slot5_h;
    const int row_x0 = cx - full_w / 2;
    const int p_r = scale_s(18);
    const int p_gap = scale_x(8);

    const int y0_pwr = layout().slot5_y(0);
    render_card_box(it, row_x0, y0_pwr - row_h / 2, full_w, row_h, card_bg, col_red, kRadiusCardPx());
    ui::shadow_print(it, cx, y0_pwr, font_small, col_white, TextAlign::CENTER, "Toggle Power");
    frame_hits().add_centered_rect(cx, y0_pwr, full_w, row_h,
                                   WidgetId::REMOTE_PRIMARY_PWR, kHitTap | kHitFocusSlot, 0);

    const int start_idx = cur_page * 4;
    const int vis_count = std::min(4, std::max(0, valid_count - start_idx));
    const int bounce_y = edge_bounce().offset_px(
        PageId::REMOTE, GestureAxisLock::Vertical, now_ms, LATTER_PANEL_PPI);
    for (int row = 0; row < vis_count; row++) {
      const int s_idx = valid_slots[start_idx + row];
      const int y_mid = layout().slot5_y(row + 1) + bounce_y;
      const bool is_sel = (s_idx == slot);
      const bool is_primary_slot = (s_idx == valid_slots[0]);

      std::string d_name = t_names[s_idx];
      if (d_name.empty()) d_name = format_remote_entity_fallback(t_ents[s_idx]);

      const Color border_c = is_sel ? theme_primary : card_brd;
      const Color text_c = is_sel ? theme_primary : col_white;

      const int eff_w = is_primary_slot ? full_w : std::max(100, full_w - (2 * p_r + p_gap));
      const int card_cx = row_x0 + eff_w / 2;
      const int dot_x = row_x0 + scale_x(19);
      const int txt_x = row_x0 + scale_x(35);
      const int max_txt = std::max(60, (row_x0 + eff_w - scale_x(12)) - txt_x);

      render_card_box(it, row_x0, y_mid - row_h / 2, eff_w, row_h, card_bg, border_c, kRadiusCardPx());
      frame_hits().add_centered_rect(card_cx, y_mid, eff_w, row_h,
                                     static_cast<uint16_t>(WidgetId::REMOTE_SLOT_CARD + row),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(s_idx));
      if (is_sel) {
        fast_filled_circle_aa(it, dot_x, y_mid, scale_s(6), theme_primary);
      } else {
        fast_circle_aa(it, dot_x, y_mid, static_cast<float>(scale_s(5)), col_slate, 1.5f);
      }
      std::string fit_lbl = fit_text_to_width_font(d_name, max_txt, font_small);
      ui::shadow_print(it, txt_x, y_mid, font_small, text_c, TextAlign::CENTER_LEFT, fit_lbl.c_str());

      if (!is_primary_slot) {
        const int p_cx = row_x0 + eff_w + p_gap + p_r;
        const bool is_dev_on = remote_slot_states_on()[s_idx];
        ui::icon_circle_button(it, p_cx, y_mid, p_r, kChromeGlassTint,
                               is_dev_on ? theme_secondary : col_red, col_white,
                               draw_rem_power_icon, nullptr,
                               static_cast<uint16_t>(WidgetId::REMOTE_SEC_PWR + row),
                               kHitTap | kHitFocusSlot | kHitFocusCircle,
                               static_cast<int16_t>(s_idx), scale_s(16));
      }
    }

    if (total_pages > 1) {
      render_vertical_page_pips(it, cur_page > 0, cur_page + 1 < total_pages,
                                ctx.col_bg, col_white, col_slate);
    }

    render_back_button(it, cx, font_small, layout().back_y, "< Back");
    return;
  }

  // ---------------------------------------------------------------------------
  // SUBPAGE 0: MAIN REMOTE CONTROL INTERFACE
  // ---------------------------------------------------------------------------
  std::string upper_title = rem_name;
  for (char &c : upper_title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  std::string disp_title = fit_text_to_width_font(upper_title, safe_span(layout().hdr_y, 20), font_small);
  render_page_header(it, cx, disp_title.c_str(), font_small, col_white);
  frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(260), scale_y(36),
                                 WidgetId::REMOTE_SLOT_PILL, kHitTap | kHitFocusSlot);
  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(remote_action_feedback, safe_span(layout().sub_hdr_y, 20), font_small);
    render_sub_header(it, cx, fit_fb.c_str(), font_small, theme_secondary);
  }

  const RemoteDpadGeom dp = compute_remote_dpad_geom(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
  const int dpad_cy  = dp.cy;
  const int left_fx  = LATTER_ROUND_DISPLAY ? 64 : ((SCREEN_W < 380) ? 34 : ((SCREEN_W > 520) ? 104 : ((SCREEN_W >= 480) ? 44 : 48)));
  const int right_fx = SCREEN_W - left_fx;
  const int flank_dy = LATTER_ROUND_DISPLAY ? 99 : ((SCREEN_W < 380) ? 128 : ((SCREEN_W > 520) ? 196 : ((SCREEN_W >= 480) ? 148 : 116)));
  const int top_fy   = dpad_cy - flank_dy;
  const int btm_fy   = dpad_cy + flank_dy;
  const int f_r      = scale_s(19);

  const auto &gest = touch_gesture();
  const auto hold_brd = [&](uint16_t wid, Color base_brd) -> Color {
    if (!gest.active || gest.long_press_wid != wid) return base_brd;
    if (gest.long_press_fired) return theme_secondary;
    return gest.long_press_armed ? theme_primary : base_brd;
  };

  // 1..4 Canonical SDF circular flank buttons (zero PSRAM readback, full focus + long-press parity)
  ui::icon_circle_button(it, left_fx, top_fy, f_r, kChromeGlassTint,
                         hold_brd(WidgetId::REMOTE_NAV_HOME, kChromeCardBorder), col_white,
                         draw_rem_home_icon, nullptr,
                         WidgetId::REMOTE_NAV_HOME, kHitTap | kHitLongPress | kHitFocusSlot | kHitFocusCircle,
                         0, scale_s(20));
  ui::icon_circle_button(it, right_fx, top_fy, f_r, kChromeGlassTint, kChromeCardBorder, col_white,
                         draw_rem_plus_icon, nullptr,
                         WidgetId::REMOTE_VOL_UP, kHitTap | kHitFocusSlot | kHitFocusCircle,
                         0, scale_s(20));
  ui::icon_circle_button(it, left_fx, btm_fy, f_r, kChromeGlassTint,
                         hold_brd(WidgetId::REMOTE_NAV_BACK, kChromeCardBorder), col_white,
                         draw_rem_back_icon, nullptr,
                         WidgetId::REMOTE_NAV_BACK, kHitTap | kHitLongPress | kHitFocusSlot | kHitFocusCircle,
                         0, scale_s(20));
  ui::icon_circle_button(it, right_fx, btm_fy, f_r, kChromeGlassTint, kChromeCardBorder, col_white,
                         draw_rem_minus_icon, nullptr,
                         WidgetId::REMOTE_VOL_DOWN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                         0, scale_s(20));

  // Adaptive Frosted-Glass Concentric D-Pad (inner_hole_r avoids double-blending under center OK orb)
  const int dpad_r_out = dp.r_out;
  const int dpad_r_in  = dp.r_in;
  const int ok_r       = dp.ok_r;
  render_glass_circle(it, cx, dpad_cy, dpad_r_out, kChromeGlassTint, kChromeCardBorder, dpad_r_in);
  fast_circle_aa(it, cx, dpad_cy, static_cast<float>(dpad_r_in), kChromeCardBorder, 1.5f);

  const int d_in  = (dpad_r_in * 707) / 1000;
  const int d_out = ((dpad_r_out - 1) * 707) / 1000;
  fast_line(it, cx + d_in, dpad_cy + d_in, cx + d_out, dpad_cy + d_out, kChromeCardBorder);
  fast_line(it, cx - d_in, dpad_cy + d_in, cx - d_out, dpad_cy + d_out, kChromeCardBorder);
  fast_line(it, cx + d_in, dpad_cy - d_in, cx + d_out, dpad_cy - d_out, kChromeCardBorder);
  fast_line(it, cx - d_in, dpad_cy - d_in, cx - d_out, dpad_cy - d_out, kChromeCardBorder);

  const int arr_mid = dp.arr_mid;
  const int arr_h   = scale_s(8);
  const int arr_w   = scale_s(10);
  auto draw_dpad_arrows = [&](int ox, int oy, Color c) {
    fast_filled_triangle(it, cx + ox, dpad_cy - arr_mid - arr_h + oy,
                         cx - arr_w + ox, dpad_cy - arr_mid + arr_h + oy,
                         cx + arr_w + ox, dpad_cy - arr_mid + arr_h + oy, c);
    fast_filled_triangle(it, cx + ox, dpad_cy + arr_mid + arr_h + oy,
                         cx - arr_w + ox, dpad_cy + arr_mid - arr_h + oy,
                         cx + arr_w + ox, dpad_cy + arr_mid - arr_h + oy, c);
    fast_filled_triangle(it, cx - arr_mid - arr_h + ox, dpad_cy + oy,
                         cx - arr_mid + arr_h + ox, dpad_cy - arr_w + oy,
                         cx - arr_mid + arr_h + ox, dpad_cy + arr_w + oy, c);
    fast_filled_triangle(it, cx + arr_mid + arr_h + ox, dpad_cy + oy,
                         cx + arr_mid - arr_h + ox, dpad_cy - arr_w + oy,
                         cx + arr_mid - arr_h + ox, dpad_cy + arr_w + oy, c);
  };
  draw_dpad_arrows(1, 1, kChromeShadow);
  draw_dpad_arrows(0, 0, col_white);

  frame_hits().add_centered_rect(cx, dpad_cy - arr_mid, dp.hit_tangent, dp.hit_radial, WidgetId::REMOTE_DPAD_UP, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx, dpad_cy + arr_mid, dp.hit_tangent, dp.hit_radial, WidgetId::REMOTE_DPAD_DOWN, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx - arr_mid, dpad_cy, dp.hit_radial, dp.hit_tangent, WidgetId::REMOTE_DPAD_LEFT, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx + arr_mid, dpad_cy, dp.hit_radial, dp.hit_tangent, WidgetId::REMOTE_DPAD_RIGHT, kHitTap | kHitFocusSlot);

  render_glass_circle(it, cx, dpad_cy, ok_r, kChromeGlassTint, hold_brd(WidgetId::REMOTE_DPAD_OK, theme_primary));
  ui::shadow_print(it, cx, dpad_cy, font_btn_bold, col_white, TextAlign::CENTER, "OK");
  frame_hits().add_circle(cx, dpad_cy, ok_r, WidgetId::REMOTE_DPAD_OK, kHitTap | kHitLongPress | kHitFocusSlot | kHitFocusCircle);
}

}  // namespace latter_ui
