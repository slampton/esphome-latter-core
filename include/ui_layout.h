#pragma once

// ==============================================================================
// Latter Core UI Layout, Geometry, Navigation Predicates & Dock Engine (ui_layout.h)
// Pure C++20 UI header — zero ESPHome or HAL includes.
// ==============================================================================

#include "state/device_runtime_state.h"
#include "ui_gfx.h"
#include "ui_hit_table.h"
#include "ui_touch_recognizer.h"
#include "ui_types.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace latter_ui {

inline int menu_scroll_shift(int span_px, uint32_t now_ms) {
  const uint32_t start = latter_core::menu_scroll_start_ms();
  const uint32_t elapsed = (now_ms >= start) ? (now_ms - start) : 0U;
  return menu_scroll_shift_elapsed(span_px, elapsed);
}

// ==============================================================================
// 1. Unified Adaptive Panel Geometry (delegates to pure-C++20 compute_panel_layout)
// ==============================================================================
struct UiLayout {
  UiDisplaySpec spec = current_display_spec();
  SafeFrame frame = SafeFrame::for_spec(spec);
  PanelLayoutMetrics base = compute_panel_layout(
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0,
      LATTER_PANEL_PPI, LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
  HomeActivityMetrics home_act = compute_home_activity_metrics(
      spec, base.home_weather_y, base.dock_y - base.dock_r, kAutoFonts.small, kAutoFonts.date);

  static constexpr int mm_to_px(float mm, int ppi = LATTER_PANEL_PPI) {
    return mm_to_px_for(mm, ppi);
  }
  constexpr int min_hit_px() const {
    return std::max(dp_px(36), mm_to_px(kHitFloorPrimaryMm));
  }
  constexpr int min_row_hit_px() const {
    return min_row_hit_px_for(LATTER_PANEL_PPI);
  }
  constexpr int safe_span(int y, int inset = 12) const {
    return frame.safe_span(y, inset);
  }
  constexpr int band_safe_span(int cy_band, int band_h, int inset = 12) const {
    return frame.band_safe_span(cy_band, band_h, inset);
  }
  constexpr int max_content_w(int max_dp = 460) const {
    return frame.max_content_w(max_dp);
  }

  int w = base.w;
  int h = base.h;
  int cx = base.cx;
  int cy = base.cy;
  bool is_round = base.is_round;
  SizeClass size_class = base.size_class;

  int hdr_y = base.hdr_y;
  int sub_hdr_y = base.sub_hdr_y;
  int back_y = base.back_y;
  int home_bar_y = base.home_bar_y;
  int dock_y = base.dock_y;
  int app_dock_y = base.app_dock_y;
  int dock_pips_y = base.dock_pips_y;
  int app_dock_pips_y = base.app_dock_pips_y;
  int dock_r = base.dock_r;
  int dock_stride = base.dock_stride;
  int dock_slot_count = base.dock_slot_count;

  int celestial_x = base.celestial_x;
  int celestial_y = base.celestial_y;
  int battery_cx = base.battery_cx;
  int battery_y = base.battery_y;

  int list_card_w = base.list_card_w;
  int list_card_left = base.list_card_left;
  int list_rows = base.list_rows;
  int peer_list_rows = base.peer_list_rows;
  int queue_list_rows = base.queue_list_rows;
  int list_h = base.list_h;
  int list_stride = base.list_stride;
  int peer_list_stride = base.peer_list_stride;
  int list_top_cy = base.list_top_cy;
  int peer_list_top_cy = base.peer_list_top_cy;
  constexpr int list_row_y(int idx) const { return base.list_row_y(idx); }
  constexpr int peer_list_row_y(int idx) const { return base.peer_list_row_y(idx); }

  int slot5_stride = base.slot5_stride;
  int slot5_h = base.slot5_h;
  int slot5_cy = base.slot5_cy;
  constexpr int slot5_y(int idx_0_to_4) const { return base.slot5_y(idx_0_to_4); }

  int area_card3_stride = base.area_card3_stride;
  int area_card3_h = base.area_card3_h;
  int area_card3_w = base.area_card3_w;
  int area_card3_cy = base.area_card3_cy;
  constexpr int area_card3_y(int idx_0_to_2) const { return base.area_card3_y(idx_0_to_2); }
  constexpr int area_card_left() const { return cx - (area_card3_w / 2); }
  constexpr int area_card_right() const { return cx + (area_card3_w / 2); }
  constexpr int area_card_div_x() const { return area_card_left() + std::max(dp_px(64), min_hit_px()); }
  constexpr int area_card_orb_x() const { return area_card_right() - dp_px(28); }

  int tool_card3_stride = base.tool_card3_stride;

  int home_date_y = base.home_date_y;
  int home_clock_y = base.home_clock_y;
  int home_weather_y = base.home_weather_y;
  constexpr bool home_activity_two_line() const { return home_act.max_rows >= 2; }
  constexpr int home_activity_max_rows() const { return home_act.max_rows; }
  constexpr int home_activity_w() const { return home_act.card_w; }
  constexpr int home_activity_pitch() const { return home_act.pitch; }
  constexpr int home_activity_h_for_rows(int rows) const {
    return (rows >= 2 && home_act.max_rows >= 2) ? home_act.row2_h : home_act.row1_h;
  }
  constexpr int home_activity_h() const { return home_activity_h_for_rows(home_activity_max_rows()); }
  constexpr int home_activity_dock_gap() const { return home_act.dock_gap; }
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

  constexpr int calc_col_x(int col_0_to_3) const {
    const int safe_w = frame.band_safe_span(cy, dp_px(260), dp_px(12));
    const int max_span = std::min(safe_w, max_content_w(360));
    const int stride = std::max(dp_px(64), std::min(dp_px(84), max_span / 4));
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  constexpr int light_slider_card_w() const {
    return std::min(max_content_w(380), frame.safe_span(cy, dp_px(12)));
  }
  constexpr int light_kelvin_bar_w() const { return light_slider_card_w() - dp_px(32); }
  constexpr int light_kelvin_bar_x0() const { return cx - (light_kelvin_bar_w() / 2); }

  constexpr int sys_color_swatch_x() const { return cx + (list_card_w / 2) - dp_px(34); }
  constexpr int sys_info_x() const {
    return is_round ? (cx - list_card_w / 2) : std::max(dp_px(20), frame.x_inset(cy, dp_px(18)));
  }

  constexpr int drag_start() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).slider_start_px; }
  constexpr int flick_min() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_flick_px; }
  constexpr int tap_max() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).tap_slop_px; }
  constexpr int axis_lock() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).axis_lock_px; }
  constexpr int swipe_min() const { return KinematicMetrics::for_ppi(LATTER_PANEL_PPI).swipe_live_px; }
  constexpr int edge_guard_y() const { return h - mm_to_px(1.5f); }
  constexpr int top_edge_zone_y() const {
    return compute_top_edge_zone_y(w, h, is_round, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI, LATTER_CORNER_R);
  }
  constexpr int home_zone_y() const {
    if (is_round) return (h * 884) / 1000;
    const int zone_h = std::max(mm_to_px(5.2f), dp_px(52));
    return std::max(app_dock_y + dp_px(4), h - zone_h);
  }
  constexpr int chin_guard_y() const { return is_round ? ((h * 884) / 1000) : (h - mm_to_px(2.7f)); }
  constexpr int mode_bar_zone_top() const { return app_dock_y - dock_r - dp_px(14); }
  constexpr int dock_hit_r() const { return base.dock_hit_r; }

  constexpr bool cal_wide_layout() const {
    return (frame.w_dp >= 500) && !is_round && (spec.corner_r <= dp_px(32));
  }
  constexpr int cal_sub_y() const { return sub_hdr_y + dp_px(is_round ? 8 : 10); }
  constexpr int cal_nav_btn_r() const { return dp_px(size_class == SizeClass::Compact ? 15 : 17); }
  constexpr int cal_events_per_page_for_avail(int avail_h) const {
    const int gap = dp_px(6);
    const int pref_h = std::max(dp_px(62), min_row_hit_px() + dp_px(4));
    return safe_clamp((avail_h + gap) / (pref_h + gap), 1, 5);
  }
  constexpr int cal_events_per_page(bool with_week_strip = false) const {
    const int strip_extra = with_week_strip
        ? (std::max(dp_px(48), min_row_hit_px()) + dp_px(8))
        : 0;
    const int content_top = cal_sub_y() + cal_nav_btn_r() + dp_px(10) + strip_extra;
    const int content_bot = mode_bar_zone_top() - dp_px(8);
    return cal_events_per_page_for_avail(std::max(dp_px(80), content_bot - content_top));
  }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
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
  static int h = kAutoFonts.date;
  return h;
}

