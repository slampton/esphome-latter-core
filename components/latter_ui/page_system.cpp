#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "esphome_bridge.h"
#include "ui_pages.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

namespace {

enum class PowerProfile : uint8_t {
  PORTABLE_CARRY = 0,
  HANDHELD_ROAMING = 1,
  WEARABLE_WATCH = 2,
  TABLETOP_DOCK = 3,
  ALWAYS_ON_STATIONARY = 4,
  CUSTOM = 5,
};

PowerProfile parse_power_profile_local(const std::string &opt) {
  if (opt == "Portable Carry") return PowerProfile::PORTABLE_CARRY;
  if (opt == "Handheld Roaming") return PowerProfile::HANDHELD_ROAMING;
  if (opt == "Wearable Watch") return PowerProfile::WEARABLE_WATCH;
  if (opt == "Tabletop Dock") return PowerProfile::TABLETOP_DOCK;
  if (opt == "Always-On Wall / Desk") return PowerProfile::ALWAYS_ON_STATIONARY;
  return PowerProfile::CUSTOM;
}

const char *power_profile_short_label_local(const std::string &opt) {
  switch (parse_power_profile_local(opt)) {
    case PowerProfile::PORTABLE_CARRY:       return "Portable";
    case PowerProfile::HANDHELD_ROAMING:     return "Roaming";
    case PowerProfile::WEARABLE_WATCH:       return "Watch";
    case PowerProfile::TABLETOP_DOCK:        return "Tabletop";
    case PowerProfile::ALWAYS_ON_STATIONARY: return "Wall/Desk";
    case PowerProfile::CUSTOM:               return "Custom";
  }
  return opt.c_str();
}

int card_hit_width(int w, int h) {
  if (SCREEN_W > 520 && w <= 340) return scale_x(w);
  if (SCREEN_W < 380 && h <= 48 && w >= 240 && w <= 330) return SCREEN_W - 88;
  if (w > SCREEN_W - 20) return SCREEN_W - 20;
  return w;
}

}  // namespace

