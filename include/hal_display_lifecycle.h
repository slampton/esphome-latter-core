#pragma once

// ==============================================================================
// Latter Core Display Power Lifecycle, Standby/Poller State, Voice Display
// Restore & Battery Motion Render Budget (include/hal_display_lifecycle.h)
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "hal_manifest.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace latter_ui {

enum class DisplayPowerState : uint8_t {
  Active = 0,
  Screensaver = 1,
  OffStandby = 2,
  Hibernating = 3,
  Ota = 4,
};

static constexpr uint32_t kBedtimeIdleTimeoutMs = 15000U;

constexpr bool screensaver_allowed_for_power(uint8_t screensaver_mode,
                                             bool is_stationary,
                                             bool usb_connected) noexcept {
  if (screensaver_mode == 1U) return true;   // "Always" (docked or on battery)
  if (screensaver_mode == 2U) return false;  // "Never"
  return is_stationary || usb_connected;     // 0 = "Docked Only"
}

constexpr uint32_t resolve_idle_screen_timeout_ms(bool is_stationary, bool usb_connected,
                                                  uint8_t screensaver_mode,
                                                  std::string_view docked_opt,
                                                  std::string_view battery_opt,
                                                  bool bedtime_active = false) noexcept {
  uint32_t normal_ms = 0U;
  if (!(is_stationary && screensaver_mode == 2U)) {
    const std::string_view opt = (is_stationary || usb_connected) ? docked_opt : battery_opt;
    if (opt == "10s") normal_ms = 10000U;
    else if (opt == "15s") normal_ms = 15000U;
    else if (opt == "30s") normal_ms = 30000U;
    else if (opt == "1 min") normal_ms = 60000U;
    else if (opt == "2 min") normal_ms = 120000U;
    else if (opt == "5 min") normal_ms = 300000U;
    else if (opt == "Never") normal_ms = 0U;
    else normal_ms = (is_stationary || usb_connected) ? 60000U : 30000U;
  }
  if (bedtime_active) {
    return (normal_ms > 0U && normal_ms < kBedtimeIdleTimeoutMs) ? normal_ms : kBedtimeIdleTimeoutMs;
  }
  return normal_ms;
}

constexpr uint32_t resolve_battery_hibernate_timeout_ms(std::string_view opt) noexcept {
  if (opt == "5 min") return 300000U;
  if (opt == "10 min") return 600000U;
  if (opt == "15 min") return 900000U;
  if (opt == "30 min") return 1800000U;
  if (opt == "Disabled" || opt == "Never") return 0U;
  return 900000U;
}

struct DisplaySupervisorInputs {
  bool is_stationary{false};
  bool has_battery{false};
  bool usb_connected{true};
  uint8_t screensaver_mode{0U};  // 0 = "Docked Only", 1 = "Always", 2 = "Never"
  bool bedtime_active{false};
  std::string_view docked_timeout_opt{"1 min"};
  std::string_view battery_timeout_opt{"30s"};
  std::string_view hibernate_timeout_opt{"15 min"};
  bool wake_greeting_enabled{true};
  bool panel_brightness_on{true};
  bool is_hibernating{false};
  bool ota_in_progress{false};
  bool voice_or_announce_busy{false};
  bool alert_or_stopwatch_busy{false};
  bool in_hand_hold{false};
  bool active_tool_awake{false};
  bool active_game_running{false};
  bool hibernate_blocked{false};
  bool showcase_active{false};
};

struct DisplaySupervisorActions {
  bool turn_screen_off_now{false};
  bool enter_standby_now{false};
  bool start_hibernate_countdown_now{false};
};

struct DisplayLifecycleController {
  DisplayPowerState state{DisplayPowerState::Active};
  bool screen_off_standby{false};
  bool poller_held{false};
  bool anim_suppressed{false};
  bool hibernate_prepared{false};
  uint32_t last_wake_ms{0U};
  uint32_t last_touch_ms{0U};
  uint32_t idle_anchor_ms{1U};
  uint32_t idle_retry_after_ms{0U};

  void reset() noexcept { *this = DisplayLifecycleController{}; }

  void note_activity(uint32_t now_ms) noexcept {
    idle_anchor_ms = (now_ms != 0U) ? now_ms : 1U;
    idle_retry_after_ms = 0U;
  }

  void disarm_idle() noexcept {
    idle_anchor_ms = 0U;
    idle_retry_after_ms = 0U;
  }

  constexpr bool idle_armed() const noexcept { return idle_anchor_ms != 0U; }

  void on_wake_active(uint32_t now_ms, int &nav_power_state, uint32_t &sleep_start_time,
                      uint32_t &standby_entered_ms, bool wake_greeting_enabled,
                      bool &wake_greeting_pending) noexcept;

  void on_enter_screensaver(uint32_t now_ms, int &nav_power_state, uint32_t &sleep_start_time,
                            uint32_t &standby_entered_ms) noexcept;

  void on_enter_standby_off(uint32_t now_ms, int &nav_power_state, uint32_t &sleep_start_time,
                            uint32_t &standby_entered_ms) noexcept;

  void on_prepare_hibernate(int &nav_power_state, uint32_t &sleep_start_time) noexcept;

  void on_ota_begin(int &nav_power_state, uint32_t &sleep_start_time) noexcept;

  void on_ota_end(uint32_t now_ms) noexcept;

  DisplaySupervisorActions evaluate_1s(uint32_t now_ms, const DisplaySupervisorInputs &in,
                                       int &nav_power_state, uint32_t &sleep_start_time,
                                       uint32_t &standby_entered_ms,
                                       bool &wake_greeting_pending) noexcept;
};

DisplayLifecycleController &display_lifecycle() noexcept;

inline uint32_t &last_screen_wake_ms() { return display_lifecycle().last_wake_ms; }

inline uint32_t &last_user_touch_ms() { return display_lifecycle().last_touch_ms; }

inline bool &screen_is_off_standby() { return display_lifecycle().screen_off_standby; }

inline bool &disp_poller_held() { return display_lifecycle().poller_held; }

inline bool &anim_poller_suppressed() { return display_lifecycle().anim_suppressed; }

inline bool &hibernate_ui_prepared() { return display_lifecycle().hibernate_prepared; }

struct VaDisplayRestore {
  bool armed = false;
  int prior_state = 0;  // 0 active, 1 screensaver, 2 off
  uint32_t session_start_ms = 0;

  void capture(int display_power_state, bool brightness_on, uint32_t now_ms, bool force) {
    if (armed && !force) return;
    armed = true;
    prior_state = brightness_on ? display_power_state : 2;
    session_start_ms = (now_ms == 0) ? 1U : now_ms;
  }
  bool touched_since_start(uint32_t last_touch_ms) const {
    return armed && last_touch_ms != 0 && static_cast<int32_t>(last_touch_ms - session_start_ms) > 0;
  }
};

inline VaDisplayRestore &va_display_restore() {
  static VaDisplayRestore r;
  return r;
}

inline constexpr uint32_t kBatteryMotionWindowMs = 35000U;

inline bool battery_motion_allowed(bool usb_on, uint32_t now_ms, uint32_t last_touch_ms = 0) {
#if LATTER_IS_STATIONARY
  (void)usb_on;
  (void)now_ms;
  (void)last_touch_ms;
  return true;
#else
  if (usb_on) return true;
  const uint32_t last = std::max({last_screen_wake_ms(), last_user_touch_ms(), last_touch_ms});
  return (now_ms - last) < kBatteryMotionWindowMs;
#endif
}

}  // namespace latter_ui
