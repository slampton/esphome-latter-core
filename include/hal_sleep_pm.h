#pragma once

// ==============================================================================
// Latter Core Light Sleep, Quiet-Loop Pacing, Stall Guard, Wake Attribution
// & Flight Recorder Summaries (include/hal_sleep_pm.h)
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#endif

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Main-loop Stall Guard (compiled in components/latter_core/pm_telemetry_stall.cpp)
// ------------------------------------------------------------------------------
constexpr uint32_t kStallAbortMs = 30000U;
constexpr uint32_t kStallReportMs = 2000U;
constexpr uint32_t kStallRecordMagic = 0x5354414CU;  // "STAL"
constexpr uint32_t kStallLiveMagic = 0x53544C56U;    // "STLV"
constexpr uint32_t kTwdtRecordMagic = 0x54574454U;   // "TWDT"

struct StallRecord {
  uint32_t magic;
  uint32_t blocked_ms;
  uint32_t uptime_s;
  char source[40];
  char cpu0[16];
  char cpu1[16];
  char loop_state;
};

struct StallGuardState {
  StallGuardState();

  std::atomic<bool> paused{false};
  std::atomic<uint32_t> resumed_ms{0};
  bool started{false};
  StallRecord previous_boot{};
  StallRecord previous_live{};
  StallRecord previous_twdt{};
  StallRecord longest{};
#ifdef USE_ESP32
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif
};

#ifdef USE_ESP32
StallGuardState &stall_guard();
bool previous_boot_stalled();
void stall_guard_pause(bool paused);
void stall_guard_notify_chip_awake(bool stay_awake) noexcept;
void start_stall_guard();
void log_stall_summary();
#else
inline StallGuardState::StallGuardState() = default;
inline StallGuardState &stall_guard() {
  static StallGuardState s;
  return s;
}
inline bool previous_boot_stalled() { return false; }
inline void stall_guard_pause(bool p) { stall_guard().paused.store(p, std::memory_order_relaxed); }
inline void stall_guard_notify_chip_awake(bool /*stay_awake*/) noexcept {}
inline void start_stall_guard() {}
inline void log_stall_summary() {}
#endif

// ------------------------------------------------------------------------------
// 2. Automatic Light Sleep (S3 Battery) & Quiet-Loop Pacing (P4 Battery)
// ------------------------------------------------------------------------------
enum PmWakeSlot : uint8_t {
  PM_WAKE_PWR_KEY = 0,
  PM_WAKE_ACTION_BTN = 1,
  PM_WAKE_TOUCH = 2,
  PM_WAKE_IMU = 3,
  PM_WAKE_SLOTS = 4,
};

inline constexpr uint32_t pm_wake_bit(PmWakeSlot slot) { return 1UL << static_cast<uint32_t>(slot); }

// Main loop interval while the awake lock is released: 1,000 ms quiet, 250 ms with touch wake armed.
constexpr uint32_t kPmQuietLoopIntervalMs = 1000U;
constexpr uint32_t kPmQuietTouchLoopIntervalMs = 250U;

// Post-GPIO-wake fast-loop grace window: holds the awake lock and active loop interval (~16 ms)
// for 600 ms after any hardware wake-pin exit from light sleep so 20 ms button debounce filters,
// capacitive touch I2C scans (including 40..450 ms wearable double-tap), and QMI8658 WoM I2C
// status reads complete deterministically before returning to quiet sleep if no wake occurred.
constexpr uint32_t kPmWakeGraceMs = 600U;

inline constexpr uint32_t pm_quiet_loop_interval(uint32_t arm_mask) {
  return (arm_mask & pm_wake_bit(PM_WAKE_TOUCH)) != 0 ? kPmQuietTouchLoopIntervalMs : kPmQuietLoopIntervalMs;
}

struct PmWakeGraceWindow {
  uint32_t until_ms = 0U;

  constexpr void arm(uint32_t now_ms, uint32_t duration_ms = kPmWakeGraceMs) {
    const uint32_t target = now_ms + duration_ms;
    until_ms = (target == 0U) ? 1U : target;
  }

