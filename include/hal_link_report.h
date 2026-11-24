#pragma once

// ==============================================================================
// Latter Core Link Report & Weak-Link RSSI Policy (include/hal_link_report.h)
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include <atomic>
#include <cstdint>
#include <string>

#include "hal_manifest.h"

namespace latter_core {

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

// Light-sleep RSSI gate: keeps chip awake when smoothed RSSI stays below kWeakLinkEnterDbm (-78 dBm
// default, validated with MAIN_XTAL_PU_DURING_LIGHT_SLEEP down to -72 avg / -80 dBm) across 2 samples
// after post-association AGC settle (5 s), and clears after holding above kWeakLinkClearDbm (-74 dBm
// default) for 2 minutes. Overridable via build_flags for RF bench tests.
#ifndef LATTER_WEAK_LINK_ENTER_DBM
#define LATTER_WEAK_LINK_ENTER_DBM (-78)
#endif
#ifndef LATTER_WEAK_LINK_CLEAR_DBM
#define LATTER_WEAK_LINK_CLEAR_DBM ((LATTER_WEAK_LINK_ENTER_DBM) + 4)
#endif
constexpr int32_t kWeakLinkEnterDbm = LATTER_WEAK_LINK_ENTER_DBM;
constexpr int32_t kWeakLinkClearDbm = LATTER_WEAK_LINK_CLEAR_DBM;
constexpr uint32_t kWeakLinkClearHoldMs = 120000U;
constexpr uint32_t kWeakLinkSampleMs = 10000U;
constexpr uint32_t kWeakLinkPostConnectSettleMs = 5000U;
constexpr uint8_t kWeakLinkEnterSamples = 2U;
constexpr float kWeakLinkEmaAlpha = 0.3f;

struct WeakLinkGate {
  bool weak{false};
  bool have{false};
  uint8_t below_count{0};
  float ema{0.0f};
  uint32_t last_sample_ms{0};
  uint32_t above_since_ms{0};
  uint32_t weak_since_ms{0};
  uint32_t weak_total_ms{0};
};

inline constexpr bool weak_link_can_sample(uint32_t now_ms, uint32_t last_wifi_connect_ms) {
  return last_wifi_connect_ms == 0U || (now_ms - last_wifi_connect_ms) >= kWeakLinkPostConnectSettleMs;
}

LinkReport &link_report();
void link_report_init();
void link_report_sample();
void link_report_api_connected(bool home_assistant, uint32_t now_ms);
void link_report_api_disconnected(bool home_assistant, uint32_t now_ms);
uint32_t link_report_permille(uint64_t part, uint64_t whole);
std::string link_report_line(uint32_t now_ms, const char *reset_reason);
std::string link_report_log_line(uint32_t now_ms, const char *reset_reason);
std::string link_report_json(uint32_t now_ms, const char *reset_reason, bool hibernating = false);

WeakLinkGate &weak_link_gate();
void weak_link_note_disconnect(uint32_t now_ms, WeakLinkGate &g = weak_link_gate());
bool weak_link_feed_rssi(int32_t rssi, uint32_t now_ms, WeakLinkGate &g = weak_link_gate());
bool weak_link_update(uint32_t now_ms, uint32_t last_wifi_connect_ms = 0U);
uint32_t weak_link_ms(uint32_t now_ms);

}  // namespace latter_core
