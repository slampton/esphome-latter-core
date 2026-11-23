#pragma once

// ==============================================================================
// Latter Core Wi-Fi Modem Power Save & Smart Off-Site Radio Policy
// (include/hal_radio_policy.h)
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

#include <cstdint>
#include <string>

#ifdef USE_ESP32
#include <esp_wifi.h>
#include <sdkconfig.h>
#endif
#if defined(USE_WIFI) && __has_include("esphome/components/wifi/wifi_component.h")
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace latter_ui {

struct WifiPowerState {
  bool hp_held{false};
  bool base_valid{false};
  bool ota_active{false};
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
  if (wifi == nullptr) return;
  auto &st = wifi_power_state();
#if !LATTER_IS_STATIONARY
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
  bool want_hp = high_tput_needed || st.ota_active;
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
  (void)high_tput_needed;
  (void)display_power_state;
  if (st.ota_active && !st.hp_held) {
    st.hp_held = wifi->request_high_performance();
  } else if (!st.ota_active && st.hp_held) {
    wifi->release_high_performance();
    st.hp_held = false;
  }
#endif
}
#endif

inline void wifi_power_ota_begin() {
  wifi_power_state().ota_active = true;
}

inline void wifi_power_ota_end() {
  auto &st = wifi_power_state();
  st.ota_active = false;
  st.base_valid = false;
}

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
#ifdef USE_WIFI
    if (ota_in_progress && !is_hibernating && wifi != nullptr) {
      apply_wifi_power_policy(wifi, true, display_power_state);
    }
#endif
    return;
  }

  if (!ble_beacon_switch_on) {
    if (ble_is_up()) {
      ESP_LOGD("ble", "Disabling BLE radio (beacon switch off)");
      ble_ctrl->disable();
    }
  } else {
#if LATTER_HAS_BATTERY
    const bool beacon_context_ok = usb_powered || (beacon_mode >= 1) || (display_power_state != 2);
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
      const bool radio_ready = wifi_connected && (ha_connected || now_ms > 8000U);
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
                                                                        : "paused for screen-off on battery")));
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
          ESP_LOGD("power", "Wake / USB: resuming Wi-Fi station scan immediately");
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
            ESP_LOGD("power", "Off-site standby: pausing Wi-Fi scan for %lus",
                     static_cast<unsigned long>(pause_ms / 1000U));
            if (ws.hp_held) {
              wifi->release_high_performance();
              ws.hp_held = false;
            }
            ws.base_valid = false;
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
