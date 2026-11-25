#include "ui_pages.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace {

void draw_rem_power_icon(Rgb565Surface &it, int cx, int cy, int r,
                         UiColor col, bool shadow, void *) {
  ui::draw_icon_power(it, cx, cy, r, col, shadow);
}

void draw_rem_home_icon(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor col, bool shadow, void *) {
  ui::draw_icon_home(it, cx, cy, r, col, shadow);
}

void draw_rem_plus_icon(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor col, bool shadow, void *) {
  ui::draw_icon_plus(it, cx, cy, r, col, shadow);
}

void draw_rem_back_icon(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor col, bool shadow, void *) {
  ui::draw_icon_chevron(it, cx, cy, r, false, col, shadow);
}

void draw_rem_minus_icon(Rgb565Surface &it, int cx, int cy, int r,
                         UiColor col, bool shadow, void *) {
  ui::draw_icon_minus(it, cx, cy, r, col, shadow);
}

}  // namespace

void render_tv_remote_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const int remote_subpage = rs.nav.remote_subpage;
  const std::string &remote_action_feedback = rs.remote.action_feedback;
  const uint32_t remote_feedback_timer = rs.remote.feedback_timer;

  const UiFontView font_btn_bold = ctx.font_btn_bold;
  const UiFontView font_small = ctx.font_small;
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
    const int p_r = dp_px(18);
    const int p_gap = dp_px(8);

    const int bounce_y = edge_bounce().offset_px(
        PageId::REMOTE, GestureAxisLock::Vertical, now_ms, LATTER_PANEL_PPI);
    const int y0_pwr = layout().slot5_y(0) + bounce_y;
    render_card_box(it, row_x0, y0_pwr - row_h / 2, full_w, row_h, card_bg, col_red, kRadiusCardPx());
    ui::shadow_print(it, cx, y0_pwr, font_small, col_white, TextAlign::CENTER, "Toggle Power");
    frame_hits().add_centered_rect(cx, y0_pwr, full_w, row_h,
                                   WidgetId::REMOTE_PRIMARY_PWR, kHitTap | kHitFocusSlot, 0);

    const int start_idx = cur_page * 4;
    const int vis_count = std::min(4, std::max(0, valid_count - start_idx));
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
      const int dot_x = row_x0 + dp_px(19);
      const int txt_x = row_x0 + dp_px(35);
      const int max_txt = std::max(60, (row_x0 + eff_w - dp_px(12)) - txt_x);

      render_card_box(it, row_x0, y_mid - row_h / 2, eff_w, row_h, card_bg, border_c, kRadiusCardPx());
      frame_hits().add_centered_rect(card_cx, y_mid, eff_w, row_h,
                                     static_cast<uint16_t>(WidgetId::REMOTE_SLOT_CARD + row),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(s_idx));
      if (is_sel) {
        fast_filled_circle_aa(it, dot_x, y_mid, std::max(4, dp_px(6)), theme_primary);
      } else {
        fast_circle_aa(it, dot_x, y_mid, static_cast<float>(std::max(4, dp_px(5))), col_slate, 1.5f);
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
                               static_cast<int16_t>(s_idx), dp_px(16));
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
  const auto ly = layout();
  std::string upper_title = rem_name;
  for (char &c : upper_title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  std::string disp_title = fit_text_to_width_font(upper_title, safe_span(ly.hdr_y, 20), font_small);
  render_page_header(it, cx, disp_title.c_str(), font_small, col_white);
  const int slot_hit_w = std::min(ly.safe_span(ly.hdr_y, dp_px(12)),
                                  std::max(dp_px(240), ly.min_hit_px() * 2));
  const int slot_hit_h = dp_px(36);
  const int slot_hit_pad = std::max(0, (ly.min_hit_px() - slot_hit_h + 1) / 2);
  frame_hits().add_centered_rect(cx, ly.hdr_y, slot_hit_w, slot_hit_h,
                                 WidgetId::REMOTE_SLOT_PILL, kHitTap | kHitFocusSlot, 0, slot_hit_pad);
  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(remote_action_feedback, safe_span(ly.sub_hdr_y, 20), font_small);
    render_sub_header(it, cx, fit_fb.c_str(), font_small, theme_secondary);
  }

  const RemoteDpadGeom dp = compute_remote_dpad_geom(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_PANEL_PPI,
      LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
  const int dpad_cy  = dp.cy;
  const int f_r      = std::clamp(dp_px(19), 16, 32);
  const int top_min_y = ly.sub_hdr_y + f_r + dp_px(8);
  const int bot_max_y = SCREEN_H - f_r - dp_px(16);
  const int max_dy = std::max(f_r + dp_px(10), std::min(dpad_cy - top_min_y, bot_max_y - dpad_cy));
  int flank_dy = 0;
  if (LATTER_ROUND_DISPLAY) {
    flank_dy = std::min(max_dy, (dp.r_out * 69) / 100);
  } else if (SCREEN_W > SCREEN_H) {
    flank_dy = std::min(max_dy, std::max(f_r + dp_px(12), dp.r_out - f_r));
  } else {
    flank_dy = std::min(max_dy, dp.r_out + dp_px(6));
  }
  const int top_fy = dpad_cy - flank_dy;
  const int btm_fy = dpad_cy + flank_dy;
  const int flank_margin = (LATTER_CORNER_R == 0) ? dp_px(14) : std::max(6, dp_px(8));
  const int safe_flank = std::min(ly.band_safe_span(top_fy, 2 * f_r, flank_margin),
                                  ly.band_safe_span(btm_fy, 2 * f_r, flank_margin));
  const int min_left_fx = std::max(f_r + flank_margin, cx - safe_flank / 2 + f_r);
  const int desired_dx = (SCREEN_W > SCREEN_H && !LATTER_ROUND_DISPLAY)
                             ? std::min(dp.r_out + f_r + dp_px(24), safe_flank / 2 - f_r)
                             : std::min(dp.r_out + dp_px(18), safe_flank / 2 - f_r);
  const int left_fx = std::clamp(cx - desired_dx, min_left_fx, std::max(min_left_fx, cx - dp.r_in - f_r));
  const int right_fx = SCREEN_W - left_fx;

  const auto &gest = touch_gesture();
  const auto hold_brd = [&](uint16_t wid, Color base_brd) -> Color {
    if (!gest.active || gest.long_press_wid != wid) return base_brd;
    if (gest.long_press_fired) return theme_secondary;
    return gest.long_press_armed ? theme_primary : base_brd;
  };

  // 1..4 Canonical SDF circular flank buttons (zero PSRAM readback, full focus + long-press parity)
  const int flank_pad = dp_px(18);
  ui::icon_circle_button(it, left_fx, top_fy, f_r, kChromeGlassTint,
                         hold_brd(WidgetId::REMOTE_NAV_HOME, kChromeCardBorder), col_white,
                         draw_rem_home_icon, nullptr,
                         WidgetId::REMOTE_NAV_HOME, kHitTap | kHitLongPress | kHitFocusSlot | kHitFocusCircle,
                         0, flank_pad);
  ui::icon_circle_button(it, right_fx, top_fy, f_r, kChromeGlassTint, kChromeCardBorder, col_white,
                         draw_rem_plus_icon, nullptr,
                         WidgetId::REMOTE_VOL_UP, kHitTap | kHitFocusSlot | kHitFocusCircle,
                         0, flank_pad);
  ui::icon_circle_button(it, left_fx, btm_fy, f_r, kChromeGlassTint,
                         hold_brd(WidgetId::REMOTE_NAV_BACK, kChromeCardBorder), col_white,
                         draw_rem_back_icon, nullptr,
                         WidgetId::REMOTE_NAV_BACK, kHitTap | kHitLongPress | kHitFocusSlot | kHitFocusCircle,
                         0, flank_pad);
  ui::icon_circle_button(it, right_fx, btm_fy, f_r, kChromeGlassTint, kChromeCardBorder, col_white,
                         draw_rem_minus_icon, nullptr,
                         WidgetId::REMOTE_VOL_DOWN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                         0, flank_pad);

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
  const int arr_px  = std::max(18, dp_px(26));
  render_mdi_icon(it, "mdi:chevron-up", cx, dpad_cy - arr_mid, arr_px, col_white, true);
  render_mdi_icon(it, "mdi:chevron-down", cx, dpad_cy + arr_mid, arr_px, col_white, true);
  render_mdi_icon(it, "mdi:chevron-left", cx - arr_mid, dpad_cy, arr_px, col_white, true);
  render_mdi_icon(it, "mdi:chevron-right", cx + arr_mid, dpad_cy, arr_px, col_white, true);

  frame_hits().add_centered_rect(cx, dpad_cy - arr_mid, dp.hit_tangent, dp.hit_radial, WidgetId::REMOTE_DPAD_UP, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx, dpad_cy + arr_mid, dp.hit_tangent, dp.hit_radial, WidgetId::REMOTE_DPAD_DOWN, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx - arr_mid, dpad_cy, dp.hit_radial, dp.hit_tangent, WidgetId::REMOTE_DPAD_LEFT, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx + arr_mid, dpad_cy, dp.hit_radial, dp.hit_tangent, WidgetId::REMOTE_DPAD_RIGHT, kHitTap | kHitFocusSlot);

  render_glass_circle(it, cx, dpad_cy, ok_r, kChromeGlassTint, hold_brd(WidgetId::REMOTE_DPAD_OK, theme_primary));
  ui::shadow_print(it, cx, dpad_cy, font_btn_bold, col_white, TextAlign::CENTER, "OK");
  frame_hits().add_circle(cx, dpad_cy, ok_r, WidgetId::REMOTE_DPAD_OK, kHitTap | kHitLongPress | kHitFocusSlot | kHitFocusCircle);
}

}  // namespace latter_ui
