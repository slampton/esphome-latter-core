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

#include "esphome/core/log.h"

namespace latter_ui {
// Defined in ui_core.h (the Telemetry Profiler's flight recorder state).
inline bool &profiler_enabled();
}  // namespace latter_ui

#define LATTER_PROF_LOGI(...)                    \
  do {                                           \
    if (::latter_ui::profiler_enabled()) {       \
      ESP_LOGI("PROF", __VA_ARGS__);             \
    }                                            \
  } while (0)