  constexpr void clear() { until_ms = 0U; }

  constexpr bool active(uint32_t now_ms) const {
    return until_ms != 0U && static_cast<int32_t>(until_ms - now_ms) > 0;
  }

  constexpr uint32_t remaining_ms(uint32_t now_ms) const {
    if (!active(now_ms)) return 0U;
    return until_ms - now_ms;
  }
};

struct PmCrumbSleep {
  uint32_t t_us;
  uint32_t req_us;
  uint32_t slept_us;
  uint16_t cause;
  uint8_t arm;
  uint8_t levels;
};

constexpr uint32_t kPmCrumbRing = 5U;

struct PmCrumb {
  uint32_t magic;
  uint32_t phase;  // 1 entered, 2 exited
  uint32_t seq;
  uint32_t imu_armed;
  uint32_t imu_events;
  uint32_t ring_head;
  uint32_t ring_next;
  uint32_t tick_us[2];
  PmCrumbSleep ring[kPmCrumbRing];
};

constexpr uint32_t kPmCrumbMagic = 0x4C534C50UL;  // "LSLP"

struct PmSleepStats {
  uint32_t sleeps;
  uint64_t slept_us;
  uint32_t awake_ms;
  uint32_t timer_wakes;
  uint32_t gpio_wakes;
  uint32_t wifi_wakes;
  uint32_t other_wakes;
  uint32_t pin_wakes[PM_WAKE_SLOTS];
  uint32_t gpio_unattributed;
};

struct PmProfileSnapshot {
  bool valid{false};
  bool awake{true};
  bool dump_pending{false};
  uint32_t slept_us{0};
  uint32_t sleeps{0};
  uint32_t gpio_wakes{0};
  uint32_t synth{0};
  uint32_t arm_mask{0};
  uint32_t attempts{0};
  uint32_t timer_bound{0};
  uint32_t rtos_bound{0};
  uint32_t req[5]{};
};

struct FlashWriteStats {
  std::atomic<uint32_t> nvs_writes{0};
  std::atomic<uint32_t> erases{0};
  std::atomic<uint32_t> nvs_max_us{0};
};

#ifdef USE_ESP32
extern FlashWriteStats g_flash_write_stats;
PmCrumb &pm_previous_crumb();
void pm_init();
void pm_apply(bool stay_awake, uint32_t wake_mask);
void pm_poll_loop(uint32_t now_ms);
void pm_wake_grace_arm(uint32_t now_ms);
void pm_note_imu(bool armed, uint32_t events);
const char *pm_crash_where();
bool pm_sleep_stats(PmSleepStats &out, uint32_t now_ms);
bool pm_take_profile_snapshot(PmProfileSnapshot &out, bool clear_dump_pending) noexcept;
void pm_log_profile();
void pm_log_wake_profile();
#else
inline FlashWriteStats g_flash_write_stats{};
inline PmCrumb &pm_previous_crumb() {
  static PmCrumb c{};
  return c;
}
inline void pm_init() {}
inline void pm_apply(bool /*stay_awake*/, uint32_t /*wake_mask*/) {}
inline void pm_poll_loop(uint32_t /*now_ms*/) {}
inline void pm_wake_grace_arm(uint32_t /*now_ms*/) {}
inline void pm_note_imu(bool /*armed*/, uint32_t /*events*/) {}
inline const char *pm_crash_where() { return ""; }
inline bool pm_sleep_stats(PmSleepStats & /*out*/, uint32_t /*now_ms*/) { return false; }
inline bool pm_take_profile_snapshot(PmProfileSnapshot & /*out*/, bool /*clear_dump_pending*/) noexcept {
  return false;
}
inline void pm_log_profile() {}
inline void pm_log_wake_profile() {}
#endif

