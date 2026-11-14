#pragma once

// Latter Core main-loop probe for [PROF:WAKE] (include/latter_prof.h) & paced RPC outbox drain.

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "hal_power_pm.h"
#include "latter_prof.h"
#include "latter_rpc.h"

#include <cstdint>

namespace esphome::latter_core {

class LatterLoopProbe : public Component {
 public:
  void loop() override {
    ::esphome::latter_core::LoopProbeStats &s = ::esphome::latter_core::loop_probe_stats();
    const uint32_t now = millis();
    ::latter_ui::pm_poll_loop(now);
    if (s.last_ms != 0) {
      const uint32_t gap = now - s.last_ms;
      const int b = gap < 20U ? 0 : gap < 50U ? 1 : gap < 200U ? 2 : gap < 1000U ? 3 : 4;
      s.gaps[b]++;
    }
    s.last_ms = now;
    s.passes++;
    if (HighFrequencyLoopRequester::is_high_frequency()) s.hf_passes++;

    // Drain paced multi-chunk RPC outbox frames with zero timers while idle.
    const bool more_rpc = ::latter_rpc::step_outbox_loop(now);
    if (more_rpc && !this->rpc_hf_held_) {
      this->rpc_hf_.start();
      this->rpc_hf_held_ = true;
    } else if (!more_rpc && this->rpc_hf_held_) {
      this->rpc_hf_.stop();
      this->rpc_hf_held_ = false;
    }
  }
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  HighFrequencyLoopRequester rpc_hf_{};
  bool rpc_hf_held_{false};
};

}  // namespace esphome::latter_core