void render_system_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();

  if (!frame_base_state().painted) {
    const bool has_wall = (!ac.wallpaper_url.empty() &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           ctx.home_wallpaper_art.valid());
    render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg);
  }

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const char *version_str = (ctx.fw_version != nullptr) ? ctx.fw_version : "0.0.0";
  const int system_menu_mode = rs.nav.system_menu_mode;
  const int system_dashboard_page = rs.nav.system_dashboard_page;
  const int sleep_menu_page = rs.nav.sleep_menu_page;
  const int display_menu_page = rs.nav.display_menu_page;
  const bool menu_scroll_active = rs.nav.menu_scroll_active;
  const int menu_scroll_dir = rs.nav.menu_scroll_dir;
  const int menu_scroll_target_page = rs.nav.menu_scroll_target_page;

  auto *font_small = ctx.font_small;
  const esphome::Color theme_primary = ctx.theme_primary;
  const esphome::Color theme_secondary = ctx.theme_secondary;
  const esphome::Color col_bg = ctx.col_bg;
  const esphome::Color col_white = ctx.col_white;
  const esphome::Color col_slate = ctx.col_slate;
  const esphome::Color col_gold = ctx.col_gold;
  const esphome::Color col_red = ctx.col_red;

  const esphome::Color card_bg_on = kChromeGlassTint;
  const esphome::Color card_bg_off = kChromeGlassTint;
  const esphome::Color card_brd_on = kChromeCardBrdOn;
  const esphome::Color card_brd_off = kChromeCardBrdOff;

  const int hit_w = card_hit_width(276, 44);
  const int hit_h = (SCREEN_W > 520) ? scale_y(44) : 44;

  auto draw_back = [&]() {
    render_back_button(it, cx, font_small, layout().back_y, "< Back");
  };

  auto add_row_hit = [&](int y_mid, uint16_t wid, uint8_t flags, int16_t param) {
    if (!menu_scroll_active && wid != WidgetId::NONE) {
      frame_hits().add_centered_rect(cx, y_mid, hit_w, hit_h, wid, flags, param);
    }
  };

  auto draw_cycle_card_y = [&](int y_mid, const char *label, bool enabled = true,
                               uint16_t wid = WidgetId::NONE,
                               uint8_t flags = kHitTap | kHitFocusSlot,
                               int16_t param = 0) {
    const esphome::Color bg_c = enabled ? card_bg_on : card_bg_off;
    const esphome::Color brd_c = enabled ? card_brd_on : card_brd_off;
    const esphome::Color txt_c = enabled ? col_white : col_slate;
    const esphome::Color chv_c = enabled ? theme_secondary : col_slate;
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
    if (enabled) {
      const int c_l = layout().list_card_left + 15;
      const int c_r = cx + (layout().list_card_w / 2) - 15;
      fast_line(it, c_l, y_mid, c_l + 5, y_mid - 5, chv_c);
      fast_line(it, c_l, y_mid, c_l + 5, y_mid + 5, chv_c);
      fast_line(it, c_r, y_mid, c_r - 5, y_mid - 5, chv_c);
      fast_line(it, c_r, y_mid, c_r - 5, y_mid + 5, chv_c);
      add_row_hit(y_mid, wid, flags, param);
    }
  };

  auto draw_toggle_card_y = [&](int y_mid, const char *label, bool is_on, int row_slot = -1, int item_param = -1) {
    const uint16_t wid = (!menu_scroll_active && row_slot >= 0 && row_slot < 5)
                             ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                             : WidgetId::NONE;
    const int16_t p = static_cast<int16_t>((item_param >= 0) ? item_param : row_slot);
    ui::toggle_card_row(it, cx, y_mid, 276, 44, label, is_on, font_small,
                        theme_primary, col_white, col_slate, wid, p);
  };

  auto draw_action_card_y = [&](int y_mid, const char *label, esphome::Color brd_c,
                                esphome::Color txt_c, esphome::Color bg_c,
                                uint16_t wid = WidgetId::NONE,
                                int16_t param = 0) {
    const std::string fit_lbl = fit_text_to_width_font(label != nullptr ? label : "", hit_w - 20, font_small);
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, fit_lbl.c_str(), txt_c, font_small);
    add_row_hit(y_mid, wid, kHitTap | kHitFocusSlot, param);
  };

  auto draw_right_chevrons = [&](bool show_up, bool show_down) {
    render_vertical_page_pips(it, show_up, show_down, col_bg, theme_secondary, col_slate);
  };

  const int row0_y = layout().slot5_y(0);
  const int row_s = layout().slot5_stride;
  const int mask_top_h = scale_y(82);
  const int mask_btm_y = scale_y(388);
  const int min_vis_y = scale_y(60);
  const int max_vis_y = scale_y(395);

  auto compute_scroll_offset = [&](int cur_page, int max_shift_466) -> int {
    const int max_shift = scale_y(max_shift_466);
    if (menu_scroll_active) {
      const int shift = menu_scroll_shift(max_shift);
      return (menu_scroll_dir == 1) ? shift : (max_shift - shift);
    }
    return (cur_page == 2) ? max_shift : 0;
  };

  char buf[64];

  if (system_menu_mode == 0) {
    const int current_offset = compute_scroll_offset(system_dashboard_page, 189);
    static constexpr const char *kSysLabels[8] = {
        "Display", "Sleep & Wake", "Sound", "Voice",
        "Music",   "Home",         "Actions", "Info"};
    int vis_row = 0;
    for (int i = 0; i < 8; i++) {
      const int y = row0_y + i * row_s - current_offset;
      const bool vis = menu_scroll_active
                           ? (y >= min_vis_y && y <= max_vis_y)
                           : (y >= row0_y - 10 && y <= layout().slot5_y(4) + 10);
      if (vis) {
        const bool row_en = (i != 5) || prefs.home_screen_enabled;
        render_modern_card(it, cx, y, 276, 44, 12,
                           row_en ? card_bg_on : card_bg_off,
                           row_en ? card_brd_on : card_brd_off,
                           kSysLabels[i], row_en ? col_white : col_slate, font_small);
        if (!menu_scroll_active && vis_row < 5) {
          add_row_hit(y, static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + vis_row),
                      kHitTap | kHitFocusSlot, static_cast<int16_t>(vis_row));
          vis_row++;
        }
      }
    }

    if (menu_scroll_active && !backdrop_active()) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_page_header(it, cx, "SYSTEM", font_small, theme_primary);

    const int active_page = menu_scroll_active ? menu_scroll_target_page : system_dashboard_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 1) {
    const int current_offset = compute_scroll_offset(sleep_menu_page, 315);
    char sleep_labels[9][36];
    const char *short_p = power_profile_short_label_local(ctx.power_profile_opt);
    std::snprintf(sleep_labels[0], sizeof(sleep_labels[0]), "Profile: %s", short_p);
#if LATTER_HAS_IMU
    std::snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Pickup Wake: %s", ctx.pickup_wake_opt.c_str());
#else
    std::snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Pickup Wake: N/A");
#endif
    std::snprintf(sleep_labels[2], sizeof(sleep_labels[2]), "Tap Wake: %s", ctx.tap_wake_opt.c_str());
#if LATTER_HAS_BATTERY
    std::snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Battery Timeout: %s", ctx.display_off_timeout_opt.c_str());
#else
    std::snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Battery Timeout: N/A");
#endif
    std::snprintf(sleep_labels[4], sizeof(sleep_labels[4]), "Docked Saver: %s", ctx.docked_screensaver_enabled ? "On" : "Off");
    const char *short_s = (ctx.screensaver_style_opt == "Digital Rain") ? "Rain" : ctx.screensaver_style_opt.c_str();
    std::snprintf(sleep_labels[5], sizeof(sleep_labels[5]), "Saver Style: %s", short_s);
    std::snprintf(sleep_labels[6], sizeof(sleep_labels[6]), "Docked Timeout: %s", ctx.screensaver_docked_timeout_opt.c_str());
#if LATTER_HAS_BATTERY
    std::snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Hibernate: %s", ctx.hibernate_timeout_opt.c_str());
    std::snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Hold Crown: %s",
                  ctx.instant_crown_hibernate ? "Instant" : "5s Delay");
