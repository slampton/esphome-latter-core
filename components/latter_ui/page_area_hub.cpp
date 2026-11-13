#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

// 90-Degree Symmetric 4-Blade Ceiling Fan Pixel Predicate (Central Motor Hub + 1px Moat + 4 Swept Paddle Blades)
bool is_ceiling_fan_blade_arm(float u, float v) {
  if (v < 4.2f || v > 13.0f) return false;
  if (v <= 9.6f) {
    const float u_min = -1.5f;
    const float u_max = 1.5f + (v - 4.2f) * 0.44f;
    return (u >= u_min && u <= u_max);
  }
  const float du = (u - 1.2f) / 3.1f;
  const float dv = (v - 9.6f) / 3.3f;
  return (du * du + dv * dv) <= 1.0f;
}

bool is_ceiling_fan_4blade_pixel(int dx, int dy) {
  const int r2 = dx * dx + dy * dy;
  if (r2 <= 1) return false;   // Negative-space center motor pin
  if (r2 <= 10) return true;   // Solid central motor housing (r ~ 3.2px)
  if (r2 <= 18) return false;  // Crisp negative-space collar moat around motor hub
  if (r2 > 172) return false;
  const float fx = static_cast<float>(dx);
  const float fy = static_cast<float>(dy);
  return is_ceiling_fan_blade_arm(fx, -fy) ||  // Top blade
         is_ceiling_fan_blade_arm(fy,  fx) ||  // Right blade
         is_ceiling_fan_blade_arm(-fx, fy) ||  // Bottom blade
         is_ceiling_fan_blade_arm(-fy, -fx);   // Left blade
}

bool is_curtain_entity(const std::string &name, const std::string &id_str) {
  std::string s = name + " " + id_str;
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return (s.find("curtain") != std::string::npos ||
          s.find("drape") != std::string::npos ||
          s.find("sheer") != std::string::npos);
}

}  // namespace

std::string strip_trailing_paren_state(const std::string &s) {
  if (s.size() > 4 && s.back() == ')') {
    const size_t lp = s.rfind(" (");
    if (lp != std::string::npos && lp > 0) {
      return s.substr(0, lp);
    }
  }
  return s;
}

