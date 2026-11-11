#pragma once

// ==============================================================================
// Latter [PROF:*] logging gate (latter_prof.h)
// ==============================================================================
// [PROF:*] diagnostics log at INFO only while "System: Telemetry Profiler" is on, so a device at the
// default INFO level stays quiet in steady state. Use LATTER_PROF_LOGI(fmt, ...) for every [PROF:*]
// INFO line. Exempt (plain ESP_LOGI("PROF", ...)): [PROF:BOOT], the once-per-boot summary, and
// [PROF:LINK], the Link Report. Genuine warnings ([PROF:LOWWATER], [PROF:STALL], [PROF:AUDIO_STALL],
// IMU / battery faults) use ESP_LOGW("PROF", ...) and always log. latter-verify [PROF GATE] enforces this.
// ==============================================================================

#include <cstdint>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#else
#ifndef ESP_LOGI
#define ESP_LOGI(tag, ...) ((void)(tag))
#endif
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
inline bool &profiler_enabled() {
  static bool s_enabled = false;
  return s_enabled;
}
}  // namespace latter_ui

#define LATTER_PROF_LOGI(...)                    \
  do {                                           \
    if (::latter_ui::profiler_enabled()) {       \
      ESP_LOGI("PROF", __VA_ARGS__);             \
    }                                            \
  } while (0)
