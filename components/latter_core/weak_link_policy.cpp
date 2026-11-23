#if __has_include("hal_link_report.h")
#include "hal_link_report.h"
#include "hal_battery.h"
#include "hal_sleep_pm.h"
#include "latter_prof.h"
#else
#include "../../include/hal_link_report.h"
#include "../../include/hal_battery.h"
#include "../../include/hal_sleep_pm.h"
#include "../../include/latter_prof.h"
#endif

#include <cstdio>

#ifdef USE_ESP32
#include <esp_event.h>
#include <esp_wifi.h>
#endif
#if defined(USE_WIFI) && __has_include("esphome/components/wifi/wifi_component.h")
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace latter_ui {

LinkReport &link_report() {
  static LinkReport r;
  return r;
}

#ifdef USE_ESP32
static void link_report_on_wifi_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void)arg;
  (void)base;
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

void link_report_init() {
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

void link_report_sample() {
  PmSleepStats pm;
  (void)pm_sleep_stats(pm, hal_millis_now());
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

WeakLinkGate &weak_link_gate() {
  static WeakLinkGate g;
  return g;
}

void weak_link_note_disconnect(uint32_t now_ms, WeakLinkGate &g) {
  if (g.weak) {
    g.weak = false;
    g.weak_total_ms += now_ms - g.weak_since_ms;
  }
  g.have = false;
  g.below_count = 0;
  g.last_sample_ms = 0;
  g.above_since_ms = 0;
}

bool weak_link_feed_rssi(int32_t rssi, uint32_t now_ms, WeakLinkGate &g) {
  if (g.last_sample_ms != 0 && now_ms - g.last_sample_ms < kWeakLinkSampleMs) return g.weak;
  if (rssi <= -127 || rssi >= 0) return g.weak;
  g.last_sample_ms = (now_ms == 0U) ? 1U : now_ms;
  const float r = static_cast<float>(rssi);
  g.ema = g.have ? g.ema + kWeakLinkEmaAlpha * (r - g.ema) : r;
  g.have = true;
  if (!g.weak) {
    if (g.ema < static_cast<float>(kWeakLinkEnterDbm)) {
      if (++g.below_count >= kWeakLinkEnterSamples) {
        g.weak = true;
        g.weak_since_ms = now_ms;
        g.above_since_ms = 0;
        LATTER_PROF_LOGI("[PROF:PM] weak_link=1 rssi_ema=%.0f", g.ema);
      }
    } else {
      g.below_count = 0;
    }
  } else if (g.ema > static_cast<float>(kWeakLinkClearDbm)) {
    if (g.above_since_ms == 0) {
      g.above_since_ms = (now_ms == 0U) ? 1U : now_ms;
    } else if (now_ms - g.above_since_ms >= kWeakLinkClearHoldMs) {
      g.weak = false;
      g.below_count = 0;
      g.weak_total_ms += now_ms - g.weak_since_ms;
      g.above_since_ms = 0;
      LATTER_PROF_LOGI("[PROF:PM] weak_link=0 rssi_ema=%.0f", g.ema);
    }
  } else {
    g.above_since_ms = 0;
  }
  return g.weak;
}

bool weak_link_update(uint32_t now_ms, uint32_t last_wifi_connect_ms) {
  WeakLinkGate &g = weak_link_gate();
#ifdef USE_WIFI
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr || !wifi->is_connected()) {
    weak_link_note_disconnect(now_ms, g);
    return false;
  }
  if (!weak_link_can_sample(now_ms, last_wifi_connect_ms)) return g.weak;
  return weak_link_feed_rssi(wifi->wifi_rssi(), now_ms, g);
#else
  (void)now_ms;
  (void)last_wifi_connect_ms;
  return g.weak;
#endif
}

uint32_t weak_link_ms(uint32_t now_ms) {
  const WeakLinkGate &g = weak_link_gate();
  return g.weak_total_ms + (g.weak ? now_ms - g.weak_since_ms : 0U);
}

void link_report_api_connected(bool home_assistant, uint32_t now_ms) {
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

void link_report_api_disconnected(bool home_assistant, uint32_t now_ms) {
  LinkReport &r = link_report();
  if (!home_assistant || r.ha_clients == 0) return;
  r.ha_clients--;
  if (r.ha_clients == 0) r.ha_gap_start_ms = now_ms;
}

uint32_t link_report_permille(uint64_t part, uint64_t whole) {
  if (whole == 0) return 0;
  const uint64_t p = part * 1000ULL / whole;
  return p > 1000ULL ? 1000U : static_cast<uint32_t>(p);
}

std::string link_report_line(uint32_t now_ms, const char *reset_reason) {
  const LinkReport &r = link_report();
  const uint32_t down_ms = (r.ha_clients == 0) ? now_ms - r.ha_gap_start_ms : 0U;
  const uint32_t gap_ms = (down_ms > r.ha_gap_max_ms) ? down_ms : r.ha_gap_max_ms;
  char rssi[32];
  if (r.rssi_samples > 0) {
    std::snprintf(rssi, sizeof(rssi), "%ld/%ld", static_cast<long>(r.rssi_sum / static_cast<int32_t>(r.rssi_samples)),
                  static_cast<long>(r.rssi_min));
  } else {
    std::snprintf(rssi, sizeof(rssi), "na");
  }
  char sleep[160] = "";
  PmSleepStats pm;
  if (pm_sleep_stats(pm, now_ms)) {
    const uint32_t slp = link_report_permille(pm.slept_us, static_cast<uint64_t>(now_ms) * 1000ULL);
    const uint32_t awk = link_report_permille(pm.awake_ms, now_ms);
    std::snprintf(sleep, sizeof(sleep), " ls=%lu slp=%lu.%lu awk=%lu.%lu wk=%lu/%lu/%lu/%lu gp=%lu/%lu/%lu/%lu/%lu",
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
  char line[640];
  std::snprintf(line, sizeof(line),
                "up=%lu rst=%.32s%s%.32s bto=%lu disc=%lu rsn=%lu rssi=%s n=%lu wlk=%lu.%lu%s api=%lu ha=%lu gap=%lu down=%lu nvs=%lu/%lu/%lu%s",
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

std::string link_report_log_line(uint32_t now_ms, const char *reset_reason) {
  if (!profiler_enabled()) return {};
  std::string line = link_report_line(now_ms, reset_reason);
  LATTER_PROF_LOGI("[PROF:LINK] %s", line.c_str());
  return line;
}

std::string link_report_json(uint32_t now_ms, const char *reset_reason, bool hibernating) {
  const LinkReport &r = link_report();
  const uint32_t down_ms = (r.ha_clients == 0) ? now_ms - r.ha_gap_start_ms : 0U;
  const uint32_t gap_ms = (down_ms > r.ha_gap_max_ms) ? down_ms : r.ha_gap_max_ms;
  const long rssi_avg = (r.rssi_samples > 0)
                            ? static_cast<long>(r.rssi_sum / static_cast<int32_t>(r.rssi_samples))
                            : 0L;
  const long rssi_min = (r.rssi_samples > 0) ? static_cast<long>(r.rssi_min) : 0L;
  const uint32_t wlk = link_report_permille(weak_link_ms(now_ms), now_ms);
  const FlashWriteStats &fw = g_flash_write_stats;
  char rssi_fields[64];
  if (r.rssi_samples > 0) {
    std::snprintf(rssi_fields, sizeof(rssi_fields), "\"rssi_avg\":%ld,\"rssi_min\":%ld,", rssi_avg, rssi_min);
  } else {
    std::snprintf(rssi_fields, sizeof(rssi_fields), "\"rssi_avg\":null,\"rssi_min\":null,");
  }
  char safe_rst[36]{};
  if (reset_reason != nullptr) {
    size_t out_i = 0;
    for (size_t i = 0; reset_reason[i] != '\0' && out_i + 1U < sizeof(safe_rst); ++i) {
      const char ch = reset_reason[i];
      if (ch != '"' && ch != '\\' && static_cast<unsigned char>(ch) >= 0x20U) {
        safe_rst[out_i++] = ch;
      }
    }
    safe_rst[out_i] = '\0';
  }
  char buf[384];
  const int len = std::snprintf(
      buf, sizeof(buf),
      "{\"state\":\"%s\",\"up_s\":%lu,\"rst\":\"%s\",\"bto\":%lu,\"disc\":%lu,\"rsn\":%lu,"
      "%s\"rssi_n\":%lu,\"wlk_pm\":%lu,\"api\":%lu,\"ha\":%lu,\"gap_s\":%lu,\"down_s\":%lu,"
      "\"nvs_w\":%lu,\"nvs_e\":%lu,\"nvs_max_ms\":%lu}",
      hibernating ? "hibernating" : "online",
      static_cast<unsigned long>(now_ms / 1000U),
      safe_rst,
      static_cast<unsigned long>(r.beacon_timeouts.load(std::memory_order_relaxed)),
      static_cast<unsigned long>(r.disconnects.load(std::memory_order_relaxed)),
      static_cast<unsigned long>(r.last_reason.load(std::memory_order_relaxed)),
      rssi_fields,
      static_cast<unsigned long>(r.rssi_samples),
      static_cast<unsigned long>(wlk),
      static_cast<unsigned long>(r.api_connects),
      static_cast<unsigned long>(r.ha_connects),
      static_cast<unsigned long>(gap_ms / 1000U),
      static_cast<unsigned long>(down_ms / 1000U),
      static_cast<unsigned long>(fw.nvs_writes.load(std::memory_order_relaxed)),
      static_cast<unsigned long>(fw.erases.load(std::memory_order_relaxed)),
      static_cast<unsigned long>((fw.nvs_max_us.load(std::memory_order_relaxed) + 999U) / 1000U));
  if (len < 0 || static_cast<size_t>(len) >= sizeof(buf)) {
    return hibernating ? "{\"state\":\"hibernating\"}" : "{\"state\":\"online\"}";
  }
  return std::string(buf, static_cast<size_t>(len));
}

}  // namespace latter_ui
