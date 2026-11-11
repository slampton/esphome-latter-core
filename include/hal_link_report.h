#pragma once

// ==============================================================================
// Latter Core Link Report: Wi-Fi and Home Assistant Link Counters (hal_link_report.h)
// ==============================================================================

#include "esphome.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#ifdef USE_ESP32
#include <esp_event.h>
#include <esp_wifi.h>
#endif

#include "hal_battery.h"
#include "hal_power_pm.h"

namespace latter_ui {

struct LinkReport {
  std::atomic<uint32_t> beacon_timeouts{0};  // written on the event-loop task
  std::atomic<uint32_t> disconnects{0};
  std::atomic<uint32_t> last_reason{0};
  bool hooked{false};
  bool hook_warned{false};
  int32_t rssi_sum{0};
  int32_t rssi_min{0};
  uint32_t rssi_samples{0};
  uint32_t api_connects{0};
  uint32_t ha_connects{0};
  uint32_t ha_clients{0};
  uint32_t ha_gap_start_ms{0};  // start of the current gap while ha_clients == 0 (0 = boot)
  uint32_t ha_gap_max_ms{0};    // longest finished gap
};

inline LinkReport &link_report() {
  static LinkReport r;
  return r;
}

#ifdef USE_ESP32
inline void link_report_on_wifi_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void) arg;
  (void) base;
  LinkReport &r = link_report();
  if (event_id == WIFI_EVENT_STA_BEACON_TIMEOUT) {
    r.beacon_timeouts.fetch_add(1U, std::memory_order_relaxed);
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    r.disconnects.fetch_add(1U, std::memory_order_relaxed);
    if (event_data != nullptr) {
      const auto *ev = static_cast<const wifi_event_sta_disconnected_t *>(event_data);
      r.last_reason.store(static_cast<uint32_t>(ev->reason), std::memory_order_relaxed);
    }
  }
}
#endif

inline void link_report_init() {
#ifdef USE_ESP32
  LinkReport &r = link_report();
  if (r.hooked) return;
  const esp_err_t err =
      esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &link_report_on_wifi_event, nullptr, nullptr);
  if (err != ESP_OK) {
    if (!r.hook_warned) {
      r.hook_warned = true;
      ESP_LOGW("latter", "Link report: Wi-Fi event hook failed (%s); retrying every minute", esp_err_to_name(err));
    }
    return;
  }
  r.hooked = true;
#endif
}

inline void link_report_sample() {
  PmSleepStats pm;
  (void) pm_sleep_stats(pm, esphome::millis());
#ifdef USE_WIFI
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr || !wifi->is_connected()) return;
  const int32_t rssi = wifi->wifi_rssi();
  if (rssi <= -127 || rssi >= 0) return;
  LinkReport &r = link_report();
  if (r.rssi_samples == 0 || rssi < r.rssi_min) r.rssi_min = rssi;
  r.rssi_sum += rssi;
  r.rssi_samples++;
#endif
}

// Light-sleep RSSI gate (v1): keeps chip awake when smoothed RSSI drops below -65 dBm (cleared above -61 dBm for 2 min).
constexpr int32_t kWeakLinkEnterDbm = -65;
constexpr int32_t kWeakLinkClearDbm = -61;
constexpr uint32_t kWeakLinkClearHoldMs = 120000U;
constexpr uint32_t kWeakLinkSampleMs = 10000U;
constexpr float kWeakLinkEmaAlpha = 0.3f;

struct WeakLinkGate {
  bool weak{false};
  bool have{false};
  float ema{0.0f};
  uint32_t last_sample_ms{0};
  uint32_t above_since_ms{0};
  uint32_t weak_since_ms{0};
  uint32_t weak_total_ms{0};
};

inline WeakLinkGate &weak_link_gate() {
  static WeakLinkGate g;
  return g;
}

inline bool weak_link_update(uint32_t now_ms) {
  WeakLinkGate &g = weak_link_gate();
#ifdef USE_WIFI
  if (g.last_sample_ms != 0 && now_ms - g.last_sample_ms < kWeakLinkSampleMs) return g.weak;
  g.last_sample_ms = now_ms | 1U;
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr || !wifi->is_connected()) return g.weak;
  const int32_t rssi = wifi->wifi_rssi();
  if (rssi <= -127 || rssi >= 0) return g.weak;
  const float r = static_cast<float>(rssi);
  g.ema = g.have ? g.ema + kWeakLinkEmaAlpha * (r - g.ema) : r;
  g.have = true;
  if (!g.weak) {
    if (g.ema < static_cast<float>(kWeakLinkEnterDbm)) {
      g.weak = true;
      g.weak_since_ms = now_ms;
      g.above_since_ms = 0;
      ESP_LOGI("power", "Light sleep: weak Wi-Fi link (%.0f dBm smoothed), staying awake", g.ema);
    }
  } else if (g.ema > static_cast<float>(kWeakLinkClearDbm)) {
    if (g.above_since_ms == 0) {
      g.above_since_ms = now_ms | 1U;
    } else if (now_ms - g.above_since_ms >= kWeakLinkClearHoldMs) {
      g.weak = false;
      g.weak_total_ms += now_ms - g.weak_since_ms;
      g.above_since_ms = 0;
      ESP_LOGI("power", "Light sleep: Wi-Fi link recovered (%.0f dBm smoothed)", g.ema);
    }
  } else {
    g.above_since_ms = 0;
  }
#else
  (void) now_ms;
#endif
  return g.weak;
}

