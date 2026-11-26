#pragma once

// ==============================================================================
// Latter Touch Dispatch Internal Cross-TU Declarations
// (components/latter_ui/touch_dispatch_internal.h)
// Component-local internal header; NOT part of staged include/ public API.
// ==============================================================================

#include "ui_touch_engine.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace latter_ui::touch_detail {

// System & Tools helpers (touch_dispatch_system_tools.cpp)
int step_brightness_tap_10pct(int cur_pct) noexcept;

// Time & Calendar helpers (touch_dispatch_time_cal.cpp)
void record_stopwatch_lap(latter_core::StopwatchRuntimeState &sw, uint32_t cur_ms) noexcept;
void ensure_calendar_window_for_ymd(int32_t anchor_ymd,
                                    const latter_core::PersistedDevicePrefs &prefs,
                                    bool force = false);
void stop_calendar_dictation_if_active(latter_core::DeviceRuntimeState &rs,
                                       const UiTouchRuntime &rt,
                                       uint32_t now_ms,
                                       const char *msg = "");

// Home & Area helpers (touch_dispatch_home_area.cpp)
void do_select_active_area_latter();
void stop_broadcast_dictation_if_active(latter_core::DeviceRuntimeState &rs,
                                        const UiTouchRuntime &rt,
                                        uint32_t now_ms,
                                        const char *msg = "");

// Music helpers (touch_dispatch_music.cpp)
void reset_music_loading_slots(latter_core::DeviceRuntimeState &rs, const char *msg);
void resolve_music_track_strings(const latter_core::DeviceRuntimeState &rs,
                                 const UiTouchRuntime &rt,
                                 std::string &out_title,
                                 std::string &out_artist,
                                 std::string &out_album,
                                 bool &out_speech);
void reset_music_to_now_playing(latter_core::DeviceRuntimeState &rs,
                                uint32_t now_ms,
                                bool show_hud = true);
void cycle_music_pref_slot(const UiTouchRuntime &rt, int slot);

// Cameras & Games helpers (touch_dispatch_cameras_games.cpp)
void leave_home_hub_cameras_if_active(latter_core::DeviceRuntimeState &rs,
                                      const UiTouchRuntime &rt,
                                      uint32_t now_ms);
void enter_cameras_app(int target_tab,
                       std::string_view focus_entity,
                       bool start_listen,
                       latter_core::DeviceRuntimeState &rs,
                       const UiTouchRuntime &rt,
                       uint32_t now_ms);

}  // namespace latter_ui::touch_detail
