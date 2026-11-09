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
//   wlk       share of uptime the light-sleep RSSI gate kept the chip awake (%; see kWeakLinkEnterDbm)
// Boards with automatic light sleep only (hal_power_pm.h):
//   ls        light sleeps that actually slept
//   slp       share of uptime spent in light sleep (%)
//   awk       share of uptime the latter_awake lock was held (%): the power policy kept the chip up
//   wk        wake cause per sleep, timer/gpio/wifi/other (sums to ls; Wi-Fi beacon wakes are timer wakes)
//   gp        GPIO wakes per armed pin, pwr/btn/touch/imu/unattributed (a pin counts when its level had
//             left the armed level at the exit; unattributed = no armed pin had)
//   rst       why the previous boot ended (power-on, int-wdt, ...), with @ls-in / @ls-out when a watchdog
//             or panic reset hit inside a light sleep or after the last one came back (hal_power_pm.h)
//   api       API client connects (any client); ha = Home Assistant client connects
//   gap       longest time (s) with no Home Assistant client connected, the current gap included;
//             down = the current gap (0 while connected)
// Battery boards only (hal_battery.h):
//   bat       raw VBAT mV / compensated rest-voltage mV / Battery Level % / state (d discharging,
//             c charging, v constant-voltage phase, f full, n on USB but not charging)
//   dr        average discharge (%/h) since the estimate settled on battery ("-" for the first 30 min)
// All boards:
//   nvs       flash writes since boot: NVS blob writes / flash erases / longest single NVS write (ms).
//             Counted by linker wraps (components/latter_core adds -Wl,--wrap=nvs_set_blob and
//             -Wl,--wrap=esp_flash_erase_region), so every caller counts: ESPHome's preference sync
//             (restore_value globals, restoring selects/numbers/switches, media player volume, wake word
//             model state, Wi-Fi fast connect) and OTA. An erase that isn't OTA is NVS garbage collection.
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
#include <esp_flash.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs.h>
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

// Flash writes from any task (the linker wraps below). Constant-initialized.
struct FlashWriteStats {
  std::atomic<uint32_t> nvs_writes{0};
  std::atomic<uint32_t> erases{0};
  std::atomic<uint32_t> nvs_max_us{0};
};
inline FlashWriteStats g_flash_write_stats;

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

// Light-sleep RSSI gate (v1). On 2026-11-08, at -64...-72 dBm with automatic light sleep, Home Assistant's
// API link dropped about every 5 minutes while Wi-Fi stayed associated (bto=0, disc=0); with the awake
// lock held at the same spot the link stayed up for 24 minutes. The root cause is deferred to v3; until
// then a weak link keeps the chip awake (apply_power_policy's weak_link).
constexpr int32_t kWeakLinkEnterDbm = -65;          // weak when the smoothed RSSI falls below this
constexpr int32_t kWeakLinkClearDbm = -61;          // cleared only above this...
constexpr uint32_t kWeakLinkClearHoldMs = 120000U;  // ...for this long without a dip
constexpr uint32_t kWeakLinkSampleMs = 10000U;      // RSSI sample period (from the 1 s power supervisor)
constexpr float kWeakLinkEmaAlpha = 0.3f;           // about 30 s of smoothing at 10 s samples

struct WeakLinkGate {
  bool weak{false};
  bool have{false};
  float ema{0.0f};
  uint32_t last_sample_ms{0};
  uint32_t above_since_ms{0};  // 0 = not above the clear threshold
  uint32_t weak_since_ms{0};
  uint32_t weak_total_ms{0};   // finished weak periods
};

inline WeakLinkGate &weak_link_gate() {
  static WeakLinkGate g;
  return g;
}

// Script apply_power_policy (main loop): true while the smoothed RSSI says the link is too weak for
// light sleep. Samples at most every kWeakLinkSampleMs while associated; keeps its state otherwise
// (a dropped Home Assistant link keeps the chip awake on its own).
inline bool weak_link_update(uint32_t now_ms) {
  WeakLinkGate &g = weak_link_gate();
#ifdef USE_WIFI
  if (g.last_sample_ms != 0 && now_ms - g.last_sample_ms < kWeakLinkSampleMs) return g.weak;
  g.last_sample_ms = now_ms | 1U;
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr || !wifi->is_connected()) return g.weak;
  const int32_t rssi = wifi->wifi_rssi();
  if (rssi <= -127 || rssi >= 0) return g.weak;  // -127: ESPHome's "not associated"
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

// The report line. Realistic overnight values stay near 200 characters with the battery tail; the buffer
// caps it at Home Assistant's 255-character state limit.
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

// The System: Link Report text sensor's lambda: the line, also logged as [PROF:LINK].
inline std::string link_report_log_line(uint32_t now_ms, const char *reset_reason) {
  std::string line = link_report_line(now_ms, reset_reason);
  ESP_LOGI("PROF", "[PROF:LINK] %s", line.c_str());
  return line;
}

}  // namespace latter_ui

#ifdef USE_ESP32
// Linker wraps (-Wl,--wrap=..., added by components/latter_core): count every NVS blob write and flash
// erase, and time the NVS write (garbage collection, when it runs, runs inside it). Task context only:
// both functions are called by tasks, never from an interrupt or with the cache disabled.
extern "C" {
esp_err_t __real_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t __wrap_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
  const int64_t t0 = esp_timer_get_time();
  const esp_err_t err = __real_nvs_set_blob(handle, key, value, length);
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
  auto &fw = latter_ui::g_flash_write_stats;
  fw.nvs_writes.fetch_add(1U, std::memory_order_relaxed);
  uint32_t prev = fw.nvs_max_us.load(std::memory_order_relaxed);
  while (us > prev && !fw.nvs_max_us.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
  }
  return err;
}

esp_err_t __real_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len);
esp_err_t __wrap_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len) {
  latter_ui::g_flash_write_stats.erases.fetch_add(1U, std::memory_order_relaxed);
  return __real_esp_flash_erase_region(chip, start, len);
}
}  // extern "C"
#endif
