#pragma once

// ==============================================================================
// Latter Core UI Layout, Geometry, Navigation Predicates & Dock Engine (ui_layout.h)
// Provides resolution- & shape-adaptive layout metrics and mm-calibrated touch
// thresholds across circular (466x466), watch (410x502), and rectangular/square
// (320x480, 480x480, 720x720) displays.
// ==============================================================================

#include "esphome/core/defines.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "hal_link_report.h"
#include "hal_manifest.h"
#include "hal_panel_power.h"
#include "hal_power_pm.h"
#include "latter_state.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_types.h"

namespace latter_ui {

// ==============================================================================
// 1. Unified Adaptive Panel Geometry (delegates to pure-C++20 compute_panel_layout)
// ==============================================================================
struct UiLayout {
  PanelLayoutMetrics base = compute_panel_layout(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  static constexpr int mm_to_px(float mm, int ppi = LATTER_PANEL_PPI) {
    return mm_to_px_for(mm, ppi);
  }

  int w = base.w;
  int h = base.h;
  int cx = base.cx;
  int cy = base.cy;
  bool is_round = base.is_round;

  // Header & Footer Y coordinates
  int hdr_y = base.hdr_y;
  int sub_hdr_y = base.sub_hdr_y;
  int back_y = base.back_y;
  int home_bar_y = base.home_bar_y;
  int dock_y = base.dock_y;
  int app_dock_y = base.app_dock_y;
  int dock_pips_y = base.dock_pips_y;
  int app_dock_pips_y = base.app_dock_pips_y;
  int dock_r = base.dock_r;

  // Upper-Left Celestial Sun/Moon & Battery Indicator Coordinates
  int celestial_x = base.celestial_x;
  int celestial_y = base.celestial_y;
  int battery_cx = base.battery_cx;
  int battery_y = base.battery_y;

  // Standard Menu Card Width & Left Text Margin
  int list_card_w = base.list_card_w;
  int list_card_left = base.list_card_left;

  // 1. Adaptive 5-Slot Vertical List Geometry
  int slot5_stride = base.slot5_stride;
  int slot5_h = base.slot5_h;
  int slot5_cy = base.slot5_cy;
  constexpr int slot5_y(int idx_0_to_4) const { return base.slot5_y(idx_0_to_4); }

  template <typename TouchT>
  constexpr int hit_slot5(const TouchT &t, int x_min, int x_max) const {
    const int half_w = (w > 520) ? ((w * 150) / 466) : 150;
    const int scaled_min = (w < 380 && x_min < 60) ? 4 : scale_x(x_min);
    const int scaled_max = (w < 380) ? (w - 10) : scale_x(x_max);
    const int eff_min = (w == 466) ? std::min(x_min, 38) : std::max(4, std::min(scaled_min, cx - half_w));
    const int eff_max = (w == 466) ? x_max : std::min(w - 10, std::max(scaled_max, cx + half_w));
    if (t.x < eff_min || t.x > eff_max) return -1;
    const int half_s = slot5_stride / 2;
    for (int i = 0; i < 5; i++) {
      const int ym = slot5_y(i);
      const int y_top = ym - ((i == 0) ? (half_s + 4) : half_s);
      const int y_bot = ym + ((i == 4) ? (half_s + 10) : half_s);
      if (t.y >= y_top && t.y <= y_bot) return i;
    }
    return -1;
  }

  // 2. Adaptive 3-Card Area Control & Home Hub Geometry
  int area_card3_stride = base.area_card3_stride;
  int area_card3_h = base.area_card3_h;
  int area_card3_w = base.area_card3_w;
  int area_card3_cy = base.area_card3_cy;
  constexpr int area_card3_y(int idx_0_to_2) const { return base.area_card3_y(idx_0_to_2); }
  constexpr int area_card_left() const { return cx - (area_card3_w / 2); }
  constexpr int area_card_right() const { return cx + (area_card3_w / 2); }
  constexpr int area_card_div_x() const { return area_card_left() + scale_x(64); }
  constexpr int area_card_orb_x() const { return area_card_right() - scale_x(28); }
  constexpr int hit_area_card3(int tx, int ty) const {
    const int hw = (area_card3_w / 2) + 10;
    if (tx < cx - hw || tx > cx + hw) return -1;
    const int hs = area_card3_stride / 2;
    for (int i = 0; i < 3; i++) {
      const int ym = area_card3_y(i);
      if (ty >= ym - hs && ty <= ym + hs) return i;
    }
    return -1;
  }
  constexpr bool is_area_light_orb(int tx) const {
    const int ox = area_card_orb_x();
    return (tx >= ox - scale_x(28) && tx <= area_card_right() + 8);
  }
  constexpr int area_slider_pct(int tx) const {
    const int x0 = area_card_div_x() + 4;
    const int x1 = area_card_right() - 8;
    if (x1 <= x0) return 100;
    const int raw = ((tx - x0) * 100) / (x1 - x0);
    const int stepped = ((raw + 2) / 5) * 5;
    return (stepped < 5) ? 5 : ((stepped > 100) ? 100 : stepped);
  }

  // 3. Adaptive 3-Card Timers & Alarms Landing Geometry
  int tool_card3_stride = base.tool_card3_stride;
  int tool_card3_h = base.tool_card3_h;
  int tool_card3_w = base.tool_card3_w;
  int tool_card3_cy = base.tool_card3_cy;
  constexpr int tool_card3_y(int idx_0_to_2) const { return base.tool_card3_y(idx_0_to_2); }
  constexpr int tool_card3_div_x() const { return cx + (tool_card3_w / 2) - scale_x(93); }
  constexpr int hit_tool_card3(int tx, int ty) const {
    const int hw = (tool_card3_w / 2) + 12;
    if (tx < cx - hw || tx > cx + hw) return -1;
    const int hs = tool_card3_stride / 2;
    for (int i = 0; i < 3; i++) {
      const int ym = tool_card3_y(i);
      if (ty >= ym - hs && ty <= ym + hs) return i;
    }
    return -1;
  }

  // 4. Adaptive 2x3 Tools & Games Launcher Grid Geometry
  int tools_col_off = base.tools_col_off;
  int tools_row_stride = base.tools_row_stride;
  int tools_grid_cy = base.tools_grid_cy;
  constexpr int tools_row_y(int row_0_to_2) const { return base.tools_row_y(row_0_to_2); }
  constexpr int hit_tools_row(int ty) const {
    const int hs = tools_row_stride / 2;
    for (int r = 0; r < 3; r++) {
      const int ym = tools_row_y(r);
      if (ty >= ym - hs && ty <= ym + hs) return r;
    }
    return -1;
  }
  constexpr int hit_tools_col(int tx) const {
    const int max_dx = tools_col_off * 2 + 12;
    if (tx >= cx - max_dx && tx <= cx) return 0;
    if (tx > cx && tx <= cx + max_dx) return 1;
    return -1;
  }

  // 5. Adaptive Home Screen Vertical Zones
  int home_date_y = base.home_date_y;
  int home_clock_y = base.home_clock_y;
  int home_weather_y = base.home_weather_y;
  constexpr bool hit_home_clock(int tx, int ty) const {
    const int hw = (w > 520) ? 180 : 135;
    const int hh = (h > 520) ? 64 : 48;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_clock_y - hh && ty <= home_clock_y + hh);
  }
  constexpr bool hit_home_weather(int tx, int ty) const {
    const int hw = (w > 520) ? 210 : 165;
    const int hh = (h > 520) ? 36 : 28;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_weather_y - hh && ty <= home_weather_y + hh);
  }
  // Home activity card (docs/device-ui-design.md D-028 / D-045).
  constexpr bool home_activity_two_line() const { return !is_round; }
  constexpr int home_activity_max_rows() const { return is_round ? 1 : 2; }
  constexpr int home_activity_w() const { return (w > 520) ? scale_x(380) : std::min(380, w - 32); }
  constexpr int home_activity_pitch() const { return scale_y(56); }
  constexpr int home_activity_h_for_rows(int rows) const {
    if (is_round) return scale_y(68);
    return (rows >= 2) ? (scale_y(60) + home_activity_pitch()) : scale_y(60);
  }
  constexpr int home_activity_h() const { return home_activity_h_for_rows(home_activity_max_rows()); }
  constexpr int home_activity_dock_gap() const { return mm_to_px(3.0f); }
  constexpr int home_activity_bottom() const { return dock_y - dock_r - home_activity_dock_gap(); }
  constexpr int home_activity_cy_for_h(int weather_h, int card_h) const {
    return is_round ? ((home_weather_y + weather_h / 2) + (dock_y - dock_r)) / 2
                    : (home_activity_bottom() - card_h / 2);
  }
  constexpr int home_activity_cy_for_rows(int weather_h, int rows) const {
    return home_activity_cy_for_h(weather_h, home_activity_h_for_rows(rows));
  }
  constexpr int home_activity_cy(int weather_h) const {
    return home_activity_cy_for_rows(weather_h, home_activity_max_rows());
  }
  constexpr bool home_activity_has_drop(int front_count) const {
    return front_count > home_activity_max_rows();
  }
  constexpr int home_dock_zone_top(int weather_h, bool card_shown, int shown_rows = 2) const {
    const int base_y = scale_y(320);
    if (!card_shown) return base_y;
    const int r = (shown_rows <= 1) ? 1 : home_activity_max_rows();
    const int ch = home_activity_h_for_rows(r);
    const int ccy = home_activity_cy_for_h(weather_h, ch);
    const int below_card = ccy + ch / 2 + mm_to_px(1.0f);
    return std::min(dock_y - dock_hit_r(), std::max(base_y, below_card));
  }
  constexpr bool hit_home_activity(int tx, int ty, int weather_h, int shown_rows = 2) const {
    const int r = (shown_rows <= 1) ? 1 : home_activity_max_rows();
    const int hw = home_activity_w() / 2;
    const int hh = home_activity_h_for_rows(r) / 2 + scale_y(4);
    const int acy = home_activity_cy_for_rows(weather_h, r);
    return (tx >= cx - hw && tx <= cx + hw && ty >= acy - hh && ty <= acy + hh);
  }