void render_area_control_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const int area_subpage = rs.nav.area_subpage;
  const std::string &area_browse_mode = rs.area.browse_mode;
  const std::string &area_domain_filter = rs.area.domain_filter;
  const std::string &active_area_name = rs.area.active_area_name;
  const float area_temp = ac.area_temp;
  const float area_humidity = ac.area_humidity;
  const std::string &area_action_feedback = rs.area.action_feedback;
  const uint32_t area_feedback_timer = rs.area.feedback_timer;

  auto *font_small = ctx.font_small;
  const Color &theme_primary = ctx.theme_primary;
  const Color &theme_secondary = ctx.theme_secondary;
  const Color &col_bg = ctx.col_bg;
  const Color &col_black = ctx.col_black;
  const Color &col_white = ctx.col_white;
  const Color &col_slate = ctx.col_slate;
  const Color &col_gold = ctx.col_gold;

  const bool is_fb = (now_ms - area_feedback_timer < 1500U && !area_action_feedback.empty());
  const Color card_bg_off = ui::chrome_card_bg_off();
  const Color card_bg_on = ui::chrome_card_bg_on();
  const Color card_brd_off = ui::chrome_card_brd_off();

  // Helper: Draw Unified Area Category Mode Bar at layout().app_dock_y (1..4 Icon Conveyor Window)
  auto render_area_bottom_dock = [&](const std::string &active_domain) {
    std::string cats[8];
    const int cat_count = collect_area_categories(cats);
    if (cat_count <= 0) return;

    int active_global_idx = 0;
    for (int i = 0; i < cat_count; i++) {
      if (cats[i] == active_domain) {
        active_global_idx = i;
        break;
      }
    }
    area_cat_dock_page() = get_mode_bar_start_index(cat_count, area_cat_dock_page());
    const int start_idx = area_cat_dock_page();
    const int vis_count = std::min(4, cat_count - start_idx);
    int win_xs[4] = {0, 0, 0, 0};
    bool has_l = false, has_r = false;
    get_mode_bar_window_xs(cat_count, start_idx, cx, win_xs, has_l, has_r);

    const int active_vis_idx = (active_global_idx >= start_idx && active_global_idx < start_idx + vis_count)
                                   ? (active_global_idx - start_idx)
                                   : -1;
    render_in_app_mode_bar_track(it, cx, win_xs, vis_count, active_vis_idx,
                                 has_l, has_r, cat_count, active_global_idx,
                                 theme_primary, col_white, start_idx);

    const int ICON_Y   = layout().app_dock_y;
    const int left_cx  = win_xs[0];
    const int right_cx = win_xs[vis_count - 1];
    const int stride   = get_dock_slot_stride(win_xs, vis_count);
    const float anim_start = mode_bar_anim().cur_start;

    for (int g_idx = 0; g_idx < cat_count; g_idx++) {
      const float rel_slot = static_cast<float>(g_idx) - anim_start;
      if (rel_slot < -0.55f || rel_slot > static_cast<float>(vis_count - 1) + 0.55f) continue;
      const int bx = left_cx + static_cast<int>(std::roundf(rel_slot * static_cast<float>(stride)));
      if (bx < left_cx - scale_x(16) || bx > right_cx + scale_x(16)) continue;

      const int by = ICON_Y;
      const std::string &cat = cats[g_idx];
      const bool is_act = (cat == active_domain);
      const Color raw_c = is_act ? theme_primary : col_white;
      float alpha = 1.0f;
      if (rel_slot < 0.0f) {
        alpha = std::max(0.25f, 1.0f + rel_slot * 1.4f);
      } else if (rel_slot > static_cast<float>(vis_count - 1)) {
        alpha = std::max(0.25f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.4f);
      }
      const Color fg_c(static_cast<uint8_t>(16 + (raw_c.r - 16) * alpha),
                       static_cast<uint8_t>(20 + (raw_c.g - 20) * alpha),
                       static_cast<uint8_t>(27 + (raw_c.b - 27) * alpha));

      if (cat == "scene") {
        fast_line(it, bx, by - 11, bx, by + 11, fg_c);
        fast_line(it, bx - 11, by, bx + 11, by, fg_c);
        fast_line(it, bx - 6, by - 6, bx + 6, by + 6, fg_c);
        fast_line(it, bx - 6, by + 6, bx + 6, by - 6, fg_c);
        fast_filled_circle(it, bx, by, 3, fg_c);
      } else if (cat == "light") {
        fast_filled_circle(it, bx, by - 4, 8, fg_c);
        fill_rect_row_major(it, bx - 5, by + 1, 11, 5, fg_c);
        fill_rect_row_major(it, bx - 4, by + 7, 9, 3, fg_c);
        fast_hline(it, bx - 2, by + 11, 5, fg_c);
      } else if (cat == "cover") {
        fast_rect_outline(it, bx - 10, by - 9, 21, 19, fg_c);
        fast_hline(it, bx - 10, by - 8, 21, fg_c);
        fast_filled_triangle(it, bx - 9, by - 7, bx - 1, by - 7, bx - 9, by + 3, fg_c);
        fill_rect_row_major(it, bx - 9, by + 3, 4, 6, fg_c);
        fast_filled_triangle(it, bx + 9, by - 7, bx + 1, by - 7, bx + 9, by + 3, fg_c);
        fill_rect_row_major(it, bx + 6, by + 3, 4, 6, fg_c);
      } else if (cat == "fan") {
        for (int dy = -13; dy <= 13; dy++) {
          for (int dx = -13; dx <= 13; dx++) {
            if (is_ceiling_fan_4blade_pixel(dx, dy)) {
              fast_pixel(it, bx + dx, by + dy, fg_c);
            }
          }
        }
      } else if (cat == "switch") {
        const Color cut_c = sample_glass_surface_color(bx - 12, by - 12, 25, 25);
        fast_filled_circle(it, bx - 5, by, 7, fg_c);
        fast_filled_circle(it, bx + 5, by, 7, fg_c);
        fill_rect_row_major(it, bx - 5, by - 7, 10, 15, fg_c);
        fast_filled_circle(it, bx + 5, by, 4, cut_c);
      } else if (cat == "climate") {
        const Color cut_c = sample_glass_surface_color(bx - 12, by - 12, 25, 25);
        const int tx = bx - 2;
        fast_line(it, tx - 3, by - 11, tx + 3, by - 11, fg_c);
        fast_line(it, tx - 2, by - 12, tx + 2, by - 12, fg_c);
        fast_line(it, tx - 3, by - 11, tx - 3, by + 3,  fg_c);
        fast_line(it, tx - 2, by - 11, tx - 2, by + 3,  fg_c);
        fast_line(it, tx + 3, by - 11, tx + 3, by + 3,  fg_c);
        fast_line(it, tx + 2, by - 11, tx + 2, by + 3,  fg_c);
        fast_filled_circle(it, tx, by + 7, 5, fg_c);
        fast_filled_circle(it, tx, by + 7, 3, cut_c);
        fast_filled_circle(it, tx, by + 7, 1, fg_c);
        fill_rect_row_major(it, tx - 1, by - 4, 3, 10, fg_c);
        fill_rect_row_major(it, tx + 5, by - 9, 4, 2, fg_c);
        fill_rect_row_major(it, tx + 5, by - 5, 3, 2, fg_c);
        fill_rect_row_major(it, tx + 5, by - 1, 4, 2, fg_c);
      } else if (cat == "media") {
        ui::draw_icon_eighth_notes(it, bx, by, fg_c);
      } else {
        for (int dy = -10; dy <= 10; dy++) {
          for (int dx = -10; dx <= 10; dx++) {
            const int ax = std::abs(dx), ay = std::abs(dy);
            if (ax < 2 || ax > 9 || ay < 2 || ay > 9) continue;
            const int lx = ax - 2, ly = ay - 2;
            if ((lx == 0 || lx == 7) && (ly == 0 || ly == 7)) continue;
            if (dx > 0 && dy > 0 && lx >= 2 && lx <= 5 && ly >= 2 && ly <= 5) continue;
            fast_pixel(it, bx + dx, by + dy, fg_c);
          }
        }
      }
    }
  };

  // ===========================================================================
  // SUBPAGE 2: LIGHT COLOR (HSV WHEEL) & WHITE KELVIN INSPECTOR
  // ===========================================================================
  if (area_subpage == 2) {
    const bool is_kelvin_mode = (light_insp_mode() == 1);
    render_modern_card(it, cx, layout().hdr_y, 212, 34, 10, card_bg_on, theme_primary,
                       is_kelvin_mode ? "Mode: White Temp >" : "Mode: Color Wheel >",
                       col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(212), scale_y(34),
                                   WidgetId::LIGHT_COLOR_BTN, kHitTap | kHitFocusSlot);

    if (!is_kelvin_mode) {
      const int wheel_cy = scale_y(212);
      ui::hsv_color_wheel(it, cx, wheel_cy, 140, 62, light_insp_hue_deg(), light_insp_sat(),
                          col_bg, col_white, WidgetId::COLOR_WHEEL_RING);
    } else {
      std::string fit_ln = fit_text_to_width_font(light_insp_name(), scale_x(260), font_small);
      fast_print(it, cx, scale_y(86), font_small, theme_secondary, TextAlign::CENTER, fit_ln.c_str());

      const int sl_card_w = layout().light_slider_card_w();
      const int k_bar_w   = layout().light_kelvin_bar_w();
      const int k_bar_x0  = layout().light_kelvin_bar_x0();
      const int k_cy      = scale_y(162);
      render_modern_card(it, cx, k_cy, sl_card_w, 68, 16, card_bg_on, theme_primary, nullptr, col_white, font_small);
      frame_hits().add_centered_rect(cx, k_cy, sl_card_w, scale_y(68),
                                     WidgetId::LIGHT_TEMP_SLIDER, kHitTap | kHitHorizSlider | kHitFocusSlot);
      for (int b = 0; b < 24; b++) {
        const float t = static_cast<float>(b) / 23.0f;
        const Color band_c = lerp_color(Color(255, 168, 72), Color(185, 222, 255), t);
        const int bx0 = k_bar_x0 + (b * k_bar_w) / 24;
        const int bx1 = k_bar_x0 + ((b + 1) * k_bar_w) / 24;
        fill_rect_row_major(it, bx0, k_cy + scale_y(6), std::max(2, bx1 - bx0), scale_y(16), band_c);
      }
      const int k_clamped = std::max(2700, std::min(6500, light_insp_kelvin()));
      const int k_knob_x = k_bar_x0 + ((k_clamped - 2700) * k_bar_w) / (6500 - 2700);
      fast_filled_circle(it, k_knob_x, k_cy + scale_y(14), scale_s(10), col_white);
      fast_circle(it, k_knob_x, k_cy + scale_y(14), scale_s(11), col_black);
      char k_buf[48];
      std::snprintf(k_buf, sizeof(k_buf), "White Temp: %dK", k_clamped);
      fast_print(it, cx, k_cy - scale_y(12), font_small, col_white, TextAlign::CENTER, k_buf);

      const int b_cy = scale_y(258);
      render_modern_card(it, cx, b_cy, sl_card_w, 64, 16, card_bg_on, card_brd_off, nullptr, col_white, font_small);
      frame_hits().add_centered_rect(cx, b_cy, sl_card_w, scale_y(64),
                                     WidgetId::LIGHT_BRI_SLIDER, kHitTap | kHitHorizSlider | kHitFocusSlot);
      const int b_clamped = std::max(5, std::min(100, light_insp_bri_pct()));
      const int b_max_w = layout().light_bri_bar_w();
      const int b_fill_w = (b_max_w * b_clamped) / 100;
      if (b_fill_w > 6) {
        fill_round_rect_glass(it, layout().light_bri_bar_x0(), b_cy - scale_y(28), b_fill_w, scale_y(56),
                              static_cast<float>(scale_s(12)), Color(92, 68, 22), 152, 132);
      }
      char b_buf[48];
      std::snprintf(b_buf, sizeof(b_buf), "Brightness: %d%%", b_clamped);
      fast_print(it, cx, b_cy, font_small, col_white, TextAlign::CENTER, b_buf);
    }

    render_back_button(it, cx, font_small, layout().back_y, "< Back");
    return;
  }

  // ===========================================================================
  // SUBPAGE 1 / AREAS MODE: 5-SLOT AREA SWITCHER
  // ===========================================================================
  if (area_subpage == 1 || area_browse_mode == "areas" || area_browse_mode == "area_areas") {
    render_page_header(it, cx, "SELECT AREA", font_small, theme_primary);

    const auto *list_slots = rs.slots_for(latter_rpc::SlotChannel::AREA_LIST);
    const int list_page = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::AREA_LIST)];
    const int list_total_pages = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::AREA_LIST)];

    for (int i = 0; i < 5; i++) {
      const auto &s = list_slots[i];
      if (s.name.empty() && s.id.empty()) continue;
      const int y_mid = layout().slot5_y(i);
      const bool is_act = s.active || (!s.id.empty() && s.id == rs.area.active_area_id);
      const Color brd_c = is_act ? theme_primary : card_brd_off;
      const Color txt_c = is_act ? theme_primary : col_white;
      const int area_l = layout().list_card_left;
      render_modern_card(it, cx, y_mid, 276, 44, 12, is_act ? card_bg_on : card_bg_off, brd_c, nullptr, txt_c, font_small);
      frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                     static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + i),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
      if (is_act) fast_filled_circle(it, area_l + 19, y_mid, 6, theme_primary);
      else fast_circle(it, area_l + 19, y_mid, 5, col_slate);
      std::string fit_n = fit_text_to_width_font(s.name, layout().list_card_w - 54, font_small);
      fast_print(it, area_l + 37, y_mid, font_small, txt_c, TextAlign::CENTER_LEFT, fit_n.c_str());
    }

    if (list_total_pages > 1) {
      render_vertical_page_pips(it, list_page > 1, list_page < list_total_pages, col_bg, col_white, col_slate);
    }

    const bool hide_picker_back = rs.area.active_area_id.empty() &&
                                  !persisted_prefs().home_screen_enabled &&
                                  is_on_anchor_root(rs, persisted_prefs());
    if (!hide_picker_back) {
      render_back_button(it, cx, font_small, layout().back_y, "< Back");
    }
    return;
  }

  // ===========================================================================
  // SUBPAGE 0: MAIN AREA CONTROL (HEADER + CLIMATE + 3 TALL SLIDER CARDS + DOCK)
  // ===========================================================================
  std::string hdr = active_area_name.empty() ? "AREA CONTROL" : active_area_name;
  for (char &c : hdr) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  std::string fit_hdr = fit_text_to_width_font(hdr, scale_x(250), font_small);
  render_page_header(it, cx, fit_hdr.c_str(), font_small, theme_primary);
  frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(250), scale_y(36),
                                 WidgetId::AREA_PICKER_PILL, kHitTap | kHitFocusSlot);

  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(area_action_feedback, scale_x(280), font_small);
    render_sub_header(it, cx, fit_fb.c_str(), font_small, theme_secondary);
  } else if (!std::isnan(area_temp) || !std::isnan(area_humidity)) {
    const int tele_y = layout().sub_hdr_y;
    const Color icon_c(185, 225, 255);

    auto draw_mini_thermometer = [&](int tx, int ty, Color c) {
      fast_rect_outline(it, tx - 2, ty - 9, 5, 11, c);
      fast_filled_circle(it, tx, ty + 4, 4, c);
      fast_vline(it, tx, ty - 4, 7, c);
      fast_hline(it, tx + 4, ty - 6, 3, c);
      fast_hline(it, tx + 4, ty - 2, 3, c);
    };

    auto draw_mini_droplet = [&](int dx, int dy, Color c) {
      fast_line(it, dx, dy - 8, dx - 5, dy + 1, c);
      fast_line(it, dx, dy - 8, dx + 5, dy + 1, c);
      for (int i = 17; i < 43; i += 3) {
        const int i1 = std::min(43, i + 3);
        const int x0 = dx + static_cast<int>(std::roundf(5.2f * DIAL_SIN[i]));
        const int y0 = (dy + 2) - static_cast<int>(std::roundf(5.2f * DIAL_COS[i]));
        const int x1 = dx + static_cast<int>(std::roundf(5.2f * DIAL_SIN[i1]));
        const int y1 = (dy + 2) - static_cast<int>(std::roundf(5.2f * DIAL_COS[i1]));
        fast_line(it, x0, y0, x1, y1, c);
      }
      fast_pixel(it, dx - 2, dy + 3, c);
    };

    char t_str[16] = "", h_str[16] = "";
    int w_t = 0, w_h = 0, x_off = 0, base = 0, th = 0;
    const bool has_t = !std::isnan(area_temp);
    const bool has_h = !std::isnan(area_humidity);
    if (has_t) {
      std::snprintf(t_str, sizeof(t_str), "%.0f°", area_temp);
      font_small->measure(t_str, &w_t, &x_off, &base, &th);
    }
    if (has_h) {
      std::snprintf(h_str, sizeof(h_str), "%.0f%%", area_humidity);
      font_small->measure(h_str, &w_h, &x_off, &base, &th);
    }

    if (has_t && has_h) {
      const int total_w = 12 + 6 + w_t + 22 + 12 + 6 + w_h;
      const int left_x = cx - (total_w / 2);
      draw_mini_thermometer(left_x + 5, tele_y, icon_c);
      fast_print(it, left_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, t_str);
      const int drop_x = left_x + 12 + 6 + w_t + 22;
      draw_mini_droplet(drop_x + 5, tele_y, icon_c);
      fast_print(it, drop_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, h_str);
    } else if (has_t) {
      const int total_w = 12 + 6 + w_t;
      const int left_x = cx - (total_w / 2);
      draw_mini_thermometer(left_x + 5, tele_y, icon_c);
      fast_print(it, left_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, t_str);
    } else {
      const int total_w = 12 + 6 + w_h;
      const int left_x = cx - (total_w / 2);
      draw_mini_droplet(left_x + 5, tele_y, icon_c);
      fast_print(it, left_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, h_str);
    }
  } else {
    const char *sub_lbl = (area_domain_filter == "scene")   ? "Lighting Scenes"
                        : (area_domain_filter == "light")   ? "Lights"
                        : (area_domain_filter == "cover")   ? "Blinds & Curtains"
                        : (area_domain_filter == "fan")     ? "Fans"
                        : (area_domain_filter == "switch")  ? "Switches & Outlets"
                        : (area_domain_filter == "climate") ? "Climate & Thermostats"
                        : (area_domain_filter == "media")   ? "Area Media"
                        : "Tap Header to Switch Area";
    render_sub_header(it, cx, sub_lbl, font_small, col_slate);
  }

  auto draw_tall_card_icon = [&](int gx, int gy, const std::string &dtype,
                                 const std::string &name, const std::string &id_str,
                                 bool is_on, Color fg_c) {
    if (dtype == "light") {
      const Color bulb_c = is_on ? col_gold : fg_c;
      if (is_on) {
        fast_filled_circle(it, gx, gy - 4, 9, bulb_c);
        fill_rect_row_major(it, gx - 5, gy + 1, 11, 5, bulb_c);
      } else {
        fast_circle(it, gx, gy - 4, 9, bulb_c);
        fast_circle(it, gx, gy - 4, 8, bulb_c);
        fast_rect_outline(it, gx - 5, gy + 2, 11, 4, bulb_c);
      }
      fill_rect_row_major(it, gx - 4, gy + 7, 9, 3, fg_c);
      fast_hline(it, gx - 2, gy + 11, 5, fg_c);
    } else if (dtype == "cover") {
      const bool is_curt = is_curtain_entity(name, id_str);
      const Color cov_c = is_on ? theme_primary : fg_c;
      if (is_curt) {
        fill_rect_row_major(it, gx - 12, gy - 12, 25, 2, fg_c);
        fast_filled_triangle(it, gx - 11, gy - 10, gx - 2, gy - 10, gx - 11, gy + 3, cov_c);
        fill_rect_row_major(it, gx - 11, gy + 3, 5, 9, cov_c);
        fast_line(it, gx - 11, gy + 3, gx - 5, gy + 3, col_white);
        fast_filled_triangle(it, gx + 11, gy - 10, gx + 2, gy - 10, gx + 11, gy + 3, cov_c);
        fill_rect_row_major(it, gx + 7, gy + 3, 5, 9, cov_c);
        fast_line(it, gx + 6, gy + 3, gx + 11, gy + 3, col_white);
      } else {
        fast_rect_outline(it, gx - 11, gy - 11, 23, 22, fg_c);
        fill_rect_row_major(it, gx - 11, gy - 11, 23, 3, fg_c);
        const int num_slats = is_on ? 2 : 4;
        for (int s = 0; s < num_slats; s++) {
          const int sy = (gy - 5) + s * 4;
          fast_hline(it, gx - 8, sy,     17, cov_c);
          fast_hline(it, gx - 8, sy + 1, 17, cov_c);
        }
        fast_line(it, gx + 8, gy - 8, gx + 8, gy + 6, theme_secondary);
        fast_filled_circle(it, gx + 8, gy + 7, 2, theme_secondary);
      }
    } else if (dtype == "fan") {
      const Color fan_c = is_on ? Color(48, 196, 156) : fg_c;
      for (int dy = -13; dy <= 13; dy++) {
        for (int dx = -13; dx <= 13; dx++) {
          if (is_ceiling_fan_4blade_pixel(dx, dy)) {
            const int r2 = dx * dx + dy * dy;
            fast_pixel(it, gx + dx, gy + dy, (r2 <= 10 && is_on) ? col_white : fan_c);
          }
        }
      }
    } else if (dtype == "scene" || dtype == "script") {
      fast_line(it, gx, gy - 11, gx, gy + 11, col_gold);
      fast_line(it, gx - 11, gy, gx + 11, gy, col_gold);
      fast_line(it, gx - 6, gy - 6, gx + 6, gy + 6, col_gold);
      fast_line(it, gx - 6, gy + 6, gx + 6, gy - 6, col_gold);
      fast_filled_circle(it, gx, gy, 3, col_white);
    } else if (dtype == "climate") {
      const Color cl_c = is_on ? col_gold : fg_c;
      fast_rect_outline(it, gx - 3, gy - 12, 7, 15, fg_c);
      fast_filled_circle(it, gx, gy + 6, 6, cl_c);
      fill_rect_row_major(it, gx - 1, gy - 6, 3, 10, cl_c);
      fast_line(it, gx + 6, gy - 9, gx + 9, gy - 9, theme_secondary);
      fast_line(it, gx + 6, gy - 5, gx + 8, gy - 5, theme_secondary);
      fast_line(it, gx + 6, gy - 1, gx + 9, gy - 1, theme_secondary);
    } else if (dtype == "media_player") {
      ui::draw_icon_eighth_notes(it, gx + 2, gy - 1, theme_primary);
    } else {
      const Color sw_c = is_on ? theme_primary : kChromeCardBrdOn;
      fast_filled_circle(it, gx - 5, gy, 7, sw_c);
      fast_filled_circle(it, gx + 5, gy, 7, sw_c);
      fill_rect_row_major(it, gx - 5, gy - 7, 10, 15, sw_c);
      fast_filled_circle(it, is_on ? (gx + 5) : (gx - 5), gy, 5, col_white);
    }
  };

  const auto *ent_slots = rs.slots_for(latter_rpc::SlotChannel::AREA_ENTITIES);
  const int browse_page = rs.slot_page[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)];
  const int browse_total_pages = rs.slot_total_pages[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)];
  const int card_slots = static_cast<int>(std::min<size_t>(DeviceRuntimeState::kCardSlotsPerPage, 3));
  bool any_card = false;

  for (int idx = 0; idx < card_slots; idx++) {
    const auto &s = ent_slots[idx];
    const std::string &raw_name = s.name;
    const std::string &id_str = s.id;
    const std::string &dtype = s.type;
    const bool is_act = s.active;
    const int level = s.level;
    const int y_mid = layout().area_card3_y(idx);

    if (raw_name.empty() && id_str.empty()) continue;
    any_card = true;

    if (dtype == "text" || id_str.empty()) {
      std::string fit_msg = fit_text_to_width_font(raw_name, scale_x(280), font_small);
      render_modern_card(it, cx, y_mid, 316, 64, 16, card_bg_off, card_brd_off,
                         fit_msg.c_str(), col_slate, font_small);
      continue;
    }

    const bool has_slider = (dtype == "light" || dtype == "cover" || dtype == "fan") && (level >= 0);
    const Color brd_c = is_act ? theme_primary : card_brd_off;
    const Color bg_c  = is_act ? card_bg_on    : card_bg_off;

    Color fill_c = scale_color(theme_primary, 0.48f);
    if (dtype == "light") fill_c = Color(92, 68, 22);
    else if (dtype == "cover") fill_c = Color(34, 68, 96);
    else if (dtype == "fan") fill_c = Color(24, 76, 64);

    const int r_card_w   = layout().area_card3_w;
    const int r_card_h   = layout().area_card3_h;
    const int r_left     = layout().area_card_left();
    const int r_right    = layout().area_card_right();
    const int r_div_x    = layout().area_card_div_x();
    const int r_slider_w = (r_right - 5) - (r_div_x + 1);
    render_modern_card(it, cx, y_mid, r_card_w, 64, 16, bg_c, brd_c, nullptr, col_white, font_small);

    if (has_slider && is_act && level > 0 && r_slider_w > 10) {
      const int clamped_lvl = std::max(0, std::min(100, level));
      const int fill_w = (r_slider_w * clamped_lvl) / 100;
      if (fill_w > 4) {
        const int fh = r_card_h - 6;
        fill_round_rect_glass(it, r_div_x + 1, y_mid - fh / 2, fill_w, fh, 0.0f, fill_c, 152, 132);
        fast_vline(it, r_div_x + fill_w, y_mid - fh / 2 + 1, fh - 2, scale_color(col_white, 0.35f));
      }
    }

    draw_tall_card_icon(r_left + scale_x(32), y_mid, dtype, raw_name, id_str, is_act, is_act ? col_white : col_slate);
    fast_line(it, r_div_x, y_mid - scale_y(22), r_div_x, y_mid + scale_y(22), brd_c);

    std::string clean_name = strip_trailing_paren_state(raw_name);
    const int max_t_w = (dtype == "light") ? (r_slider_w - scale_x(58)) : (r_slider_w - scale_x(20));
    std::string fit_title = fit_text_to_width_font(clean_name, std::max(80, max_t_w), font_small);
    fast_print(it, r_div_x + scale_x(13), y_mid - scale_y(11), font_small, is_act ? col_white : col_slate, TextAlign::CENTER_LEFT, fit_title.c_str());

    char sub_buf[40];
    if (dtype == "light") {
      if (!is_act) std::snprintf(sub_buf, sizeof(sub_buf), "Off");
      else if (level > 0) std::snprintf(sub_buf, sizeof(sub_buf), "On  •  %d%%", level);
      else std::snprintf(sub_buf, sizeof(sub_buf), "On");
    } else if (dtype == "cover") {
      const bool is_curt = is_curtain_entity(raw_name, id_str);
      const char *kind_s = is_curt ? "Curtains" : "Blinds";
      if (!is_act || level == 0) std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  Closed", kind_s);
      else if (level >= 99) std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  Open", kind_s);
      else std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d%% Open", kind_s, level);
    } else if (dtype == "fan") {
      if (!is_act || level == 0) std::snprintf(sub_buf, sizeof(sub_buf), "Off");
      else std::snprintf(sub_buf, sizeof(sub_buf), "Running  •  %d%%", level);
    } else if (dtype == "climate") {
      if (level > 0) std::snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d°", is_act ? "Active" : "Idle", level);
      else std::snprintf(sub_buf, sizeof(sub_buf), is_act ? "Active" : "Off");
    } else if (dtype == "scene" || dtype == "script") {
      std::snprintf(sub_buf, sizeof(sub_buf), "Tap to Activate");
    } else if (dtype == "media_player") {
      std::snprintf(sub_buf, sizeof(sub_buf), is_act ? "Playing  •  Open Music" : "Idle  •  Open Music");
    } else {
      std::snprintf(sub_buf, sizeof(sub_buf), is_act ? "On" : "Off");
    }
    const Color sub_c = is_act ? theme_secondary : kChromeMuted;
    fast_print(it, r_div_x + scale_x(13), y_mid + scale_y(14), font_small, sub_c, TextAlign::CENTER_LEFT, sub_buf);

    frame_hits().add_rect(r_left, y_mid - r_card_h / 2, r_div_x - r_left, r_card_h,
                          static_cast<uint16_t>(WidgetId::AREA_CARD_TOGGLE + idx),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));

    int right_limit = r_right;
    if (dtype == "light") {
      const int ox = layout().area_card_orb_x();
      const int orb_r = scale_s(16);
      const int dot_r = scale_s(4);
      right_limit = ox - orb_r - 4;
      fast_filled_circle(it, ox, y_mid, orb_r, Color(20, 24, 32));
      fast_filled_circle(it, ox - scale_s(5), y_mid - scale_s(4), dot_r, Color(255, 95, 95));
      fast_filled_circle(it, ox + scale_s(5), y_mid - scale_s(4), dot_r, Color(90, 225, 135));
      fast_filled_circle(it, ox,             y_mid + scale_s(5), dot_r, Color(95, 165, 255));
      fast_circle(it, ox, y_mid, orb_r, is_act ? col_white : col_slate);
      frame_hits().add_circle(ox, y_mid, orb_r,
                              WidgetId::LIGHT_ORB_BTN, kHitTap | kHitFocusCircle,
                              static_cast<int16_t>(idx), scale_s(14));
    }

    const int body_w = std::max(8, right_limit - (r_div_x + 1));
    if (has_slider) {
      frame_hits().add_rect(r_div_x + 1, y_mid - r_card_h / 2, body_w, r_card_h,
                            static_cast<uint16_t>(WidgetId::AREA_CARD_SLIDER + idx),
                            kHitTap | kHitHorizSlider, static_cast<int16_t>(idx));
    } else {
      frame_hits().add_rect(r_div_x + 1, y_mid - r_card_h / 2, body_w, r_card_h,
                            static_cast<uint16_t>(WidgetId::AREA_CARD_TAP + idx),
                            kHitTap, static_cast<int16_t>(idx));
    }
  }

  if (!any_card) {
    render_empty_state(it, cx, layout().area_card3_y(1), "No controls in this Area", font_small, col_slate);
  }

  if (browse_total_pages > 1) {
    render_vertical_page_pips(it, browse_page > 1, browse_page < browse_total_pages, col_bg, col_white, col_slate);
  }

  render_area_bottom_dock(area_domain_filter);
}

