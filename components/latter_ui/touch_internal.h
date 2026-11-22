#pragma once

// ==============================================================================
// Latter Compiled Touch Engine Internal Cross-TU Declarations
// (components/latter_ui/touch_internal.h)
// Pure C++20 internal header — zero ESPHome includes; shared across flat
// components/latter_ui/touch_*.cpp translation units.
// ==============================================================================

#include "ui_touch_engine.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "hal_sleep_pm.h"
#include "latter_actions.h"
#include "latter_prof.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latter_ui {

namespace touch_detail {

inline uint32_t now_or_hal_ms(uint32_t fallback_ms = 0U) noexcept {
  const uint32_t h = hal_millis_now();
  return (h != 0U) ? h : fallback_ms;
}

inline bool is_newer_semver(const std::string &lat_str, const char *cur_cstr) noexcept {
  if (lat_str.empty() || lat_str == "unavailable" || lat_str == "unknown") return false;
  auto parse_ver = [](const char *raw, int &maj, int &min_v, int &pat) noexcept -> bool {
    if (raw == nullptr || *raw == '\0') return false;
    const char *p = (*raw == 'v' || *raw == 'V') ? (raw + 1) : raw;
    char *endp = nullptr;
    maj = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p || *endp != '.') return false;
    p = endp + 1;
    min_v = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p) return false;
    pat = (*endp == '.') ? static_cast<int>(std::strtol(endp + 1, &endp, 10)) : 0;
    return true;
  };
  const char *lp = lat_str.c_str();
  if (*lp == 'v' || *lp == 'V') ++lp;
  if (std::strncmp(lp, "202", 3) == 0) return false;
  int l_maj = 0, l_min = 0, l_pat = 0, c_maj = 0, c_min = 0, c_pat = 0;
  if (!parse_ver(lat_str.c_str(), l_maj, l_min, l_pat) ||
      !parse_ver(cur_cstr != nullptr ? cur_cstr : "", c_maj, c_min, c_pat)) {
    return false;
  }
  if (l_maj != c_maj) return l_maj > c_maj;
  if (l_min != c_min) return l_min > c_min;
  return l_pat > c_pat;
}

inline bool resolve_sys_bri_slider_id(int16_t param, std::string_view auto_bri_mode,
                                      UiNumberId &out_id) noexcept {
  const bool ab_off = (auto_bri_mode == "Disabled" || auto_bri_mode.empty());
  if (param == 5 && ab_off) {
    out_id = UiNumberId::MANUAL_BRIGHTNESS;
    return true;
  }
  if (param == 7 && !ab_off) {
    out_id = UiNumberId::DAY_BRIGHTNESS;
    return true;
  }
  if (param == 8 && !ab_off) {
    out_id = UiNumberId::NIGHT_BRIGHTNESS;
    return true;
  }
  return false;
}

inline int step_brightness_tap_10pct(int cur_pct) noexcept {
  if (cur_pct < 10 || cur_pct >= 100) return 10;
  return std::min(100, ((cur_pct / 10) + 1) * 10);
}

inline UiNumberId brightness_number_id_for_kind(ActiveBrightnessTargetKind kind) noexcept {
  switch (kind) {
    case ActiveBrightnessTargetKind::DAY:     return UiNumberId::DAY_BRIGHTNESS;
    case ActiveBrightnessTargetKind::NIGHT:   return UiNumberId::NIGHT_BRIGHTNESS;
    case ActiveBrightnessTargetKind::BEDTIME: return UiNumberId::BEDTIME_BRIGHTNESS;
    case ActiveBrightnessTargetKind::MANUAL:
    default:                                  return UiNumberId::MANUAL_BRIGHTNESS;
  }
}

inline int32_t resolve_touch_today_ymd(const UiTouchRuntime &rt) noexcept {
  if (rt.time_valid && rt.time_year >= 2020) {
    return make_ymd(rt.time_year, rt.time_month, rt.time_day);
  }
  return 20260101;
}

inline void record_stopwatch_lap(StopwatchRuntimeState &sw, uint32_t cur_ms) noexcept {
  sw.record_lap_elapsed(cur_ms);
}

inline void update_light_inspector_hex(float hue_deg, float sat) {
  const UiColor rgb = hsv_to_rgb(hue_deg / 360.0f, sat, 1.0f);
  char hex_buf[12];
  std::snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
  area_color_hex_param() = hex_buf;
}