  // 4-Column Calculator Keypad X centers
  constexpr int calc_col_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 72 : ((w > 520) ? ((w * 82) / 466) : 82);
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  // 6. Single-Source Music Now Playing & Scrubber Geometry
  int music_art_cy = scale_y(233);
  constexpr int music_prev_x() const { return cx - (is_round ? scale_w(109) : ((w < 380) ? 96 : scale_x(104))); }
  constexpr int music_next_x() const { return cx + (is_round ? scale_w(109) : ((w < 380) ? 96 : scale_x(104))); }
  int music_scrub_y = scale_y(242);
  constexpr int music_seek_left_x() const { return is_round ? scale_w(50) : scale_x(34); }
  constexpr int music_seek_right_x() const { return w - music_seek_left_x(); }
  constexpr int music_scrub_x0() const { return is_round ? scale_w(95) : scale_x(72); }
  constexpr int music_scrub_x1() const { return w - music_scrub_x0(); }
  constexpr int music_scrub_w() const { return music_scrub_x1() - music_scrub_x0(); }
  constexpr float music_scrub_frac(int tx) const {
    const int bw = music_scrub_w();
    if (bw <= 10) return 0.0f;
    const float f = static_cast<float>(tx - music_scrub_x0()) / static_cast<float>(bw);
    return (f < 0.0f) ? 0.0f : ((f > 1.0f) ? 1.0f : f);
  }