#else
    std::snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Hibernate: N/A");
    std::snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Hold Crown: N/A");
#endif

    const int page_base = (sleep_menu_page == 2) ? 5 : 0;
    for (int i = 0; i < 9; i++) {
      const int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        const int row_slot = (!menu_scroll_active && i >= page_base && i < page_base + 5) ? (i - page_base) : -1;
        if (i == 4) {
          draw_toggle_card_y(y, "Docked Saver", ctx.docked_screensaver_enabled, row_slot, row_slot);
        } else if (i == 8) {
#if LATTER_HAS_BATTERY
          draw_toggle_card_y(y, ctx.instant_crown_hibernate ? "Hold Crown: Instant" : "Hold Crown: 5s Delay",
                             ctx.instant_crown_hibernate, row_slot, row_slot);
#else
          draw_cycle_card_y(y, sleep_labels[i], false);
#endif
        } else {
          const bool en = (i == 0) ? true
                        : (i == 1) ? ((LATTER_HAS_IMU != 0) && (ctx.pickup_wake_opt != "Disabled"))
                        : (i == 2) ? (ctx.tap_wake_opt != "Disabled")
                        : (i == 3) ? ((LATTER_HAS_BATTERY != 0) && (ctx.display_off_timeout_opt != "Never"))
                        : (i == 5) ? ctx.docked_screensaver_enabled
                        : (i == 6) ? (ctx.screensaver_docked_timeout_opt != "Never")
                        : (i == 7) ? ((LATTER_HAS_BATTERY != 0) && (ctx.hibernate_timeout_opt != "Disabled"))
                                   : true;
          const uint16_t wid = (row_slot >= 0)
                                   ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                                   : WidgetId::NONE;
          draw_cycle_card_y(y, sleep_labels[i], en, wid, kHitTap | kHitFocusSlot, static_cast<int16_t>(row_slot));
        }
      }
    }

    if (menu_scroll_active && !backdrop_active()) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_page_header(it, cx, "SLEEP & WAKE", font_small, theme_primary);
    draw_back();
    const int active_page = menu_scroll_active ? menu_scroll_target_page : sleep_menu_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 2) {
    const int current_offset = compute_scroll_offset(display_menu_page, 252);
    char disp_labels[9][36];
    const char *start_lbl =
        (ctx.default_start_app_opt == "Area Controls") ? "Area"
        : (ctx.default_start_app_opt.empty() ? "Home" : ctx.default_start_app_opt.c_str());
    std::snprintf(disp_labels[0], sizeof(disp_labels[0]), "Start Screen: %s", start_lbl);
    std::snprintf(disp_labels[1], sizeof(disp_labels[1]), "Clock Face: %s",
                  prefs.clock_face_digital ? "Digital" : "Analog");
    std::snprintf(disp_labels[2], sizeof(disp_labels[2]), "Format: %s",
                  prefs.is_24h_format ? "24-Hour" : "12-Hour");
    std::snprintf(disp_labels[3], sizeof(disp_labels[3]), "Color: %s", ctx.theme_palette_opt.c_str());
    const char *ab_s = (ctx.auto_brightness_opt == "Sun Synchronized") ? "Sun"
                       : ((ctx.auto_brightness_opt == "Time Schedule") ? "Sched" : "Off");
    std::snprintf(disp_labels[4], sizeof(disp_labels[4]), "Auto Bright: %s", ab_s);
    std::snprintf(disp_labels[5], sizeof(disp_labels[5]), "Brightness: %s",
                  ctx.display_brightness_opt.empty() ? "90%" : ctx.display_brightness_opt.c_str());
    const int nh12 = (ctx.night_start_hr > 12) ? (ctx.night_start_hr - 12) : ctx.night_start_hr;
    std::snprintf(disp_labels[6], sizeof(disp_labels[6]), "Schedule: %dAM-%dPM", ctx.day_start_hr, nh12);
    std::snprintf(disp_labels[7], sizeof(disp_labels[7]), "Day Bri: %s", ctx.day_brightness_opt.c_str());
    std::snprintf(disp_labels[8], sizeof(disp_labels[8]), "Night Bri: %s", ctx.night_brightness_opt.c_str());

    const bool ab_off = (ctx.auto_brightness_opt == "Disabled" || ctx.auto_brightness_opt.empty());
    const bool ab_sched = (ctx.auto_brightness_opt == "Time Schedule");
    const int page_base = (display_menu_page == 2) ? 4 : 0;

    for (int i = 0; i < 9; i++) {
      const int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        const int row_slot = (!menu_scroll_active && i >= page_base && i < page_base + 5) ? (i - page_base) : -1;
        if (i <= 2) {
          const uint16_t wid = (row_slot >= 0)
                                   ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                                   : WidgetId::NONE;
          draw_cycle_card_y(y, disp_labels[i], true, wid, kHitTap | kHitFocusSlot, static_cast<int16_t>(row_slot));
        } else {
          const bool active = !((i == 5 && !ab_off) || (i == 6 && !ab_sched) || ((i == 7 || i == 8) && ab_off));
          uint16_t wid = WidgetId::NONE;
          uint8_t flags = kHitTap | kHitFocusSlot;
          if (i == 5 && ab_off && row_slot >= 0) {
            wid = WidgetId::SYS_BRI_SLIDER;
            flags = kHitTap | kHitHorizSlider | kHitFocusSlot;
          } else if (row_slot >= 0) {
            wid = static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot);
          }
          draw_cycle_card_y(y, disp_labels[i], active, wid, flags, static_cast<int16_t>(row_slot));
          if (i == 3) {
            const int swatch_x = layout().sys_color_swatch_x();
            const int sw_r = scale_s(9);
            fast_filled_circle(it, swatch_x, y, sw_r, theme_primary);
            fast_circle(it, swatch_x, y, sw_r, col_white);
            if (!menu_scroll_active && row_slot >= 0) {
              frame_hits().add_circle(swatch_x, y, sw_r + scale_s(8),
                                      static_cast<uint16_t>(WidgetId::COLOR_SWATCH_BASE + 0),
                                      kHitTap, 0);
            }
          }
        }
      }
    }

    if (menu_scroll_active && !backdrop_active()) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_page_header(it, cx, "DISPLAY", font_small, theme_primary);
    draw_back();
    const int active_page = menu_scroll_active ? menu_scroll_target_page : display_menu_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 3) {
    render_page_header(it, cx, "ACTIONS", font_small, theme_primary);
    draw_action_card_y(layout().slot5_y(0), "Activate Screensaver", card_brd_on, col_white, card_bg_on,
                       static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 0), 0);