void render_whole_home_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const std::string &alarm_state = (ac.epoch == 0 && ac.alarm_entity.empty())
                                       ? ac.alarm_state
                                       : rs.area.alarm_state;
  const std::string &area_action_feedback = rs.area.action_feedback;
  const uint32_t area_feedback_timer = rs.area.feedback_timer;

  auto *font_date = ctx.font_date;
  auto *font_small = ctx.font_small;
  const Color &theme_primary = ctx.theme_primary;
  const Color &theme_secondary = ctx.theme_secondary;
  const Color &col_white = ctx.col_white;
  const Color &col_slate = ctx.col_slate;
  const Color &col_gold = ctx.col_gold;
  const Color &col_green = ctx.col_green;
  const Color &col_red = ctx.col_red;

  const bool is_fb = (now_ms - area_feedback_timer < 1500U && !area_action_feedback.empty());
  const Color card_bg_on = ui::chrome_card_bg_on();
  const Color card_bg_off = ui::chrome_card_bg_off();
  const Color card_brd_off = ui::chrome_card_brd_off();
  int tabs[4] = {0, 0, 0, 0};
  const int tab_count = collect_home_hub_tabs(tabs, ac);
  if (tab_count <= 0) {
    render_page_header(it, cx, "HOME HUB", font_small, theme_primary);
    render_empty_state(it, cx, layout().area_card3_y(1), "No Home Controls configured", font_small, col_slate);
    return;
  }

  int active_slot = -1;
  for (int i = 0; i < tab_count; i++) {
    if (tabs[i] == whole_home_tab()) {
      active_slot = i;
      break;
    }
  }
  if (active_slot < 0) {
    whole_home_tab() = tabs[0];
    active_slot = 0;
  }
  const int tab = tabs[active_slot];

  if (tab == 0) {
    // TAB 0: SECURITY ALARM (Tri-state: unknown/unconfigured never claims "Disarmed")
    render_page_header(it, cx, "SECURITY ALARM", font_small, theme_primary);

    std::string st_lbl = "Status: Unknown";
    Color st_col = col_slate;
    if (!alarm_state.empty()) {
      if (alarm_state == "disarmed") { st_lbl = "Status: Disarmed"; st_col = col_green; }
      else if (alarm_state == "armed_home") { st_lbl = "Status: Armed Home"; st_col = col_gold; }
      else if (alarm_state == "armed_away") { st_lbl = "Status: Armed Away"; st_col = col_red; }
      else if (alarm_state == "armed_night") { st_lbl = "Status: Armed Night"; st_col = col_gold; }
      else if (alarm_state == "arming" || alarm_state == "pending") { st_lbl = "Status: Arming..."; st_col = col_gold; }
      else if (alarm_state == "triggered") { st_lbl = "ALARM TRIGGERED!"; st_col = col_red; }
      else { st_lbl = "Status: " + alarm_state; st_col = theme_secondary; }
    }
    render_sub_header(it, cx, is_fb ? area_action_feedback.c_str() : st_lbl.c_str(),
                      font_small, is_fb ? theme_secondary : st_col);

    const bool is_disarmed = (alarm_state == "disarmed");
    const bool is_home     = (alarm_state == "armed_home" || alarm_state == "armed_night");
    const bool is_away     = (alarm_state == "armed_away");

    render_modern_card(it, cx, layout().area_card3_y(0), 296, 58, 14,
                       is_disarmed ? Color(18, 36, 26) : card_bg_on,
                       is_disarmed ? col_green : card_brd_off,
                       "Disarm", col_white, font_date);
    ui::draw_icon_shield(it, cx - scale_x(118), layout().area_card3_y(0), is_disarmed ? col_green : col_slate);
    frame_hits().add_centered_rect(cx, layout().area_card3_y(0), scale_x(296), scale_y(58),
                                   static_cast<uint16_t>(WidgetId::ALARM_MODE_BASE + 0),
                                   kHitTap | kHitFocusSlot, 0);

    render_modern_card(it, cx, layout().area_card3_y(1), 296, 58, 14,
                       is_home ? Color(38, 32, 16) : card_bg_on,
                       is_home ? col_gold : card_brd_off,
                       "Arm Home", col_white, font_date);
    frame_hits().add_centered_rect(cx, layout().area_card3_y(1), scale_x(296), scale_y(58),
                                   static_cast<uint16_t>(WidgetId::ALARM_MODE_BASE + 1),
                                   kHitTap | kHitFocusSlot, 1);

    render_modern_card(it, cx, layout().area_card3_y(2), 296, 58, 14,
                       is_away ? Color(40, 18, 22) : card_bg_on,
                       is_away ? col_red : card_brd_off,
                       "Arm Away", col_white, font_date);
    frame_hits().add_centered_rect(cx, layout().area_card3_y(2), scale_x(296), scale_y(58),
                                   static_cast<uint16_t>(WidgetId::ALARM_MODE_BASE + 2),
                                   kHitTap | kHitFocusSlot, 2);

  } else if (tab == 1) {
    render_page_header(it, cx, "THERMOSTAT", font_small, theme_primary);
    char sub_t[48];
    const std::string &m = sim_thermostat_mode();
    const char *m_lbl = m.empty() ? "Mode: Unknown"
                      : (m == "heat") ? "Heating"
                      : (m == "cool") ? "Cooling"
                      : (m == "off")  ? "System Off"
                      : "Auto Climate";
    const float cur_f = whole_home_state().thermostat_current_f;
    if (!std::isnan(cur_f) && cur_f > 20.0f) {
      std::snprintf(sub_t, sizeof(sub_t), "%s  •  Indoor %.0f°", m_lbl, cur_f);
    } else {
      std::snprintf(sub_t, sizeof(sub_t), "%s", m_lbl);
    }
    render_sub_header(it, cx, is_fb ? area_action_feedback.c_str() : sub_t,
                      font_small, is_fb ? theme_secondary : col_slate);

    const int hero_y = scale_y(156);
    const int mode_y = scale_y(266);
    const Color mode_brd = (m == "heat") ? col_gold
                         : ((m == "cool") ? Color(75, 175, 255)
                         : ((m == "auto" || m == "heat_cool") ? col_green : card_brd_off));
    render_modern_card(it, cx, hero_y, 312, 84, 18, card_bg_on, mode_brd, nullptr, col_white, font_date);
    const int minus_x = cx - scale_x(115);
    const int plus_x  = cx + scale_x(115);
    const int btn_r   = scale_s(24);
    const int gl_s    = scale_s(8);

    render_glass_circle(it, minus_x, hero_y, btn_r, kChromeGlassTint, card_brd_off);
    fast_hline(it, minus_x - gl_s, hero_y,     gl_s * 2 + 1, col_white);
    fast_hline(it, minus_x - gl_s, hero_y + 1, gl_s * 2 + 1, col_white);
    frame_hits().add_circle(minus_x, hero_y, btn_r, WidgetId::CLIMATE_MINUS_BTN,
                            kHitTap | kHitFocusCircle, 0, scale_s(14));

    render_glass_circle(it, plus_x, hero_y, btn_r, kChromeGlassTint, card_brd_off);
    fast_hline(it, plus_x - gl_s, hero_y,     gl_s * 2 + 1, col_white);
    fast_hline(it, plus_x - gl_s, hero_y + 1, gl_s * 2 + 1, col_white);
    fast_vline(it, plus_x,     hero_y - gl_s, gl_s * 2 + 1, col_white);
    fast_vline(it, plus_x + 1, hero_y - gl_s, gl_s * 2 + 1, col_white);
    frame_hits().add_circle(plus_x, hero_y, btn_r, WidgetId::CLIMATE_PLUS_BTN,
                            kHitTap | kHitFocusCircle, 0, scale_s(14));

    char t_val[16];
    if (sim_thermostat_target_f() > 0) {
      std::snprintf(t_val, sizeof(t_val), "%d°", sim_thermostat_target_f());
    } else {
      std::snprintf(t_val, sizeof(t_val), "%s°", kUnknownGlyph);
    }
    fast_print(it, cx, hero_y, font_date, col_white, TextAlign::CENTER, t_val);
    ui::draw_icon_thermometer(it, cx - scale_x(58), hero_y, mode_brd, kChromeShadow);

    const int M_XS[4] = {cx - scale_x(129), cx - scale_x(43), cx + scale_x(43), cx + scale_x(129)};
    const char *M_NAMES[4] = {"Heat", "Cool", "Auto", "Off"};
    const char *M_IDS[4]   = {"heat", "cool", "auto", "off"};
    for (int i = 0; i < 4; i++) {
      const bool act = (m == M_IDS[i]) || (i == 2 && m == "heat_cool");
      render_modern_card(it, M_XS[i], mode_y, 76, 48, 12,
                         act ? card_bg_on : card_bg_off,
                         act ? mode_brd : card_brd_off,
                         M_NAMES[i], act ? col_white : col_slate, font_small);
      frame_hits().add_centered_rect(M_XS[i], mode_y, scale_x(76), scale_y(48),
                                     static_cast<uint16_t>(WidgetId::CLIMATE_MODE_BASE + i),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }

  } else if (tab == 2) {
    render_page_header(it, cx, "ENTRY & GARAGE", font_small, theme_primary);
    render_sub_header(it, cx, is_fb ? area_action_feedback.c_str() : "Tap Card to Toggle",
                      font_small, is_fb ? theme_secondary : col_slate);

    const bool has_cfg_pair = (ac.epoch > 0U) && (!ac.garage_entity.empty() || !ac.lock_entity.empty());
    const bool show_garage = !has_cfg_pair || !ac.garage_entity.empty();
    const bool show_lock   = !has_cfg_pair || !ac.lock_entity.empty();
    const int garage_y = (show_garage && show_lock) ? scale_y(156) : scale_y(206);
    const int lock_y   = (show_garage && show_lock) ? scale_y(256) : scale_y(206);

    if (show_garage) {
      const GarageState gs = wh_garage_state();
      const Color g_bg  = (gs == GarageState::OPEN) ? Color(40, 24, 18)
                        : ((gs == GarageState::CLOSED) ? card_bg_on : card_bg_off);
      const Color g_brd = (gs == GarageState::OPEN) ? col_gold
                        : ((gs == GarageState::CLOSED) ? col_green : card_brd_off);
      const char *g_lbl = (gs == GarageState::OPEN)   ? "Garage: Open (Close)"
                        : (gs == GarageState::CLOSED) ? "Garage: Closed (Open)"
                        : "Garage: Unknown";
      render_modern_card(it, cx, garage_y, 308, 72, 16,
                         g_bg, g_brd, g_lbl,
                         (gs == GarageState::UNKNOWN) ? col_slate : col_white, font_small);
      frame_hits().add_centered_rect(cx, garage_y, scale_x(308), scale_y(72),
                                     WidgetId::COVER_OPEN_BTN, kHitTap | kHitFocusSlot);
    }

    if (show_lock) {
      const LockState ls = wh_lock_state();
      const Color l_bg  = (ls == LockState::LOCKED) ? Color(18, 36, 26)
                        : ((ls == LockState::UNLOCKED) ? Color(40, 18, 22) : card_bg_off);
      const Color l_brd = (ls == LockState::LOCKED) ? col_green
                        : ((ls == LockState::UNLOCKED) ? col_red : card_brd_off);
      const char *l_lbl = (ls == LockState::LOCKED)   ? "Front Door: Locked"
                        : (ls == LockState::UNLOCKED) ? "Front Door: Unlocked"
                        : "Front Door: Unknown";
      render_modern_card(it, cx, lock_y, 308, 72, 16,
                         l_bg, l_brd, l_lbl,
                         (ls == LockState::UNKNOWN) ? col_slate : col_white, font_small);
      ui::draw_icon_lock(it, cx - scale_x(122), lock_y,
                         (ls == LockState::LOCKED) ? col_green : ((ls == LockState::UNLOCKED) ? col_red : col_slate),
                         kChromeShadow, ls != LockState::UNLOCKED);
      frame_hits().add_centered_rect(cx, lock_y, scale_x(308), scale_y(72),
                                     WidgetId::LOCK_ACTION_BTN, kHitTap | kHitFocusSlot);
    }

  } else {
    render_page_header(it, cx, "ROBOT VACUUM", font_small, theme_primary);
    const std::string v_st = sim_vacuum_state().empty() ? "Status: Unknown" : ("Status: " + sim_vacuum_state());
    render_sub_header(it, cx, is_fb ? area_action_feedback.c_str() : v_st.c_str(),
                      font_small, is_fb ? theme_secondary : col_slate);

    const bool is_cln = (sim_vacuum_state() == "Cleaning");
    const bool is_psd = (sim_vacuum_state() == "Paused");
    const bool is_dck = (sim_vacuum_state() == "Docked");

    render_modern_card(it, cx, layout().area_card3_y(0), 296, 58, 14,
                       is_cln ? Color(18, 36, 26) : card_bg_on,
                       is_cln ? col_green : card_brd_off,
                       "Start Cleaning", col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().area_card3_y(0), scale_x(296), scale_y(58),
                                   static_cast<uint16_t>(WidgetId::HUB_ROW_ACTION + 0),
                                   kHitTap | kHitFocusSlot, 0);

    render_modern_card(it, cx, layout().area_card3_y(1), 296, 58, 14,
                       is_psd ? Color(38, 32, 16) : card_bg_on,
                       is_psd ? col_gold : card_brd_off,
                       "Pause Vacuum", col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().area_card3_y(1), scale_x(296), scale_y(58),
                                   static_cast<uint16_t>(WidgetId::HUB_ROW_ACTION + 1),
                                   kHitTap | kHitFocusSlot, 1);

    render_modern_card(it, cx, layout().area_card3_y(2), 296, 58, 14,
                       is_dck ? Color(22, 32, 46) : card_bg_on,
                       is_dck ? theme_primary : card_brd_off,
                       "Return to Dock", col_white, font_small);
    frame_hits().add_centered_rect(cx, layout().area_card3_y(2), scale_x(296), scale_y(58),
                                   static_cast<uint16_t>(WidgetId::HUB_ROW_ACTION + 2),
                                   kHitTap | kHitFocusSlot, 2);
  }

  // Bottom Whole-Home In-App Segmented Mode Bar at layout().app_dock_y
  int WH_XS[4] = {0, 0, 0, 0};
  bool wh_dl = false, wh_dr = false;
  get_mode_bar_window_xs(tab_count, 0, cx, WH_XS, wh_dl, wh_dr);
  const int WH_Y = layout().app_dock_y;
  render_in_app_mode_bar_track(it, cx, WH_XS, tab_count, active_slot, false, false,
                               tab_count, active_slot, theme_primary, col_white);
  for (int i = 0; i < tab_count; i++) {
    const int bx = WH_XS[i];
    const int cat_id = tabs[i];
    const bool is_act = (i == active_slot);
    const Color fg_c = is_act ? theme_primary : col_white;
    const Color cut_c = sample_glass_surface_color(bx - 14, WH_Y - 14, 29, 29);

    if (cat_id == 0) {
      static constexpr int8_t SH_HW[31] = {
         1,  4,  7, 10, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12,
        12, 11, 11, 11, 10, 10,  9,  9,  8,  7,  6,  5,  4,  3,  2,  1
      };
      for (int dy = -15; dy <= 15; dy++) {
        const int hw = SH_HW[dy + 15];
        fast_hline(it, bx - hw, WH_Y + dy, 2 * hw + 1, fg_c);
      }
      for (int dy = -8; dy <= 7; dy++) {
        for (int dx = -8; dx <= 8; dx++) {
          const int ax = std::abs(dx);
          bool carve = false;
          if (dy <= 0) {
            const int r_out = dy + 8;
            if (ax == r_out || ax == r_out - 1) carve = true;
            else if (dx >= 4 && dx <= 5 && dy >= -7 && dy <= -3) carve = true;
            else if (ax <= r_out - 3) carve = true;
          } else {
            if (ax <= 5 && !(ax <= 1 && dy >= 3)) carve = true;
          }
          if (carve) {
            fast_pixel(it, bx + dx, WH_Y + dy, cut_c);
          }
        }
      }
    } else if (cat_id == 1) {
      const int tx = bx - 2;
      fast_line(it, tx - 3, WH_Y - 13, tx + 3, WH_Y - 13, fg_c);
      fast_line(it, tx - 2, WH_Y - 14, tx + 2, WH_Y - 14, fg_c);
      fast_line(it, tx - 3, WH_Y - 13, tx - 3, WH_Y + 3,  fg_c);
      fast_line(it, tx - 2, WH_Y - 13, tx - 2, WH_Y + 3,  fg_c);
      fast_line(it, tx + 3, WH_Y - 13, tx + 3, WH_Y + 3,  fg_c);
      fast_line(it, tx + 2, WH_Y - 13, tx + 2, WH_Y + 3,  fg_c);
      fast_filled_circle(it, tx, WH_Y + 8, 6, fg_c);
      fast_filled_circle(it, tx, WH_Y + 8, 4, cut_c);
      fast_filled_circle(it, tx, WH_Y + 8, 2, fg_c);
      fill_rect_row_major(it, tx - 1, WH_Y - 5, 3, 12, fg_c);
      fill_rect_row_major(it, tx + 6, WH_Y - 11, 5, 2, fg_c);
      fill_rect_row_major(it, tx + 6, WH_Y - 6,  4, 2, fg_c);
      fill_rect_row_major(it, tx + 6, WH_Y - 1,  5, 2, fg_c);
    } else if (cat_id == 2) {
      fast_filled_triangle(it, bx, WH_Y - 14, bx - 15, WH_Y - 4, bx + 15, WH_Y - 4, fg_c);
      fill_rect_row_major(it, bx - 15, WH_Y - 4, 31, 2, fg_c);
      fill_rect_row_major(it, bx - 12, WH_Y - 2, 25, 14, fg_c);
      fast_hline(it, bx - 12, WH_Y - 2, 25, cut_c);
      fill_rect_row_major(it, bx - 9, WH_Y, 19, 12, cut_c);
      fill_rect_row_major(it, bx - 7, WH_Y + 1, 15, 3, fg_c);
      fill_rect_row_major(it, bx - 7, WH_Y + 5, 15, 3, fg_c);
      fill_rect_row_major(it, bx - 7, WH_Y + 9, 15, 3, fg_c);
    } else {
      fast_line(it, bx - 9, WH_Y - 9, bx - 14, WH_Y - 13, fg_c);
      fast_line(it, bx - 8, WH_Y - 10, bx - 12, WH_Y - 14, fg_c);
      fast_line(it, bx + 9, WH_Y - 9, bx + 14, WH_Y - 13, fg_c);
      fast_line(it, bx + 8, WH_Y - 10, bx + 12, WH_Y - 14, fg_c);
      fast_filled_circle(it, bx, WH_Y + 1, 12, fg_c);
      fast_hline(it, bx - 8, WH_Y - 6, 17, cut_c);
      fast_hline(it, bx - 9, WH_Y - 5, 19, cut_c);
      fast_line(it, bx - 9, WH_Y - 5, bx - 11, WH_Y - 2, cut_c);
      fast_line(it, bx + 9, WH_Y - 5, bx + 11, WH_Y - 2, cut_c);
      fast_filled_circle(it, bx, WH_Y + 2, 5, cut_c);
      fast_filled_circle(it, bx, WH_Y + 2, 3, fg_c);
      fast_hline(it, bx - 3, WH_Y + 9, 7, cut_c);
    }
  }
}

}  // namespace latter_ui
