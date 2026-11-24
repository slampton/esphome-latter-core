#if __has_include("hal_display_lifecycle.h")
#include "hal_display_lifecycle.h"
#else
#include "../../include/hal_display_lifecycle.h"
#endif

namespace latter_core {

namespace {
constexpr uint32_t kStandbyGreetingMinMs = 1800000U;
}  // namespace

DisplayLifecycleController &display_lifecycle() noexcept {
  static DisplayLifecycleController ctrl{};
  return ctrl;
}

void DisplayLifecycleController::on_wake_active(uint32_t now_ms, int &nav_power_state,
                                                uint32_t &sleep_start_time,
                                                uint32_t &standby_entered_ms,
                                                bool wake_greeting_enabled,
                                                bool &wake_greeting_pending) noexcept {
  const uint32_t start_ms = (sleep_start_time != 0U) ? sleep_start_time : standby_entered_ms;
  standby_entered_ms = 0U;
  if (wake_greeting_enabled && start_ms != 0U &&
      static_cast<uint32_t>(now_ms - start_ms) >= kStandbyGreetingMinMs) {
    wake_greeting_pending = true;
  }
  hibernate_prepared = false;
  screen_off_standby = false;
  anim_suppressed = false;
  poller_held = false;
  state = DisplayPowerState::Active;
  last_wake_ms = now_ms;
  sleep_start_time = 0U;
  nav_power_state = 0;
  note_activity(now_ms);
}

void DisplayLifecycleController::on_enter_screensaver(uint32_t now_ms, int &nav_power_state,
                                                      uint32_t &sleep_start_time,
                                                      uint32_t &standby_entered_ms) noexcept {
  disarm_idle();
  screen_off_standby = false;
  anim_suppressed = false;
  state = DisplayPowerState::Screensaver;
  nav_power_state = 1;
  sleep_start_time = 0U;
  if (standby_entered_ms == 0U) {
    standby_entered_ms = (now_ms != 0U) ? now_ms : 1U;
  }
}

void DisplayLifecycleController::on_enter_standby_off(uint32_t now_ms, int &nav_power_state,
                                                      uint32_t &sleep_start_time,
                                                      uint32_t &standby_entered_ms) noexcept {
  disarm_idle();
  screen_off_standby = true;
  anim_suppressed = false;
  state = DisplayPowerState::OffStandby;
  nav_power_state = 2;
  const uint32_t ts = (now_ms != 0U) ? now_ms : 1U;
  sleep_start_time = ts;
  if (standby_entered_ms == 0U) {
    standby_entered_ms = ts;
  }
}

void DisplayLifecycleController::on_prepare_hibernate(int &nav_power_state,
                                                      uint32_t &sleep_start_time) noexcept {
  disarm_idle();
  screen_off_standby = false;
  anim_suppressed = false;
  hibernate_prepared = true;
  state = DisplayPowerState::Hibernating;
  sleep_start_time = 0U;
  nav_power_state = 0;
}

void DisplayLifecycleController::on_ota_begin(int &nav_power_state,
                                              uint32_t &sleep_start_time) noexcept {
  disarm_idle();
  screen_off_standby = false;
  poller_held = true;
  anim_suppressed = false;
  state = DisplayPowerState::Ota;
  sleep_start_time = 0U;
  nav_power_state = 0;
}

void DisplayLifecycleController::on_ota_end(uint32_t now_ms) noexcept {
  poller_held = false;
  anim_suppressed = false;
  state = DisplayPowerState::Active;
  note_activity(now_ms);
}

DisplaySupervisorActions DisplayLifecycleController::evaluate_1s(
    uint32_t now_ms, const DisplaySupervisorInputs &in, int &nav_power_state,
    uint32_t &sleep_start_time, uint32_t &standby_entered_ms,
    bool &wake_greeting_pending) noexcept {
  DisplaySupervisorActions out{};

  if (nav_power_state != 0) {
    if (standby_entered_ms == 0U) {
      standby_entered_ms = (sleep_start_time != 0U) ? sleep_start_time : ((now_ms != 0U) ? now_ms : 1U);
    }
  } else if (standby_entered_ms != 0U) {
    const uint32_t start_ms = (sleep_start_time != 0U) ? sleep_start_time : standby_entered_ms;
    standby_entered_ms = 0U;
    if (in.wake_greeting_enabled && !in.bedtime_active && start_ms != 0U &&
        static_cast<uint32_t>(now_ms - start_ms) >= kStandbyGreetingMinMs) {
      wake_greeting_pending = true;
    }
  }

  if (nav_power_state == 0 && !in.panel_brightness_on && !in.is_hibernating &&
      !in.ota_in_progress && now_ms > 10000U) {
    out.turn_screen_off_now = true;
  }

  const uint32_t idle_timeout_ms = resolve_idle_screen_timeout_ms(
      in.is_stationary, in.usb_connected, in.screensaver_mode,
      in.docked_timeout_opt, in.battery_timeout_opt, in.bedtime_active);

  if (in.ota_in_progress) {
    state = DisplayPowerState::Ota;
    disarm_idle();
  } else if (in.is_hibernating) {
    state = DisplayPowerState::Hibernating;
    disarm_idle();
  } else if (nav_power_state == 1) {
    state = DisplayPowerState::Screensaver;
    disarm_idle();
  } else if (nav_power_state == 2) {
    state = DisplayPowerState::OffStandby;
    disarm_idle();
  } else {
    state = DisplayPowerState::Active;
    if (!out.turn_screen_off_now) {
      if (idle_timeout_ms == 0U) {
        disarm_idle();
      } else if (idle_anchor_ms == 0U || in.voice_or_announce_busy || in.showcase_active) {
        note_activity(now_ms);
      } else if (static_cast<uint32_t>(now_ms - idle_anchor_ms) >= idle_timeout_ms) {
        const bool retry_ready = (idle_retry_after_ms == 0U) ||
                                 (static_cast<int32_t>(now_ms - idle_retry_after_ms) >= 0);
        if (retry_ready) {
          const bool inhibited = in.alert_or_stopwatch_busy || in.in_hand_hold ||
                                 in.active_tool_awake || in.active_game_running;
          if (inhibited) {
            const uint32_t next_try = now_ms + 2000U;
            idle_retry_after_ms = (next_try != 0U) ? next_try : 1U;
          } else {
            disarm_idle();
            out.enter_standby_now = true;
          }
        }
      }
    }
  }

  if (in.has_battery) {
    const uint32_t hib_timeout_ms = resolve_battery_hibernate_timeout_ms(in.hibernate_timeout_opt);
    const bool in_standby = (nav_power_state == 2) || (nav_power_state == 1) || !in.panel_brightness_on;
    if (in_standby && !in.hibernate_blocked && !in.showcase_active) {
      if (sleep_start_time == 0U) {
        sleep_start_time = (now_ms != 0U) ? now_ms : 1U;
      } else if (hib_timeout_ms > 0U &&
                 static_cast<uint32_t>(now_ms - sleep_start_time) >= hib_timeout_ms) {
        sleep_start_time = 0U;
        out.start_hibernate_countdown_now = true;
      }
    } else {
      sleep_start_time = 0U;
    }
  }

  return out;
}

}  // namespace latter_core