// PIN Gate & Security Actions (touch_pin_gate.cpp)
void do_execute_area_action();
void do_execute_alarm_action();
void apply_optimistic_area_entity_dispatch(const std::string &entity_id,
                                           const std::string &action,
                                           DeviceRuntimeState &rs);
void apply_optimistic_shortcut_feedback(const std::string &entity_id,
                                        const std::string &action,
                                        const std::string &label,
                                        DeviceRuntimeState &rs,
                                        uint32_t now_ms);
void activate_shortcut_item(const ShortcutItem &item, ShortcutOrigin origin,
                            DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                            uint32_t now_ms);

// Navigation & Shortcuts (touch_dispatch_nav.cpp)
void launch_dock_app(int chosen_app, DeviceRuntimeState &rs,
                     const UiTouchRuntime &rt, uint32_t now_ms);
void launch_shortcut_route(RouteId route_id, ShortcutOrigin origin,
                           DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                           uint32_t now_ms);
bool return_from_shortcut_origin(DeviceRuntimeState &rs,
                                 const UiTouchRuntime &rt,
                                 uint32_t now_ms);

// Media App v3 Helpers (touch_dispatch_media.cpp)
void reset_music_loading_slots(DeviceRuntimeState &rs, const char *msg);
std::string resolve_music_speaker_entity(const DeviceRuntimeState &rs);
void resolve_music_track_strings(const DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                 std::string &out_title, std::string &out_artist,
                                 std::string &out_album, bool &out_speech);
int fetch_music_browse_for_current_view(const DeviceRuntimeState &rs, uint32_t now_ms);
void reset_music_to_now_playing(DeviceRuntimeState &rs, uint32_t now_ms, bool show_hud = true);
bool open_music_row_action_sheet(int slot_idx, DeviceRuntimeState &rs, uint32_t now_ms);
bool handle_music_back_step(DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms);

// Home, Area & Home Hub Helpers (touch_dispatch_home_area.cpp)
void do_fetch_area_browse();
void do_fetch_area_list_browse();
void do_select_active_area_latter();
void stop_broadcast_dictation_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                        uint32_t now_ms, const char *msg = "");
void enter_home_hub_tab(int target_tab, bool expand_cam, DeviceRuntimeState &rs,
                        const UiTouchRuntime &rt, uint32_t now_ms);
const HomeActivityItem *resolve_home_activities(uint32_t now_ms, const DeviceRuntimeState &rs,
                                                const PersistedDevicePrefs &prefs,
                                                const UiTouchRuntime &rt, int &out_count);

// Time & Calendar Helpers (touch_dispatch_time_cal.cpp)
void ensure_calendar_window_for_ymd(int32_t anchor_ymd, const PersistedDevicePrefs &prefs,
                                    bool force = false);
void stop_calendar_dictation_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                       uint32_t now_ms, const char *msg = "");

// System, Remote & Tools Helpers (touch_dispatch_system_tools.cpp)
int remote_key_for_widget(uint16_t wid) noexcept;
void do_remote_send_key(int key_idx, int target_slot, bool long_press = false,
                        uint32_t now_override_ms = 0U);
void cycle_music_pref_slot(const UiTouchRuntime &rt, int slot);

// Cameras & Games Helpers (touch_dispatch_cameras_games.cpp)
void leave_home_hub_cameras_if_active(DeviceRuntimeState &rs, const UiTouchRuntime &rt,
                                      uint32_t now_ms);
void enter_cameras_app(int target_tab, std::string_view focus_entity, bool start_listen,
                       DeviceRuntimeState &rs, const UiTouchRuntime &rt, uint32_t now_ms);

}  // namespace touch_detail

struct TouchEngineAccess {
  static void scrub_pin_and_pending(TouchGestureEngine &eng, DeviceRuntimeState &rs,
                                    bool clear_feedback_and_shortcut = true);
  static bool allow_active_touch_down(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms);
  static bool handle_down_takeovers(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms);
  static void handle_quick_actions_release(TouchGestureEngine &eng, uint32_t now_ms,
                                           const HitRegion *start_slider);
  static void handle_down_alert_overlay(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms);
  static bool handle_down_cameras_games(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms,
                                        uint16_t wid);
  static bool handle_down_system_tools(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms,
                                       const HitRegion *hit, uint16_t wid, uint32_t tkey);
  static bool handle_down_time_cal(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms,
                                   const HitRegion *hit, uint16_t wid, uint32_t tkey);
};

}  // namespace latter_ui