inline bool is_on_anchor_root(int gui_page, int system_menu_mode, int active_game,
                              int area_subpage, bool area_picker,
                              int remote_subpage, int tools_subpage,
                              const latter_core::PersistedDevicePrefs &p = latter_core::persisted_prefs()) {
  const int anchor = latter_core::resolve_anchor_dock_app(p);
  if (anchor == latter_core::PageId::HOME) return gui_page == latter_core::PageId::HOME;
  if (anchor == latter_core::PageId::PINNED) return gui_page == latter_core::PageId::PINNED;
  if (anchor == latter_core::PageId::CLOCK) return gui_page == latter_core::PageId::CLOCK;
  if (anchor == latter_core::PageId::TIME) {
    return gui_page == latter_core::PageId::TIME &&
           latter_core::timer_view_slot() == 0 &&
           latter_core::runtime_state().alarms.edit_idx == 0;
  }
  if (anchor == latter_core::PageId::MUSIC) {
    return gui_page == latter_core::PageId::MUSIC &&
           latter_core::runtime_state().music.drilldown == latter_core::MusicDrilldown::NONE &&
           latter_core::runtime_state().music.takeover == latter_core::MusicTakeover::NONE;
  }
  if (anchor == latter_core::PageId::HOME_HUB) {
    return gui_page == latter_core::PageId::HOME_HUB;
  }
  if (anchor == latter_core::PageId::CAMERAS) {
    return gui_page == latter_core::PageId::CAMERAS && !latter_core::runtime_state().cameras.quick_reply_open;
  }
  if (anchor == latter_core::PageId::AREA) {
    if (latter_core::runtime_state().area.active_area_id.empty()) {
      return gui_page == latter_core::PageId::AREA &&
             (area_subpage == AreaSubpage::OVERVIEW || area_subpage == AreaSubpage::PICKER);
    }
    return gui_page == latter_core::PageId::AREA && area_subpage == AreaSubpage::OVERVIEW && !area_picker;
  }
  if (anchor == latter_core::PageId::REMOTE) return gui_page == latter_core::PageId::REMOTE && remote_subpage == 0;
  if (anchor == latter_core::PageId::WEATHER) return gui_page == latter_core::PageId::WEATHER;
  if (anchor == latter_core::PageId::GAMES) return gui_page == latter_core::PageId::GAMES && active_game == 0;
  if (anchor == latter_core::PageId::TOOLS) {
    return gui_page == latter_core::PageId::TOOLS && tools_subpage == latter_core::ToolsSubpage::LAUNCHER;
  }
  if (anchor == latter_core::PageId::CALENDAR) {
    return gui_page == latter_core::PageId::CALENDAR &&
           latter_core::runtime_state().cal.drilldown == latter_core::CalDrilldown::NONE &&
           !latter_core::runtime_state().cal.delete_confirm_open;
  }
  if (anchor == latter_core::PageId::SYSTEM) return gui_page == latter_core::PageId::SYSTEM && system_menu_mode == 0;
  return false;
}