#if LATTER_HAS_BATTERY
    draw_action_card_y(layout().slot5_y(1), "Hibernate Device", card_brd_on, col_white, card_bg_on,
                       static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), 1);
#else
    draw_action_card_y(layout().slot5_y(1), "Turn Screen Off", card_brd_on, col_white, card_bg_on,
                       static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), 1);
#endif
    draw_action_card_y(layout().slot5_y(2), "Software Update >", card_brd_on, col_white, card_bg_on,
                       static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 2), 2);
    draw_toggle_card_y(layout().slot5_y(3), "BLE Beacon", ctx.ble_active, 3, 3);
    draw_action_card_y(layout().slot5_y(4), "Restart Device", col_red, col_white, card_bg_on,
                       WidgetId::SYS_SECONDARY_BTN, 4);
    draw_back();

  } else if (system_menu_mode == 4) {
    ui::confirm_modal(it, cx, "RESTART DEVICE?", "Reboot firmware now", "",
                      "Cancel", "Restart", col_red, kChromeModalBg,
                      font_small, font_small, theme_primary, col_white, col_slate);

  } else if (system_menu_mode == 5) {
    render_page_header(it, cx, "UPDATE", font_small, theme_primary);
    std::snprintf(buf, sizeof(buf), "Installed: v%s", version_str);
    fast_print(it, cx, scale_y(95), font_small, col_slate, esphome::display::TextAlign::CENTER, buf);

    const bool has_update = is_newer_semver(ctx.fw_latest_ver, version_str);
    if (rs.nav.update_check_state == 1) {
      if (now_ms - rs.nav.update_check_timer > 1500U) {
        rs.nav.update_check_state = has_update ? 0 : 2;
        rs.nav.update_check_timer = now_ms;
      }
    } else if (rs.nav.update_check_state == 2) {
      if (now_ms - rs.nav.update_check_timer > 3500U) {
        rs.nav.update_check_state = 0;
      }
    }

    if (has_update) {
      if (ctx.fw_latest_ver.empty()) {
        std::snprintf(buf, sizeof(buf), "Update available");
      } else {
        std::snprintf(buf, sizeof(buf), "Available: v%s", ctx.fw_latest_ver.c_str());
      }
      fast_print(it, cx, scale_y(125), font_small, theme_secondary, esphome::display::TextAlign::CENTER, buf);
      std::string summ = ctx.fw_release_summary;
      if (summ.empty() || summ == "unavailable" || summ == "unknown") {
        summ = "Firmware update ready.";
      }
      summ = fit_text_to_width_font(summ, scale_x(280), font_small);
      fast_print(it, cx, scale_y(160), font_small, col_white, esphome::display::TextAlign::CENTER, summ.c_str());
      draw_action_card_y(scale_y(215), "Update Now", theme_primary, theme_secondary, card_bg_on,
                         WidgetId::SYS_PRIMARY_BTN, 0);
    } else {
      std::snprintf(buf, sizeof(buf), "Latest: v%s", version_str);
      fast_print(it, cx, scale_y(125), font_small, theme_secondary, esphome::display::TextAlign::CENTER, buf);
      if (rs.nav.update_check_state == 1) {
        fast_print(it, cx, scale_y(160), font_small, theme_secondary, esphome::display::TextAlign::CENTER,
                   "Checking Home Assistant...");
        draw_action_card_y(scale_y(215), "Checking...", theme_primary, theme_secondary, card_bg_on,
                           WidgetId::SYS_PRIMARY_BTN, 0);
      } else if (rs.nav.update_check_state == 2) {
        fast_print(it, cx, scale_y(160), font_small, col_slate, esphome::display::TextAlign::CENTER,
                   "No updates available");
        draw_action_card_y(scale_y(215), "Up to date", card_brd_on, col_white, card_bg_on,
                           WidgetId::SYS_PRIMARY_BTN, 0);
      } else {
        fast_print(it, cx, scale_y(160), font_small, col_slate, esphome::display::TextAlign::CENTER,
                   "Firmware is up to date");
        draw_action_card_y(scale_y(215), "Check for Updates", theme_primary, col_white, card_bg_on,
                           WidgetId::SYS_PRIMARY_BTN, 0);
      }
    }
    draw_back();

  } else if (system_menu_mode == 6) {
    ui::confirm_modal(it, cx, "INSTALL UPDATE?", "Download & flash firmware", "Keep device powered on",
                      "Cancel", "Install", theme_primary, card_bg_on,
                      font_small, font_small, theme_primary, col_white, theme_secondary);

  } else if (system_menu_mode == 7) {
    render_page_header(it, cx, "SOUND", font_small, theme_primary);
    std::snprintf(buf, sizeof(buf), "Speaker Vol: %d%%", ctx.spk_vol_pct);
    draw_cycle_card_y(layout().slot5_y(0), buf, true,
                      WidgetId::SYS_VOL_SLIDER, kHitTap | kHitHorizSlider | kHitFocusSlot, 0);
    std::snprintf(buf, sizeof(buf), "Alert Vol: %d%%", ctx.alert_vol_pct);
    draw_cycle_card_y(layout().slot5_y(1), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), kHitTap | kHitFocusSlot, 1);
    std::snprintf(buf, sizeof(buf), "Vol Step: %d%%", ctx.vol_step_pct);
    draw_cycle_card_y(layout().slot5_y(2), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 2), kHitTap | kHitFocusSlot, 2);
    draw_toggle_card_y(layout().slot5_y(3), "Game Sound", ctx.game_sound_enabled, 3, 3);
    draw_back();

  } else if (system_menu_mode == 8) {
    render_page_header(it, cx, "SYSTEM INFO", font_small, theme_primary);
    render_modern_card(it, cx, scale_y(196), 312, 260, 16, card_bg_on, card_brd_on, nullptr, col_white, font_small);
    const int info_x = layout().sys_info_x();
    auto print_info_row = [&](int y, const char *lbl, const char *val, esphome::Color val_col) {
      const int ey = scale_y(y);
      int w_lbl = 0, x_off = 0, base = 0, th = 0;
      font_small->measure(lbl, &w_lbl, &x_off, &base, &th);
      fast_print(it, info_x, ey, font_small, col_white, esphome::display::TextAlign::TOP_LEFT, lbl);
      fast_print(it, info_x + w_lbl + 8, ey, font_small, val_col, esphome::display::TextAlign::TOP_LEFT, val);
    };

    const bool has_update = is_newer_semver(ctx.fw_latest_ver, version_str);
    if (has_update) {
      std::snprintf(buf, sizeof(buf), "v%s (Update: v%s)", version_str, ctx.fw_latest_ver.c_str());
      print_info_row(84, "Version:", buf, col_gold);
    } else {
      std::snprintf(buf, sizeof(buf), "v%s (Up to date)", version_str);
      print_info_row(84, "Version:", buf, col_slate);
    }

    const uint32_t up_sec = now_ms / 1000U;
    const uint32_t up_d = up_sec / 86400U;
    const uint32_t up_h = (up_sec % 86400U) / 3600U;
    const uint32_t up_m = (up_sec % 3600U) / 60U;
    std::snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
                  static_cast<unsigned long>(up_d),
                  static_cast<unsigned long>(up_h),
                  static_cast<unsigned long>(up_m));
    print_info_row(116, "Uptime:", buf, col_slate);

    if (!latter_site_id().empty() && latter_site_id() != "primary") {
      std::snprintf(buf, sizeof(buf), "Site: %s", latter_site_id().c_str());
      print_info_row(148, "Latter:", buf, theme_secondary);
    } else {
      print_info_row(148, "Home Assistant:", ctx.now.is_valid() ? "Connected" : "Syncing...", col_slate);
    }

    if (std::isnan(ctx.wifi_rssi)) {
      std::snprintf(buf, sizeof(buf), "%s dBm  •  BLE: %s", kUnknownGlyph, ctx.ble_active ? "Active" : "Off");
    } else {
      std::snprintf(buf, sizeof(buf), "%.0f dBm  •  BLE: %s", ctx.wifi_rssi, ctx.ble_active ? "Active" : "Off");
    }
    print_info_row(180, "Wi-Fi:", buf, col_slate);

    print_info_row(212, "IP:", ctx.ip_addr.empty() ? "Connecting..." : ctx.ip_addr.c_str(), col_slate);

    std::string area_str = latter_area_label().empty() ? std::string(kUnknownGlyph) : latter_area_label();
    if (!latter_ble_proxy().empty()) {
      area_str += " (" + latter_ble_proxy() + ")";
    }
    area_str = fit_text_to_width_font(area_str, scale_x(220), font_small);
    print_info_row(244, "Area:", area_str.c_str(), col_slate);

    if (std::isnan(rs.hw_batt_level) || std::isnan(rs.hw_batt_voltage)) {
      std::snprintf(buf, sizeof(buf), "%s", kUnknownGlyph);
    } else {
      std::snprintf(buf, sizeof(buf), "%.0f%% (%.2fV)%s",
                    rs.hw_batt_level, rs.hw_batt_voltage, rs.hw_batt_charging ? " • Chg" : "");
    }
    print_info_row(276, "Battery:", buf, col_slate);

    if (std::isnan(rs.hw_imu_temp)) {
      std::snprintf(buf, sizeof(buf), "%s • %dKB", kUnknownGlyph, ctx.free_heap_kb);
    } else {
      const float temp_c = rs.hw_imu_temp;
      const float temp_f = temp_c * 1.8f + 32.0f;
      std::snprintf(buf, sizeof(buf), "%.1fC (%.1fF) • %dKB", temp_c, temp_f, ctx.free_heap_kb);
    }
    print_info_row(308, "HW / RAM:", buf, col_slate);

    draw_back();

  } else if (system_menu_mode == 9) {
    render_page_header(it, cx, "MUSIC", font_small, theme_primary);
    std::snprintf(buf, sizeof(buf), "Art: %s", ctx.music_fullscreen_art_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(0), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 0), kHitTap | kHitFocusSlot, 0);
    std::snprintf(buf, sizeof(buf), "Overlay: %s", ctx.music_overlay_style_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(1), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), kHitTap | kHitFocusSlot, 1);
    std::snprintf(buf, sizeof(buf), "Album Sort: %s", ctx.music_album_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(2), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 2), kHitTap | kHitFocusSlot, 2);
    std::snprintf(buf, sizeof(buf), "Artist Discog: %s", ctx.music_artist_album_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(3), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 3), kHitTap | kHitFocusSlot, 3);
    std::snprintf(buf, sizeof(buf), "Podcast Sort: %s", ctx.music_podcast_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(4), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 4), kHitTap | kHitFocusSlot, 4);
    draw_back();

  } else if (system_menu_mode == 11) {
    render_page_header(it, cx, "VOICE", font_small, theme_primary);
    const bool ww_en = (ctx.va_wake_word_mode_opt != "Disabled");
    std::snprintf(buf, sizeof(buf), "Wake Mode: %s", ctx.va_wake_word_mode_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(0), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 0), kHitTap | kHitFocusSlot, 0);
    std::snprintf(buf, sizeof(buf), "Wake Word: %s", ctx.primary_wake_word.c_str());
    draw_cycle_card_y(layout().slot5_y(1), buf, ww_en,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), kHitTap | kHitFocusSlot, 1);
    draw_action_card_y(layout().slot5_y(2), "Sensitivity (Per Model) >",
                       ww_en ? card_brd_on : card_brd_off,
                       ww_en ? col_white : col_slate,
                       ww_en ? card_bg_on : card_bg_off,
                       ww_en ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 2) : WidgetId::NONE, 2);
    std::snprintf(buf, sizeof(buf), "Profile: %s", ctx.va_visual_profile_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(3), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 3), kHitTap | kHitFocusSlot, 3);
    draw_toggle_card_y(layout().slot5_y(4), "Wake Screen", ctx.va_wake_screen, 4, 4);
    draw_back();

  } else if (system_menu_mode == 12) {
    render_page_header(it, cx, "SENSITIVITY", font_small, theme_primary);
    auto short_sens = [](const std::string &s) -> const char * {
      if (s == "Slightly sensitive") return "Low";
      if (s == "Very sensitive") return "High";
      return "Medium";
    };
    std::snprintf(buf, sizeof(buf), "Okay Nabu: %s", short_sens(ctx.ww_sens_okay_nabu_opt));
    draw_cycle_card_y(layout().slot5_y(0), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 0), kHitTap | kHitFocusSlot, 0);
    std::snprintf(buf, sizeof(buf), "Hey Luna: %s", short_sens(ctx.ww_sens_hey_luna_opt));
    draw_cycle_card_y(layout().slot5_y(1), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), kHitTap | kHitFocusSlot, 1);
    std::snprintf(buf, sizeof(buf), "Hey Jarvis: %s", short_sens(ctx.ww_sens_hey_jarvis_opt));
    draw_cycle_card_y(layout().slot5_y(2), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 2), kHitTap | kHitFocusSlot, 2);
    std::snprintf(buf, sizeof(buf), "Hey Mycroft: %s", short_sens(ctx.ww_sens_hey_mycroft_opt));
    draw_cycle_card_y(layout().slot5_y(3), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 3), kHitTap | kHitFocusSlot, 3);
    std::snprintf(buf, sizeof(buf), "Alexa: %s", short_sens(ctx.ww_sens_alexa_opt));
    draw_cycle_card_y(layout().slot5_y(4), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 4), kHitTap | kHitFocusSlot, 4);
    draw_back();

  } else if (system_menu_mode == 13) {
    const int wheel_cy = scale_y(180);
    ui::hsv_color_wheel(it, cx, wheel_cy, 156, 56, color_wheel_hue_deg(), color_wheel_sat(),
                        col_bg, col_white, WidgetId::COLOR_WHEEL_RING);
    const int save_r = scale_s(19);
    const int save_hw = std::min(SCREEN_W / 2 - save_r - 16, scale_x(92));
    ui::pill_button(it, cx - save_hw, cx + save_hw, scale_y(376), save_r,
                    theme_primary, card_bg_on, "Save Custom Color", col_white, font_small,
                    WidgetId::COLOR_WHEEL_SAVE, kHitTap | kHitFocusSlot);
    draw_back();
  }
}

}  // namespace latter_ui
