#pragma once

// ==============================================================================
// Latter UI 2px-Aligned Dirty-Band Tracker & Subpage Signature Helpers
// (include/ui_dirty_band.h)
// Pure C++20 UI header — zero ESPHome or HAL includes.
// ==============================================================================

#include "ui_panel_metrics.h"
#include "ui_panel_traits.h"

#include <cstdint>

namespace latter_ui {

struct DirtyBandTracker {
  int pending_y0{0};
  int pending_y1{SCREEN_H - 1};
  bool has_pending_band{false};
  bool force_full{true};

  int active_y0{0};
  int active_y1{SCREEN_H - 1};
  bool active_full{true};
  bool active_idle_skip{false};

  int last_page{-1};
  int last_subpage{-1};
  uint32_t last_overlay_sig{0xFFFFFFFFU};
  int last_hhmm{-2};
  uint32_t last_idle_state_sig{0xFFFFFFFFU};

  // Aligns any vertical span to 2px-aligned full-width rows:
  //   y0 -> even row (y0 & ~1), y1 -> odd row (min(SCREEN_H - 1, y1 | 1))
  static void align_band(int raw_y0, int raw_y1, int screen_h, int &out_y0, int &out_y1);

  void mark_band(int y0, int y1);
  void mark_full();
  void begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active = false);
  bool begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active,
                   int hhmm_key, uint32_t idle_state_sig, int home_clock_band_y1,
                   bool live_sec_active = false);

  constexpr int y0() const { return active_y0; }
  constexpr int y1() const { return active_y1; }
  constexpr int band_y0() const { return active_y0; }
  constexpr int band_y1() const { return active_y1; }
  constexpr bool is_full() const { return active_full; }
  constexpr bool is_idle_skip() const { return active_idle_skip; }
};

DirtyBandTracker &dirty_band();
void mark_dirty_band(int y0, int y1);
void mark_dirty_full();

inline constexpr int music_metadata_band_y0_for(int /*screen_h*/) noexcept { return 0; }
inline constexpr int music_metadata_band_y1_for(int screen_h) noexcept {
  return scale_y_for(136, screen_h);
}
inline constexpr int music_progress_band_y0_for(int screen_h) noexcept {
  return scale_y_for(74, screen_h);
}
inline constexpr int music_progress_band_y1_for(int screen_h) noexcept {
  return scale_y_for(136, screen_h);
}
inline constexpr int music_metadata_band_y0() noexcept { return 0; }
inline constexpr int music_metadata_band_y1() noexcept { return music_metadata_band_y1_for(SCREEN_H); }
inline constexpr int music_progress_band_y0() noexcept { return music_progress_band_y0_for(SCREEN_H); }
inline constexpr int music_progress_band_y1() noexcept { return music_progress_band_y1_for(SCREEN_H); }
void mark_music_progress_dirty_band(int y0 = 0, int y1 = music_metadata_band_y1_for(SCREEN_H));

constexpr int compute_subpage_id(int gui_page, int home_subpage, int home_dock_pg,
                                 int music_mode, int active_game,
                                 int sys_mode, int sys_dash_pg, int sleep_pg, int disp_pg,
                                 int weather_sub, int area_sub, int rem_sub,
                                 int tools_sub, int tools_menu_pg, int cal_detail, int timer_slot,
                                 int hub_tab,
                                 int time_sub = 0, int wc_pg = 0,
                                 int alarm_edit = 0, int home_set_pg = 1,
                                 int hub_mode_pg = 0, int hub_drill = 0,
                                 int pinned_pg = 0, int cal_sub_id = 0,
                                 int cam_sub_id = 0) {
  switch (gui_page) {
    case 1:
      return (home_subpage & 0xFF) | ((home_dock_pg & 0xFF) << 8) | ((home_set_pg & 0x0F) << 16);
    case 2:
      return music_mode;
    case 3:
      return active_game;
    case 4:
      return (sys_mode & 0xFF) | ((sys_dash_pg & 0x0F) << 8) |
             ((sleep_pg & 0x0F) << 12) | ((disp_pg & 0x0F) << 16);
    case 5:
      return weather_sub;
    case 6:
      return area_sub;
    case 7:
      return rem_sub;
    case 8:
      return (tools_sub & 0xFF) | ((tools_menu_pg & 0xFF) << 8) |
             ((cal_detail & 0x0F) << 16) | ((timer_slot & 0x0F) << 20);
    case 9:
      return (hub_tab & 0x0F) | ((hub_mode_pg & 0x0F) << 4) | ((hub_drill & 0x0F) << 8);
    case 10:
      return (time_sub & 0x0F) | ((wc_pg & 0x0F) << 4) |
             ((timer_slot & 0x0F) << 8) | ((alarm_edit & 0x0F) << 12);
    case 11:
      return pinned_pg & 0xFFFF;
    case 12:
      return cal_sub_id;
    case 13:
      return cam_sub_id;
    default:
      return 0;
  }
}

}  // namespace latter_ui