inline bool is_on_anchor_root(const latter_core::DeviceRuntimeState &rt = latter_core::runtime_state(),
                              const latter_core::PersistedDevicePrefs &p = latter_core::persisted_prefs()) {
  if (latter_core::resolve_anchor_dock_app(p) == latter_core::PageId::MUSIC) {
    return rt.nav.gui_page == latter_core::PageId::MUSIC &&
           rt.music.drilldown == latter_core::MusicDrilldown::NONE &&
           rt.music.takeover == latter_core::MusicTakeover::NONE;
  }
  return is_on_anchor_root(rt.nav.gui_page, rt.nav.system_menu_mode, rt.nav.active_game,
                           rt.nav.area_subpage,
                           rt.area.browse_mode == "areas", rt.nav.remote_subpage,
                           rt.nav.tools_subpage, p);
}

// Single go-home predicate shared by gui.yaml and TouchGestureEngine ([HOME PREDICATE]).
inline bool can_go_home(int gui_page, int system_menu_mode, int active_game,
                        int area_subpage, bool area_picker, int remote_subpage, int tools_subpage,
                        int alarm_edit_idx, bool alert_ringing) {
  if (gui_page == latter_core::PageId::HOME || gui_page == latter_core::PageId::CLOCK || alert_ringing) return false;
  if (latter_core::runtime_state().nav.shortcut_confirm.open || latter_core::runtime_state().nav.quick_actions_open) {
    return false;
  }
  if (gui_page == latter_core::PageId::TIME && (latter_core::timer_view_slot() > 0 || alarm_edit_idx > 0)) {
    return false;
  }
  if (gui_page == latter_core::PageId::CAMERAS && latter_core::runtime_state().cameras.quick_reply_open) {
    return false;
  }
  if (gui_page == latter_core::PageId::SYSTEM && system_menu_mode > 0) return false;
  if (gui_page == latter_core::PageId::GAMES && active_game > 0) return false;
  if (gui_page == latter_core::PageId::MUSIC &&
      (latter_core::runtime_state().music.drilldown != latter_core::MusicDrilldown::NONE ||
       latter_core::runtime_state().music.takeover != latter_core::MusicTakeover::NONE)) {
    return false;
  }
  if (gui_page == latter_core::PageId::AREA && (area_subpage > 0 || area_picker)) {
    const bool root_picker = !latter_core::persisted_prefs().home_screen_enabled &&
                             latter_core::resolve_anchor_dock_app() == latter_core::PageId::AREA &&
                             latter_core::runtime_state().area.active_area_id.empty() &&
                             area_subpage == AreaSubpage::PICKER;
    if (!root_picker) return false;
  }
  if (gui_page == latter_core::PageId::REMOTE && remote_subpage > 0) return false;
  if (gui_page == latter_core::PageId::TOOLS && tools_subpage > 0) {
    return false;
  }
  if (gui_page == latter_core::PageId::CALENDAR &&
      (latter_core::runtime_state().cal.drilldown != latter_core::CalDrilldown::NONE ||
       latter_core::runtime_state().cal.delete_confirm_open)) {
    return false;
  }
  if (!latter_core::persisted_prefs().home_screen_enabled &&
      is_on_anchor_root(gui_page, system_menu_mode, active_game,
                        area_subpage, area_picker, remote_subpage, tools_subpage,
                        latter_core::persisted_prefs())) {
    return false;
  }
  return true;
}

