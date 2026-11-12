#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace latter_ui {

void render_tv_remote_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  const int remote_subpage = rs.nav.remote_subpage;
  const std::string &remote_action_feedback = rs.remote.action_feedback;
  const uint32_t remote_feedback_timer = rs.remote.feedback_timer;

  auto *font_date = ctx.font_date;
  auto *font_small = ctx.font_small;
  const Color &theme_primary = ctx.theme_primary;
  const Color &theme_secondary = ctx.theme_secondary;
  const Color &col_white = ctx.col_white;
  const Color &col_slate = ctx.col_slate;
  const Color &col_gray = ctx.col_gray;
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

    if (is_fb) {
      std::string fit_fb = fit_text_to_width_font(remote_action_feedback, scale_x(280), font_small);
      render_page_header(it, cx, fit_fb.c_str(), font_small, theme_primary);
    } else if (total_pages > 1) {
      char hdr_buf[32];
      std::snprintf(hdr_buf, sizeof(hdr_buf), "CONTROL  %d/%d", cur_page + 1, total_pages);
      render_page_header(it, cx, hdr_buf, font_small, theme_primary);
    } else {
      render_page_header(it, cx, "CONTROL", font_small, theme_primary);
    }

    const Color card_bg = ui::chrome_card_bg_off();
    const Color card_brd = ui::chrome_card_brd_on();

    render_modern_card(it, cx, layout().slot5_y(0), 276, 44, 12, ui::chrome_danger_bg(), col_red,
                       "Toggle Power", col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().slot5_y(0), layout().list_card_w, layout().slot5_h,
                                   WidgetId::REMOTE_PRIMARY_PWR, kHitTap | kHitFocusSlot, 0);

    const int start_idx = cur_page * 4;
    const int vis_count = std::min(4, std::max(0, valid_count - start_idx));
    for (int row = 0; row < vis_count; row++) {
      const int s_idx = valid_slots[start_idx + row];
      const int y_mid = layout().slot5_y(row + 1);
      const bool is_sel = (s_idx == slot);
      const bool is_primary_slot = (s_idx == valid_slots[0]);

      std::string d_name = t_names[s_idx];
      if (d_name.empty()) d_name = format_remote_entity_fallback(t_ents[s_idx]);

      const Color border_c = is_sel ? theme_primary : card_brd;
      const Color text_c = is_sel ? theme_primary : col_white;

      const bool narrow = (SCREEN_W < 380);
      const int card_cx = is_primary_slot ? cx : (narrow ? (cx - 26) : (cx - scale_x(25)));
      const int card_w  = is_primary_slot ? 276 : (narrow ? 212 : scale_x(226));
      const int eff_w   = is_primary_slot ? layout().list_card_w : card_w;
      const int dot_x   = narrow ? (card_cx - eff_w / 2 + 19) : (cx - scale_x(119));
      const int txt_x   = narrow ? (dot_x + 16) : (cx - scale_x(103));
      const int max_txt = std::max(60, (card_cx + eff_w / 2 - 12) - txt_x);

      render_modern_card(it, card_cx, y_mid, card_w, 44, 12, card_bg, border_c, nullptr, text_c, font_small);
      frame_hits().add_centered_rect(card_cx, y_mid, eff_w, layout().slot5_h,
                                     static_cast<uint16_t>(WidgetId::REMOTE_SLOT_CARD + row),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(s_idx));
      if (is_sel) {
        fast_filled_circle_aa(it, dot_x, y_mid, scale_s(6), theme_primary);
      } else {
        fast_circle_aa(it, dot_x, y_mid, static_cast<float>(scale_s(5)), col_slate, 1.5f);
      }
      std::string fit_lbl = fit_text_to_width_font(d_name, max_txt, font_small);
      fast_print(it, txt_x, y_mid, font_small, text_c, TextAlign::CENTER_LEFT, fit_lbl.c_str());

      if (!is_primary_slot) {
        const int p_cx = narrow ? (SCREEN_W - 36) : (cx + scale_x(120));
        const int p_r  = scale_s(18);
        const bool is_dev_on = remote_slot_states_on()[s_idx];
        const Color p_surf = render_glass_circle(
            it, p_cx, y_mid, p_r, kChromeGlassTint, is_dev_on ? theme_secondary : col_red);
        ui::draw_icon_power(it, p_cx, y_mid, p_r, col_white, p_surf);
        frame_hits().add_circle(p_cx, y_mid, p_r,
                                static_cast<uint16_t>(WidgetId::REMOTE_SEC_PWR + row),
                                kHitTap | kHitFocusCircle, static_cast<int16_t>(s_idx), scale_s(16));
      }
    }

    if (total_pages > 1) {
      render_vertical_page_pips(it, cur_page > 0, cur_page + 1 < total_pages,
                                kChromeShadow, theme_secondary, col_slate);
    }

    render_back_button(it, cx, font_small, layout().back_y, "< Back");
    return;
  }

  // ---------------------------------------------------------------------------
  // SUBPAGE 0: MAIN REMOTE CONTROL INTERFACE
  // ---------------------------------------------------------------------------
  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(remote_action_feedback, scale_x(280), font_small);
    render_page_header(it, cx, fit_fb.c_str(), font_small, theme_primary);
  } else {
    std::string disp_title = fit_text_to_width_font(rem_name, scale_x(280), font_small);
    render_page_header(it, cx, disp_title.c_str(), font_small, theme_primary);
  }
  frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(260), scale_y(36),
                                 WidgetId::REMOTE_SLOT_PILL, kHitTap | kHitFocusSlot);

  const int dpad_cy  = LATTER_ROUND_DISPLAY ? cy : (cy + scale_y(8));
  const int left_fx  = LATTER_ROUND_DISPLAY ? 64 : ((SCREEN_W < 380) ? 34 : ((SCREEN_W > 520) ? 104 : ((SCREEN_W >= 480) ? 44 : 48)));
  const int right_fx = SCREEN_W - left_fx;
  const int flank_dy = LATTER_ROUND_DISPLAY ? 99 : ((SCREEN_W < 380) ? 128 : ((SCREEN_W > 520) ? 196 : ((SCREEN_W >= 480) ? 148 : 116)));
  const int top_fy   = dpad_cy - flank_dy;
  const int btm_fy   = dpad_cy + flank_dy;
  const int f_r      = scale_s(19);

  // 1. HOME: Top-Left
  const Color home_surf = render_glass_circle(it, left_fx, top_fy, f_r, kChromeGlassTint, kChromeCardBorder);
  fast_filled_triangle(it, left_fx, top_fy - scale_s(8), left_fx - scale_s(9), top_fy - 1, left_fx + scale_s(9), top_fy - 1, col_white);
  fill_rect_row_major(it, left_fx - scale_s(6), top_fy - 1, scale_s(13), scale_s(8), col_white);
  fill_rect_row_major(it, left_fx - scale_s(2), top_fy + scale_s(2), scale_s(5), scale_s(5), home_surf);
  frame_hits().add_circle(left_fx, top_fy, f_r, WidgetId::REMOTE_NAV_HOME, kHitTap | kHitFocusCircle, 0, scale_s(20));

  // 2. VOL UP: Top-Right
  const int pls = scale_s(7);
  const int pth = scale_s(3);
  render_glass_circle(it, right_fx, top_fy, f_r, kChromeGlassTint, kChromeCardBorder);
  fill_rect_row_major(it, right_fx - pls, top_fy - pth / 2, pls * 2 + 1, pth, col_white);
  fill_rect_row_major(it, right_fx - pth / 2, top_fy - pls, pth, pls * 2 + 1, col_white);
  frame_hits().add_circle(right_fx, top_fy, f_r, WidgetId::REMOTE_VOL_UP, kHitTap | kHitFocusCircle, 0, scale_s(20));

  // 3. BACK: Bottom-Left
  render_glass_circle(it, left_fx, btm_fy, f_r, kChromeGlassTint, kChromeCardBorder);
  fast_line(it, left_fx + scale_s(3), btm_fy - scale_s(7), left_fx - scale_s(4), btm_fy, col_white);
  fast_line(it, left_fx + scale_s(4), btm_fy - scale_s(7), left_fx - scale_s(3), btm_fy, col_white);
  fast_line(it, left_fx - scale_s(4), btm_fy, left_fx + scale_s(3), btm_fy + scale_s(7), col_white);
  fast_line(it, left_fx - scale_s(3), btm_fy, left_fx + scale_s(4), btm_fy + scale_s(7), col_white);
  frame_hits().add_circle(left_fx, btm_fy, f_r, WidgetId::REMOTE_NAV_BACK, kHitTap | kHitFocusCircle, 0, scale_s(20));

  // 4. VOL DOWN: Bottom-Right
  render_glass_circle(it, right_fx, btm_fy, f_r, kChromeGlassTint, kChromeCardBorder);
  fill_rect_row_major(it, right_fx - pls, btm_fy - pth / 2, pls * 2 + 1, pth, col_white);
  frame_hits().add_circle(right_fx, btm_fy, f_r, WidgetId::REMOTE_VOL_DOWN, kHitTap | kHitFocusCircle, 0, scale_s(20));

  // Adaptive Concentric D-Pad
  const int dpad_r_out = (SCREEN_W < 380) ? 128 : ((SCREEN_W > 520) ? 236 : ((!LATTER_ROUND_DISPLAY && SCREEN_W >= 480) ? 180 : 144));
  const int dpad_r_in  = (SCREEN_W < 380) ?  46 : ((SCREEN_W > 520) ?  86 : ((!LATTER_ROUND_DISPLAY && SCREEN_W >= 480) ?  60 :  50));
  const int ok_r       = dpad_r_in - scale_s(8);
  fast_circle(it, cx, dpad_cy, dpad_r_out, col_gray);
  fast_circle(it, cx, dpad_cy, dpad_r_in, col_gray);

  const int d_in  = (dpad_r_in * 707) / 1000;
  const int d_out = (dpad_r_out * 707) / 1000;
  fast_line(it, cx + d_in, dpad_cy + d_in, cx + d_out, dpad_cy + d_out, col_gray);
  fast_line(it, cx - d_in, dpad_cy + d_in, cx - d_out, dpad_cy + d_out, col_gray);
  fast_line(it, cx + d_in, dpad_cy - d_in, cx + d_out, dpad_cy - d_out, col_gray);
  fast_line(it, cx - d_in, dpad_cy - d_in, cx - d_out, dpad_cy - d_out, col_gray);

  const int arr_mid = (dpad_r_in + dpad_r_out) / 2;
  fast_filled_triangle(it, cx, dpad_cy - arr_mid - 8, cx - 10, dpad_cy - arr_mid + 8, cx + 10, dpad_cy - arr_mid + 8, col_white);
  fast_filled_triangle(it, cx, dpad_cy + arr_mid + 8, cx - 10, dpad_cy + arr_mid - 8, cx + 10, dpad_cy + arr_mid - 8, col_white);
  fast_filled_triangle(it, cx - arr_mid - 8, dpad_cy, cx - arr_mid + 8, dpad_cy - 10, cx - arr_mid + 8, dpad_cy + 10, col_white);
  fast_filled_triangle(it, cx + arr_mid + 8, dpad_cy, cx + arr_mid - 8, dpad_cy - 10, cx + arr_mid - 8, dpad_cy + 10, col_white);

  frame_hits().add_centered_rect(cx, dpad_cy - arr_mid, scale_x(56), scale_y(44), WidgetId::REMOTE_DPAD_UP, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx, dpad_cy + arr_mid, scale_x(56), scale_y(44), WidgetId::REMOTE_DPAD_DOWN, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx - arr_mid, dpad_cy, scale_x(44), scale_y(56), WidgetId::REMOTE_DPAD_LEFT, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(cx + arr_mid, dpad_cy, scale_x(44), scale_y(56), WidgetId::REMOTE_DPAD_RIGHT, kHitTap | kHitFocusSlot);

  render_glass_circle(it, cx, dpad_cy, ok_r, kChromeGlassTint, theme_primary);
  fast_print(it, cx + 1, dpad_cy + 1, font_date, kChromeShadow, TextAlign::CENTER, "OK");
  fast_print(it, cx, dpad_cy, font_date, col_white, TextAlign::CENTER, "OK");
  frame_hits().add_circle(cx, dpad_cy, ok_r, WidgetId::REMOTE_DPAD_OK, kHitTap | kHitFocusCircle);
}

}  // namespace latter_ui
