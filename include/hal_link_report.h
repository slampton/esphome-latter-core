#pragma once

// ==============================================================================
// Latter Core Link Report: Wi-Fi and Home Assistant Link Counters (hal_link_report.h)
// ==============================================================================
// Shows from Home Assistant's own history whether the Wi-Fi link explains a dropped Home Assistant
// connection. The diagnostic text sensor "System: Link Report" publishes one key=value line every
// 15 minutes and on every API client connect (Home Assistant's recorder keeps it, so it can be read
// with log forwarding off); the same line is logged as [PROF:LINK]. Counters run since boot:
//   up        uptime (s)
//   bto       WIFI_EVENT_STA_BEACON_TIMEOUT events: the station stopped hearing the AP's beacons
//   disc      station disconnects; rsn = reason code of the last one (wifi_err_reason_t, 200 = beacon timeout)
//   rssi      average/minimum signal (dBm) of the 60 s samples taken while associated; n = samples
// Boards with automatic light sleep only (hal_power_pm.h):
//   ls        light sleeps that actually slept
//   slp       share of uptime spent in light sleep (%)
//   awk       share of uptime the latter_awake lock was held (%): the power policy kept the chip up
//   wk        wake cause per sleep, timer/gpio/wifi/other (sums to ls; Wi-Fi beacon wakes are timer wakes)
//   gp        GPIO wakes per armed pin, pwr/btn/touch/imu/unattributed (a pin counts when its level had
//             left the armed level at the exit; unattributed = no armed pin had)
//   api       API client connects (any client); ha = Home Assistant client connects
//   gap       longest time (s) with no Home Assistant client connected, the current gap included;
//             down = the current gap (0 while connected)
// Nothing here takes the awake lock: the Wi-Fi event handler only bumps counters (on the event-loop
// task), the sleep counters come from the light-sleep callbacks, and the sample and the report run
// from the main loop's scheduler.
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
// Event-loop task: counters only.
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

// Hooks the Wi-Fi events once. Called from on_boot (the network stack has created the default event
// loop by then) and from the 60 s sample, which retries until the hook is in place.
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

// Every 60 s from the main loop: one RSSI sample while associated, and the light-sleep time fold.
inline void link_report_sample() {
  PmSleepStats pm;
  (void) pm_sleep_stats(pm, esphome::millis());
#ifdef USE_WIFI
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr || !wifi->is_connected()) return;
  const int32_t rssi = wifi->wifi_rssi();
  if (rssi <= -127 || rssi >= 0) return;  // -127: ESPHome's "not associated"
  LinkReport &r = link_report();
  if (r.rssi_samples == 0 || rssi < r.rssi_min) r.rssi_min = rssi;
  r.rssi_sum += rssi;
  r.rssi_samples++;
#endif
}

// api on_client_connected / on_client_disconnected.
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

// Share of uptime in tenths of a percent.
inline uint32_t link_report_permille(uint64_t part, uint64_t whole) {
  if (whole == 0) return 0;
  const uint64_t p = part * 1000ULL / whole;
  return p > 1000ULL ? 1000U : static_cast<uint32_t>(p);
}

// The report line. Realistic overnight values stay near 170 characters; the buffer caps it at Home
// Assistant's 255-character state limit.
inline std::string link_report_line(uint32_t now_ms) {
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
  char line[256];
  snprintf(line, sizeof(line), "up=%lu bto=%lu disc=%lu rsn=%lu rssi=%s n=%lu%s api=%lu ha=%lu gap=%lu down=%lu",
           static_cast<unsigned long>(now_ms / 1000U),
           static_cast<unsigned long>(r.beacon_timeouts.load(std::memory_order_relaxed)),
           static_cast<unsigned long>(r.disconnects.load(std::memory_order_relaxed)),
           static_cast<unsigned long>(r.last_reason.load(std::memory_order_relaxed)), rssi,
           static_cast<unsigned long>(r.rssi_samples), sleep, static_cast<unsigned long>(r.api_connects),
           static_cast<unsigned long>(r.ha_connects), static_cast<unsigned long>(gap_ms / 1000U),
           static_cast<unsigned long>(down_ms / 1000U));
  return std::string(line);
}

// The System: Link Report text sensor's lambda: the line, also logged as [PROF:LINK].
inline std::string link_report_log_line(uint32_t now_ms) {
  std::string line = link_report_line(now_ms);
  ESP_LOGI("PROF", "[PROF:LINK] %s", line.c_str());
  return line;
}

}  // namespace latter_ui