// ------------------------------------------------------------------------------
// 3. Armed Alarm Wake Horizon
// ------------------------------------------------------------------------------
template <typename TimeT>
inline int32_t seconds_until_next_armed_alarm(const TimeT &now, uint32_t snooze_until,
                                              int a1_h, int a1_m, bool a1_en, int a1_rec,
                                              int a2_h, int a2_m, bool a2_en, int a2_rec,
                                              int a3_h, int a3_m, bool a3_en, int a3_rec) {
  if (!now.is_valid()) return -1;
  int32_t best_sec = 43201;  // > 12h sentinel
  if (snooze_until > static_cast<uint32_t>(now.timestamp)) {
    int32_t s_rem = static_cast<int32_t>(snooze_until - static_cast<uint32_t>(now.timestamp));
    if (s_rem > 0 && s_rem <= 43200) best_sec = s_rem;
  }
  const int32_t now_tod = now.hour * 3600 + now.minute * 60 + now.second;
  auto eval_alarm = [&](int a_hr, int a_min, bool a_en, int a_rec) {
    if (!a_en) return;
    const int32_t alarm_tod = a_hr * 3600 + a_min * 60;
    int32_t delta_sec = 0;
    int target_dow = now.day_of_week;  // 1=SUN .. 7=SAT
    if (alarm_tod > now_tod) {
      delta_sec = alarm_tod - now_tod;
    } else {
      delta_sec = (86400 - now_tod) + alarm_tod;
      target_dow = (now.day_of_week % 7) + 1;
    }
    if (delta_sec <= 0 || delta_sec > 43200) return;
    if (a_rec == 1 && (target_dow < 2 || target_dow > 6)) return;
    if (a_rec == 2 && (target_dow != 1 && target_dow != 7)) return;
    if (delta_sec < best_sec) best_sec = delta_sec;
  };
  eval_alarm(a1_h, a1_m, a1_en, a1_rec);
  eval_alarm(a2_h, a2_m, a2_en, a2_rec);
  eval_alarm(a3_h, a3_m, a3_en, a3_rec);
  return (best_sec <= 43200) ? best_sec : -1;
}

// ------------------------------------------------------------------------------
// 4. Wake Attribution, Raise-to-Wake & Screen-Off Double-Tap
// ------------------------------------------------------------------------------
enum class WakeReason : uint8_t { TOUCH = 0, RAISE, CROWN, VOICE, NOTIFY, ALERT, MEDIA, OTHER, COUNT };

inline const char *wake_reason_name(WakeReason r) {
  switch (r) {
    case WakeReason::TOUCH:  return "touch";
    case WakeReason::RAISE:  return "raise";
    case WakeReason::CROWN:  return "crown";
    case WakeReason::VOICE:  return "voice";
    case WakeReason::NOTIFY: return "notify";
    case WakeReason::ALERT:  return "alert";
    case WakeReason::MEDIA:  return "media";
    default:                 return "other";
  }
}

struct WakeStats {
  uint32_t counts[static_cast<int>(WakeReason::COUNT)] = {};
  WakeReason pending = WakeReason::OTHER;
};

inline WakeStats &wake_stats() {
  static WakeStats st;
  return st;
}

inline void note_wake_reason(WakeReason r) { wake_stats().pending = r; }

inline void record_screen_wake() {
  auto &ws = wake_stats();
  const WakeReason r = ws.pending;
  ws.pending = WakeReason::OTHER;
  ws.counts[static_cast<int>(r)]++;
  if (!profiler_enabled()) return;
  LATTER_PROF_LOGI("[PROF:WAKE] reason=%s total_touch=%lu raise=%lu crown=%lu voice=%lu notify=%lu alert=%lu media=%lu other=%lu",
                   wake_reason_name(r),
                   (unsigned long)ws.counts[0], (unsigned long)ws.counts[1], (unsigned long)ws.counts[2],
                   (unsigned long)ws.counts[3], (unsigned long)ws.counts[4], (unsigned long)ws.counts[5],
                   (unsigned long)ws.counts[6], (unsigned long)ws.counts[7]);
}

inline float get_pickup_motion_threshold(bool screen_is_active) {
#if LATTER_IS_WEARABLE
  return screen_is_active ? 0.22f : 0.38f;
#else
  return screen_is_active ? 0.068f : 0.15f;
#endif
}