inline bool can_go_home(const latter_core::DeviceRuntimeState &rt = latter_core::runtime_state()) {
  if (rt.nav.gui_page == latter_core::PageId::HOME || rt.nav.gui_page == latter_core::PageId::CLOCK ||
      rt.alarms.ringing || rt.timers.ringing || rt.alarms.find_my_active) {
    return false;
  }
  if (rt.nav.shortcut_confirm.open || rt.nav.quick_actions_open) return false;
  if (rt.nav.gui_page == latter_core::PageId::TIME && (rt.timers.view_slot > 0 || rt.alarms.edit_idx > 0)) {
    return false;
  }
  if (rt.nav.gui_page == latter_core::PageId::CAMERAS && rt.cameras.quick_reply_open) {
    return false;
  }
  if (rt.nav.gui_page == latter_core::PageId::SYSTEM && rt.nav.system_menu_mode > 0) return false;
  if (rt.nav.gui_page == latter_core::PageId::GAMES && rt.nav.active_game > 0) return false;
  if (rt.nav.gui_page == latter_core::PageId::MUSIC &&
      (rt.music.drilldown != latter_core::MusicDrilldown::NONE ||
       rt.music.takeover != latter_core::MusicTakeover::NONE)) {
    return false;
  }
  const bool area_picker = (rt.area.browse_mode == "areas");
  if (rt.nav.gui_page == latter_core::PageId::AREA && (rt.nav.area_subpage > 0 || area_picker)) {
    const bool root_picker = !latter_core::persisted_prefs().home_screen_enabled &&
                             latter_core::resolve_anchor_dock_app() == latter_core::PageId::AREA &&
                             rt.area.active_area_id.empty() &&
                             rt.nav.area_subpage == AreaSubpage::PICKER;
    if (!root_picker) return false;
  }
  if (rt.nav.gui_page == latter_core::PageId::REMOTE && rt.nav.remote_subpage > 0) return false;
  if (rt.nav.gui_page == latter_core::PageId::TOOLS && rt.nav.tools_subpage > 0) return false;
  if (rt.nav.gui_page == latter_core::PageId::CALENDAR &&
      (rt.cal.drilldown != latter_core::CalDrilldown::NONE || rt.cal.delete_confirm_open)) {
    return false;
  }
  if (!latter_core::persisted_prefs().home_screen_enabled &&
      is_on_anchor_root(rt, latter_core::persisted_prefs())) {
    return false;
  }
  return true;
}

inline bool music_art_only(int gui_page, bool overlay_always_visible, bool hud_active) {
  const auto &m = latter_core::runtime_state().music;
  if (gui_page != latter_core::PageId::MUSIC ||
      m.drilldown != latter_core::MusicDrilldown::NONE ||
      m.takeover != latter_core::MusicTakeover::NONE ||
      hud_active) {
    return false;
  }
  if (m.view == latter_core::MusicView::NOW_PLAYING) return !overlay_always_visible;
  return m.view == latter_core::MusicView::LYRICS;
}