  // 7. Single-Source Light Kelvin / Brightness Inspector Slider Geometry
  constexpr int light_slider_card_w() const { return (w < 380) ? (w - 24) : scale_x(316); }
  constexpr int light_kelvin_bar_w() const { return light_slider_card_w() - scale_x(32); }
  constexpr int light_kelvin_bar_x0() const { return cx - (light_kelvin_bar_w() / 2); }
  constexpr int light_bri_bar_w() const { return light_slider_card_w() - scale_x(16); }
  constexpr int light_bri_bar_x0() const { return cx - (light_bri_bar_w() / 2); }

  // 8. Single-Source System Color Swatch & Info Margin Geometry
  constexpr int sys_color_swatch_x() const { return cx + (list_card_w / 2) - scale_x(34); }
  constexpr bool hit_sys_color_swatch(int tx) const { return tx >= (sys_color_swatch_x() - scale_x(22)); }
  constexpr int sys_info_x() const { return is_round ? 88 : ((w < 380) ? 18 : scale_x(42)); }

  // 9. Single-Source Calendar Map Floating Zoom Button Geometry
  int map_zoom_y = scale_y(233);
  constexpr int map_zoom_left_x() const { return is_round ? 44 : scale_x(34); }
  constexpr int map_zoom_right_x() const { return w - map_zoom_left_x(); }
  constexpr bool hit_map_zoom_out(int tx, int ty) const {
    const int r = scale_s(45);
    return (ty >= map_zoom_y - r && ty <= map_zoom_y + r && tx <= map_zoom_left_x() + r);
  }
  constexpr bool hit_map_zoom_in(int tx, int ty) const {
    const int r = scale_s(45);
    return (ty >= map_zoom_y - r && ty <= map_zoom_y + r && tx >= map_zoom_right_x() - r);
  }

