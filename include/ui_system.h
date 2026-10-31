#pragma once

// ==============================================================================
// Latter Core UI Module: System Settings, HSV Color Wheel & Diagnostics (ui_system.h)
// ==============================================================================

#include "ui_layout.h"
#include "ui_music.h"

namespace latter_ui {

// =============================================================================
// SECTION 18: SYSTEM SETTINGS & DIAGNOSTICS ENGINE (PAGE 4)
// =============================================================================

enum class PowerProfile : uint8_t {
  PORTABLE_CARRY = 0,
  HANDHELD_ROAMING = 1,
  WEARABLE_WATCH = 2,
  TABLETOP_DOCK = 3,
  ALWAYS_ON_STATIONARY = 4,
  CUSTOM = 5,
};

inline PowerProfile parse_power_profile(const std::string &opt) {
  if (opt == "Portable Carry") return PowerProfile::PORTABLE_CARRY;
  if (opt == "Handheld Roaming") return PowerProfile::HANDHELD_ROAMING;
  if (opt == "Wearable Watch") return PowerProfile::WEARABLE_WATCH;
  if (opt == "Tabletop Dock") return PowerProfile::TABLETOP_DOCK;
  if (opt == "Always-On Wall / Desk") return PowerProfile::ALWAYS_ON_STATIONARY;
  return PowerProfile::CUSTOM;
}

inline const char *power_profile_short_label(const std::string &opt) {
  switch (parse_power_profile(opt)) {
    case PowerProfile::PORTABLE_CARRY:       return "Portable";
    case PowerProfile::HANDHELD_ROAMING:     return "Roaming";
    case PowerProfile::WEARABLE_WATCH:       return "Watch";
    case PowerProfile::TABLETOP_DOCK:        return "Tabletop";
    case PowerProfile::ALWAYS_ON_STATIONARY: return "Wall/Desk";
    case PowerProfile::CUSTOM:               return "Custom";
  }
  return opt.c_str();
}

inline std::string &latter_site_id() {
  static std::string s = "primary";
  return s;
}

inline bool &latter_vpn_enabled() {
  static bool en = false;
  return en;
}

inline std::string &latter_vpn_client_ip() {
  static std::string s;
  return s;
}

inline std::string &latter_ble_proxy() {
  static std::string s;
  return s;
}

inline std::string &latter_area_label() {
  static std::string s = "Home";
  return s;
}

template <typename DisplayT, typename FontDateT, typename FontSmallT>
inline void render_system_page(
    DisplayT &it, int cx, uint32_t now_ms, const char *version_str,
    int system_menu_mode, int system_dashboard_page,
    int sleep_menu_page, int display_menu_page,
    bool menu_scroll_active, int menu_scroll_step, int menu_scroll_dir, int menu_scroll_target_page,
    int &update_check_state, uint32_t &update_check_timer,
    const std::string &ha_fw_latest, const std::string &ha_fw_summary,
    float batt, bool is_charging, float batt_v,
    float wifi_sig, const std::string &ip_str, float imu_temp_c,
    uint32_t free_heap_kb, bool ha_time_valid, bool ble_active,
    // Sleep & Wake values
    const std::string &power_profile_opt,
    const std::string &pickup_wake_opt, const std::string &tap_wake_opt,
    const std::string &screen_off_opt, bool docked_ss_on, const std::string &ss_style_opt,
    const std::string &dock_inact_opt, const std::string &hibernate_to_opt,
    bool instant_crown_hib, bool stay_awake_on,
    // Display & Clock values
    bool clock_face_digital, bool is_24h_format,
    const std::string &palette_opt, const std::string &default_app_opt,
    const std::string &auto_brightness_opt, const std::string &manual_bright_opt,
    int day_start_hr, int night_start_hr,
    const std::string &day_brightness_opt, const std::string &night_brightness_opt,
    // Sound values
    int spk_vol_pct, int alert_vol_pct, int vol_step_pct,
    // Music values
    const std::string &art_mode_opt, const std::string &overlay_style_opt,
    const std::string &album_sort_opt, const std::string &artist_sort_opt,
    const std::string &podcast_sort_opt,
    // Voice & Per-Model Wake Word Sensitivity values
    const std::string &va_ww_mode_opt, const std::string &va_ww_active_opt,
    const std::string &sens_nabu_opt, const std::string &sens_luna_opt,
    const std::string &sens_jarvis_opt, const std::string &sens_mycroft_opt,
    const std::string &sens_alexa_opt,
    const std::string &va_profile_opt, bool va_wake_scr,
    // Fonts & Colors
    FontDateT *font_date, FontSmallT *font_small,
    const esphome::Color &theme_primary, const esphome::Color &theme_secondary,
    const esphome::Color &col_bg, const esphome::Color &col_black,
    const esphome::Color &col_white, const esphome::Color &col_slate,
    const esphome::Color &col_dark_gray, const esphome::Color &col_gold,
    const esphome::Color &col_red, bool game_sound_on = true) {
  (void)font_date; (void)col_dark_gray;
  char buf[64];

  const esphome::Color card_bg_on(22, 27, 36);
  const esphome::Color card_bg_off(15, 18, 24);
  const esphome::Color card_brd_on(52, 62, 78);
  const esphome::Color card_brd_off(34, 40, 50);

  auto draw_back = [&]() {
    render_back_button(it, cx, font_small, 426, "< Back");
  };

  // Multi-item cycle card (3+ options): renders squared-off modern card + subtle < > flank chevrons
  auto draw_cycle_card_y = [&](int y_mid, const char *label, bool enabled = true) {
    esphome::Color bg_c  = enabled ? card_bg_on  : card_bg_off;
    esphome::Color brd_c = enabled ? card_brd_on : card_brd_off;
    esphome::Color txt_c = enabled ? col_white   : col_slate;
    esphome::Color chv_c = enabled ? theme_secondary : col_slate;
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
    if (enabled) {
      const int c_l = layout().list_card_left + 15;
      const int c_r = cx + (layout().list_card_w / 2) - 15;
      it.line(c_l, y_mid, c_l + 5, y_mid - 5, chv_c);
      it.line(c_l, y_mid, c_l + 5, y_mid + 5, chv_c);
      it.line(c_r, y_mid, c_r - 5, y_mid - 5, chv_c);
      it.line(c_r, y_mid, c_r - 5, y_mid + 5, chv_c);
    }
  };

  // Binary toggle card (2 states): renders squared-off modern card (white when active, dimmed when off, zero arrows)
  auto draw_toggle_card_y = [&](int y_mid, const char *label, bool is_on) {
    esphome::Color bg_c  = is_on ? card_bg_on  : card_bg_off;
    esphome::Color brd_c = is_on ? card_brd_on : card_brd_off;
    esphome::Color txt_c = is_on ? col_white   : col_slate;
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
  };

  // Action / Submenu drill-down card
  auto draw_action_card_y = [&](int y_mid, const char *label, esphome::Color brd_c,
                                esphome::Color txt_c, esphome::Color bg_c) {
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
  };

  auto draw_right_chevrons = [&](bool show_up, bool show_down) {
    render_vertical_page_pips(it, show_up, show_down, col_bg, theme_secondary, col_slate);
  };

  const int row0_y = layout().slot5_y(0);
  const int row_s  = layout().slot5_stride;
  const int mask_top_h = scale_y(82);
  const int mask_btm_y = scale_y(388);
  const int min_vis_y  = scale_y(60);
  const int max_vis_y  = scale_y(395);

  auto compute_scroll_offset = [&](int cur_page, const int16_t ease_lut[5], int max_shift_466) -> int {
    const int max_shift = scale_y(max_shift_466);
    if (menu_scroll_active) {
      int step = std::max(0, std::min(4, menu_scroll_step));
      int shift = scale_y(ease_lut[step]);
      return (menu_scroll_dir == 1) ? shift : (max_shift - shift);
    }
    return (cur_page == 2) ? max_shift : 0;
  };

  if (system_menu_mode == 0) {
    static constexpr int16_t SCROLL_EASE_189[5] = {0, 41, 102, 161, 189};
    int current_offset = compute_scroll_offset(system_dashboard_page, SCROLL_EASE_189, 189);
    static constexpr const char *kSysLabels[8] = {
      "Display", "Sleep & Wake", "Sound",   "Voice",
      "Music",   "Home",         "Actions", "Info"
    };
    for (int i = 0; i < 8; i++) {
      int y = row0_y + i * row_s - current_offset;
      if (menu_scroll_active ? (y >= min_vis_y && y <= max_vis_y) : (y >= row0_y - 10 && y <= layout().slot5_y(4) + 10)) {
        render_modern_card(it, cx, y, 276, 44, 12, card_bg_on, card_brd_on, kSysLabels[i], col_white, font_small);
      }
    }

    if (menu_scroll_active) {
      it.filled_rectangle(0, 0, SCREEN_W, mask_top_h, col_bg);
      it.filled_rectangle(0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    it.print(cx, layout().hdr_y, font_small, theme_primary, esphome::display::TextAlign::CENTER, "SYSTEM");

    int active_page = menu_scroll_active ? menu_scroll_target_page : system_dashboard_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 1) {
    static constexpr int16_t SCROLL_EASE_315[5] = {0, 68, 170, 268, 315};
    int current_offset = compute_scroll_offset(sleep_menu_page, SCROLL_EASE_315, 315);
    char sleep_labels[10][36];
    const char *short_p = power_profile_short_label(power_profile_opt);
    snprintf(sleep_labels[0], sizeof(sleep_labels[0]), "Profile: %s", short_p);
#if LATTER_HAS_IMU
    snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Pickup Wake: %s", pickup_wake_opt.c_str());
#else
    snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Pickup Wake: N/A");
#endif
    snprintf(sleep_labels[2], sizeof(sleep_labels[2]), "Tap Wake: %s", tap_wake_opt.c_str());
#if LATTER_HAS_BATTERY
    snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Battery Timeout: %s", screen_off_opt.c_str());
#else
    snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Battery Timeout: N/A");
#endif
    snprintf(sleep_labels[4], sizeof(sleep_labels[4]), "Docked Saver: %s", docked_ss_on ? "On" : "Off");
    const char *short_s = (ss_style_opt == "Aurora Borealis") ? "Aurora"
                          : ((ss_style_opt == "Matrix Rain") ? "Matrix" : ss_style_opt.c_str());
    snprintf(sleep_labels[5], sizeof(sleep_labels[5]), "Saver Style: %s", short_s);
    snprintf(sleep_labels[6], sizeof(sleep_labels[6]), "Docked Timeout: %s", dock_inact_opt.c_str());
#if LATTER_HAS_BATTERY
    snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Hibernate: %s", hibernate_to_opt.c_str());
    snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Hold Crown: %s", instant_crown_hib ? "Instant" : "5s Delay");
    snprintf(sleep_labels[9], sizeof(sleep_labels[9]), "Stay Awake: %s", stay_awake_on ? "On" : "Off");
#else
    snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Hibernate: N/A");
    snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Hold Crown: N/A");
    snprintf(sleep_labels[9], sizeof(sleep_labels[9]), "Stay Awake: N/A");
#endif

    for (int i = 0; i < 10; i++) {
      int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        if (i == 4) {
          draw_toggle_card_y(y, sleep_labels[i], docked_ss_on);
        } else if (i == 8) {
          draw_toggle_card_y(y, sleep_labels[i], LATTER_HAS_BATTERY != 0);
        } else if (i == 9) {
          draw_toggle_card_y(y, sleep_labels[i], (LATTER_HAS_BATTERY != 0) && stay_awake_on);
        } else {
          bool en = (i == 0) ? true
                  : (i == 1) ? ((LATTER_HAS_IMU != 0) && (pickup_wake_opt != "Disabled"))
                  : (i == 2) ? (tap_wake_opt != "Disabled")
                  : (i == 3) ? ((LATTER_HAS_BATTERY != 0) && (screen_off_opt != "Never"))
                  : (i == 5) ? docked_ss_on
                  : (i == 6) ? (dock_inact_opt != "Never")
                  : (i == 7) ? ((LATTER_HAS_BATTERY != 0) && (hibernate_to_opt != "Disabled"))
                  : true;
          draw_cycle_card_y(y, sleep_labels[i], en);
        }
      }
    }

    if (menu_scroll_active) {
      it.filled_rectangle(0, 0, SCREEN_W, mask_top_h, col_bg);
      it.filled_rectangle(0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_menu_header(it, cx, 48, "SLEEP & WAKE", font_small, theme_primary);
    draw_back();
    int active_page = menu_scroll_active ? menu_scroll_target_page : sleep_menu_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 2) {
    int current_offset = compute_scroll_offset(display_menu_page, SHARED_SCROLL_EASE_252, 252);
    char disp_labels[9][36];
    snprintf(disp_labels[0], sizeof(disp_labels[0]), "Start Screen: %s",
             (default_app_opt == "Clock") ? "Clock" : "Home");
    snprintf(disp_labels[1], sizeof(disp_labels[1]), "Clock Face: %s", clock_face_digital ? "Digital" : "Analog");
    snprintf(disp_labels[2], sizeof(disp_labels[2]), "Format: %s", is_24h_format ? "24-Hour" : "12-Hour");
    const char *pal_s = palette_opt.c_str();
    snprintf(disp_labels[3], sizeof(disp_labels[3]), "Color: %s", pal_s);
    const char *ab_s = (auto_brightness_opt == "Sun Synchronized") ? "Sun"
                       : ((auto_brightness_opt == "Time Schedule") ? "Sched" : "Off");
    snprintf(disp_labels[4], sizeof(disp_labels[4]), "Auto Bright: %s", ab_s);
    snprintf(disp_labels[5], sizeof(disp_labels[5]), "Brightness: %s",
             manual_bright_opt.empty() ? "90%" : manual_bright_opt.c_str());
    int nh12 = (night_start_hr > 12) ? (night_start_hr - 12) : night_start_hr;
    snprintf(disp_labels[6], sizeof(disp_labels[6]), "Schedule: %dAM-%dPM", day_start_hr, nh12);
    snprintf(disp_labels[7], sizeof(disp_labels[7]), "Day Bri: %s", day_brightness_opt.c_str());
    snprintf(disp_labels[8], sizeof(disp_labels[8]), "Night Bri: %s", night_brightness_opt.c_str());

    bool ab_off = (auto_brightness_opt == "Disabled" || auto_brightness_opt.empty());
    bool ab_sched = (auto_brightness_opt == "Time Schedule");

    for (int i = 0; i < 9; i++) {
      int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        if (i <= 2) {
          draw_toggle_card_y(y, disp_labels[i], true);
        } else {
          bool active = !( (i == 5 && !ab_off) || (i == 6 && !ab_sched) || ((i == 7 || i == 8) && ab_off) );
          draw_cycle_card_y(y, disp_labels[i], active);
          if (i == 3) {
            const int swatch_x = layout().sys_color_swatch_x();
            const int sw_r = scale_s(9);
            it.filled_circle(swatch_x, y, sw_r, theme_primary);
            it.circle(swatch_x, y, sw_r, col_white);
          }
        }
      }
    }

    if (menu_scroll_active) {
      it.filled_rectangle(0, 0, SCREEN_W, mask_top_h, col_bg);
      it.filled_rectangle(0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_menu_header(it, cx, 48, "DISPLAY", font_small, theme_primary);
    draw_back();
    int active_page = menu_scroll_active ? menu_scroll_target_page : display_menu_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 3) {
    render_menu_header(it, cx, 48, "ACTIONS", font_small, theme_primary);
    draw_action_card_y(layout().slot5_y(0), "Screensaver Now",   card_brd_on, col_white, card_bg_on);
#if LATTER_HAS_BATTERY
    draw_action_card_y(layout().slot5_y(1), "Hibernate Now",     card_brd_on, col_white, card_bg_on);
#else
    draw_action_card_y(layout().slot5_y(1), "Screen Off Now",    card_brd_on, col_white, card_bg_on);
#endif
    draw_action_card_y(layout().slot5_y(2), "Software Update >", card_brd_on, col_white, card_bg_on);
    snprintf(buf, sizeof(buf), "BLE Beacon: %s", ble_active ? "On" : "Off");
    draw_toggle_card_y(layout().slot5_y(3), buf, ble_active);
    draw_action_card_y(layout().slot5_y(4), "Restart Device",    col_red,     col_white, esphome::Color(28, 18, 22));
    draw_back();

  } else if (system_menu_mode == 4) {
    it.print(cx, scale_y(130), font_small, theme_primary, esphome::display::TextAlign::CENTER, "RESTART?");
    render_modern_card(it, cx - scale_x(73), scale_y(230), 112, 44, 12, card_bg_on, card_brd_on, "Cancel", col_white, font_small);
    render_modern_card(it, cx + scale_x(73), scale_y(230), 112, 44, 12, esphome::Color(28, 18, 22), col_red, "Restart", col_white, font_small);

  } else if (system_menu_mode == 5) {
    render_menu_header(it, cx, 48, "UPDATE", font_small, theme_primary);
    snprintf(buf, sizeof(buf), "Installed: v%s", version_str);
    it.print(cx, scale_y(95), font_small, col_slate, esphome::display::TextAlign::CENTER, buf);

    bool has_update = is_newer_semver(ha_fw_latest, version_str);
    if (update_check_state == 1) {
      if (now_ms - update_check_timer > 1500U) {
        update_check_state = has_update ? 0 : 2;
        update_check_timer = now_ms;
      }
    } else if (update_check_state == 2) {
      if (now_ms - update_check_timer > 3500U) {
        update_check_state = 0;
      }
    }

    if (has_update) {
      snprintf(buf, sizeof(buf), "Available: v%s", ha_fw_latest.empty() ? "2.0.x" : ha_fw_latest.c_str());
      it.print(cx, scale_y(125), font_small, theme_secondary, esphome::display::TextAlign::CENTER, buf);
      std::string summ = ha_fw_summary;
      if (summ.empty() || summ == "unavailable" || summ == "unknown") {
        summ = "Firmware update ready.";
      }
      summ = fit_text_to_width_font(summ, scale_x(280), font_small);
      it.print(cx, scale_y(160), font_small, col_white, esphome::display::TextAlign::CENTER, summ.c_str());
      draw_action_card_y(scale_y(215), "Update Now", theme_primary, theme_secondary, card_bg_on);
    } else {
      snprintf(buf, sizeof(buf), "Latest: v%s", version_str);
      it.print(cx, scale_y(125), font_small, theme_secondary, esphome::display::TextAlign::CENTER, buf);
      if (update_check_state == 1) {
        it.print(cx, scale_y(160), font_small, theme_secondary, esphome::display::TextAlign::CENTER, "Checking Home Assistant...");
        draw_action_card_y(scale_y(215), "Checking...", theme_primary, theme_secondary, card_bg_on);
      } else if (update_check_state == 2) {
        it.print(cx, scale_y(160), font_small, col_slate, esphome::display::TextAlign::CENTER, "No updates available");
        draw_action_card_y(scale_y(215), "Up to date", card_brd_on, col_white, card_bg_on);
      } else {
        it.print(cx, scale_y(160), font_small, col_slate, esphome::display::TextAlign::CENTER, "Firmware is up to date");
        draw_action_card_y(scale_y(215), "Check for Updates", theme_primary, col_white, card_bg_on);
      }
    }
    draw_back();

  } else if (system_menu_mode == 6) {
    it.print(cx, scale_y(100), font_small, theme_primary, esphome::display::TextAlign::CENTER, "INSTALL UPDATE?");
    it.print(cx, scale_y(145), font_small, col_white, esphome::display::TextAlign::CENTER, "Download & flash firmware");
    it.print(cx, scale_y(175), font_small, theme_secondary, esphome::display::TextAlign::CENTER, "Keep device powered on");
    render_modern_card(it, cx - scale_x(73), scale_y(240), 112, 44, 12, card_bg_on, card_brd_on, "Cancel", col_white, font_small);
    render_modern_card(it, cx + scale_x(73), scale_y(240), 112, 44, 12, card_bg_on, theme_primary, "Install", col_white, font_small);

  } else if (system_menu_mode == 7) {
    render_menu_header(it, cx, 48, "SOUND", font_small, theme_primary);
    snprintf(buf, sizeof(buf), "Speaker Vol: %d%%", spk_vol_pct);
    draw_cycle_card_y(layout().slot5_y(0), buf, true);
    snprintf(buf, sizeof(buf), "Alert Vol: %d%%", alert_vol_pct);
    draw_cycle_card_y(layout().slot5_y(1), buf, true);
    snprintf(buf, sizeof(buf), "Vol Step: %d%%", vol_step_pct);
    draw_cycle_card_y(layout().slot5_y(2), buf, true);
    snprintf(buf, sizeof(buf), "Game Sound: %s", game_sound_on ? "On" : "Muted");
    draw_toggle_card_y(layout().slot5_y(3), buf, game_sound_on);
    draw_back();

  } else if (system_menu_mode == 8) {
    render_menu_header(it, cx, 48, "SYSTEM INFO", font_small, theme_primary);
    const int info_x = layout().sys_info_x();
    auto print_info_row = [&](int y, const char *lbl, const char *val, esphome::Color val_col) {
      int ey = scale_y(y);
      int w_lbl = 0, x_off = 0, base = 0, th = 0;
      font_small->measure(lbl, &w_lbl, &x_off, &base, &th);
      it.print(info_x, ey, font_small, col_white, esphome::display::TextAlign::LEFT, lbl);
      it.print(info_x + w_lbl + 8, ey, font_small, val_col, esphome::display::TextAlign::LEFT, val);
    };

    bool has_update = is_newer_semver(ha_fw_latest, version_str);
    if (has_update) {
      snprintf(buf, sizeof(buf), "v%s (Update: v%s)", version_str, ha_fw_latest.c_str());
      print_info_row(84, "Version:", buf, col_gold);
    } else {
      snprintf(buf, sizeof(buf), "v%s (Up to date)", version_str);
      print_info_row(84, "Version:", buf, col_slate);
    }

    uint32_t up_sec = now_ms / 1000U;
    uint32_t up_d = up_sec / 86400U;
    uint32_t up_h = (up_sec % 86400U) / 3600U;
    uint32_t up_m = (up_sec % 3600U) / 60U;
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
             (unsigned long)up_d, (unsigned long)up_h, (unsigned long)up_m);
    print_info_row(116, "Uptime:", buf, col_slate);

    if (latter_vpn_enabled() && !latter_vpn_client_ip().empty()) {
      snprintf(buf, sizeof(buf), "VPN (%s)", latter_vpn_client_ip().c_str());
      print_info_row(148, "Latter:", buf, theme_secondary);
    } else if (!latter_site_id().empty() && latter_site_id() != "primary") {
      snprintf(buf, sizeof(buf), "Site: %s", latter_site_id().c_str());
      print_info_row(148, "Latter:", buf, theme_secondary);
    } else {
      print_info_row(148, "Home Assistant:", ha_time_valid ? "Connected" : "Syncing...", col_slate);
    }

    if (std::isnan(wifi_sig)) {
      snprintf(buf, sizeof(buf), "-- dBm  •  BLE: %s", ble_active ? "Active" : "Off");
    } else {
      snprintf(buf, sizeof(buf), "%.0f dBm  •  BLE: %s", wifi_sig, ble_active ? "Active" : "Off");
    }
    print_info_row(180, "Wi-Fi:", buf, col_slate);

    print_info_row(212, "IP:", ip_str.empty() ? "Connecting..." : ip_str.c_str(), col_slate);

    std::string area_str = latter_area_label().empty() ? "Home" : latter_area_label();
    if (!latter_ble_proxy().empty() && latter_ble_proxy() != "unknown" && latter_ble_proxy() != "unavailable") {
      area_str += " (" + latter_ble_proxy() + ")";
    }
    area_str = fit_text_to_width_font(area_str, 220, font_small);
    print_info_row(244, "Area:", area_str.c_str(), col_slate);

    snprintf(buf, sizeof(buf), "%.0f%% (%.2fV)%s",
             std::isnan(batt) ? 100.0f : batt,
             std::isnan(batt_v) ? 4.15f : batt_v,
             is_charging ? " • Chg" : "");
    print_info_row(276, "Battery:", buf, col_slate);

    float temp_c = std::isnan(imu_temp_c) ? 30.0f : imu_temp_c;
    float temp_f = temp_c * 1.8f + 32.0f;
    snprintf(buf, sizeof(buf), "%.1fC (%.1fF) • %luKB", temp_c, temp_f, (unsigned long)free_heap_kb);
    print_info_row(308, "HW / RAM:", buf, col_slate);

    draw_back();

  } else if (system_menu_mode == 9) {
    render_menu_header(it, cx, 48, "MUSIC", font_small, theme_primary);
    snprintf(buf, sizeof(buf), "Art: %s", art_mode_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(0), buf, true);
    snprintf(buf, sizeof(buf), "Overlay: %s", overlay_style_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(1), buf, true);
    snprintf(buf, sizeof(buf), "Album Sort: %s", album_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(2), buf, true);
    snprintf(buf, sizeof(buf), "Artist Discog: %s", artist_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(3), buf, true);
    snprintf(buf, sizeof(buf), "Podcast Sort: %s", podcast_sort_opt.c_str());
    draw_toggle_card_y(layout().slot5_y(4), buf, true);
    draw_back();

  } else if (system_menu_mode == 11) {
    render_menu_header(it, cx, 48, "VOICE", font_small, theme_primary);
    const bool ww_en = (va_ww_mode_opt != "Disabled");
    snprintf(buf, sizeof(buf), "Wake Mode: %s", va_ww_mode_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(0), buf, ww_en);
    snprintf(buf, sizeof(buf), "Wake Word: %s", va_ww_active_opt.empty() ? "Okay Nabu" : va_ww_active_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(1), buf, ww_en);
    draw_action_card_y(layout().slot5_y(2), "Sensitivity (Per Model) >",
                       ww_en ? card_brd_on : card_brd_off,
                       ww_en ? col_white   : col_slate,
                       ww_en ? card_bg_on  : card_bg_off);
    snprintf(buf, sizeof(buf), "Profile: %s", va_profile_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(3), buf, true);
    snprintf(buf, sizeof(buf), "Wake Screen: %s", va_wake_scr ? "On" : "Off");
    draw_toggle_card_y(layout().slot5_y(4), buf, va_wake_scr);
    draw_back();

  } else if (system_menu_mode == 12) {
    render_menu_header(it, cx, 48, "SENSITIVITY", font_small, theme_primary);
    auto short_sens = [](const std::string &s) -> const char * {
      if (s == "Slightly sensitive") return "Low";
      if (s == "Very sensitive") return "High";
      return "Medium";
    };
    snprintf(buf, sizeof(buf), "Okay Nabu: %s", short_sens(sens_nabu_opt));
    draw_cycle_card_y(layout().slot5_y(0), buf, true);
    snprintf(buf, sizeof(buf), "Hey Luna: %s", short_sens(sens_luna_opt));
    draw_cycle_card_y(layout().slot5_y(1), buf, true);
    snprintf(buf, sizeof(buf), "Hey Jarvis: %s", short_sens(sens_jarvis_opt));
    draw_cycle_card_y(layout().slot5_y(2), buf, true);
    snprintf(buf, sizeof(buf), "Hey Mycroft: %s", short_sens(sens_mycroft_opt));
    draw_cycle_card_y(layout().slot5_y(3), buf, true);
    snprintf(buf, sizeof(buf), "Alexa: %s", short_sens(sens_alexa_opt));
    draw_cycle_card_y(layout().slot5_y(4), buf, true);
    draw_back();

  } else if (system_menu_mode == 13) {
    const int wheel_cy = scale_y(180);
    const float ws = (SCREEN_MIN_DIM > 520) ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f) : 1.0f;
    it.circle(cx, wheel_cy, scale_s(166), card_brd_off);

    for (int a = 0; a < 60; a++) {
      float hue_01 = static_cast<float>(a) * (1.0f / 60.0f);
      float c_a = DIAL_COS[a];
      float s_a = DIAL_SIN[a];

      int x1 = cx + static_cast<int>(c_a * 148.0f * ws);
      int y1 = wheel_cy + static_cast<int>(s_a * 148.0f * ws);
      it.filled_circle(x1, y1, scale_s(16), hsv_to_rgb(hue_01, 1.00f, 1.0f));

      int x2 = cx + static_cast<int>(c_a * 124.0f * ws);
      int y2 = wheel_cy + static_cast<int>(s_a * 124.0f * ws);
      it.filled_circle(x2, y2, scale_s(14), hsv_to_rgb(hue_01, 0.84f, 1.0f));

      int x3 = cx + static_cast<int>(c_a * 101.0f * ws);
      int y3 = wheel_cy + static_cast<int>(s_a * 101.0f * ws);
      it.filled_circle(x3, y3, scale_s(12), hsv_to_rgb(hue_01, 0.68f, 1.0f));

      int x4 = cx + static_cast<int>(c_a * 79.0f * ws);
      int y4 = wheel_cy + static_cast<int>(s_a * 79.0f * ws);
      it.filled_circle(x4, y4, scale_s(11), hsv_to_rgb(hue_01, 0.52f, 1.0f));

      int x5 = cx + static_cast<int>(c_a * 59.0f * ws);
      int y5 = wheel_cy + static_cast<int>(s_a * 59.0f * ws);
      it.filled_circle(x5, y5, scale_s(9), hsv_to_rgb(hue_01, 0.36f, 1.0f));
    }

    it.filled_circle(cx, wheel_cy, scale_s(49), col_black);
    it.filled_circle(cx, wheel_cy, scale_s(44), theme_primary);
    it.circle(cx, wheel_cy, scale_s(44), col_white);
    it.circle(cx, wheel_cy, scale_s(45), theme_secondary);

    float rad = color_wheel_hue_deg() * (PI_F / 180.0f);
    float r_sel = (59.0f + ((color_wheel_sat() - 0.35f) / 0.65f) * 89.0f) * ws;
    r_sel = std::max(57.0f * ws, std::min(150.0f * ws, r_sel));
    int sel_x = cx + static_cast<int>(fast_cos(rad) * r_sel);
    int sel_y = wheel_cy + static_cast<int>(fast_sin(rad) * r_sel);
    it.filled_circle(sel_x, sel_y, scale_s(12), theme_primary);
    it.circle(sel_x, sel_y, scale_s(12), col_white);
    it.circle(sel_x, sel_y, scale_s(13), col_white);
    it.circle(sel_x, sel_y, scale_s(14), col_black);

    render_modern_card(it, cx, scale_y(376), 224, 38, 12, card_bg_on, theme_primary, "Save Custom Color", col_white, font_small);
    draw_back();
  }
}


// ==============================================================================

}  // namespace latter_ui

using namespace latter_ui;