inline uint32_t weak_link_ms(uint32_t now_ms) {
  const WeakLinkGate &g = weak_link_gate();
  return g.weak_total_ms + (g.weak ? now_ms - g.weak_since_ms : 0U);
}

inline void link_report_api_connected(bool home_assistant, uint32_t now_ms) {
  LinkReport &r = link_report();
  r.api_connects++;
  if (!home_assistant) return;
  r.ha_connects++;
  if (r.ha_clients == 0) {
    const uint32_t gap_ms = now_ms - r.ha_gap_start_ms;
    if (gap_ms > r.ha_gap_max_ms) r.ha_gap_max_ms = gap_ms;
  }
  r.ha_clients++;
}

inline void link_report_api_disconnected(bool home_assistant, uint32_t now_ms) {
  LinkReport &r = link_report();
  if (!home_assistant || r.ha_clients == 0) return;
  r.ha_clients--;
  if (r.ha_clients == 0) r.ha_gap_start_ms = now_ms;
}

inline uint32_t link_report_permille(uint64_t part, uint64_t whole) {
  if (whole == 0) return 0;
  const uint64_t p = part * 1000ULL / whole;
  return p > 1000ULL ? 1000U : static_cast<uint32_t>(p);
}

inline std::string link_report_line(uint32_t now_ms, const char *reset_reason) {
  const LinkReport &r = link_report();
  const uint32_t down_ms = (r.ha_clients == 0) ? now_ms - r.ha_gap_start_ms : 0U;
  const uint32_t gap_ms = (down_ms > r.ha_gap_max_ms) ? down_ms : r.ha_gap_max_ms;
  char rssi[32];
  if (r.rssi_samples > 0) {
    snprintf(rssi, sizeof(rssi), "%ld/%ld", static_cast<long>(r.rssi_sum / static_cast<int32_t>(r.rssi_samples)),
             static_cast<long>(r.rssi_min));
  } else {
    snprintf(rssi, sizeof(rssi), "na");
  }
  char sleep[160] = "";
  PmSleepStats pm;
  if (pm_sleep_stats(pm, now_ms)) {
    const uint32_t slp = link_report_permille(pm.slept_us, static_cast<uint64_t>(now_ms) * 1000ULL);
    const uint32_t awk = link_report_permille(pm.awake_ms, now_ms);
    snprintf(sleep, sizeof(sleep), " ls=%lu slp=%lu.%lu awk=%lu.%lu wk=%lu/%lu/%lu/%lu gp=%lu/%lu/%lu/%lu/%lu",
             static_cast<unsigned long>(pm.sleeps), static_cast<unsigned long>(slp / 10U),
             static_cast<unsigned long>(slp % 10U), static_cast<unsigned long>(awk / 10U),
             static_cast<unsigned long>(awk % 10U), static_cast<unsigned long>(pm.timer_wakes),
             static_cast<unsigned long>(pm.gpio_wakes), static_cast<unsigned long>(pm.wifi_wakes),
             static_cast<unsigned long>(pm.other_wakes), static_cast<unsigned long>(pm.pin_wakes[PM_WAKE_PWR_KEY]),
             static_cast<unsigned long>(pm.pin_wakes[PM_WAKE_ACTION_BTN]),
             static_cast<unsigned long>(pm.pin_wakes[PM_WAKE_TOUCH]),
             static_cast<unsigned long>(pm.pin_wakes[PM_WAKE_IMU]), static_cast<unsigned long>(pm.gpio_unattributed));
  }
  const uint32_t wlk = link_report_permille(weak_link_ms(now_ms), now_ms);
  const char *where = pm_crash_where();
  char batt[48];
  battery_report_fragment(batt, sizeof(batt), now_ms);
  const FlashWriteStats &fw = g_flash_write_stats;
  char line[256];
  snprintf(line, sizeof(line),
           "up=%lu rst=%s%s%s bto=%lu disc=%lu rsn=%lu rssi=%s n=%lu wlk=%lu.%lu%s api=%lu ha=%lu gap=%lu down=%lu nvs=%lu/%lu/%lu%s",
           static_cast<unsigned long>(now_ms / 1000U), reset_reason != nullptr ? reset_reason : "", where[0] ? "@" : "",
           where, static_cast<unsigned long>(r.beacon_timeouts.load(std::memory_order_relaxed)),
           static_cast<unsigned long>(r.disconnects.load(std::memory_order_relaxed)),
           static_cast<unsigned long>(r.last_reason.load(std::memory_order_relaxed)), rssi,
           static_cast<unsigned long>(r.rssi_samples), static_cast<unsigned long>(wlk / 10U),
           static_cast<unsigned long>(wlk % 10U), sleep, static_cast<unsigned long>(r.api_connects),
           static_cast<unsigned long>(r.ha_connects), static_cast<unsigned long>(gap_ms / 1000U),
           static_cast<unsigned long>(down_ms / 1000U),
           static_cast<unsigned long>(fw.nvs_writes.load(std::memory_order_relaxed)),
           static_cast<unsigned long>(fw.erases.load(std::memory_order_relaxed)),
           static_cast<unsigned long>((fw.nvs_max_us.load(std::memory_order_relaxed) + 999U) / 1000U), batt);
  return std::string(line);
}

inline std::string link_report_log_line(uint32_t now_ms, const char *reset_reason) {
  std::string line = link_report_line(now_ms, reset_reason);
  ESP_LOGI("PROF", "[PROF:LINK] %s", line.c_str());
  return line;
}

}  // namespace latter_ui