  // 10. Physical kinematic touch thresholds in millimetres (docs/device-ui-design.md D-049),
  //     converted with LATTER_PANEL_PPI so tap_max() (1.65 mm) < swipe_min() (2.35 mm) across all panels.
  constexpr int drag_start() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).slider_start_px; }
  constexpr int swipe_flick() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_flick_px; }
  constexpr int flick_min() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_flick_px; }
  constexpr int tap_max() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).tap_slop_px; }
  constexpr int axis_lock() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).axis_lock_px; }
  constexpr int dock_release_swipe() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_flick_px; }
  constexpr int swipe_short() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int swipe_min() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int list_swipe() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int page_swipe() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int wide_swipe() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int canvas_swipe() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int fast_pan() const { return mm_to_px(10.5f); }
  constexpr int edge_guard_y() const { return h - mm_to_px(1.5f); }                      // 16 px band below in-app content
  constexpr int home_zone_y() const { return is_round ? scale_min(412) : (h - mm_to_px(5.2f)); }   // 54 px: swipe-up home starts here
  constexpr int chin_guard_y() const { return is_round ? scale_min(412) : (h - mm_to_px(2.7f)); }  // 28 px: the home pill's chin
  constexpr int mode_bar_zone_top() const { return app_dock_y - dock_r - scale_s(14); }
  constexpr int dock_hit_r() const { return std::max(dock_r + 16, mm_to_px(3.6f)); }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
}

