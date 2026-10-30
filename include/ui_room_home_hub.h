#pragma once

// ==============================================================================
// Latter Core UI Module: Area Room Control & Whole-Home Hub (ui_room_home_hub.h)
// ==============================================================================

#include "ui_layout.h"
#include "ui_clock_home.h"

namespace latter_ui {

// =============================================================================
// SECTION 16C: AREA-AWARE ROOM CONTROL ENGINE (PAGE 6 — HORIZONTAL FILL-SLIDERS)
// =============================================================================

// 90-Degree Symmetric 4-Blade Ceiling Fan Pixel Predicate (Central Motor Hub + 1px Moat + 4 Swept Paddle Blades)
inline bool is_ceiling_fan_blade_arm(float u, float v) {
  if (v < 4.2f || v > 13.0f) return false;
  if (v <= 9.6f) {
    float u_min = -1.5f;
    float u_max = 1.5f + (v - 4.2f) * 0.44f;
    return (u >= u_min && u <= u_max);
  }
  float du = (u - 1.2f) / 3.1f;
  float dv = (v - 9.6f) / 3.3f;
  return (du * du + dv * dv) <= 1.0f;
}

inline bool is_ceiling_fan_4blade_pixel(int dx, int dy) {
  int r2 = dx * dx + dy * dy;
  if (r2 <= 1) return false;   // Negative-space center motor pin
  if (r2 <= 10) return true;   // Solid central motor housing (r ~ 3.2px)
  if (r2 <= 18) return false;  // Crisp negative-space collar moat around motor hub
  if (r2 > 172) return false;
  float fx = static_cast<float>(dx);
  float fy = static_cast<float>(dy);
  return is_ceiling_fan_blade_arm(fx, -fy) ||  // Top blade
         is_ceiling_fan_blade_arm(fy,  fx) ||  // Right blade
         is_ceiling_fan_blade_arm(-fx, fy) ||  // Bottom blade
         is_ceiling_fan_blade_arm(-fy, -fx);   // Left blade
}

inline bool is_curtain_entity(const std::string &name, const std::string &id_str) {
  std::string s = name + " " + id_str;
  for (char &c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return (s.find("curtain") != std::string::npos ||
          s.find("drape") != std::string::npos ||
          s.find("sheer") != std::string::npos);
}

inline std::string strip_trailing_paren_state(const std::string &s) {
  if (s.size() > 4 && s.back() == ')') {
    size_t lp = s.rfind(" (");
    if (lp != std::string::npos && lp > 0) {
      return s.substr(0, lp);
    }
  }
  return s;
}

// Dynamic Per-Room Category State & Helpers:
// Canonical Category Codes:
//   "scene"  = Lighting Scenes
//   "light"  = Lights
//   "cover"  = Curtains & Blinds
//   "fan"    = Ceiling & Room Fans
//   "switch" = Switches, Outlets & Helpers
//   "all"    = Optional Master All-Devices View (disabled by default)
inline std::string &room_available_domains_csv() {
  static std::string s = "";
  return s;
}

inline int &room_cat_dock_page() {
  static int pg = 0;
  return pg;
}

// Light Color & Kelvin Inspector State (room_subpage == 2)
inline std::string &light_insp_entity_id() { static std::string s = ""; return s; }
inline std::string &light_insp_name() { static std::string s = "Light"; return s; }
inline int &light_insp_mode() { static int m = 0; return m; } // 0 = HSV Color Wheel, 1 = White Kelvin + Brightness
inline float &light_insp_hue_deg() { static float h = 35.0f; return h; }
inline float &light_insp_sat() { static float s = 0.85f; return s; }
inline int &light_insp_kelvin() { static int k = 3500; return k; }
inline int &light_insp_bri_pct() { static int b = 80; return b; }
inline std::string &room_color_hex_param() { static std::string s = "#FFAA55"; return s; }

// Whole-Home Hub State & Built-In Demo Simulator State (Page 9, Tabs 0..3)
inline int &whole_home_tab() { static int t = 0; return t; } // 0=Alarm, 1=Thermostat, 2=Garage/Locks, 3=Vacuum
inline int &sim_thermostat_target_f() { static int tf = 71; return tf; }
inline std::string &sim_thermostat_mode() { static std::string m = "auto"; return m; } // "heat", "cool", "auto", "off"
inline bool &sim_garage_open() { static bool o = false; return o; }
inline bool &sim_front_door_locked() { static bool l = true; return l; }
inline std::string &sim_vacuum_state() { static std::string v = "Docked"; return v; } // "Cleaning", "Paused", "Docked"

inline const char *map_room_default_label_to_domain(const std::string &opt) {
  if (opt == "Lights") return "light";
  if (opt == "Blinds & Curtains") return "cover";
  if (opt == "Fans") return "fan";
  if (opt == "Switches") return "switch";
  if (opt == "Media") return "media";
  if (opt == "All Devices") return "all";
  return "scene"; // Default: Scenes
}

inline int collect_room_categories(bool show_all_cat, std::string out_cats[7]) {
  int n = 0;
  const std::string &csv = room_available_domains_csv();
  auto has_tok = [&](const char *tok) -> bool {
    if (csv.empty() || csv == "unknown" || csv == "unavailable") {
      return true;
    }
    return (csv.find(tok) != std::string::npos);
  };

  if (show_all_cat) out_cats[n++] = "all";
  if (has_tok("scene") && n < 7)  out_cats[n++] = "scene";
  if (has_tok("light") && n < 7)  out_cats[n++] = "light";
  if (has_tok("cover") && n < 7)  out_cats[n++] = "cover";
  if (has_tok("fan") && n < 7)    out_cats[n++] = "fan";
  if (has_tok("switch") && n < 7) out_cats[n++] = "switch";
  if (has_tok("media") && n < 7)  out_cats[n++] = "media";

  if (n == 0) {
    out_cats[n++] = "light";
  }
  return n;
}

inline std::string resolve_preferred_room_category(const std::string &pref_label, bool show_all_cat) {
  std::string cats[7];
  int cnt = collect_room_categories(show_all_cat, cats);
  std::string wanted = map_room_default_label_to_domain(pref_label);
  for (int i = 0; i < cnt; i++) {
    if (cats[i] == wanted) return wanted;
  }
  return cats[0];
}

template <typename DisplayT, typename FontDateT, typename FontSmallT>
inline void render_room_control_page(
    DisplayT &it, int cx, uint32_t now_ms,
    int room_subpage, const std::string &room_browse_mode,
    const std::string &room_domain_filter,
    const std::string &active_area_name,
    float room_temp, float room_humidity,
    const std::string &alarm_entity, const std::string &alarm_state,
    int room_browse_page, int room_browse_total_pages,
    const std::string &room_action_feedback, uint32_t room_feedback_timer,
    const std::string &s1_name, const std::string &s1_id, const std::string &s1_type, bool s1_act, int s1_lvl,
    const std::string &s2_name, const std::string &s2_id, const std::string &s2_type, bool s2_act, int s2_lvl,
    const std::string &s3_name, const std::string &s3_id, const std::string &s3_type, bool s3_act, int s3_lvl,
    const std::string &s4_name, const std::string &s4_id, const std::string &s4_type, bool s4_act, int s4_lvl,
    const std::string &s5_name, const std::string &s5_id, const std::string &s5_type, bool s5_act, int s5_lvl,
    FontDateT *font_date, FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_bg, esphome::Color col_black, esphome::Color col_white,
    esphome::Color col_slate, esphome::Color col_dark_gray,
    esphome::Color col_gold, esphome::Color col_green, esphome::Color col_red,
    bool show_all_cat = false) {
  using esphome::Color;
  using esphome::display::TextAlign;
  (void)col_dark_gray;
  (void)font_date;
  (void)alarm_entity;
  (void)alarm_state;
  (void)col_red;

  const bool is_fb = (now_ms - room_feedback_timer < 1500U && !room_action_feedback.empty());
  const Color card_bg_off(16, 20, 27);
  const Color card_bg_on(22, 28, 38);
  const Color card_brd_off(44, 52, 66);

  // Helper: Draw Dynamic Room Category Dock at y = 359, r = 27 (Fluid 1..4 Icon Window)
  auto render_room_bottom_dock = [&](const std::string &active_domain) {
    std::string cats[7];
    int cat_count = collect_room_categories(show_all_cat, cats);
    if (cat_count <= 0) return;

    int total_cat_pages = (cat_count + 3) / 4;
    if (room_cat_dock_page() < 0 || room_cat_dock_page() >= total_cat_pages) {
      room_cat_dock_page() = 0;
    }
    int win_xs[4] = {0, 0, 0, 0};
    bool dummy_l = false, dummy_r = false;
    get_home_dock_window_xs(cat_count, room_cat_dock_page(), cx, win_xs, dummy_l, dummy_r);
    int start_idx = get_home_dock_start_index(cat_count, room_cat_dock_page());
    int vis_count = std::min(4, cat_count - start_idx);

    const int ICON_Y = layout().app_dock_y;
    Color shadow_c(0, 0, 0);

    const int dr = layout().dock_r;
    for (int i = 0; i < vis_count; i++) {
      int bx = win_xs[i];
      int by = ICON_Y;
      const std::string &cat = cats[start_idx + i];
      bool is_act = (cat == active_domain);
      Color ring_c = is_act ? theme_primary : Color(90, 112, 138);
      Color fg_c   = is_act ? theme_primary : col_white;

      it.filled_circle(bx, by, dr - 1, Color(16, 20, 27));
      it.circle(bx + 1, by + 1, dr, shadow_c);
      it.circle(bx,     by,     dr, ring_c);
      if (is_act) {
        it.circle(bx, by, dr - 1, ring_c);
      }

      if (cat == "scene") {
        // Lighting Scenes: Sculpted 8-Ray Sparkle Star
        it.line(bx, by - 11, bx, by + 11, fg_c);
        it.line(bx - 11, by, bx + 11, by, fg_c);
        it.line(bx - 6, by - 6, bx + 6, by + 6, fg_c);
        it.line(bx - 6, by + 6, bx + 6, by - 6, fg_c);
        it.filled_circle(bx, by, 3, fg_c);
      } else if (cat == "light") {
        // Lights: Clean Classic Sculpted Lightbulb (No Ray Lines!)
        it.filled_circle(bx, by - 4, 8, fg_c);
        it.filled_rectangle(bx - 5, by + 1, 11, 5, fg_c);
        it.filled_rectangle(bx - 4, by + 7, 9, 3, fg_c);
        it.horizontal_line(bx - 2, by + 11, 5, fg_c);
      } else if (cat == "cover") {
        // Blinds / Curtains: Split Parted Curtain Drapes inside Window Frame
        it.rectangle(bx - 10, by - 9, 21, 19, fg_c);
        it.horizontal_line(bx - 10, by - 8, 21, fg_c);
        it.filled_triangle(bx - 9, by - 7, bx - 1, by - 7, bx - 9, by + 3, fg_c);
        it.filled_rectangle(bx - 9, by + 3, 4, 6, fg_c);
        it.filled_triangle(bx + 9, by - 7, bx + 1, by - 7, bx + 9, by + 3, fg_c);
        it.filled_rectangle(bx + 6, by + 3, 4, 6, fg_c);
      } else if (cat == "fan") {
        // Fans: Traditional 4-Blade Ceiling Fan (Central Motor Hub + 4 Swept Paddle Blades)
        for (int dy = -13; dy <= 13; dy++) {
          for (int dx = -13; dx <= 13; dx++) {
            if (is_ceiling_fan_4blade_pixel(dx, dy)) {
              it.draw_pixel_at(bx + dx, by + dy, fg_c);
            }
          }
        }
      } else if (cat == "switch") {
        // Switches & Outlets: Rounded Toggle Pill + Knob
        it.filled_circle(bx - 5, by, 7, fg_c);
        it.filled_circle(bx + 5, by, 7, fg_c);
        it.filled_rectangle(bx - 5, by - 7, 10, 15, fg_c);
        it.filled_circle(bx + 5, by, 4, Color(16, 20, 27));
      } else if (cat == "media") {
        // Room Media: Beamed Eighth Notes
        it.filled_circle(bx - 6, by + 6, 4, fg_c);
        it.filled_circle(bx + 5, by + 4, 4, fg_c);
        it.filled_rectangle(bx - 3, by - 9, 2, 15, fg_c);
        it.filled_rectangle(bx + 8, by - 11, 2, 15, fg_c);
        it.line(bx - 3, by - 9, bx + 9, by - 11, fg_c);
        it.line(bx - 3, by - 8, bx + 9, by - 10, fg_c);
      } else {
        // All Devices: 2x2 Rounded App-Tile Grid (3 Solid Squircles + 1 Framed Bottom-Right Tile)
        for (int dy = -10; dy <= 10; dy++) {
          for (int dx = -10; dx <= 10; dx++) {
            int ax = std::abs(dx), ay = std::abs(dy);
            if (ax < 2 || ax > 9 || ay < 2 || ay > 9) continue;
            int lx = ax - 2, ly = ay - 2; // 0..7
            if ((lx == 0 || lx == 7) && (ly == 0 || ly == 7)) continue;
            if (dx > 0 && dy > 0 && lx >= 2 && lx <= 5 && ly >= 2 && ly <= 5) continue;
            it.draw_pixel_at(bx + dx, by + dy, fg_c);
          }
        }
      }
    }

    if (total_cat_pages > 1) {
      render_dock_page_pips(it, cx, layout().app_dock_pips_y, total_cat_pages, room_cat_dock_page(),
                            col_white, Color(95, 112, 134));
    }
  };

  // ===========================================================================
  // SUBPAGE 2: LIGHT COLOR (HSV WHEEL) & WHITE KELVIN INSPECTOR
  // ===========================================================================
  if (room_subpage == 2) {
    // Top Mode Pill at y = 44 (Toggles between "Color Wheel" and "White Temp")
    const bool is_kelvin_mode = (light_insp_mode() == 1);
    render_modern_card(it, cx, 44, 212, 34, 10, card_bg_on, theme_primary,
                       is_kelvin_mode ? "Mode: White Temp >" : "Mode: Color Wheel >",
                       col_white, font_small);

    if (!is_kelvin_mode) {
      // MODE 0: 300px 5-Ring HSV Color Wheel for RGB Lights (cy = 212)
      const int wheel_cy = 212;
      it.circle(cx, wheel_cy, 150, card_brd_off);
      for (int a = 0; a < 60; a++) {
        float hue_01 = static_cast<float>(a) * (1.0f / 60.0f);
        float c_a = DIAL_COS[a];
        float s_a = DIAL_SIN[a];
        it.filled_circle(cx + static_cast<int>(c_a * 134.0f), wheel_cy + static_cast<int>(s_a * 134.0f), 14, hsv_to_rgb(hue_01, 1.00f, 1.0f));
        it.filled_circle(cx + static_cast<int>(c_a * 112.0f), wheel_cy + static_cast<int>(s_a * 112.0f), 13, hsv_to_rgb(hue_01, 0.82f, 1.0f));
        it.filled_circle(cx + static_cast<int>(c_a * 90.0f),  wheel_cy + static_cast<int>(s_a * 90.0f),  11, hsv_to_rgb(hue_01, 0.65f, 1.0f));
        it.filled_circle(cx + static_cast<int>(c_a * 70.0f),  wheel_cy + static_cast<int>(s_a * 70.0f),  10, hsv_to_rgb(hue_01, 0.45f, 1.0f));
      }
      Color preview_c = hsv_to_rgb(light_insp_hue_deg() / 360.0f, light_insp_sat(), 1.0f);
      it.filled_circle(cx, wheel_cy, 44, col_black);
      it.filled_circle(cx, wheel_cy, 39, preview_c);
      it.circle(cx, wheel_cy, 39, col_white);

      float rad = light_insp_hue_deg() * (PI_F / 180.0f);
      float r_sel = 70.0f + ((light_insp_sat() - 0.35f) / 0.65f) * 64.0f;
      r_sel = std::max(68.0f, std::min(136.0f, r_sel));
      int sel_x = cx + static_cast<int>(fast_cos(rad) * r_sel);
      int sel_y = wheel_cy + static_cast<int>(fast_sin(rad) * r_sel);
      it.filled_circle(sel_x, sel_y, 11, preview_c);
      it.circle(sel_x, sel_y, 11, col_white);
      it.circle(sel_x, sel_y, 12, col_black);
    } else {
      // MODE 1: White Color Temperature Slider (2700K Warm Amber <-> 6500K Cool Daylight) + Brightness Slider
      std::string fit_ln = fit_text_to_width_font(light_insp_name(), 260, font_small);
      it.print(cx, 86, font_small, theme_secondary, TextAlign::CENTER, fit_ln.c_str());

      // Card 1 (y_mid = 162): Color Temperature Gradient Slider (2700K .. 6500K)
      const int sl_card_w = layout().light_slider_card_w();
      const int k_bar_w   = layout().light_kelvin_bar_w();
      const int k_bar_x0  = layout().light_kelvin_bar_x0();
      render_modern_card(it, cx, 162, sl_card_w, 68, 16, card_bg_on, theme_primary, nullptr, col_white, font_small);
      for (int b = 0; b < 24; b++) {
        float t = static_cast<float>(b) / 23.0f;
        Color band_c = lerp_color(Color(255, 168, 72), Color(185, 222, 255), t);
        int bx0 = k_bar_x0 + (b * k_bar_w) / 24;
        int bx1 = k_bar_x0 + ((b + 1) * k_bar_w) / 24;
        it.filled_rectangle(bx0, 162 + 6, std::max(2, bx1 - bx0), 16, band_c);
      }
      int k_clamped = std::max(2700, std::min(6500, light_insp_kelvin()));
      int k_knob_x = k_bar_x0 + ((k_clamped - 2700) * k_bar_w) / (6500 - 2700);
      it.filled_circle(k_knob_x, 162 + 14, 10, col_white);
      it.circle(k_knob_x, 162 + 14, 11, col_black);
      char k_buf[48];
      snprintf(k_buf, sizeof(k_buf), "White Temp: %dK", k_clamped);
      it.print(cx, 162 - 12, font_small, col_white, TextAlign::CENTER, k_buf);

      // Card 2 (y_mid = 258): Brightness Fill-Slider (5% .. 100%)
      render_modern_card(it, cx, 258, sl_card_w, 64, 16, card_bg_on, card_brd_off, nullptr, col_white, font_small);
      int b_clamped = std::max(5, std::min(100, light_insp_bri_pct()));
      int b_max_w = layout().light_bri_bar_w();
      int b_fill_w = (b_max_w * b_clamped) / 100;
      if (b_fill_w > 6) {
        it.filled_rectangle(layout().light_bri_bar_x0(), 258 - 28, b_fill_w, 56, Color(92, 68, 22));
      }
      char b_buf[48];
      snprintf(b_buf, sizeof(b_buf), "Brightness: %d%%", b_clamped);
      it.print(cx, 258, font_small, col_white, TextAlign::CENTER, b_buf);
    }
    return;
  }

  // ===========================================================================
  // SUBPAGE 1 / AREAS MODE: 5-SLOT ROOM SWITCHER
  // ===========================================================================
  if (room_subpage == 1 || room_browse_mode == "areas" || room_browse_mode == "room_areas") {
    it.print(cx, layout().hdr_y, font_small, theme_primary, TextAlign::CENTER, "SELECT AREA");

    auto draw_area_row = [&](int y_mid, const std::string &name, const std::string &id_str, bool is_act) {
      if (name.empty() && id_str.empty()) return;
      Color brd_c = is_act ? theme_primary : card_brd_off;
      Color txt_c = is_act ? theme_primary : col_white;
      const int area_l = layout().list_card_left;
      render_modern_card(it, cx, y_mid, 276, 44, 12, is_act ? card_bg_on : card_bg_off, brd_c, nullptr, txt_c, font_small);
      if (is_act) it.filled_circle(area_l + 19, y_mid, 6, theme_primary);
      else it.circle(area_l + 19, y_mid, 5, col_slate);
      std::string fit_n = fit_text_to_width_font(name, layout().list_card_w - 54, font_small);
      it.print(area_l + 37, y_mid, font_small, txt_c, TextAlign::CENTER_LEFT, fit_n.c_str());
    };

    draw_area_row(layout().slot5_y(0), s1_name, s1_id, s1_act);
    draw_area_row(layout().slot5_y(1), s2_name, s2_id, s2_act);
    draw_area_row(layout().slot5_y(2), s3_name, s3_id, s3_act);
    draw_area_row(layout().slot5_y(3), s4_name, s4_id, s4_act);
    draw_area_row(layout().slot5_y(4), s5_name, s5_id, s5_act);

    if (room_browse_total_pages > 1) {
      render_vertical_page_pips(it, room_browse_page > 1, room_browse_page < room_browse_total_pages, col_bg, col_white, col_slate);
    }
    return;
  }

  // ===========================================================================
  // SUBPAGE 0: MAIN ROOM CONTROL (HEADER + CLIMATE + 3 TALL SLIDER CARDS + DOCK)
  // ===========================================================================
  // 1. Top Room Title (y = 44) & Live Room Climate Line (y = 74)
  std::string hdr = active_area_name.empty() ? "AREA CONTROL" : active_area_name;
  for (char &c : hdr) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  std::string fit_hdr = fit_text_to_width_font(hdr, 250, font_small);
  it.print(cx, layout().hdr_y, font_small, theme_primary, TextAlign::CENTER, fit_hdr.c_str());

  if (is_fb) {
    std::string fit_fb = fit_text_to_width_font(room_action_feedback, 280, font_small);
    it.print(cx, 74, font_small, theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  } else if (!std::isnan(room_temp) || !std::isnan(room_humidity)) {
    const int tele_y = 74;
    const Color icon_c(185, 225, 255);

    auto draw_mini_thermometer = [&](int tx, int ty, Color c) {
      it.rectangle(tx - 2, ty - 9, 5, 11, c);
      it.filled_circle(tx, ty + 4, 4, c);
      it.vertical_line(tx, ty - 4, 7, c);
      it.horizontal_line(tx + 4, ty - 6, 3, c);
      it.horizontal_line(tx + 4, ty - 2, 3, c);
    };

    auto draw_mini_droplet = [&](int dx, int dy, Color c) {
      it.line(dx, dy - 8, dx - 5, dy + 1, c);
      it.line(dx, dy - 8, dx + 5, dy + 1, c);
      for (int i = 17; i < 43; i += 3) {
        int i1 = std::min(43, i + 3);
        int x0 = dx + static_cast<int>(roundf(5.2f * DIAL_SIN[i]));
        int y0 = (dy + 2) - static_cast<int>(roundf(5.2f * DIAL_COS[i]));
        int x1 = dx + static_cast<int>(roundf(5.2f * DIAL_SIN[i1]));
        int y1 = (dy + 2) - static_cast<int>(roundf(5.2f * DIAL_COS[i1]));
        it.line(x0, y0, x1, y1, c);
      }
      it.draw_pixel_at(dx - 2, dy + 3, c);
    };

    char t_str[16] = "", h_str[16] = "";
    int w_t = 0, w_h = 0, x_off = 0, base = 0, th = 0;
    bool has_t = !std::isnan(room_temp);
    bool has_h = !std::isnan(room_humidity);
    if (has_t) {
      snprintf(t_str, sizeof(t_str), "%.0f°", room_temp);
      font_small->measure(t_str, &w_t, &x_off, &base, &th);
    }
    if (has_h) {
      snprintf(h_str, sizeof(h_str), "%.0f%%", room_humidity);
      font_small->measure(h_str, &w_h, &x_off, &base, &th);
    }

    if (has_t && has_h) {
      int total_w = 12 + 6 + w_t + 22 + 12 + 6 + w_h;
      int left_x = cx - (total_w / 2);
      draw_mini_thermometer(left_x + 5, tele_y, icon_c);
      it.print(left_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, t_str);
      int drop_x = left_x + 12 + 6 + w_t + 22;
      draw_mini_droplet(drop_x + 5, tele_y, icon_c);
      it.print(drop_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, h_str);
    } else if (has_t) {
      int total_w = 12 + 6 + w_t;
      int left_x = cx - (total_w / 2);
      draw_mini_thermometer(left_x + 5, tele_y, icon_c);
      it.print(left_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, t_str);
    } else {
      int total_w = 12 + 6 + w_h;
      int left_x = cx - (total_w / 2);
      draw_mini_droplet(left_x + 5, tele_y, icon_c);
      it.print(left_x + 12 + 6, tele_y, font_small, theme_secondary, TextAlign::CENTER_LEFT, h_str);
    }
  } else {
    const char *sub_lbl = (room_domain_filter == "scene") ? "Lighting Scenes"
                        : (room_domain_filter == "light") ? "Lights"
                        : (room_domain_filter == "cover") ? "Blinds & Curtains"
                        : (room_domain_filter == "fan")   ? "Fans"
                        : (room_domain_filter == "switch") ? "Switches & Outlets"
                        : "Tap Header to Switch Area";
    it.print(cx, 74, font_small, col_slate, TextAlign::CENTER, sub_lbl);
  }

  // Helper: Draw Enlarged Domain Vector Icon inside the Left 64x64 Card Well (gx = 108)
  auto draw_tall_card_icon = [&](int gx, int gy, const std::string &dtype,
                                 const std::string &name, const std::string &id_str,
                                 bool is_on, Color fg_c) {
    if (dtype == "light") {
      // Clean Classic Sculpted Lightbulb (No Ray Lines!)
      Color bulb_c = is_on ? col_gold : fg_c;
      if (is_on) {
        it.filled_circle(gx, gy - 4, 9, bulb_c);
        it.filled_rectangle(gx - 5, gy + 1, 11, 5, bulb_c);
      } else {
        it.circle(gx, gy - 4, 9, bulb_c);
        it.circle(gx, gy - 4, 8, bulb_c);
        it.rectangle(gx - 5, gy + 2, 11, 4, bulb_c);
      }
      it.filled_rectangle(gx - 4, gy + 7, 9, 3, fg_c);
      it.horizontal_line(gx - 2, gy + 11, 5, fg_c);
    } else if (dtype == "cover") {
      bool is_curt = is_curtain_entity(name, id_str);
      Color cov_c = is_on ? theme_primary : fg_c;
      if (is_curt) {
        // CURTAINS: Split Parted Drapes with Tie-Backs & Top Curtain Rod
        it.filled_rectangle(gx - 12, gy - 12, 25, 2, fg_c);
        // Left drape
        it.filled_triangle(gx - 11, gy - 10, gx - 2, gy - 10, gx - 11, gy + 3, cov_c);
        it.filled_rectangle(gx - 11, gy + 3, 5, 9, cov_c);
        it.line(gx - 11, gy + 3, gx - 5, gy + 3, col_white);
        // Right drape
        it.filled_triangle(gx + 11, gy - 10, gx + 2, gy - 10, gx + 11, gy + 3, cov_c);
        it.filled_rectangle(gx + 7, gy + 3, 5, 9, cov_c);
        it.line(gx + 6, gy + 3, gx + 11, gy + 3, col_white);
      } else {
        // BLINDS / SHADES: Window Frame + Horizontal Venetian Slats + Pull Cord
        it.rectangle(gx - 11, gy - 11, 23, 22, fg_c);
        it.filled_rectangle(gx - 11, gy - 11, 23, 3, fg_c);
        int num_slats = is_on ? 2 : 4;
        for (int s = 0; s < num_slats; s++) {
          int sy = (gy - 5) + s * 4;
          it.horizontal_line(gx - 8, sy,     17, cov_c);
          it.horizontal_line(gx - 8, sy + 1, 17, cov_c);
        }
        it.line(gx + 8, gy - 8, gx + 8, gy + 6, theme_secondary);
        it.filled_circle(gx + 8, gy + 7, 2, theme_secondary);
      }
    } else if (dtype == "fan") {
      // Traditional 4-Blade Ceiling Fan (Central Motor Hub + 4 Swept Paddle Blades)
      Color fan_c = is_on ? Color(48, 196, 156) : fg_c;
      for (int dy = -13; dy <= 13; dy++) {
        for (int dx = -13; dx <= 13; dx++) {
          if (is_ceiling_fan_4blade_pixel(dx, dy)) {
            int r2 = dx * dx + dy * dy;
            it.draw_pixel_at(gx + dx, gy + dy, (r2 <= 10 && is_on) ? col_white : fan_c);
          }
        }
      }
    } else if (dtype == "scene" || dtype == "script") {
      // Scene Sparkle Star
      it.line(gx, gy - 11, gx, gy + 11, col_gold);
      it.line(gx - 11, gy, gx + 11, gy, col_gold);
      it.line(gx - 6, gy - 6, gx + 6, gy + 6, col_gold);
      it.line(gx - 6, gy + 6, gx + 6, gy - 6, col_gold);
      it.filled_circle(gx, gy, 3, col_white);
    } else if (dtype == "climate") {
      // Sculpted Thermostat / Thermometer Bulb & Degree Ticks
      Color cl_c = is_on ? col_gold : fg_c;
      it.rectangle(gx - 3, gy - 12, 7, 15, fg_c);
      it.filled_circle(gx, gy + 6, 6, cl_c);
      it.filled_rectangle(gx - 1, gy - 6, 3, 10, cl_c);
      it.line(gx + 6, gy - 9, gx + 9, gy - 9, theme_secondary);
      it.line(gx + 6, gy - 5, gx + 8, gy - 5, theme_secondary);
      it.line(gx + 6, gy - 1, gx + 9, gy - 1, theme_secondary);
    } else if (dtype == "media_player") {
      // Room Speaker Note
      it.filled_circle(gx - 4, gy + 5, 4, theme_primary);
      it.filled_circle(gx + 5, gy + 3, 4, theme_primary);
      it.filled_rectangle(gx - 1, gy - 9, 2, 14, theme_primary);
      it.filled_rectangle(gx + 8, gy - 11, 2, 14, theme_primary);
      it.line(gx - 1, gy - 9, gx + 9, gy - 11, theme_primary);
      it.line(gx - 1, gy - 8, gx + 9, gy - 10, theme_primary);
    } else {
      // Switch / Outlet Toggle Pill
      Color sw_c = is_on ? theme_primary : Color(52, 62, 76);
      it.filled_circle(gx - 5, gy, 7, sw_c);
      it.filled_circle(gx + 5, gy, 7, sw_c);
      it.filled_rectangle(gx - 5, gy - 7, 10, 15, sw_c);
      it.filled_circle(is_on ? (gx + 5) : (gx - 5), gy, 5, col_white);
    }
  };

  // 2. Render 3 Tall Sculpted Slider Cards at y_mid = {132, 208, 284} (w = 316, h = 64, r = 16 -> x = 75..391)
  auto draw_tall_room_card = [&](int y_mid, const std::string &raw_name, const std::string &id_str,
                                 const std::string &dtype, bool is_act, int level) {
    if (raw_name.empty() && id_str.empty()) return;

    if (dtype == "text" || id_str.empty()) {
      std::string fit_msg = fit_text_to_width_font(raw_name, 280, font_small);
      render_modern_card(it, cx, y_mid, 316, 64, 16, card_bg_off, card_brd_off,
                         fit_msg.c_str(), col_slate, font_small);
      return;
    }

    const bool has_slider = (dtype == "light" || dtype == "cover" || dtype == "fan") && (level >= 0);
    Color brd_c = is_act ? theme_primary : card_brd_off;
    Color bg_c  = is_act ? card_bg_on    : card_bg_off;

    // Domain-tinted slider fill bar color
    Color fill_c = scale_color(theme_primary, 0.48f);
    if (dtype == "light") fill_c = Color(92, 68, 22);       // Warm Amber-Gold glow
    else if (dtype == "cover") fill_c = Color(34, 68, 96);  // Steel-Cyan shade bar
    else if (dtype == "fan") fill_c = Color(24, 76, 64);    // Emerald-Teal airflow bar

    const int r_card_w   = layout().area_card3_w;
    const int r_card_h   = layout().area_card3_h;
    const int r_left     = layout().area_card_left();
    const int r_right    = layout().area_card_right();
    const int r_div_x    = layout().area_card_div_x();
    const int r_slider_w = (r_right - 5) - (r_div_x + 1);
    render_modern_card(it, cx, y_mid, r_card_w, 64, 16, bg_c, brd_c, nullptr, col_white, font_small);

    if (has_slider && is_act && level > 0 && r_slider_w > 10) {
      int clamped_lvl = std::max(0, std::min(100, level));
      int fill_w = (r_slider_w * clamped_lvl) / 100;
      if (fill_w > 4) {
        int fh = r_card_h - 6;
        it.filled_rectangle(r_div_x + 1, y_mid - fh / 2, fill_w, fh, fill_c);
        it.vertical_line(r_div_x + fill_w, y_mid - fh / 2 + 1, fh - 2, scale_color(col_white, 0.35f));
      }
    }

    draw_tall_card_icon(r_left + scale_x(32), y_mid, dtype, raw_name, id_str, is_act, is_act ? col_white : col_slate);
    it.line(r_div_x, y_mid - scale_y(22), r_div_x, y_mid + scale_y(22), brd_c);

    std::string clean_name = strip_trailing_paren_state(raw_name);
    const int max_t_w = (dtype == "light") ? (r_slider_w - scale_x(58)) : (r_slider_w - scale_x(20));
    std::string fit_title = fit_text_to_width_font(clean_name, std::max(80, max_t_w), font_small);
    it.print(r_div_x + scale_x(13), y_mid - scale_y(11), font_small, is_act ? col_white : col_slate, TextAlign::CENTER_LEFT, fit_title.c_str());

    char sub_buf[40];
    if (dtype == "light") {
      if (!is_act) snprintf(sub_buf, sizeof(sub_buf), "Off");
      else if (level > 0) snprintf(sub_buf, sizeof(sub_buf), "On  •  %d%%", level);
      else snprintf(sub_buf, sizeof(sub_buf), "On");
    } else if (dtype == "cover") {
      bool is_curt = is_curtain_entity(raw_name, id_str);
      const char *kind_s = is_curt ? "Curtains" : "Blinds";
      if (!is_act || level == 0) snprintf(sub_buf, sizeof(sub_buf), "%s  •  Closed", kind_s);
      else if (level >= 99) snprintf(sub_buf, sizeof(sub_buf), "%s  •  Open", kind_s);
      else snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d%% Open", kind_s, level);
    } else if (dtype == "fan") {
      if (!is_act || level == 0) snprintf(sub_buf, sizeof(sub_buf), "Off");
      else snprintf(sub_buf, sizeof(sub_buf), "Running  •  %d%%", level);
    } else if (dtype == "climate") {
      if (level > 0) snprintf(sub_buf, sizeof(sub_buf), "%s  •  %d°", is_act ? "Active" : "Idle", level);
      else snprintf(sub_buf, sizeof(sub_buf), is_act ? "Active" : "Off");
    } else if (dtype == "scene" || dtype == "script") {
      snprintf(sub_buf, sizeof(sub_buf), "Tap to Activate");
    } else if (dtype == "media_player") {
      snprintf(sub_buf, sizeof(sub_buf), is_act ? "Playing  •  Open Music" : "Idle  •  Open Music");
    } else {
      snprintf(sub_buf, sizeof(sub_buf), is_act ? "On" : "Off");
    }
    Color sub_c = is_act ? theme_secondary : Color(105, 115, 130);
    it.print(r_div_x + scale_x(13), y_mid + scale_y(14), font_small, sub_c, TextAlign::CENTER_LEFT, sub_buf);

    if (dtype == "light") {
      int ox = layout().area_card_orb_x();
      int orb_r = scale_s(16);
      int dot_r = scale_s(4);
      it.filled_circle(ox, y_mid, orb_r, Color(20, 24, 32));
      it.filled_circle(ox - scale_s(5), y_mid - scale_s(4), dot_r, Color(255, 95, 95));
      it.filled_circle(ox + scale_s(5), y_mid - scale_s(4), dot_r, Color(90, 225, 135));
      it.filled_circle(ox,             y_mid + scale_s(5), dot_r, Color(95, 165, 255));
      it.circle(ox, y_mid, orb_r, is_act ? col_white : col_slate);
    }
  };

  draw_tall_room_card(layout().area_card3_y(0), s1_name, s1_id, s1_type, s1_act, s1_lvl);
  draw_tall_room_card(layout().area_card3_y(1), s2_name, s2_id, s2_type, s2_act, s2_lvl);
  draw_tall_room_card(layout().area_card3_y(2), s3_name, s3_id, s3_type, s3_act, s3_lvl);

  // 3. Right-Flank Vertical Page Pips (when > 1 page of 3 cards)
  if (room_browse_total_pages > 1) {
    render_vertical_page_pips(it, room_browse_page > 1, room_browse_page < room_browse_total_pages, col_bg, col_white, col_slate);
  }

  // 4. Dynamic Bottom Category Dock
  render_room_bottom_dock(room_domain_filter);
}

// =============================================================================
// SECTION 16D: WHOLE-HOME HUB APP (PAGE 9 — ALARM, THERMOSTAT, GARAGE/LOCKS, VACUUM)
// =============================================================================
template <typename DisplayT, typename FontDateT, typename FontSmallT>
inline void render_security_alarm_page(
    DisplayT &it, int cx, uint32_t now_ms,
    const std::string &alarm_entity, const std::string &alarm_state,
    const std::string &room_action_feedback, uint32_t room_feedback_timer,
    FontDateT *font_date, FontSmallT *font_small,
    esphome::Color theme_primary, esphome::Color theme_secondary,
    esphome::Color col_white, esphome::Color col_slate,
    esphome::Color col_gold, esphome::Color col_green, esphome::Color col_red) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const bool is_fb = (now_ms - room_feedback_timer < 1500U && !room_action_feedback.empty());
  const Color card_bg_on(22, 28, 38);
  const Color card_bg_off(16, 20, 27);
  const Color card_brd_off(44, 52, 66);
  const int tab = whole_home_tab();

  if (tab == 0) {
    // TAB 0: HOME SECURITY ALARM
    it.print(cx, layout().hdr_y, font_small, theme_primary, TextAlign::CENTER, "SECURITY ALARM");

    std::string st_lbl = "Status: Ready (Demo)";
    Color st_col = col_green;
    if (!alarm_entity.empty() && alarm_entity != "none") {
      if (alarm_state == "disarmed") { st_lbl = "Status: Disarmed"; st_col = col_green; }
      else if (alarm_state == "armed_home") { st_lbl = "Status: Armed Home"; st_col = col_gold; }
      else if (alarm_state == "armed_away") { st_lbl = "Status: Armed Away"; st_col = col_red; }
      else if (alarm_state == "armed_night") { st_lbl = "Status: Armed Night"; st_col = col_gold; }
      else if (alarm_state == "arming" || alarm_state == "pending") { st_lbl = "Status: Arming..."; st_col = col_gold; }
      else if (alarm_state == "triggered") { st_lbl = "ALARM TRIGGERED!"; st_col = col_red; }
      else { st_lbl = "Status: " + alarm_state; st_col = theme_secondary; }
    } else if (!alarm_state.empty() && alarm_state != "none") {
      if (alarm_state == "disarmed") { st_lbl = "Status: Disarmed"; st_col = col_green; }
      else if (alarm_state == "armed_home") { st_lbl = "Status: Armed Home"; st_col = col_gold; }
      else if (alarm_state == "armed_away") { st_lbl = "Status: Armed Away"; st_col = col_red; }
    }
    it.print(cx, 74, font_small, is_fb ? theme_secondary : st_col, TextAlign::CENTER,
             is_fb ? room_action_feedback.c_str() : st_lbl.c_str());

    bool is_disarmed = (alarm_state == "disarmed" || alarm_state.empty() || alarm_state == "none");
    bool is_home     = (alarm_state == "armed_home" || alarm_state == "armed_night");
    bool is_away     = (alarm_state == "armed_away");

    render_modern_card(it, cx, layout().area_card3_y(0), 296, 58, 14,
                       is_disarmed ? Color(18, 36, 26) : card_bg_on,
                       is_disarmed ? col_green : card_brd_off,
                       "Disarm", col_white, font_date);
    render_modern_card(it, cx, layout().area_card3_y(1), 296, 58, 14,
                       is_home ? Color(38, 32, 16) : card_bg_on,
                       is_home ? col_gold : card_brd_off,
                       "Arm Home", col_white, font_date);
    render_modern_card(it, cx, layout().area_card3_y(2), 296, 58, 14,
                       is_away ? Color(40, 18, 22) : card_bg_on,
                       is_away ? col_red : card_brd_off,
                       "Arm Away", col_white, font_date);

  } else if (tab == 1) {
    it.print(cx, layout().hdr_y, font_small, theme_primary, TextAlign::CENTER, "THERMOSTAT");
    char sub_t[48];
    const std::string &m = sim_thermostat_mode();
    const char *m_lbl = (m == "heat") ? "Heating" : ((m == "cool") ? "Cooling" : ((m == "off") ? "System Off" : "Auto Climate"));
    snprintf(sub_t, sizeof(sub_t), "%s  •  Indoor 70°", m_lbl);
    it.print(cx, layout().sub_hdr_y, font_small, is_fb ? theme_secondary : col_slate, TextAlign::CENTER,
             is_fb ? room_action_feedback.c_str() : sub_t);

    const int hero_y = scale_y(156);
    const int mode_y = scale_y(266);
    Color mode_brd = (m == "heat") ? col_gold : ((m == "cool") ? Color(75, 175, 255) : ((m == "auto") ? col_green : card_brd_off));
    render_modern_card(it, cx, hero_y, 312, 84, 18, card_bg_on, mode_brd, nullptr, col_white, font_date);
    const int minus_x = cx - scale_x(115);
    const int plus_x  = cx + scale_x(115);
    const int btn_r   = scale_s(24);
    const int gl_s    = scale_s(8);
    it.filled_circle(minus_x, hero_y, btn_r, Color(30, 38, 52));
    it.circle(minus_x, hero_y, btn_r, card_brd_off);
    it.horizontal_line(minus_x - gl_s, hero_y,     gl_s * 2 + 1, col_white);
    it.horizontal_line(minus_x - gl_s, hero_y + 1, gl_s * 2 + 1, col_white);

    it.filled_circle(plus_x, hero_y, btn_r, Color(30, 38, 52));
    it.circle(plus_x, hero_y, btn_r, card_brd_off);
    it.horizontal_line(plus_x - gl_s, hero_y,     gl_s * 2 + 1, col_white);
    it.horizontal_line(plus_x - gl_s, hero_y + 1, gl_s * 2 + 1, col_white);
    it.vertical_line(plus_x,     hero_y - gl_s, gl_s * 2 + 1, col_white);
    it.vertical_line(plus_x + 1, hero_y - gl_s, gl_s * 2 + 1, col_white);

    char t_val[16];
    snprintf(t_val, sizeof(t_val), "%d°", sim_thermostat_target_f());
    it.print(cx, hero_y, font_date, col_white, TextAlign::CENTER, t_val);

    const int M_XS[4] = {cx - scale_x(129), cx - scale_x(43), cx + scale_x(43), cx + scale_x(129)};
    const char *M_NAMES[4] = {"Heat", "Cool", "Auto", "Off"};
    const char *M_IDS[4]   = {"heat", "cool", "auto", "off"};
    for (int i = 0; i < 4; i++) {
      bool act = (m == M_IDS[i]);
      render_modern_card(it, M_XS[i], mode_y, 76, 48, 12,
                         act ? card_bg_on : card_bg_off,
                         act ? mode_brd : card_brd_off,
                         M_NAMES[i], act ? col_white : col_slate, font_small);
    }

  } else if (tab == 2) {
    it.print(cx, layout().hdr_y, font_small, theme_primary, TextAlign::CENTER, "ENTRY & GARAGE");
    it.print(cx, layout().sub_hdr_y, font_small, is_fb ? theme_secondary : col_slate, TextAlign::CENTER,
             is_fb ? room_action_feedback.c_str() : "Tap Card to Toggle");

    bool g_open = sim_garage_open();
    render_modern_card(it, cx, scale_y(156), 308, 72, 16,
                       g_open ? Color(40, 24, 18) : card_bg_on,
                       g_open ? col_gold : col_green,
                       g_open ? "Garage: Open (Close)" : "Garage: Closed (Open)",
                       col_white, font_small);

    bool d_locked = sim_front_door_locked();
    render_modern_card(it, cx, scale_y(256), 308, 72, 16,
                       d_locked ? Color(18, 36, 26) : Color(40, 18, 22),
                       d_locked ? col_green : col_red,
                       d_locked ? "Front Door: Locked" : "Front Door: Unlocked",
                       col_white, font_small);

  } else {
    // TAB 3: ROBOT VACUUM
    it.print(cx, layout().hdr_y, font_small, theme_primary, TextAlign::CENTER, "ROBOT VACUUM");
    std::string v_st = "Status: " + sim_vacuum_state();
    it.print(cx, 74, font_small, is_fb ? theme_secondary : col_slate, TextAlign::CENTER,
             is_fb ? room_action_feedback.c_str() : v_st.c_str());

    bool is_cln = (sim_vacuum_state() == "Cleaning");
    bool is_psd = (sim_vacuum_state() == "Paused");
    bool is_dck = (sim_vacuum_state() == "Docked");

    render_modern_card(it, cx, layout().area_card3_y(0), 296, 58, 14,
                       is_cln ? Color(18, 36, 26) : card_bg_on,
                       is_cln ? col_green : card_brd_off,
                       "Start Cleaning", col_white, font_small);
    render_modern_card(it, cx, layout().area_card3_y(1), 296, 58, 14,
                       is_psd ? Color(38, 32, 16) : card_bg_on,
                       is_psd ? col_gold : card_brd_off,
                       "Pause Vacuum", col_white, font_small);
    render_modern_card(it, cx, layout().area_card3_y(2), 296, 58, 14,
                       is_dck ? Color(22, 32, 46) : card_bg_on,
                       is_dck ? theme_primary : card_brd_off,
                       "Return to Dock", col_white, font_small);
  }

  // Bottom 4-Icon Whole-Home Category Dock at layout().app_dock_y (0=Alarm, 1=Thermostat, 2=Garage/Locks, 3=Vacuum)
  int WH_XS[4] = {0, 0, 0, 0};
  bool wh_dl = false, wh_dr = false;
  get_home_dock_window_xs(4, 0, cx, WH_XS, wh_dl, wh_dr);
  const int WH_Y = layout().app_dock_y;
  const int dr = layout().dock_r;
  for (int i = 0; i < 4; i++) {
    int bx = WH_XS[i];
    bool is_act = (i == tab);
    Color ring_c = is_act ? theme_primary : Color(90, 112, 138);
    Color fg_c   = is_act ? theme_primary : col_white;
    it.filled_circle(bx, WH_Y, dr - 1, card_bg_off);
    it.circle(bx + 1, WH_Y + 1, dr, Color(0, 0, 0));
    it.circle(bx, WH_Y, dr, ring_c);
    if (is_act) it.circle(bx, WH_Y, dr - 1, ring_c);

    if (i == 0) {
      // 0. Security Alarm: Pitched-Crest Shield with Negative-Space Overhanging-Roof House & Doorway
      static constexpr int8_t SH_HW[31] = {
         1,  4,  7, 10, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12,
        12, 11, 11, 11, 10, 10,  9,  9,  8,  7,  6,  5,  4,  3,  2,  1
      };
      for (int dy = -15; dy <= 15; dy++) {
        int hw = SH_HW[dy + 15];
        it.horizontal_line(bx - hw, WH_Y + dy, 2 * hw + 1, fg_c);
      }
      // Carve Negative-Space House (2px overhanging roof chevron + right chimney + 1px rafter gap + inner house + doorway)
      for (int dy = -8; dy <= 7; dy++) {
        for (int dx = -8; dx <= 8; dx++) {
          int ax = std::abs(dx);
          bool carve = false;
          if (dy <= 0) {
            int r_out = dy + 8;
            if (ax == r_out || ax == r_out - 1) carve = true;           // 2px outer roof chevron
            else if (dx >= 4 && dx <= 5 && dy >= -7 && dy <= -3) carve = true; // Right chimney stack
            else if (ax <= r_out - 3) carve = true;                     // Inner house gable (leaves 1px fg_c gap at r_out - 2)
          } else {
            if (ax <= 5 && !(ax <= 1 && dy >= 3)) carve = true;         // House walls with 3x5px centered doorway
          }
          if (carve) {
            it.draw_pixel_at(bx + dx, WH_Y + dy, card_bg_off);
          }
        }
      }
    } else if (i == 1) {
      // 1. Thermostat: Sculpted 2px-Walled Thermometer + Mercury Bulb & 3 Graduated Scale Ticks
      int tx = bx - 2;
      it.line(tx - 3, WH_Y - 13, tx + 3, WH_Y - 13, fg_c);
      it.line(tx - 2, WH_Y - 14, tx + 2, WH_Y - 14, fg_c);
      it.line(tx - 3, WH_Y - 13, tx - 3, WH_Y + 3,  fg_c);
      it.line(tx - 2, WH_Y - 13, tx - 2, WH_Y + 3,  fg_c);
      it.line(tx + 3, WH_Y - 13, tx + 3, WH_Y + 3,  fg_c);
      it.line(tx + 2, WH_Y - 13, tx + 2, WH_Y + 3,  fg_c);
      it.filled_circle(tx, WH_Y + 8, 6, fg_c);
      it.filled_circle(tx, WH_Y + 8, 4, card_bg_off);
      it.filled_circle(tx, WH_Y + 8, 2, fg_c);
      it.filled_rectangle(tx - 1, WH_Y - 5, 3, 12, fg_c);
      // 2px-thick degree ticks on right
      it.filled_rectangle(tx + 6, WH_Y - 11, 5, 2, fg_c);
      it.filled_rectangle(tx + 6, WH_Y - 6,  4, 2, fg_c);
      it.filled_rectangle(tx + 6, WH_Y - 1,  5, 2, fg_c);
    } else if (i == 2) {
      // 2. Entry & Garage: Solid Pitched-Roof Garage Facade with Recessed 3-Panel Roll-Up Door
      it.filled_triangle(bx, WH_Y - 14, bx - 15, WH_Y - 4, bx + 15, WH_Y - 4, fg_c);
      it.filled_rectangle(bx - 15, WH_Y - 4, 31, 2, fg_c);
      it.filled_rectangle(bx - 12, WH_Y - 2, 25, 14, fg_c);
      it.horizontal_line(bx - 12, WH_Y - 2, 25, card_bg_off);
      // Negative-space garage bay opening
      it.filled_rectangle(bx - 9, WH_Y, 19, 12, card_bg_off);
      // 3 distinct raised horizontal roll-up garage door panels inside bay
      it.filled_rectangle(bx - 7, WH_Y + 1, 15, 3, fg_c);
      it.filled_rectangle(bx - 7, WH_Y + 5, 15, 3, fg_c);
      it.filled_rectangle(bx - 7, WH_Y + 9, 15, 3, fg_c);
    } else {
      // 3. Robot Vacuum: Solid Top-Down Smart Robot Disc, Front Bumper Seam, LiDAR Turret & Side Brushes
      // Angled front-left & front-right side-brush whiskers peeking out from under chassis
      it.line(bx - 9, WH_Y - 9, bx - 14, WH_Y - 13, fg_c);
      it.line(bx - 8, WH_Y - 10, bx - 12, WH_Y - 14, fg_c);
      it.line(bx + 9, WH_Y - 9, bx + 14, WH_Y - 13, fg_c);
      it.line(bx + 8, WH_Y - 10, bx + 12, WH_Y - 14, fg_c);
      // Solid circular robot chassis (r = 12)
      it.filled_circle(bx, WH_Y + 1, 12, fg_c);
      // Negative-space curved front bumper seam across upper arc
      it.horizontal_line(bx - 8, WH_Y - 6, 17, card_bg_off);
      it.horizontal_line(bx - 9, WH_Y - 5, 19, card_bg_off);
      it.line(bx - 9, WH_Y - 5, bx - 11, WH_Y - 2, card_bg_off);
      it.line(bx + 9, WH_Y - 5, bx + 11, WH_Y - 2, card_bg_off);
      // Negative-space moat + raised center LiDAR laser turret dome
      it.filled_circle(bx, WH_Y + 2, 5, card_bg_off);
      it.filled_circle(bx, WH_Y + 2, 3, fg_c);
      // Rear status indicator slit
      it.horizontal_line(bx - 3, WH_Y + 9, 7, card_bg_off);
    }
  }

}

}  // namespace latter_ui