inline bool should_in_hand_block_sleep(bool on_battery_and_picked_up) {
#if LATTER_IS_WEARABLE
  (void)on_battery_and_picked_up;
  return false;
#else
  return on_battery_and_picked_up;
#endif
}

struct RaiseToWakeDetector {
  bool armed = false;
  uint32_t arm_ms = 0;
  uint32_t motion_ms = 0;
  uint32_t face_up_since_ms = 0;

  inline void arm_from_wom(uint32_t now_ms) {
    armed = true;
    arm_ms = now_ms;
    motion_ms = (now_ms == 0) ? 1U : now_ms;
    face_up_since_ms = 0;
  }

  void reset() {
    armed = false;
    arm_ms = 0;
    motion_ms = 0;
    face_up_since_ms = 0;
  }

  bool update(float dyn, float thresh, float ax, float ay, float az, uint32_t now_ms) {
    if (std::isnan(dyn) || std::isnan(ax) || std::isnan(ay) || std::isnan(az)) return false;
    if (dyn > thresh) {
      armed = true;
      arm_ms = now_ms;
      motion_ms = (now_ms == 0) ? 1U : now_ms;
    }
    if (!armed && motion_ms != 0) {
      armed = true;
      arm_ms = motion_ms;
    }
    if (!armed || (now_ms - arm_ms) > 1500U) {
      reset();
      return false;
    }
    const float horiz = std::sqrt(ax * ax + ay * ay);
    const float vert = std::fabs(az);
    const bool face_up = (vert >= 0.5f) && (vert >= horiz * 0.7f);
    if (!face_up) {
      face_up_since_ms = 0;
      return false;
    }
    if (face_up_since_ms == 0) face_up_since_ms = (now_ms == 0) ? 1U : now_ms;
    if ((now_ms - face_up_since_ms) >= 200U) {
      reset();
      return true;
    }
    return false;
  }
};

inline RaiseToWakeDetector &raise_to_wake() {
  static RaiseToWakeDetector det;
  return det;
}

inline RaiseToWakeDetector &raise_wake_detector() {
  return raise_to_wake();
}

struct ScreenOffTapState {
  uint32_t first_tap_ms = 0U;
  void reset() noexcept { first_tap_ms = 0U; }
};

inline ScreenOffTapState &screen_off_tap_state() noexcept {
  static ScreenOffTapState s{};
  return s;
}

inline bool tap_wake_needs_double_tap(std::string_view tw_mode, bool usb_on) {
  return (tw_mode == "Double Tap") && !usb_on;
}

inline bool accept_screen_off_tap(std::string_view tw_mode, bool usb_on, uint32_t now_ms) {
  auto &st = screen_off_tap_state();
  if (tw_mode == "Disabled") {
    st.first_tap_ms = 0U;
    return false;
  }
  if (tw_mode == "Docked Only") {
    st.first_tap_ms = 0U;
    return usb_on;
  }
  if (!tap_wake_needs_double_tap(tw_mode, usb_on)) {
    st.first_tap_ms = 0U;
    return true;
  }
  if (st.first_tap_ms == 0U || (now_ms - st.first_tap_ms) > 450U) {
    st.first_tap_ms = (now_ms == 0U) ? 1U : now_ms;
    return false;
  }
  if ((now_ms - st.first_tap_ms) < 35U) return false;
  st.first_tap_ms = 0U;
  return true;
}

// ------------------------------------------------------------------------------
// 5. Power & Boot Flight Recorder Summaries (compiled in pm_telemetry_stall.cpp)
// ------------------------------------------------------------------------------
void log_power_and_mem_profile(const char *trigger, int pwr_state, bool usb, bool chg,
                               float batt_pct, float batt_v, bool spk_pa, bool mww_run,
                               bool va_act, bool mp_play, bool ble_act, int game_id,
                               uint32_t sleep_start_ms);
void log_boot_summary();

}  // namespace latter_ui