// Canonical 4-column stepper stride shared by ui::stepper_row and hit_4col_stepper.
inline int hit_4col_stepper(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 66 : scale_x(80);
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

inline int hit_4col_calc(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 72 : scale_x(82);
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

inline bool &home_activity_card_shown() {
  static bool shown = false;
  return shown;
}

inline int &home_activity_rendered_rows() {
  static int rows = 0;
  return rows;
}

inline int &home_weather_text_h() {
  static int h = (SCREEN_H > 520) ? 45 : 32;
  return h;
}

// ==============================================================================
// 2. Domain UI Runtime Helpers (defined in include/latter_state.h)
// ==============================================================================

// ==============================================================================
// 3.Launcher Counts, Navigation Predicates ([HOME PREDICATE]) & Mode-Bar Motion
// ==============================================================================
inline constexpr int tools_launcher_item_count() noexcept {
  return tools_launcher_item_count_for(LATTER_ROUND_DISPLAY != 0, LATTER_HAS_IMU != 0);
}

inline constexpr int tools_launcher_page_count() noexcept {
  return tools_launcher_page_count(tools_launcher_item_count());
}

inline bool is_on_anchor_root(int gui_page, int system_menu_mode, int active_game,
                              int music_overlay_mode, int area_subpage, bool area_picker,
                              int remote_subpage, int tools_subpage,
                              const PersistedDevicePrefs &p = persisted_prefs()) {
  const int anchor = resolve_anchor_dock_app(p);
  if (anchor == PageId::HOME) return gui_page == PageId::HOME;
  if (anchor == PageId::PINNED) return gui_page == PageId::PINNED;
  if (anchor == PageId::CLOCK) return gui_page == PageId::CLOCK;
  if (anchor == PageId::TIME) {
    return gui_page == PageId::TIME &&
           timer_view_slot() == 0 &&
           runtime_state().alarms.edit_idx == 0;
  }
  if (anchor == PageId::MUSIC) {
    return gui_page == PageId::MUSIC &&
           runtime_state().music.drilldown == MusicDrilldown::NONE &&
           runtime_state().music.takeover == MusicTakeover::NONE &&
           music_overlay_mode == 0;
  }
  if (anchor == PageId::HOME_HUB) {
    return gui_page == PageId::HOME_HUB &&
           !cam_expanded_fullscreen() &&
           !cam_quick_reply_open();
  }
  if (anchor == PageId::AREA) {
    if (runtime_state().area.active_area_id.empty()) {
      return gui_page == PageId::AREA &&
             (area_subpage == AreaSubpage::OVERVIEW || area_subpage == AreaSubpage::PICKER);
    }
    return gui_page == PageId::AREA && area_subpage == AreaSubpage::OVERVIEW && !area_picker;
  }
  if (anchor == PageId::REMOTE) return gui_page == PageId::REMOTE && remote_subpage == 0;
  if (anchor == PageId::WEATHER) return gui_page == PageId::WEATHER;
  if (anchor == PageId::GAMES) return gui_page == PageId::GAMES && active_game == 0;
  if (anchor == PageId::TOOLS) {
    return gui_page == PageId::TOOLS && tools_subpage == ToolsSubpage::LAUNCHER;
  }
  if (anchor == PageId::CALENDAR) {
    return gui_page == PageId::CALENDAR &&
           runtime_state().cal.drilldown == CalDrilldown::NONE &&
           !runtime_state().cal.delete_confirm_open;
  }
  if (anchor == PageId::SYSTEM) return gui_page == PageId::SYSTEM && system_menu_mode == 0;
  return false;
}

inline bool is_on_anchor_root(const DeviceRuntimeState &rt = runtime_state(),
                              const PersistedDevicePrefs &p = persisted_prefs()) {
  if (resolve_anchor_dock_app(p) == PageId::MUSIC) {
    return rt.nav.gui_page == PageId::MUSIC &&
           rt.music.drilldown == MusicDrilldown::NONE &&
           rt.music.takeover == MusicTakeover::NONE &&
           rt.music.overlay_mode == 0;
  }
  return is_on_anchor_root(rt.nav.gui_page, rt.nav.system_menu_mode, rt.nav.active_game,
                           rt.music.overlay_mode, rt.nav.area_subpage,
                           rt.area.browse_mode == "areas", rt.nav.remote_subpage,
                           rt.nav.tools_subpage, p);
}

// Single go-home predicate shared by gui.yaml and TouchGestureEngine ([HOME PREDICATE]).
inline bool can_go_home(int gui_page, int system_menu_mode, int active_game, int music_overlay_mode,
                        int area_subpage, bool area_picker, int remote_subpage, int tools_subpage,
                        int alarm_edit_idx, bool alert_ringing) {
  if (gui_page == PageId::HOME || gui_page == PageId::CLOCK || alert_ringing) return false;
  if (runtime_state().nav.shortcut_confirm.open) return false;
  if (gui_page == PageId::TIME && (timer_view_slot() > 0 || alarm_edit_idx > 0)) {
    return false;
  }
  if (gui_page == PageId::HOME_HUB && (cam_expanded_fullscreen() || cam_quick_reply_open())) {
    return false;
  }
  if (gui_page == PageId::SYSTEM && system_menu_mode > 0) return false;
  if (gui_page == PageId::GAMES && active_game > 0) return false;
  if (gui_page == PageId::MUSIC &&
      (runtime_state().music.drilldown != MusicDrilldown::NONE ||
       runtime_state().music.takeover != MusicTakeover::NONE ||
       music_overlay_mode > 0)) {
    return false;
  }
  if (gui_page == PageId::AREA && (area_subpage > 0 || area_picker)) {
    const bool root_picker = !persisted_prefs().home_screen_enabled &&
                             resolve_anchor_dock_app() == PageId::AREA &&
                             runtime_state().area.active_area_id.empty() &&
                             area_subpage == AreaSubpage::PICKER;
    if (!root_picker) return false;
  }
  if (gui_page == PageId::REMOTE && remote_subpage > 0) return false;
  if (gui_page == PageId::TOOLS && tools_subpage > 0) {
    return false;
  }
  if (gui_page == PageId::CALENDAR &&
      (runtime_state().cal.drilldown != CalDrilldown::NONE ||
       runtime_state().cal.delete_confirm_open)) {
    return false;
  }
  if (!persisted_prefs().home_screen_enabled &&
      is_on_anchor_root(gui_page, system_menu_mode, active_game, music_overlay_mode,
                        area_subpage, area_picker, remote_subpage, tools_subpage,
                        persisted_prefs())) {
    return false;
  }
  return true;
}

inline bool can_go_home(const DeviceRuntimeState &rt = runtime_state()) {
  if (rt.nav.gui_page == PageId::HOME || rt.nav.gui_page == PageId::CLOCK ||
      rt.alarms.ringing || rt.timers.ringing || rt.alarms.find_my_active) {
    return false;
  }
  if (rt.nav.shortcut_confirm.open) return false;
  if (rt.nav.gui_page == PageId::TIME && (rt.timers.view_slot > 0 || rt.alarms.edit_idx > 0)) {
    return false;
  }
  if (rt.nav.gui_page == PageId::HOME_HUB &&
      (rt.intercom.cam_expanded_fullscreen || rt.intercom.cam_quick_reply_open)) {
    return false;
  }
  if (rt.nav.gui_page == PageId::SYSTEM && rt.nav.system_menu_mode > 0) return false;
  if (rt.nav.gui_page == PageId::GAMES && rt.nav.active_game > 0) return false;
  if (rt.nav.gui_page == PageId::MUSIC &&
      (rt.music.drilldown != MusicDrilldown::NONE ||
       rt.music.takeover != MusicTakeover::NONE ||
       rt.music.overlay_mode > 0)) {
    return false;
  }
  const bool area_picker = (rt.area.browse_mode == "areas");
  if (rt.nav.gui_page == PageId::AREA && (rt.nav.area_subpage > 0 || area_picker)) {
    const bool root_picker = !persisted_prefs().home_screen_enabled &&
                             resolve_anchor_dock_app() == PageId::AREA &&
                             rt.area.active_area_id.empty() &&
                             rt.nav.area_subpage == AreaSubpage::PICKER;
    if (!root_picker) return false;
  }
  if (rt.nav.gui_page == PageId::REMOTE && rt.nav.remote_subpage > 0) return false;
  if (rt.nav.gui_page == PageId::TOOLS && rt.nav.tools_subpage > 0) return false;
  if (rt.nav.gui_page == PageId::CALENDAR &&
      (rt.cal.drilldown != CalDrilldown::NONE || rt.cal.delete_confirm_open)) {
    return false;
  }
  if (!persisted_prefs().home_screen_enabled && is_on_anchor_root(rt, persisted_prefs())) {
    return false;
  }
  return true;
}

inline bool music_art_only(int gui_page, bool overlay_always_visible, bool hud_active) {
  const auto &m = runtime_state().music;
  if (gui_page != PageId::MUSIC ||
      m.drilldown != MusicDrilldown::NONE ||
      m.takeover != MusicTakeover::NONE ||
      hud_active) {
    return false;
  }
  if (m.view == MusicView::NOW_PLAYING) return !overlay_always_visible;
  return m.view == MusicView::LYRICS || m.view == MusicView::INFO;
}

inline int get_home_dock_start_index(int total_count, int page) {
  if (total_count <= 4 || page <= 0) return 0;
  const int total_pages = (total_count + 3) / 4;
  const int eff_page = (page >= total_pages) ? (total_pages - 1) : page;
  int raw_start = eff_page * 4;
  if (raw_start + 4 > total_count) {
    raw_start = total_count - 4;
  }
  return std::max(0, raw_start);
}

inline void get_home_dock_window_xs(int total_count, int page, int cx, int out_xs[4],
                                    bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  const int start_idx = get_home_dock_start_index(total_count, page);
  const int vis_count = std::min(4, total_count - start_idx);
  if (vis_count == 1) {
    out_xs[0] = cx;
  } else if (vis_count == 2) {
    out_xs[0] = cx - scale_x(44);
    out_xs[1] = cx + scale_x(44);
  } else if (vis_count == 3) {
    out_xs[0] = cx - scale_x(84);
    out_xs[1] = cx;
    out_xs[2] = cx + scale_x(84);
  } else {
#if LATTER_ROUND_DISPLAY
    out_xs[0] = scale_w(112);
    out_xs[1] = scale_w(193);
    out_xs[2] = scale_w(274);
    out_xs[3] = scale_w(355);
#else
    const int stride = (SCREEN_W < 380) ? 74 : scale_x(98);
    out_xs[0] = cx - (stride * 3) / 2;
    out_xs[1] = cx - (stride / 2);
    out_xs[2] = cx + (stride / 2);
    out_xs[3] = cx + (stride * 3) / 2;
#endif
  }
}

inline void get_mode_bar_window_xs(int total_count, int win_start, int cx, int out_xs[4],
                                   bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  const int start_idx = get_mode_bar_start_index(total_count, win_start);
  const int vis_count = std::min(4, total_count - start_idx);
  bool dl = false, dr = false;
  get_home_dock_window_xs(vis_count, 0, cx, out_xs, dl, dr);
  out_has_left_arrow  = (start_idx > 0);
  out_has_right_arrow = (start_idx + 4 < total_count);
}

struct DockSlideAnimator {
  float cur_start = 0.0f;
  float from_start = 0.0f;
  int target_start = -1;
  uint32_t win_start_ms = 0;
  bool win_animating = false;

  float cur_active = 0.0f;
  float from_active = 0.0f;
  int target_active = -1;
  uint32_t act_start_ms = 0;
  bool act_animating = false;

  void reset() {
    target_start = -1;
    target_active = -1;
    win_animating = false;
    act_animating = false;
  }

  void snap(int win_start = 0, int active_idx = -1) noexcept {
    target_start = win_start;
    from_start = static_cast<float>(win_start);
    cur_start = from_start;
    win_animating = false;
    const int eff_active = (active_idx >= 0) ? active_idx : win_start;
    target_active = eff_active;
    from_active = static_cast<float>(eff_active);
    cur_active = from_active;
    act_animating = false;
  }

  float step_window(int new_start, uint32_t now_ms) {
    if (target_start < 0 || std::abs(new_start - target_start) > 4) {
      target_start = new_start;
      from_start = static_cast<float>(new_start);
      cur_start = from_start;
      win_animating = false;
      return cur_start;
    }
    if (new_start != target_start) {
      from_start = cur_start;
      target_start = new_start;
      win_start_ms = now_ms;
      win_animating = true;
    }
    if (win_animating) {
      const uint32_t dt = now_ms - win_start_ms;
      if (dt >= kMotionMs) {
        cur_start = static_cast<float>(target_start);
        win_animating = false;
      } else {
        const float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kMotionMs));
        cur_start = from_start + (static_cast<float>(target_start) - from_start) * e;
      }
    } else {
      cur_start = static_cast<float>(target_start);
    }
    return cur_start;
  }

  float step_active(int new_active, uint32_t now_ms) {
    if (new_active < 0) {
      target_active = -1;
      from_active = -1.0f;
      cur_active = -1.0f;
      act_animating = false;
      return cur_active;
    }
    if (target_active < 0 || std::abs(new_active - target_active) > 8) {
      target_active = new_active;
      from_active = static_cast<float>(new_active);
      cur_active = from_active;
      act_animating = false;
      return cur_active;
    }
    if (new_active != target_active) {
      from_active = cur_active;
      target_active = new_active;
      act_start_ms = now_ms;
      act_animating = true;
    }
    if (act_animating) {
      const uint32_t dt = now_ms - act_start_ms;
      if (dt >= kMotionMs) {
        cur_active = static_cast<float>(target_active);
        act_animating = false;
      } else {
        const float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kMotionMs));
        cur_active = from_active + (static_cast<float>(target_active) - from_active) * e;
      }
    } else {
      cur_active = static_cast<float>(target_active);
    }
    return cur_active;
  }

  bool is_animating() const { return win_animating || act_animating; }
};

