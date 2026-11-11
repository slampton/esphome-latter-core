#pragma once

// ==============================================================================
// Latter Core Power Management, Light Sleep, DFS, Stall Guard & Radio Policy
// (hal_power_pm.h)
// ==============================================================================

#include "esphome.h"
#include "hal_manifest.h"
#include "latter_prof.h"

#include <atomic>
#include <cstdint>
#include <string>

#ifdef USE_ESP32
#include <esp_wifi.h>
#include <sdkconfig.h>
#endif

namespace esphome::latter_core {

struct LoopProbeStats {
  uint32_t passes{0};
  uint32_t hf_passes{0};    // passes with a HighFrequencyLoopRequester held
  uint32_t gaps[5]{};       // gap since the previous pass: <20 ms, <50 ms, <200 ms, <1 s, >=1 s
  uint32_t last_ms{0};
};

inline LoopProbeStats &loop_probe_stats() {
  static LoopProbeStats s;
  return s;
}

}  // namespace esphome::latter_core

namespace latter_ui {

// Wake-pin slots, in arming-mask bit order (pm_wake_bit).
enum PmWakeSlot : uint8_t {
  PM_WAKE_PWR_KEY = 0,
  PM_WAKE_ACTION_BTN = 1,
  PM_WAKE_TOUCH = 2,
  PM_WAKE_IMU = 3,
  PM_WAKE_SLOTS = 4,
};

inline constexpr uint32_t pm_wake_bit(PmWakeSlot slot) { return 1UL << static_cast<uint32_t>(slot); }

// Main loop interval while the awake lock is released. An IRAM-safe esp_timer started on GPIO wake
// calls App.wake_loop_threadsafe() within 50 us of cache restore, so stretching quiet loop intervals
// to 5,000 ms (1,000 ms when touch wake is armed) cuts idle CPU wakeups 4-5x with zero input lag.
constexpr uint32_t kPmQuietLoopIntervalMs = 5000U;
constexpr uint32_t kPmQuietTouchLoopIntervalMs = 1000U;

inline constexpr uint32_t pm_quiet_loop_interval(uint32_t arm_mask) {
  return (arm_mask & pm_wake_bit(PM_WAKE_TOUCH)) != 0 ? kPmQuietTouchLoopIntervalMs : kPmQuietLoopIntervalMs;
}

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
  uint32_t phase;       // 1 entered, 2 exited
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

extern FlashWriteStats g_flash_write_stats;

PmCrumb &pm_previous_crumb();
void pm_init();
void pm_apply(bool stay_awake, uint32_t wake_mask);
void pm_note_imu(bool armed, uint32_t events);
const char *pm_crash_where();
bool pm_sleep_stats(PmSleepStats &out, uint32_t now_ms);
void pm_log_profile();
void pm_log_wake_profile();

// ------------------------------------------------------------------------------
// Wi-Fi Modem Power Save & Smart Off-Site Radio Policy (Single Policy Owner)
// ------------------------------------------------------------------------------
struct WifiPowerState {
  bool hp_held{false};
  bool base_valid{false};
  int base{-1};
  bool pulse{false};
  // Off-site exponential Wi-Fi scan backoff when carried away from configured APs on battery:
  uint32_t offsite_since_ms{0};
  uint32_t scan_window_start_ms{0};
  uint32_t pause_until_ms{0};
  uint8_t backoff_stage{0};  // 0 -> 15 s, 1 -> 60 s, 2 -> 300 s
};

inline WifiPowerState &wifi_power_state() {
  static WifiPowerState s;
  return s;
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
  // Automatic light sleep idles the CPU between DTIM beacons; WIFI_POWER_SAVE_LIGHT keeps HA API rock-solid.
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

template <typename BleCtrlT, typename WifiT>
inline void evaluate_radio_policy(BleCtrlT *ble_ctrl, WifiT *wifi, bool wifi_connected, bool ble_beacon_switch_on,
                                  int beacon_mode, bool usb_powered,
                                  bool is_hibernating, bool ota_in_progress, int display_power_state,
                                  bool high_tput_needed, uint32_t now_ms) {
  if (is_hibernating || ota_in_progress) return;

  // 1. BLE Presence Beacon:
  // Always paused when !wifi_connected (off-site: no HA / BLE proxies reachable, and pausing frees
  // the BT controller on S3 so automatic light sleep engages cleanly).
#if LATTER_HAS_BATTERY
  const bool beacon_context_ok = (beacon_mode == 2) ||
                                 (!usb_powered && (beacon_mode == 1 || display_power_state != 2));
#else
  (void)beacon_mode;
  (void)usb_powered;
  const bool beacon_context_ok = true;
#endif
  const bool beacon_paused = ble_beacon_switch_on && (!beacon_context_ok || !wifi_connected);
  const bool beacon_wanted = ble_beacon_switch_on && !beacon_paused && (now_ms > 20000U);
  if (ble_ctrl != nullptr) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    const bool radio_ready = wifi_connected && (now_ms > 8000U);
#else
    const bool radio_ready = wifi_connected;
#endif
    const bool want_ble = beacon_wanted && radio_ready;
    if (want_ble && !ble_ctrl->is_active() && !ble_ctrl->is_failed()) {
      ESP_LOGD("ble", "Enabling BLE radio (Area Presence Beacon)");
      ble_ctrl->enable();
    } else if (!want_ble && ble_ctrl->is_active()) {
      ESP_LOGD("ble", "Disabling BLE radio (beacon %s)",
               !wifi_connected ? "paused off-site"
                               : (!beacon_paused ? "off"
                                                 : (usb_powered ? "paused while docked"
                                                                : "paused for screen-off on battery")));
      ble_ctrl->disable();
    }
  }

  // 2. Smart Off-Site Wi-Fi Scan Backoff (15 s -> 60 s -> 300 s) & Modem Power Save
#ifdef USE_WIFI
  if (wifi != nullptr && now_ms > 6000U) {
    auto &ws = wifi_power_state();
#if LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
    if constexpr (requires { wifi->enable(); wifi->disable(); wifi->is_disabled(); }) {
      if (wifi_connected || usb_powered || display_power_state != 2) {
        // Connected, docked, or user woke the screen -> ensure Wi-Fi is immediately enabled & reset backoff.
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
        // Unplugged + screen-off + not connected: allow 30 s initial reconnect window, then pace 12 s scans
        // with 15 s -> 60 s -> 300 s exponential quiet intervals so off-site carry sips battery.
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

}  // namespace latter_ui
