#pragma once

// Latter Core main-loop probe for [PROF:WAKE] (include/latter_prof.h) & paced RPC outbox drain.

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "audio_router.h"
#include "hal_panel_power.h"
#include "hal_sleep_pm.h"
#include "hal_touch.h"
#include "latter_prof.h"
#include "latter_rpc.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace latter_core {

class LatterLoopProbe : public ::esphome::Component {
 public:
  LatterLoopProbe() { this->bind_timeout_bridge_(); }

  void setup() override { this->bind_timeout_bridge_(); }

  void schedule_timeout(uint32_t id, uint32_t delay_ms, std::function<void()> &&fn) {
    this->set_timeout(id, delay_ms, std::move(fn));
  }

  void cancel_scheduled_timeout(uint32_t id) {
    this->cancel_timeout(id);
  }

  void loop() override {
    LoopProbeStats &s = loop_probe_stats();
    const uint32_t now = ::esphome::millis();
    pm_poll_loop(now);
    if (s.last_ms != 0) {
      const uint32_t gap = now - s.last_ms;
      const int b = gap < 20U ? 0 : gap < 50U ? 1 : gap < 200U ? 2 : gap < 1000U ? 3 : 4;
      s.gaps[b]++;
    }
    s.last_ms = now;
    s.passes++;
    if (::esphome::HighFrequencyLoopRequester::is_high_frequency()) s.hf_passes++;

    // Drain paced multi-chunk RPC outbox frames & deterministic audio path deadlines with zero busy-spinning.
    const bool more_rpc = ::latter_rpc::step_outbox_loop(now);
    if (more_rpc) {
      if (!this->rpc_drain_armed_) {
        const uint32_t delay_ms = std::max<uint32_t>(1U, ::latter_rpc::outbox_next_delay_ms(now));
        this->rpc_drain_armed_ = true;
        this->set_timeout(::latter_rpc::kTimeoutRpcDrainId, delay_ms, [this]() { this->rpc_drain_armed_ = false; });
      }
    } else if (this->rpc_drain_armed_) {
      this->cancel_timeout(::latter_rpc::kTimeoutRpcDrainId);
      this->rpc_drain_armed_ = false;
    }
    audio_arbitrator().poll_loop(now);
    const bool need_hf = audio_arbitrator().needs_fast_loop();
    if (need_hf && !this->rpc_hf_held_) {
      this->rpc_hf_.start();
      this->rpc_hf_held_ = true;
    } else if (!need_hf && this->rpc_hf_held_) {
      this->rpc_hf_.stop();
      this->rpc_hf_held_ = false;
    }
  }
  float get_setup_priority() const override { return ::esphome::setup_priority::LATE; }

 protected:
  void bind_timeout_bridge_() noexcept {
    core_timeout_bridge().bind(
        this,
        +[](void *ctx, uint32_t id, uint32_t delay_ms, std::function<void()> &&fn) {
          static_cast<LatterLoopProbe *>(ctx)->schedule_timeout(id, delay_ms, std::move(fn));
        },
        +[](void *ctx, uint32_t id) {
          static_cast<LatterLoopProbe *>(ctx)->cancel_scheduled_timeout(id);
        });
  }

  ::esphome::HighFrequencyLoopRequester rpc_hf_{};
  bool rpc_hf_held_{false};
  bool rpc_drain_armed_{false};
};

}  // namespace latter_core