inline DockSlideAnimator &home_dock_anim() {
  static DockSlideAnimator anim;
  return anim;
}

inline DockSlideAnimator &mode_bar_anim() {
  static DockSlideAnimator anim;
  return anim;
}

inline int dock_dirty_band_y0() {
  const auto ly = layout();
  return std::max(0, std::min(ly.dock_y, ly.app_dock_y) - ly.dock_r - scale_y(16));
}

inline bool any_dock_anim_active(int gui_page = -1, int home_subpage = 0, uint32_t now_ms = 0U) {
  if (gui_page >= 0) {
    if (gui_page != 1 || home_subpage != 0) {
      home_dock_anim().win_animating = false;
      home_dock_anim().act_animating = false;
    }
    if (gui_page != 2 && gui_page != 5 && gui_page != 6 && gui_page != 9 && gui_page != 10 && gui_page != 12) {
      mode_bar_anim().win_animating = false;
      mode_bar_anim().act_animating = false;
    }
  }
  const bool bounce_h = (now_ms != 0U && gui_page >= 0) &&
                        edge_bounce().is_active(gui_page, GestureAxisLock::Horizontal, now_ms);
  return home_dock_anim().is_animating() || mode_bar_anim().is_animating() || bounce_h;
}

inline bool any_vert_bounce_active(int gui_page, uint32_t now_ms) {
  return (now_ms != 0U && gui_page >= 0) &&
         edge_bounce().is_active(gui_page, GestureAxisLock::Vertical, now_ms);
}

