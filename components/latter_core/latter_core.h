#pragma once

// Latter Core main-loop probe for [PROF:WAKE] (include/hal_power_pm.h, Telemetry Profiler).
// A do-nothing component whose loop() counts ESPHome's component-phase passes: how often they run, the
// gaps between them, and how many ran while a HighFrequencyLoopRequester was held (which keeps the main
// loop from sleeping between passes). Counting only: no work, no logging, no locks.

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <cstdint>

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

class LatterLoopProbe : public Component {
 public:
  void loop() override {
    LoopProbeStats &s = loop_probe_stats();
    const uint32_t now = millis();
    if (s.last_ms != 0) {
      const uint32_t gap = now - s.last_ms;
      const int b = gap < 20U ? 0 : gap < 50U ? 1 : gap < 200U ? 2 : gap < 1000U ? 3 : 4;
      s.gaps[b]++;
    }
    s.last_ms = now;
    s.passes++;
    if (HighFrequencyLoopRequester::is_high_frequency()) s.hf_passes++;
  }
  float get_setup_priority() const override { return setup_priority::LATE; }
};

}  // namespace esphome::latter_core
