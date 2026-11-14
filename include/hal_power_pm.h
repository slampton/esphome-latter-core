#pragma once

// ==============================================================================
// Latter Core Power Management, Light Sleep, DFS, Stall Guard, Wake Attribution,
// Power Profiles & Radio Policy (hal_power_pm.h)
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "hal_battery.h"
#include "hal_imu_qmi8658.h"
#include "hal_manifest.h"
#include "latter_prof.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef USE_ESP32
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#endif
#if defined(USE_WIFI) && __has_include("esphome/components/wifi/wifi_component.h")
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace latter_ui {

// ------------------------------------------------------------------------------
// 1. Main-loop Stall Guard (compiled implementation in components/latter_core/power_pm.cpp)
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
inline void start_stall_guard() {}
inline void log_stall_summary() {}
#endif

// ------------------------------------------------------------------------------
// 2. Automatic Light Sleep (S3 Battery) & 40 MHz DFS (P4 Battery)
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
void pm_note_imu(bool armed, uint32_t events);
const char *pm_crash_where();
bool pm_sleep_stats(PmSleepStats &out, uint32_t now_ms);
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
inline void pm_note_imu(bool /*armed*/, uint32_t /*events*/) {}
inline const char *pm_crash_where() { return ""; }
inline bool pm_sleep_stats(PmSleepStats & /*out*/, uint32_t /*now_ms*/) { return false; }
inline void pm_log_profile() {}
inline void pm_log_wake_profile() {}
#endif

// ------------------------------------------------------------------------------
// 3. Shared Display Standby / Poller Flags & Voice Display Restore
// ------------------------------------------------------------------------------
inline uint32_t &last_screen_wake_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t &last_user_touch_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline bool &screen_is_off_standby() {
  static bool off = false;
  return off;
}

inline bool &disp_poller_held() {
  static bool held = false;
  return held;
}

inline bool &anim_poller_suppressed() {
  static bool suppressed = false;
  return suppressed;
}

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

// ------------------------------------------------------------------------------
// 4. Power Profiles & Capability Clamping
// ------------------------------------------------------------------------------
enum class PowerProfile : uint8_t {
  PORTABLE_CARRY = 0,
  HANDHELD_ROAMING = 1,
  WEARABLE_WATCH = 2,
  TABLETOP_DOCK = 3,
  ALWAYS_ON_STATIONARY = 4,
  CUSTOM = 5,
};

inline PowerProfile parse_power_profile(const std::string &opt) {
  if (opt == "Portable Carry") return PowerProfile::PORTABLE_CARRY;
  if (opt == "Handheld Roaming") return PowerProfile::HANDHELD_ROAMING;
  if (opt == "Wearable Watch") return PowerProfile::WEARABLE_WATCH;
  if (opt == "Tabletop Dock") return PowerProfile::TABLETOP_DOCK;
  if (opt == "Always-On Wall / Desk") return PowerProfile::ALWAYS_ON_STATIONARY;
  return PowerProfile::CUSTOM;
}

inline const char *power_profile_short_label(const std::string &opt) {
  switch (parse_power_profile(opt)) {
    case PowerProfile::PORTABLE_CARRY:       return "Portable";
    case PowerProfile::HANDHELD_ROAMING:     return "Roaming";
    case PowerProfile::WEARABLE_WATCH:       return "Watch";
    case PowerProfile::TABLETOP_DOCK:        return "Tabletop";
    case PowerProfile::ALWAYS_ON_STATIONARY: return "Wall/Desk";
    case PowerProfile::CUSTOM:               return "Custom";
  }
  return opt.c_str();
}

struct PowerProfileSpec {
  PowerProfile profile;
  const char *name;
  const char *va_wake_word_mode;
  const char *tap_wake_mode;
  const char *pickup_wake_mode;
  const char *display_off_timeout;
  const char *screensaver_docked_timeout;
  const char *hibernate_timeout_mode;
  bool docked_screensaver_enabled;
};

inline constexpr PowerProfileSpec kPowerProfiles[] = {
    {PowerProfile::PORTABLE_CARRY,       "Portable Carry",        "Docked Only", "Docked Only", "Docked Only", "30s",   "1 min", "15 min",   true},
    {PowerProfile::HANDHELD_ROAMING,     "Handheld Roaming",      "Docked Only", "Always On",   "Always On",   "30s",   "1 min", "30 min",   true},
    {PowerProfile::WEARABLE_WATCH,       "Wearable Watch",        "Docked Only", "Always On",   "Always On",   "15s",   "1 min", "15 min",   true},
    {PowerProfile::TABLETOP_DOCK,        "Tabletop Dock",         "Docked Only", "Docked Only", "Disabled",    "30s",   "5 min", "15 min",   true},
    {PowerProfile::ALWAYS_ON_STATIONARY, "Always-On Wall / Desk", "Always On",   "Always On",   "Disabled",    "Never", "5 min", "Disabled", true},
};

inline const PowerProfileSpec *get_power_profile_spec(PowerProfile p) {
  for (const auto &spec : kPowerProfiles) {
    if (spec.profile == p) return &spec;
  }
  return nullptr;
}

inline const PowerProfileSpec *get_power_profile_spec(const std::string &opt) {
  return get_power_profile_spec(parse_power_profile(opt));
}

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
// 5. Wake Attribution, Raise-to-Wake, Double-Tap & Battery Render Budget
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

inline bool tap_wake_needs_double_tap(bool usb_on) {
#if LATTER_IS_WEARABLE
  return !usb_on;
#else
  (void)usb_on;
  return false;
#endif
}

inline bool accept_screen_off_tap(bool usb_on, uint32_t now_ms) {
  if (!tap_wake_needs_double_tap(usb_on)) return true;
  static uint32_t first_tap_ms = 0;
  if (first_tap_ms == 0 || (now_ms - first_tap_ms) > 450U) {
    first_tap_ms = (now_ms == 0) ? 1U : now_ms;
    return false;
  }
  if ((now_ms - first_tap_ms) < 40U) return false;
  first_tap_ms = 0;
  return true;
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

// ------------------------------------------------------------------------------
// 6. Wi-Fi Modem Power Save & Smart Off-Site Radio Policy (Single Policy Owner)
// ------------------------------------------------------------------------------
struct WifiPowerState {
  bool hp_held{false};
  bool base_valid{false};
  int base{-1};
  bool pulse{false};
  uint32_t offsite_since_ms{0};
  uint32_t scan_window_start_ms{0};
  uint32_t pause_until_ms{0};
  uint8_t backoff_stage{0};  // 0 -> 15 s, 1 -> 60 s, 2 -> 300 s
};

inline WifiPowerState &wifi_power_state() {
  static WifiPowerState s;
  return s;
}

inline int wifi_ps_mode_now() {
#ifdef USE_ESP32
  wifi_ps_type_t t;
  if (esp_wifi_get_ps(&t) == ESP_OK) return static_cast<int>(t);
#endif
  return -1;
}

#ifdef USE_WIFI
template <typename WifiT>
inline void apply_wifi_power_policy(WifiT *wifi, bool high_tput_needed, int display_power_state) {
#if !LATTER_IS_STATIONARY
  if (wifi == nullptr) return;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  (void)display_power_state;
  const auto base = esphome::wifi::WIFI_POWER_SAVE_LIGHT;
#elif defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
  (void)display_power_state;
  const auto base = esphome::wifi::WIFI_POWER_SAVE_LIGHT;
#else
  const auto base = (display_power_state == 2) ? esphome::wifi::WIFI_POWER_SAVE_HIGH
                                               : esphome::wifi::WIFI_POWER_SAVE_LIGHT;
#endif
  auto &st = wifi_power_state();
  bool want_hp = high_tput_needed;
  if (st.pulse) {
    st.pulse = false;
  }
  if (!st.base_valid || st.base != static_cast<int>(base)) {
    wifi->set_power_save_mode(base);
    st.base = static_cast<int>(base);
    st.base_valid = true;
    st.pulse = true;
    want_hp = true;
  }
  if (want_hp && !st.hp_held) {
    st.hp_held = wifi->request_high_performance();
  } else if (!want_hp && st.hp_held) {
    wifi->release_high_performance();
    st.hp_held = false;
  }
#else
  (void)wifi;
  (void)high_tput_needed;
  (void)display_power_state;
#endif
}
#endif

inline void wifi_power_ota_begin() {
#ifdef USE_ESP32
  esp_wifi_set_ps(WIFI_PS_NONE);
#endif
}

inline void wifi_power_ota_end() { wifi_power_state().base_valid = false; }

inline uint32_t &radio_boost_until_ms() {
  static uint32_t until = 0;
  return until;
}

inline void request_radio_boost(uint32_t now_ms, uint32_t duration_ms) {
  radio_boost_until_ms() = (now_ms + duration_ms) | 1U;
}

inline bool radio_boost_active(uint32_t now_ms) {
  const uint32_t until = radio_boost_until_ms();
  return until != 0 && static_cast<int32_t>(until - now_ms) > 0;
}

inline int beacon_mode_index(const std::string &opt) {
  if (opt == "Always") return 2;
  if (opt == "Handheld + Screen Off") return 1;
  return 0;  // "Handheld Only"
}

inline constexpr uint32_t kBeaconHaSettleMs = 2000U;
inline constexpr uint32_t kBeaconWifiFallbackMs = 12000U;

// Opens once the Home Assistant API stream has had 2 s to finish its post-connect replay, or after 12 s on Wi-Fi
// when HA has not connected yet (instead of holding BLE off for a blind 20 s after boot or hibernate wake).
inline constexpr bool beacon_boot_gate_open(bool wifi_connected, bool ha_connected, uint32_t last_ha_connect_ms,
                                            uint32_t now_ms) {
  if (!wifi_connected) return false;
  if (ha_connected && last_ha_connect_ms > 0U && (now_ms - last_ha_connect_ms) >= kBeaconHaSettleMs) return true;
  return now_ms > kBeaconWifiFallbackMs;
}

template <typename BleCtrlT, typename WifiT>
inline void evaluate_radio_policy(BleCtrlT *ble_ctrl, WifiT *wifi, bool wifi_connected, bool ble_beacon_switch_on,
                                  int beacon_mode, bool usb_powered,
                                  bool is_hibernating, bool ota_in_progress, int display_power_state,
                                  bool high_tput_needed, uint32_t now_ms,
                                  bool ha_connected = false, uint32_t last_ha_connect_ms = 0U,
                                  bool va_radio_priority = false) {
  auto ble_is_up = [&]() -> bool {
    if (ble_ctrl == nullptr) return false;
    if (ble_ctrl->is_active()) return true;
    if constexpr (requires { ble_ctrl->is_transport_up(); }) {
      if (ble_ctrl->is_transport_up()) return true;
    }
    if constexpr (requires { ble_ctrl->is_paused(); }) {
      if (ble_ctrl->is_paused()) return true;
    }
    return false;
  };
  if (is_hibernating || ota_in_progress) {
    if (ble_is_up()) {
      ESP_LOGD("ble", "Disabling BLE radio (%s)", is_hibernating ? "hibernating" : "OTA in progress");
      ble_ctrl->disable();
    }
    return;
  }

  if (!ble_beacon_switch_on) {
    if (ble_is_up()) {
      ESP_LOGD("ble", "Disabling BLE radio (beacon switch off)");
      ble_ctrl->disable();
    }
  } else {
#if LATTER_HAS_BATTERY
    const bool beacon_context_ok = (beacon_mode == 2) ||
                                   (!usb_powered && (beacon_mode == 1 || display_power_state != 2));
#else
    (void)beacon_mode;
    (void)usb_powered;
    const bool beacon_context_ok = true;
#endif
    const bool beacon_paused = !beacon_context_ok || !wifi_connected;
    const bool beacon_wanted =
        !beacon_paused && beacon_boot_gate_open(wifi_connected, ha_connected, last_ha_connect_ms, now_ms);
    if (ble_ctrl != nullptr) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      const bool radio_ready = wifi_connected && (now_ms > 8000U);
#else
      const bool radio_ready = wifi_connected;
#endif
      const bool want_ble = beacon_wanted && radio_ready && !va_radio_priority;
      if (want_ble && !ble_ctrl->is_active() && !ble_ctrl->is_failed()) {
        ESP_LOGD("ble", "Enabling BLE radio (Area Presence Beacon)");
        ble_ctrl->enable();
      } else if (!want_ble && ble_ctrl->is_active()) {
        ESP_LOGD("ble", "Pausing BLE radio (beacon %s)",
                 va_radio_priority ? "yielded for voice session"
                                   : (!wifi_connected ? "paused off-site"
                                                      : (!beacon_paused ? "boot gate"
                                                                        : (usb_powered ? "paused while docked"
                                                                                       : "paused for screen-off on battery"))));
        if constexpr (requires { ble_ctrl->pause(); }) {
          ble_ctrl->pause();
        } else {
          ble_ctrl->disable();
        }
      }
    }
  }

#ifdef USE_WIFI
  if (wifi != nullptr && now_ms > 6000U) {
#if LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
    if constexpr (requires { wifi->enable(); wifi->disable(); wifi->is_disabled(); }) {
      auto &ws = wifi_power_state();
      if (wifi_connected || usb_powered || display_power_state != 2) {
        ws.offsite_since_ms = 0;
        ws.scan_window_start_ms = 0;
        ws.pause_until_ms = 0;
        ws.backoff_stage = 0;
        if (wifi->is_disabled()) {
          ESP_LOGI("power", "Wake / USB: resuming Wi-Fi station scan immediately");
          ws.base_valid = false;
          wifi->enable();
        }
      } else {
        if (ws.offsite_since_ms == 0) {
          ws.offsite_since_ms = now_ms | 1U;
          ws.scan_window_start_ms = now_ms | 1U;
        }
        if (wifi->is_disabled()) {
          if (ws.pause_until_ms != 0 && static_cast<int32_t>(now_ms - ws.pause_until_ms) >= 0) {
            ws.pause_until_ms = 0;
            ws.scan_window_start_ms = now_ms | 1U;
            ws.base_valid = false;
            wifi->enable();
          }
        } else {
          const uint32_t active_limit_ms = (ws.backoff_stage == 0 && (now_ms - ws.offsite_since_ms) < 32000U)
                                               ? 30000U
                                               : 12000U;
          if (ws.scan_window_start_ms != 0 && (now_ms - ws.scan_window_start_ms) >= active_limit_ms) {
            static constexpr uint32_t kBackoffMs[3] = {15000U, 60000U, 300000U};
            const uint32_t pause_ms = kBackoffMs[ws.backoff_stage < 3 ? ws.backoff_stage : 2];
            if (ws.backoff_stage < 2) ws.backoff_stage++;
            ws.pause_until_ms = (now_ms + pause_ms) | 1U;
            ESP_LOGI("power", "Off-site standby: pausing Wi-Fi scan for %lus", static_cast<unsigned long>(pause_ms / 1000U));
            wifi->disable();
            return;
          }
        }
      }
    }
#endif
    apply_wifi_power_policy(wifi, high_tput_needed, display_power_state);
  }
#else
  (void)wifi;
  (void)display_power_state;
  (void)high_tput_needed;
#endif
}

// ------------------------------------------------------------------------------
// 7. Power & Boot Flight Recorder Summaries
// ------------------------------------------------------------------------------
inline void log_power_and_mem_profile(const char *trigger, int pwr_state, bool usb, bool chg,
                                      float batt_pct, float batt_v, bool spk_pa, bool mww_run,
                                      bool va_act, bool mp_play, bool ble_act, int game_id,
                                      uint32_t sleep_start_ms) {
  if (!profiler_enabled()) return;
  const uint32_t now_m = hal_millis_now();
  const uint32_t sleep_age_s = (sleep_start_ms > 0 && now_m >= sleep_start_ms)
                                   ? ((now_m - sleep_start_ms) / 1000U)
                                   : 0U;
  LATTER_PROF_LOGI(
      "[PROF:POWER] trig=%s pwr=%d usb=%d chg=%d batt=%.0f%%(%.2fV) gauge=%d%% spk_pa=%d mww=%d va=%d mp=%d ble=%d game=%d sleep_age=%lus wom=%d/%lu wifi_ps=%d",
      trigger, pwr_state, (int)usb, (int)chg,
      std::isnan(batt_pct) ? -1.0f : batt_pct,
      std::isnan(batt_v) ? 0.0f : batt_v, pmu_telemetry().gauge_raw,
      (int)spk_pa, (int)mww_run, (int)va_act, (int)mp_play, (int)ble_act, game_id,
      (unsigned long)sleep_age_s, (int)imu_wom().armed, (unsigned long)imu_wom().events,
      wifi_ps_mode_now());
  battery_log_profile(now_m);
  pm_log_profile();
  pm_log_wake_profile();
#ifdef USE_ESP32
  size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t int_max  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U;
  size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t ps_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t int_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024U;
  LATTER_PROF_LOGI("[PROF:MEM] int_free=%luKB int_max_blk=%luKB int_min=%luKB dma_free=%luKB psram_free=%luKB psram_min=%luKB",
                   (unsigned long)int_free, (unsigned long)int_max, (unsigned long)int_min, (unsigned long)dma_free,
                   (unsigned long)ps_free, (unsigned long)ps_min);
#endif
}

inline void log_boot_summary() {
  log_stall_summary();
  const PmCrumb &crumb = pm_previous_crumb();
  if (crumb.magic == kPmCrumbMagic && crumb.ring_head < kPmCrumbRing) {
    const PmCrumbSleep &last_entry = crumb.ring[crumb.ring_head];
    ESP_LOGW("PROF", "[PROF:BOOT] previous reset at light sleep %s: seq=%lu imu_wom=%lu imu_events=%lu tick0=%+ldus tick1=%+ldus",
             pm_crash_where(), (unsigned long)crumb.seq, (unsigned long)crumb.imu_armed,
             (unsigned long)crumb.imu_events, (long)(int32_t)(crumb.tick_us[0] - last_entry.t_us),
             (long)(int32_t)(crumb.tick_us[1] - last_entry.t_us));
    for (uint32_t k = 0; k < kPmCrumbRing; k++) {
      const uint32_t idx = (crumb.ring_head + 1U + k) % kPmCrumbRing;
      const PmCrumbSleep &e = crumb.ring[idx];
      const bool last = idx == crumb.ring_head;
      if (e.t_us == 0 && e.req_us == 0) continue;
      ESP_LOGW("PROF", "[PROF:BOOT]   %s t=%luus req=%luus slept=%luus cause=0x%04x arm=0x%02x lv=0x%02x",
               last ? (crumb.phase == 1U ? "in-flight" : "last") : "sleep", (unsigned long)e.t_us,
               (unsigned long)e.req_us, (unsigned long)e.slept_us, (unsigned)e.cause, (unsigned)e.arm,
               (unsigned)e.levels);
    }
  }
  const uint32_t up_s = hal_millis_now() / 1000U;
  const char *rr = last_reset_reason();
  const int crash = crash_record_present() ? 1 : 0;
  const int rollback = firmware_rolled_back() ? 1 : 0;
  const auto &tel = pmu_telemetry();
  if (!tel.boot_valid) {
    ESP_LOGI("PROF", "[PROF:BOOT] sdk=%ld uptime=%lus reset=%s crash=%d rollback=%d pmu=none", (long)LATTER_SDK_REV,
             (unsigned long)up_s, rr, crash, rollback);
    return;
  }
  static const char *const kOff[8] = {"pwrkey-long", "software", "pwrkey-low", "vsys-undervolt",
                                      "vbus-overvolt", "dcdc-undervolt", "dcdc-overvolt", "over-temp"};
  static const char *const kOn[6] = {"pwrkey", "irq", "vbus-insert", "batt-charge", "batt-insert", "en-mode"};
  char off_s[96] = {0};
  char on_s[64] = {0};
  size_t off_len = 0;
  for (int b = 0; b < 8; b++) {
    if (tel.off_src & (1U << b)) {
      int n = std::snprintf(off_s + off_len, sizeof(off_s) - off_len, "%s%s", off_len ? "," : "", kOff[b]);
      if (n > 0) off_len = std::min(sizeof(off_s) - 1, off_len + static_cast<size_t>(n));
    }
  }
  size_t on_len = 0;
  for (int b = 0; b < 6; b++) {
    if (tel.on_src & (1U << b)) {
      int n = std::snprintf(on_s + on_len, sizeof(on_s) - on_len, "%s%s", on_len ? "," : "", kOn[b]);
      if (n > 0) on_len = std::min(sizeof(on_s) - 1, on_len + static_cast<size_t>(n));
    }
  }
  char hib_s[48] = "-";
  const HibernateRecord &prev = battery_model().prev_hib;
  if (prev.valid) {
    if (prev.soc <= 100) {
      std::snprintf(hib_s, sizeof(hib_s), "%s@%umV/%u%%", hibernate_reason_name(prev.reason), (unsigned)prev.mv,
                    (unsigned)prev.soc);
    } else {
      std::snprintf(hib_s, sizeof(hib_s), "%s@%umV", hibernate_reason_name(prev.reason), (unsigned)prev.mv);
    }
  }
  ESP_LOGI("PROF",
           "[PROF:BOOT] sdk=%ld uptime=%lus reset=%s crash=%d rollback=%d last_off=0x%02X(%s) hib=%s on=0x%02X(%s) icc=%dmA cv=0x%02X",
           (long)LATTER_SDK_REV, (unsigned long)up_s, rr, crash, rollback, tel.off_src, off_s[0] ? off_s : "-", hib_s,
           tel.on_src, on_s[0] ? on_s : "-", tel.icc_ma, tel.cv_reg);
}

}  // namespace latter_ui
