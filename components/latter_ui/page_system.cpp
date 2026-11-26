#include "ui_pages.h"
#include "ui_tile_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace latter_ui {
using namespace latter_core;

namespace {

bool is_newer_semver_local(std::string_view lat_sv, const char *cur_cstr) {
  if (lat_sv.empty() || lat_sv == "unavailable" || lat_sv == "unknown") return false;
  const std::string lat_str(lat_sv);
  const std::string cur_str(cur_cstr != nullptr ? cur_cstr : "");
  auto parse_ver = [](const std::string &raw, int &maj, int &min_v, int &pat) -> bool {
    const char *p = raw.c_str();
    if (*p == 'v' || *p == 'V') ++p;
    char *endp = nullptr;
    maj = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p || *endp != '.') return false;
    p = endp + 1;
    min_v = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p) return false;
    pat = 0;
    if (*endp == '.') {
      p = endp + 1;
      pat = static_cast<int>(std::strtol(p, &endp, 10));
    }
    return true;
  };
  const char *lp = lat_str.c_str();
  if (*lp == 'v' || *lp == 'V') ++lp;
  if (std::strncmp(lp, "202", 3) == 0) return false;
  int l_maj = 0, l_min = 0, l_pat = 0;
  int c_maj = 0, c_min = 0, c_pat = 0;
  if (!parse_ver(lat_str, l_maj, l_min, l_pat)) return false;
  if (!parse_ver(cur_str, c_maj, c_min, c_pat)) return false;
  if (l_maj != c_maj) return l_maj > c_maj;
  if (l_min != c_min) return l_min > c_min;
  return l_pat > c_pat;
}

struct SysDashboardTileSpec {
  const char *label;
  RouteId route;
};

static constexpr SysDashboardTileSpec kSysDashboardTiles[kSystemDashboardItemCount] = {
    {"Display",      RouteId::SETTINGS_DISPLAY},
    {"Sleep & Wake", RouteId::SETTINGS_SLEEP},
    {"Sound",        RouteId::SETTINGS_SOUND},
    {"Voice",        RouteId::SETTINGS_VOICE},
    {"Music",        RouteId::SETTINGS_MUSIC},
    {"Home",         RouteId::SETTINGS_HOME},
    {"Actions",      RouteId::SETTINGS_ACTIONS},
    {"Info",         RouteId::SETTINGS_INFO},
};

void draw_system_tile_icon(Rgb565Surface &it, int ix, int iy, int ir,
                           UiColor col, bool shadow, void *user_ctx) {
  const auto route = static_cast<RouteId>(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(user_ctx)));
  ui::draw_route_icon(it, route, ix, iy, ir, col, kChromeGlassTint, shadow);
}

}  // namespace