inline int get_dock_slot_stride(const int *win_xs, int vis_count) {
  if (vis_count >= 2) return win_xs[1] - win_xs[0];
#if LATTER_ROUND_DISPLAY
  return scale_w(81);
#else
  return (SCREEN_W < 380) ? 74 : scale_x(98);
#endif
}

// ==============================================================================
// 4. Rotary / Touchless Focus State & Ring Primitives
// ==============================================================================
inline int &ui_focus_index() {
  static int s_idx = 0;
  return s_idx;
}

inline uint32_t &ui_focus_last_ms() {
  static uint32_t s_ms = 0;
  return s_ms;
}

inline bool ui_focus_active(uint32_t now_ms) {
#if !LATTER_HAS_TOUCH
  (void)now_ms;
  return true;
#else
  const uint32_t last = ui_focus_last_ms();
  return (last > 0) && ((now_ms - last) < 8000U);
#endif
}

inline void activate_ui_focus(uint32_t now_ms) {
  ui_focus_last_ms() = (now_ms == 0) ? 1U : now_ms;
}

inline void clear_ui_focus() {
#if LATTER_HAS_TOUCH
  ui_focus_last_ms() = 0;
#endif
}

template <typename DisplayT>
inline void render_focus_ring_circle(DisplayT &it, int cx, int cy, int r,
                                     esphome::Color accent, esphome::Color inner_hi) {
  fast_circle_aa(it, cx, cy, static_cast<float>(r) + 2.5f, accent, 2.0f);
  fast_circle_aa(it, cx, cy, static_cast<float>(r) + 1.0f, inner_hi, 1.0f);
}

template <typename DisplayT>
inline void render_focus_ring_rect(DisplayT &it, int cx, int cy, int w, int h,
                                   esphome::Color accent, esphome::Color inner_hi) {
  const int x0 = cx - w / 2;
  const int y0 = cy - h / 2;
  fast_rect_outline(it, x0 - 2, y0 - 2, w + 4, h + 4, accent);
  fast_rect_outline(it, x0 - 3, y0 - 3, w + 6, h + 6, accent);
  fast_rect_outline(it, x0 - 1, y0 - 1, w + 2, h + 2, inner_hi);
}

}  // namespace latter_ui