inline int get_home_dock_start_index(int total_count, int page) {
  const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
  if (total_count <= vis_cap || page <= 0) return 0;
  const int total_pages = (total_count + vis_cap - 1) / vis_cap;
  const int eff_page = (page >= total_pages) ? (total_pages - 1) : page;
  int raw_start = eff_page * vis_cap;
  if (raw_start + vis_cap > total_count) {
    raw_start = total_count - vis_cap;
  }
  return std::max(0, raw_start);
}

inline void get_home_dock_window_xs(int total_count, int page, int out_xs[kMaxDockSlots]) {
  if (total_count <= 0) return;
  const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
  const int start_idx = get_home_dock_start_index(total_count, page);
  const int vis_count = std::min(vis_cap, total_count - start_idx);
  compute_mode_bar_slot_xs(vis_count, SCREEN_W, LATTER_ROUND_DISPLAY != 0, out_xs);
}

inline void get_mode_bar_window_xs(int total_count, int win_start, int cx, int out_xs[kMaxDockSlots],
                                   bool &out_has_left_arrow, bool &out_has_right_arrow) {
  (void)cx;
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
  const int start_idx = latter_core::get_mode_bar_start_index(total_count, win_start, vis_cap);
  const int vis_count = std::min(vis_cap, total_count - start_idx);
  compute_mode_bar_slot_xs(vis_count, SCREEN_W, LATTER_ROUND_DISPLAY != 0, out_xs);
  out_has_left_arrow  = (start_idx > 0);
  out_has_right_arrow = (start_idx + vis_cap < total_count);
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
    if (target_start < 0 || std::abs(new_start - target_start) > 9) {
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
    if (target_active < 0 || std::abs(new_active - target_active) > 9) {
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
  return std::max(0, std::min(ly.dock_y, ly.app_dock_y) - ly.dock_r - dp_px(16));
}

inline bool any_dock_anim_active(int gui_page = -1, int home_subpage = 0, uint32_t now_ms = 0U) {
  if (gui_page >= 0) {
    if (gui_page != 1 || home_subpage != 0) {
      home_dock_anim().win_animating = false;
      home_dock_anim().act_animating = false;
    }
    if (gui_page != 2 && gui_page != 5 && gui_page != 6 && gui_page != 9 && gui_page != 10 && gui_page != 12 && gui_page != 13) {
      mode_bar_anim().win_animating = false;
      mode_bar_anim().act_animating = false;
    }
  }
  const bool dock_horiz_bounce = (now_ms != 0U && gui_page == 1 && home_subpage == 0) &&
                                 edge_bounce().is_active(gui_page, GestureAxisLock::Horizontal, now_ms);
  return home_dock_anim().is_animating() || mode_bar_anim().is_animating() || dock_horiz_bounce;
}

inline bool any_page_horiz_bounce_active(int gui_page, int home_subpage, uint32_t now_ms) {
  return (now_ms != 0U && gui_page >= 0 && !(gui_page == 1 && home_subpage == 0)) &&
         edge_bounce().is_active(gui_page, GestureAxisLock::Horizontal, now_ms);
}

inline bool any_vert_bounce_active(int gui_page, uint32_t now_ms) {
  return (now_ms != 0U && gui_page >= 0) &&
         edge_bounce().is_active(gui_page, GestureAxisLock::Vertical, now_ms);
}

inline int get_dock_slot_stride(const int *win_xs, int vis_count) {
  if (vis_count >= 2) return win_xs[1] - win_xs[0];
  return layout().dock_stride;
}

// ==============================================================================
// Rotary / Touchless Focus State & Ring Primitives
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
                                     UiColor accent, UiColor inner_hi) {
  const float outer_w = std::max(2.0f, dp_f(2.0f));
  const float inner_w = std::max(1.0f, dp_f(1.0f));
  fast_circle_aa(it, cx, cy, static_cast<float>(r) + outer_w + 0.5f, accent, outer_w);
  fast_circle_aa(it, cx, cy, static_cast<float>(r) + inner_w, inner_hi, inner_w);
}

template <typename DisplayT>
inline void render_focus_ring_rect(DisplayT &it, int cx, int cy, int w, int h,
                                   UiColor accent, UiColor inner_hi) {
  const int x0 = cx - w / 2;
  const int y0 = cy - h / 2;
  const int stroke = std::max(2, dp_px(2));
  for (int t = 0; t < stroke; ++t) {
    const int d = 2 + t;
    fast_rect_outline(it, x0 - d, y0 - d, w + 2 * d, h + 2 * d, accent);
  }
  fast_rect_outline(it, x0 - 1, y0 - 1, w + 2, h + 2, inner_hi);
}

}  // namespace latter_ui