void render_system_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();

  if (!frame_base_state().painted) {
    if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
      const auto &ws_sy = weather_state();
      if (has_known_day_night_phase(ws_sy.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        render_static_day_night_sky_base(
            it, ctx.cx, ctx.cy,
            resolve_day_night_sky_is_night(ws_sy.sun_state, ctx.now.is_valid(), ctx.now.hour));
      } else {
        frame_base(it, ctx.col_bg);
      }
    } else {
      const bool has_wall = (!ac.wallpaper_url.empty() &&
                             ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                             ctx.home_wallpaper_art.valid());
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, ctx.cx, ctx.col_bg);
    }
  }

  const int cx = ctx.cx;
  const uint32_t now_ms = ctx.now_ms;
  const char *version_str = (ctx.fw_version != nullptr) ? ctx.fw_version : "0.0.0";
  const int system_menu_mode = rs.nav.system_menu_mode;
  const int system_dashboard_page = rs.nav.system_dashboard_page;
  const int sleep_menu_page = rs.nav.sleep_menu_page;
  const int display_menu_page = rs.nav.display_menu_page;
  const bool menu_scroll_active = rs.nav.menu_scroll_active;
  const int menu_scroll_target_page = rs.nav.menu_scroll_target_page;

  const UiFontView font_small = ctx.font_small;
  const UiColor theme_primary = ctx.theme_primary;
  const UiColor theme_secondary = ctx.theme_secondary;
  const UiColor col_bg = ctx.col_bg;
  const UiColor col_white = ctx.col_white;
  const UiColor col_slate = ctx.col_slate;
  const UiColor col_gold = ctx.col_gold;
  const UiColor col_red = ctx.col_red;

  const UiColor card_bg_on = kChromeGlassTint;
  const UiColor card_bg_off = kChromeGlassTint;
  const UiColor card_brd_on = kChromeCardBrdOn;
  const UiColor card_brd_off = kChromeCardBrdOff;

  const auto &ly = layout();
  const int row_w = ly.list_card_w;
  const int row_h = ly.list_h;
  const int per_pg = std::clamp(ly.list_rows, 4, kMaxListSlots);

  auto draw_back = [&]() {
    render_back_button(it, cx, font_small, ly.back_y, "< Back");
  };

  auto add_row_hit = [&](int y_mid, uint16_t wid, uint8_t flags, int16_t param) {
    if (!menu_scroll_active && wid != WidgetId::NONE) {
      frame_hits().add_centered_rect(cx, y_mid, row_w, row_h, wid, flags, param);
    }
  };

  auto draw_cycle_card_y = [&](int y_mid, const char *label, bool enabled = true,
                               uint16_t wid = WidgetId::NONE,
                               uint8_t flags = kHitTap | kHitFocusSlot,
                               int16_t param = 0) {
    ui::setting_cycle_row(it, cx, y_mid, row_w, row_h, label, enabled,
                          font_small, col_white, col_slate, theme_secondary,
                          menu_scroll_active ? WidgetId::NONE : wid, flags, param);
  };

  auto draw_slider_card_y = [&](int y_mid, const char *label, int pct, bool enabled,
                                uint16_t wid = WidgetId::NONE,
                                int16_t param = 0) {
    ui::slider_card_row(it, cx, y_mid, row_w, row_h, label, pct, 1, 100, enabled,
                        theme_primary, card_brd_on, font_small,
                        enabled ? col_white : col_slate,
                        menu_scroll_active ? WidgetId::NONE : wid, param);
  };

  auto draw_toggle_card_y = [&](int y_mid, const char *label, bool is_on, int row_slot = -1, int item_param = -1) {
    const uint16_t wid = (!menu_scroll_active && row_slot >= 0 && row_slot < kMaxListSlots)
                             ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                             : WidgetId::NONE;
    const int16_t p = static_cast<int16_t>((item_param >= 0) ? item_param : row_slot);
    ui::toggle_card_row(it, cx, y_mid, row_w, row_h, label, is_on, font_small,
                        theme_primary, col_white, col_slate, wid, p);
  };

  auto draw_action_card_y = [&](int y_mid, const char *label, UiColor brd_c,
                                UiColor txt_c, UiColor bg_c,
                                uint16_t wid = WidgetId::NONE,
                                int16_t param = 0) {
    const std::string fit_lbl = fit_text_to_width_font(label != nullptr ? label : "", row_w - dp_px(20), font_small);
    render_card_box(it, cx - row_w / 2, y_mid - row_h / 2, row_w, row_h, bg_c, brd_c, kRadiusCardPx());
    ui::shadow_print(it, cx, y_mid, font_small, txt_c, TextAlign::CENTER, fit_lbl.c_str());
    add_row_hit(y_mid, wid, kHitTap | kHitFocusSlot, param);
  };

  auto draw_right_chevrons = [&](bool show_up, bool show_down) {
    render_vertical_page_pips(it, show_up, show_down, col_bg, theme_secondary, col_slate);
  };

  const int row0_y = ly.list_row_y(0);
  const int row_s = ly.list_stride;
  const int row_last_y = ly.list_row_y(per_pg - 1);
  const int mask_top_h = std::max(ly.hdr_y + dp_px(18), row0_y - row_h / 2 - dp_px(4));
  const int mask_btm_y = std::min(ly.back_y - dp_px(16), row_last_y + row_h / 2 + dp_px(4));
  const int min_vis_y = mask_top_h - row_h / 2;
  const int max_vis_y = mask_btm_y + row_h / 2;

  auto compute_scroll_offset = [&](int cur_page, int step_rows) -> int {
    const int step_shift = step_rows * row_s;
    const int from_off = std::max(0, cur_page - 1) * step_shift;
    if (menu_scroll_active) {
      const int to_off = std::max(0, menu_scroll_target_page - 1) * step_shift;
      const int delta = to_off - from_off;
      const int shift = menu_scroll_shift(std::abs(delta), now_ms);
      return (delta >= 0) ? (from_off + shift) : (from_off - shift);
    }
    const int bounce_y = edge_bounce().offset_px(
        PageId::SYSTEM, GestureAxisLock::Vertical, now_ms, LATTER_PANEL_PPI);
    return from_off - bounce_y;
  };

  char buf[64];

  if (system_menu_mode == 0) {
    const int cols = tile_grid_cols_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
    const int rows = tile_grid_rows_for(SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0);
    const int per_page = std::max(1, cols * rows);
    const int total_pages = system_dashboard_page_count();
    const int cur_pg = std::clamp(system_dashboard_page, 0, total_pages - 1);
    if (rs.nav.system_dashboard_page != cur_pg) {
      rs.nav.system_dashboard_page = cur_pg;
    }

    ui::TileGridEntry entries[kSystemDashboardItemCount]{};
    for (int i = 0; i < kSystemDashboardItemCount; ++i) {
      const int pg = i / per_page;
      const int local = i % per_page;
      const bool row_en = (i != 5) || prefs.home_screen_enabled;
      ui::TileGridEntry e{};
      e.label = kSysDashboardTiles[i].label;
      e.active = false;
      e.disabled = !row_en;
      e.state = -1;
      e.inverted_badge = false;
      e.has_accent = false;
      e.accent_c = theme_primary;
      e.text_c = row_en ? col_white : col_slate;
      e.draw_icon_fn = draw_system_tile_icon;
      e.icon_ctx = reinterpret_cast<void *>(static_cast<uintptr_t>(static_cast<uint8_t>(kSysDashboardTiles[i].route)));
      e.widget_param = static_cast<int16_t>(i);
      e.col_span = 1U;
      e.row_span = 1U;
      e.col = static_cast<int8_t>(local % cols);
      e.row = static_cast<int8_t>(local / cols);
      e.visual_page = static_cast<int8_t>(pg);
      entries[i] = e;
    }

    ui::TileGridPageSpec spec{};
    spec.page_id = PageId::SYSTEM;
    spec.card_widget_base = WidgetId::SYSTEM_CARD_BASE;
    spec.cur_page = cur_pg;
    spec.total_pages = total_pages;
    spec.page_title_upper = "SYSTEM";
    spec.entries = entries;
    spec.entry_count = kSystemDashboardItemCount;
    ui::render_tile_grid_page(it, ctx, spec);

  } else if (system_menu_mode == 1) {
    const int total_pages = std::max(1, (10 + per_pg - 1) / per_pg);
    const int clamped_pg = std::clamp(sleep_menu_page, 1, total_pages);
    const int current_offset = compute_scroll_offset(clamped_pg, per_pg);
    char sleep_labels[10][36];
#if LATTER_HAS_IMU
    std::snprintf(sleep_labels[0], sizeof(sleep_labels[0]), "Pickup Wake: %s", ctx.pickup_wake_opt.c_str());
#else
    std::snprintf(sleep_labels[0], sizeof(sleep_labels[0]), "Pickup Wake: N/A");
#endif
#if LATTER_HAS_TOUCH
    std::snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Tap Wake: %s", ctx.tap_wake_opt.c_str());
#else
    std::snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Tap Wake: N/A");
#endif
#if LATTER_HAS_BATTERY
    std::snprintf(sleep_labels[2], sizeof(sleep_labels[2]), "Battery Timeout: %s", ctx.display_off_timeout_opt.c_str());
#else
    std::snprintf(sleep_labels[2], sizeof(sleep_labels[2]), "Battery Timeout: N/A");
#endif
    const char *short_m = (ctx.screensaver_mode_opt == "Docked Only") ? "Docked" : ctx.screensaver_mode_opt.c_str();
    std::snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Saver: %s", short_m);
    const char *short_s = (ctx.screensaver_style_opt == "Digital Rain")
                              ? "Rain"
                              : ((ctx.screensaver_style_opt == "Photo Frame")
                                     ? "Photo"
                                     : ctx.screensaver_style_opt.c_str());
    std::snprintf(sleep_labels[4], sizeof(sleep_labels[4]), "Saver Style: %s", short_s);
    std::snprintf(sleep_labels[5], sizeof(sleep_labels[5]), "Docked Timeout: %s", ctx.screensaver_docked_timeout_opt.c_str());
#if LATTER_HAS_BATTERY
    std::snprintf(sleep_labels[6], sizeof(sleep_labels[6]), "Hibernate: %s", ctx.hibernate_timeout_opt.c_str());
#else
    std::snprintf(sleep_labels[6], sizeof(sleep_labels[6]), "Hibernate: N/A");
#endif
    std::snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Bedtime Mode: %s",
                  ctx.bedtime_active ? "On" : "Off");
    const char *short_bt = (ctx.bedtime_display_opt == "Ambient Clock") ? "Clock" : "Off";
    std::snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Bedtime Screen: %s", short_bt);
    std::snprintf(sleep_labels[9], sizeof(sleep_labels[9]), "Do Not Disturb: %s",
                  ctx.do_not_disturb ? "On" : "Off");

    const bool saver_enabled = (ctx.screensaver_mode_opt != "Never");
    const int page_base = (clamped_pg - 1) * per_pg;
    for (int i = 0; i < 10; i++) {
      const int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        const int row_slot = (!menu_scroll_active && i >= page_base && i < page_base + per_pg) ? (i - page_base) : -1;
        if (i == 7) {
          draw_toggle_card_y(y, "Bedtime Mode", ctx.bedtime_active, row_slot, i);
        } else if (i == 9) {
          draw_toggle_card_y(y, "Do Not Disturb", ctx.do_not_disturb, row_slot, i);
        } else {
          const bool en = (i == 0) ? (LATTER_HAS_IMU != 0)
                        : (i == 1) ? (LATTER_HAS_TOUCH != 0)
                        : (i == 2) ? (LATTER_HAS_BATTERY != 0)
                        : (i == 4) ? saver_enabled
                        : (i == 6) ? (LATTER_HAS_BATTERY != 0)
                                   : true;
          const uint16_t wid = (row_slot >= 0)
                                   ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                                   : WidgetId::NONE;
          draw_cycle_card_y(y, sleep_labels[i], en, wid, kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
        }
      }
    }

    if (menu_scroll_active && !backdrop_active()) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_page_header(it, cx, "SLEEP & WAKE", font_small, col_white);
    draw_back();
    if (total_pages > 1) {
      const int active_page = menu_scroll_active ? std::clamp(menu_scroll_target_page, 1, total_pages) : clamped_pg;
      draw_right_chevrons(active_page > 1, active_page < total_pages);
    }

  } else if (system_menu_mode == 2) {
    const int total_pages = std::max(1, (11 + per_pg - 1) / per_pg);
    const int clamped_pg = std::clamp(display_menu_page, 1, total_pages);
    const int current_offset = compute_scroll_offset(clamped_pg, per_pg);
    char disp_labels[11][36];
    const char *start_lbl =
        (ctx.default_start_app_opt == "Area Controls") ? "Area"
        : ((ctx.default_start_app_opt == "Time App") ? "Time"
        : ((ctx.default_start_app_opt == "Clock Face") ? "Clock"
        : (ctx.default_start_app_opt.empty() ? "Home" : ctx.default_start_app_opt.c_str())));
    std::snprintf(disp_labels[0], sizeof(disp_labels[0]), "Start Screen: %s", start_lbl);
    const char *face_lbl =
        (ctx.clock_face_style_opt == "Modern Analog") ? "Analog"
        : ((ctx.clock_face_style_opt == "Hybrid Digital") ? "Hybrid" : "Minimal");
    std::snprintf(disp_labels[1], sizeof(disp_labels[1]), "Clock Face: %s", face_lbl);
    std::snprintf(disp_labels[2], sizeof(disp_labels[2]), "Format: %s",
                  prefs.is_24h_format ? "24-Hour" : "12-Hour");
    std::snprintf(disp_labels[3], sizeof(disp_labels[3]), "Color: %s", ctx.theme_palette_opt.c_str());
    const char *glass_lbl = ctx.glass_style_opt.empty()
                                ? pref_glass_style_cstr(prefs)
                                : ctx.glass_style_opt.c_str();
    std::snprintf(disp_labels[4], sizeof(disp_labels[4]), "Glass Style: %s", glass_lbl);
    const char *ab_s = (ctx.auto_brightness_opt == "Sun Synchronized") ? "Sun"
                       : ((ctx.auto_brightness_opt == "Time Schedule") ? "Sched"
                       : ((ctx.auto_brightness_opt == "Ambient Light") ? "Ambient" : "Off"));
    std::snprintf(disp_labels[5], sizeof(disp_labels[5]), "Auto Bright: %s", ab_s);
    std::snprintf(disp_labels[6], sizeof(disp_labels[6]), "Brightness: %d%%",
                  std::clamp(ctx.manual_brightness_pct, 1, 100));
    format_schedule_range_label(ctx.day_start_hr, ctx.night_start_hr, prefs.is_24h_format,
                                disp_labels[7], sizeof(disp_labels[7]));
    const bool ab_amb = (ctx.auto_brightness_opt == "Ambient Light");
    std::snprintf(disp_labels[8], sizeof(disp_labels[8]), "%s: %d%%",
                  ab_amb ? "Bright Bri" : "Day Bri",
                  std::clamp(ctx.day_brightness_pct, 1, 100));
    std::snprintf(disp_labels[9], sizeof(disp_labels[9]), "%s: %d%%",
                  ab_amb ? "Dark Bri" : "Night Bri",
                  std::clamp(ctx.night_brightness_pct, 1, 100));
    std::snprintf(disp_labels[10], sizeof(disp_labels[10]), "Photo Clock: %s",
                  ctx.screensaver_photo_clock ? "On" : "Off");

    const bool ab_off = (ctx.auto_brightness_opt == "Disabled" || ctx.auto_brightness_opt.empty());
    const bool ab_sched = (ctx.auto_brightness_opt == "Time Schedule");
    const int page_base = (clamped_pg - 1) * per_pg;

    for (int i = 0; i < 11; i++) {
      const int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        const int row_slot = (!menu_scroll_active && i >= page_base && i < page_base + per_pg) ? (i - page_base) : -1;
        if (i == 10) {
          draw_toggle_card_y(y, "Photo Clock", ctx.screensaver_photo_clock, row_slot, i);
        } else if (i <= 2 || i == 4) {
          const uint16_t wid = (row_slot >= 0)
                                   ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                                   : WidgetId::NONE;
          draw_cycle_card_y(y, disp_labels[i], true, wid, kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
        } else if (i == 6 || i == 8 || i == 9) {
          const bool active = (i == 6) ? ab_off : !ab_off;
          const int pct = (i == 6) ? ctx.manual_brightness_pct
                                   : ((i == 8) ? ctx.day_brightness_pct : ctx.night_brightness_pct);
          const uint16_t wid = (active && row_slot >= 0) ? WidgetId::SYS_BRI_SLIDER : WidgetId::NONE;
          draw_slider_card_y(y, disp_labels[i], pct, active, wid, static_cast<int16_t>(i));
        } else {
          const bool active = !(i == 7 && !ab_sched);
          const uint16_t wid = (row_slot >= 0)
                                   ? static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row_slot)
                                   : WidgetId::NONE;
          draw_cycle_card_y(y, disp_labels[i], active, wid, kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
          if (i == 3) {
            const int swatch_x = ly.sys_color_swatch_x();
            const int sw_r = dp_px(9);
            fast_filled_circle(it, swatch_x, y, sw_r, theme_primary);
            fast_circle(it, swatch_x, y, sw_r, col_white);
            if (!menu_scroll_active && row_slot >= 0) {
              frame_hits().add_circle(swatch_x, y, sw_r + dp_px(8),
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
    render_page_header(it, cx, "DISPLAY", font_small, col_white);
    draw_back();
    if (total_pages > 1) {
      const int active_page = menu_scroll_active ? std::clamp(menu_scroll_target_page, 1, total_pages) : clamped_pg;
      draw_right_chevrons(active_page > 1, active_page < total_pages);
    }

  } else if (system_menu_mode == 3) {
    render_page_header(it, cx, "ACTIONS", font_small, col_white);
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
                      "Cancel", "Restart", col_red, kChromeGlassTint,
                      font_small, font_small, col_white, col_white, col_slate);

  } else if (system_menu_mode == 5) {
    render_page_header(it, cx, "UPDATE", font_small, col_white);
    const int y_inst = ly.hdr_y + dp_px(44);
    const int y_avail = y_inst + dp_px(30);
    const int y_summ = y_avail + dp_px(34);
    std::snprintf(buf, sizeof(buf), "Installed: v%s", version_str);
    ui::shadow_print(it, cx, y_inst, font_small, col_slate, TextAlign::CENTER, buf);

    const bool has_update = is_newer_semver_local(ctx.fw_latest_ver.sv(), version_str);
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

    const int cta_h = std::max(row_h, dp_px(44));
    const int y_cta = std::min(ly.back_y - dp_px(30) - cta_h / 2, y_summ + dp_px(52));
    const int cta_w = std::min({ly.max_content_w(), row_w, ly.band_safe_span(y_cta, cta_h, dp_px(18)), dp_px(248)});
    if (has_update) {
      if (ctx.fw_latest_ver.empty()) {
        std::snprintf(buf, sizeof(buf), "Update available");
      } else {
        std::snprintf(buf, sizeof(buf), "Available: v%s", ctx.fw_latest_ver.c_str());
      }
      ui::shadow_print(it, cx, y_avail, font_small, theme_secondary, TextAlign::CENTER, buf);
      std::string summ = ctx.fw_release_summary.str();
      if (summ.empty() || summ == "unavailable" || summ == "unknown") {
        summ = "Firmware update ready.";
      }
      summ = fit_text_to_width_font(summ, std::min(ly.max_content_w(), ly.safe_span(y_summ, dp_px(20))), font_small);
      ui::shadow_print(it, cx, y_summ, font_small, col_white, TextAlign::CENTER, summ.c_str());
      ui::action_pill_centered(it, cx, cta_w, y_cta, cta_h,
                               theme_primary, card_bg_on, "Update Now", theme_secondary,
                               font_small, WidgetId::SYS_PRIMARY_BTN, kHitTap | kHitFocusSlot);
    } else {
      std::snprintf(buf, sizeof(buf), "Latest: v%s", version_str);
      ui::shadow_print(it, cx, y_avail, font_small, theme_secondary, TextAlign::CENTER, buf);
      if (rs.nav.update_check_state == 1) {
        ui::shadow_print(it, cx, y_summ, font_small, theme_secondary, TextAlign::CENTER,
                         "Checking Home Assistant...");
        ui::action_pill_centered(it, cx, cta_w, y_cta, cta_h,
                                 theme_primary, card_bg_on, "Checking...", theme_secondary,
                                 font_small, WidgetId::SYS_PRIMARY_BTN, kHitTap | kHitFocusSlot);
      } else if (rs.nav.update_check_state == 2) {
        ui::shadow_print(it, cx, y_summ, font_small, col_slate, TextAlign::CENTER,
                         "No updates available");
        ui::action_pill_centered(it, cx, cta_w, y_cta, cta_h,
                                 card_brd_on, card_bg_on, "Up to date", col_white,
                                 font_small, WidgetId::SYS_PRIMARY_BTN, kHitTap | kHitFocusSlot);
      } else {
        ui::shadow_print(it, cx, y_summ, font_small, col_slate, TextAlign::CENTER,
                         "Firmware is up to date");
        ui::action_pill_centered(it, cx, cta_w, y_cta, cta_h,
                                 theme_primary, card_bg_on, "Check for Updates", col_white,
                                 font_small, WidgetId::SYS_PRIMARY_BTN, kHitTap | kHitFocusSlot);
      }
    }
    draw_back();

  } else if (system_menu_mode == 6) {
    ui::confirm_modal(it, cx, "INSTALL UPDATE?", "Download & flash firmware", "Keep device powered on",
                      "Cancel", "Install", theme_primary, card_bg_on,
                      font_small, font_small, col_white, col_white, theme_secondary);

  } else if (system_menu_mode == 7) {
    render_page_header(it, cx, "SOUND", font_small, col_white);
    const int spk_vol = std::clamp(ctx.spk_vol_pct, 10, 100);
    std::snprintf(buf, sizeof(buf), "Speaker Vol: %d%%", spk_vol);
    ui::slider_card_row(it, cx, layout().slot5_y(0), row_w, row_h, buf, spk_vol, 10, 100, true,
                        theme_primary, card_brd_on, font_small, col_white,
                        WidgetId::SYS_VOL_SLIDER, 0);
    std::snprintf(buf, sizeof(buf), "Alert Vol: %d%%", ctx.alert_vol_pct);
    draw_cycle_card_y(layout().slot5_y(1), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 1), kHitTap | kHitFocusSlot, 1);
    std::snprintf(buf, sizeof(buf), "Vol Step: %d%%", ctx.vol_step_pct);
    draw_cycle_card_y(layout().slot5_y(2), buf, true,
                      static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + 2), kHitTap | kHitFocusSlot, 2);
    draw_toggle_card_y(layout().slot5_y(3), "Game Sound", ctx.game_sound_enabled, 3, 3);
    draw_back();

  } else if (system_menu_mode == 8) {
    // D-048 / D-070: Passive read-only telemetry renders cleanly on canvas with shadow (ZERO passive glass card!).
    render_page_header(it, cx, "SYSTEM INFO", font_small, col_white);
    const int info_x = ly.sys_info_x();
    const int info_right = SCREEN_W - info_x;
    const int info_top = ly.hdr_y + dp_px(32);
    const int info_bot = ly.back_y - dp_px(30);
    const int row_step = safe_clamp((info_bot - info_top) / 8, dp_px(22), dp_px(34));
    const int start_y = info_top + std::max(0, (info_bot - info_top - row_step * 8) / 2);
    auto print_info_row = [&](int row_idx, const char *lbl, const char *val, UiColor val_col) {
      const int ey = start_y + row_idx * row_step;
      const int row_safe_right = std::min(info_right, cx + ly.safe_span(ey + row_step / 2, dp_px(14)) / 2);
      int w_lbl = 0, x_off = 0, base = 0, th = 0;
      font_small->measure(lbl, &w_lbl, &x_off, &base, &th);
      const int val_x = info_x + w_lbl + dp_px(8);
      const int max_val_w = std::max(dp_px(60), row_safe_right - val_x);
      const std::string fit_val = fit_text_to_width_font(val != nullptr ? val : "", max_val_w, font_small);
      ui::shadow_print(it, info_x, ey, font_small, col_white, TextAlign::TOP_LEFT, lbl);
      ui::shadow_print(it, val_x, ey, font_small, val_col, TextAlign::TOP_LEFT, fit_val.c_str());
    };

    const bool has_update = is_newer_semver_local(ctx.fw_latest_ver.sv(), version_str);
    if (has_update) {
      std::snprintf(buf, sizeof(buf), "v%s (Update: v%s)", version_str, ctx.fw_latest_ver.c_str());
      print_info_row(0, "Version:", buf, col_gold);
    } else {
      std::snprintf(buf, sizeof(buf), "v%s (Up to date)", version_str);
      print_info_row(0, "Version:", buf, col_slate);
    }

    const uint32_t up_sec = now_ms / 1000U;
    const uint32_t up_d = up_sec / 86400U;
    const uint32_t up_h = (up_sec % 86400U) / 3600U;
    const uint32_t up_m = (up_sec % 3600U) / 60U;
    std::snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
                  static_cast<unsigned long>(up_d),
                  static_cast<unsigned long>(up_h),
                  static_cast<unsigned long>(up_m));
    print_info_row(1, "Uptime:", buf, col_slate);

    if (!latter_site_id().empty() && latter_site_id() != "primary") {
      std::snprintf(buf, sizeof(buf), "Site: %s", latter_site_id().c_str());
      print_info_row(2, "Latter:", buf, theme_secondary);
    } else {
      print_info_row(2, "Home Assistant:", ctx.now.is_valid() ? "Connected" : "Syncing...", col_slate);
    }

    if (std::isnan(ctx.wifi_rssi)) {
      std::snprintf(buf, sizeof(buf), "%s dBm  •  BLE: %s", kUnknownGlyph, ctx.ble_active ? "Active" : "Off");
    } else {
      std::snprintf(buf, sizeof(buf), "%.0f dBm  •  BLE: %s", ctx.wifi_rssi, ctx.ble_active ? "Active" : "Off");
    }
    print_info_row(3, "Wi-Fi:", buf, col_slate);

    print_info_row(4, "IP:", ctx.ip_addr.empty() ? "Connecting..." : ctx.ip_addr.c_str(), col_slate);

    std::string area_str = latter_area_label().empty() ? std::string(kUnknownGlyph) : latter_area_label();
    if (!latter_ble_proxy().empty()) {
      area_str += " (" + latter_ble_proxy() + ")";
    }
    print_info_row(5, "Area:", area_str.c_str(), col_slate);

    if (std::isnan(rs.hw_batt_level) || std::isnan(rs.hw_batt_voltage)) {
      std::snprintf(buf, sizeof(buf), "%s", kUnknownGlyph);
    } else {
      std::snprintf(buf, sizeof(buf), "%.0f%% (%.2fV)%s",
                    rs.hw_batt_level, rs.hw_batt_voltage, rs.hw_batt_charging ? " • Chg" : "");
    }
    print_info_row(6, "Battery:", buf, col_slate);

    if (std::isnan(rs.hw_imu_temp)) {
      std::snprintf(buf, sizeof(buf), "%s • %dKB", kUnknownGlyph, ctx.free_heap_kb);
    } else {
      const float temp_c = rs.hw_imu_temp;
      const float temp_f = temp_c * 1.8f + 32.0f;
      std::snprintf(buf, sizeof(buf), "%.1fC (%.1fF) • %dKB", temp_c, temp_f, ctx.free_heap_kb);
    }
    print_info_row(7, "HW / RAM:", buf, col_slate);

    draw_back();

  } else if (system_menu_mode == 9) {
    const int total_pages = std::max(1, (6 + per_pg - 1) / per_pg);
    const int pg = std::clamp(rs.music.settings_page, 1, total_pages);
    const int by = edge_bounce().offset_px(PageId::SYSTEM, GestureAxisLock::Vertical, now_ms, LATTER_PANEL_PPI);
    render_page_header(it, cx, "MUSIC", font_small, col_white);
    const char *prefixes[6] = {"Art", "Overlay", "Auto-Open", "Album Sort", "Artist Discog", "Podcast Sort"};
    const UiStrView vals[6] = {
        ctx.music_fullscreen_art_opt, ctx.music_overlay_style_opt, ctx.music_auto_open_opt,
        ctx.music_album_sort_opt, ctx.music_artist_album_sort_opt, ctx.music_podcast_sort_opt,
    };
    const int start_i = (pg - 1) * per_pg;
    const int vis_cnt = std::min(per_pg, 6 - start_i);
    for (int row = 0; row < vis_cnt; ++row) {
      const int g_idx = start_i + row;
      std::snprintf(buf, sizeof(buf), "%s: %s", prefixes[g_idx], vals[g_idx].c_str());
      draw_cycle_card_y(ly.list_row_y(row) + by, buf, true,
                        static_cast<uint16_t>(WidgetId::SYS_TOGGLE_ROW + row),
                        kHitTap | kHitFocusSlot, static_cast<int16_t>(g_idx));
    }
    draw_back();
    if (total_pages > 1) {
      draw_right_chevrons(pg > 1, pg < total_pages);
    }

  } else if (system_menu_mode == 11) {
    render_page_header(it, cx, "VOICE", font_small, col_white);
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
    render_page_header(it, cx, "SENSITIVITY", font_small, col_white);
    auto short_sens = [](UiStrView s) -> const char * {
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
    ui::hsv_color_wheel(it, ui::system_color_wheel_geom(), color_wheel_hue_deg(), color_wheel_sat(),
                        col_bg, col_white, WidgetId::COLOR_WHEEL_RING);
    const int save_h = std::max(row_h, dp_px(42));
    const int save_y = ly.back_y - dp_px(26) - save_h / 2;
    const int save_w = std::min({ly.max_content_w(), ly.band_safe_span(save_y, save_h, dp_px(18)), dp_px(236)});
    ui::action_pill_centered(it, cx, save_w, save_y, save_h,
                             theme_primary, card_bg_on, "Save Custom Color", col_white, font_small,
                             WidgetId::COLOR_WHEEL_SAVE, kHitTap | kHitFocusSlot);
    draw_back();
  }
}

}  // namespace latter_ui
