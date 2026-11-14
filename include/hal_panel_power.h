#pragma once

// ==============================================================================
// Latter HAL: Display Backlight LEDC, DCS Panel Sleep & Touch Monitor Mode
// (hal_panel_power.h)
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/component.h")
#include "esphome/core/component.h"
#define LATTER_HAS_ESPHOME_COMPONENT 1
#else
#define LATTER_HAS_ESPHOME_COMPONENT 0
#endif
#if defined(USE_ESP32) && __has_include("esphome/core/hal.h")
#include "esphome/core/hal.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#ifdef USE_ESP32
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <sdkconfig.h>
#endif
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

namespace latter_ui {

// Reserved numeric timeout IDs dispatched via LatterLoopProbe::schedule_timeout (CoreTimeoutBridge):
//   0x4C410001U..0x4C41000FU : UI compositor frame/weather/activity schedulers (include/ui_compositor.h)
//   0x4C410010U..0x4C41001FU : HAL panel power & MIPI-DCS SLPOUT/DISPON state machine (this file)
inline constexpr uint32_t kTimeoutPanelDcsId = 0x4C410010U;

struct CoreTimeoutBridge {
  void *ctx{nullptr};
  void (*schedule_fn)(void *ctx, uint32_t id, uint32_t delay_ms, std::function<void()> &&fn){nullptr};
  void (*cancel_fn)(void *ctx, uint32_t id){nullptr};

  void bind(void *c,
            void (*sched)(void *, uint32_t, uint32_t, std::function<void()> &&),
            void (*cancel)(void *, uint32_t)) noexcept {
    ctx = c;
    schedule_fn = sched;
    cancel_fn = cancel;
  }

  void reset() noexcept {
    ctx = nullptr;
    schedule_fn = nullptr;
    cancel_fn = nullptr;
  }
};

inline CoreTimeoutBridge &core_timeout_bridge() {
  static CoreTimeoutBridge b{};
  return b;
}

inline void schedule_core_timeout(uint32_t id, uint32_t delay_ms, std::function<void()> &&fn) {
  auto &b = core_timeout_bridge();
  if (b.schedule_fn != nullptr && fn) {
    b.schedule_fn(b.ctx, id, delay_ms, std::move(fn));
  }
}

inline void cancel_core_timeout(uint32_t id) {
  auto &b = core_timeout_bridge();
  if (b.cancel_fn != nullptr) {
    b.cancel_fn(b.ctx, id);
  }
}

struct PanelDcsState {
  enum Phase : uint8_t { AWAKE, WAKING, ASLEEP };
  Phase phase = AWAKE;     // at boot the panel's init sequence has already sent SLPOUT + DISPON
  uint8_t want_bri = 0;    // latest requested brightness byte (0 = screen off)
  uint32_t slpout_ms = 0;  // millis() of the last SLPOUT (0 = none since boot)
};

inline PanelDcsState &panel_dcs_state() {
  static PanelDcsState s;
  return s;
}

inline constexpr uint32_t kPanelDcsSlpoutMs = 120U;

// Milliseconds until the last SLPOUT has settled for state `st` (0 = settled).
inline constexpr uint32_t panel_dcs_settle_left_for(const PanelDcsState &st, uint32_t now_ms) {
  if (st.slpout_ms == 0) return 0U;
  const uint32_t since = now_ms - st.slpout_ms;
  return (since >= kPanelDcsSlpoutMs) ? 0U : (kPanelDcsSlpoutMs - since);
}

// Milliseconds until the last SLPOUT has settled (0 = settled).
inline uint32_t panel_dcs_settle_left_ms(uint32_t now_ms) {
  return panel_dcs_settle_left_for(panel_dcs_state(), now_ms);
}

struct PanelDcsStepResult {
  bool send_slpout = false;
  bool send_dispon = false;
  bool send_dispoff = false;
  bool send_slpin = false;
  bool apply_hw_bri = false;
  uint8_t hw_bri = 0;
  uint32_t rearm_ms = 0;
};

// Pure state-machine transition for MIPI-DCS panel sleep/wake sequencing.
inline constexpr PanelDcsStepResult step_panel_dcs_state(PanelDcsState &st, uint32_t now_ms) {
  PanelDcsStepResult r{};
  if (st.want_bri > 0) {
    if (st.phase == PanelDcsState::ASLEEP) {
      r.send_slpout = true;
      st.slpout_ms = (now_ms != 0U) ? now_ms : 1U;
      st.phase = PanelDcsState::WAKING;
      r.rearm_ms = kPanelDcsSlpoutMs;
      return r;
    }
    if (st.phase == PanelDcsState::WAKING) {
      const uint32_t left = panel_dcs_settle_left_for(st, now_ms);
      if (left > 0U) {
        r.rearm_ms = left;
        return r;
      }
      r.send_dispon = true;
      st.phase = PanelDcsState::AWAKE;
    }
    r.apply_hw_bri = true;
    r.hw_bri = st.want_bri;
    return r;
  }
  if (st.phase == PanelDcsState::ASLEEP) return r;
  r.apply_hw_bri = true;
  r.hw_bri = 0;
  const uint32_t left = panel_dcs_settle_left_for(st, now_ms);
  if (left > 0U) {
    r.rearm_ms = left;
    return r;
  }
  if (st.phase == PanelDcsState::AWAKE) r.send_dispoff = true;
  r.send_slpin = true;
  st.phase = PanelDcsState::ASLEEP;
  return r;
}

// Moves the panel one step towards panel_dcs_state().want_bri; re-arms itself while a DCS wait runs.
// Supports both self-emissive QSPI AMOLEDs (cmd 0x02, 24-bit addr = DCS << 8 + set_brightness)
// and 1-lane SPI IPS panels with LATTER_PANEL_DCS_SLEEP=1 (e.g. ST7796: 8-bit DCS 0x28/0x10/0x11/0x29).
template <typename DispT>
inline void hal_panel_dcs_service(DispT *disp) {
  if (disp == nullptr) return;
  auto &st = panel_dcs_state();
  auto apply_hw_bri = [disp](uint8_t b) {
    if constexpr (requires(DispT *d, uint8_t v) { d->set_brightness(v); }) {
      disp->set_brightness(b);
    } else {
      (void)b;
    }
  };
#if defined(USE_ESP32) && LATTER_HAS_ESPHOME_COMPONENT && defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
  if constexpr (requires(DispT *d, const uint8_t *b) {
                  d->enable();
                  d->disable();
                  d->write_cmd_addr_data(size_t{8}, uint32_t{0x02}, size_t{24}, uint32_t{0}, b, size_t{0});
                }) {
    if (!disp->is_ready()) {
      apply_hw_bri(st.want_bri);
      return;
    }
    auto dcs = [disp](uint8_t cmd) {
      uint8_t dummy = cmd;
#if LATTER_PANEL_IPS
      static_assert(
          false
#if defined(LATTER_DISPLAY_DC_PIN)
              || true
#endif
          ,
          "1-lane SPI panel with LATTER_PANEL_DCS_SLEEP=1 must define LATTER_DISPLAY_DC_PIN so DCX=0 is asserted");
#if defined(LATTER_DISPLAY_DC_PIN)
      gpio_set_level(static_cast<gpio_num_t>(LATTER_DISPLAY_DC_PIN), 0);
#endif
      disp->enable();
      disp->write_cmd_addr_data(8, static_cast<uint32_t>(cmd), 0, 0, &dummy, 0);
      disp->disable();
#else
      disp->enable();
      disp->write_cmd_addr_data(8, 0x02, 24, static_cast<uint32_t>(cmd) << 8, &dummy, 0);
      disp->disable();
#endif
    };
    auto service_in = [disp](uint32_t delay_ms) {
      schedule_core_timeout(kTimeoutPanelDcsId, delay_ms, [disp]() { hal_panel_dcs_service(disp); });
    };
    const PanelDcsStepResult res = step_panel_dcs_state(st, hal_millis_now());
    if (res.send_slpout) {
      dcs(0x11);
      // MIPI-DCS exit_sleep_mode (0x11): allow >= 5 ms oscillator/charge-pump lock before Frame #1 GRAM writes.
      esphome::delay(5);
    }
    if (res.send_dispon) {
      dcs(0x29);
      LATTER_PROF_LOGI("[PROF:PANEL] dcs=wake");
    }
    if (res.apply_hw_bri) {
      apply_hw_bri(res.hw_bri);
    }
    if (res.send_dispoff) {
      dcs(0x28);
    }
    if (res.send_slpin) {
      dcs(0x10);
      LATTER_PROF_LOGI("[PROF:PANEL] dcs=sleep");
    }
    if (res.rearm_ms > 0U) {
      service_in(res.rearm_ms);
    }
  } else {
    static bool warned = false;
    if (!warned) {
      warned = true;
      ESP_LOGW("power", "LATTER_PANEL_DCS_SLEEP set but display driver exposes no SPI/QSPI command path; panel sleep disabled");
    }
    apply_hw_bri(st.want_bri);
  }
#else
  apply_hw_bri(st.want_bri);
#endif
}

template <typename DispT>
inline void hal_panel_dcs_set_brightness(DispT *disp, uint8_t bri_byte) {
  panel_dcs_state().want_bri = bri_byte;
  hal_panel_dcs_service(disp);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void hal_set_display_brightness(DispT *disp, uint8_t bri_byte) {
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_EN_PIN)
  gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), (bri_byte > 0) ? 1 : 0);
#endif
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_PIN)
  constexpr uint32_t kPwmBits = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_BITS);
  static_assert(kPwmBits >= 8 && kPwmBits <= 13, "LATTER_BACKLIGHT_PWM_BITS must be 8..13");
  static_assert(LATTER_BACKLIGHT_DUTY_MIN_PCT >= 0 && LATTER_BACKLIGHT_DUTY_MIN_PCT < LATTER_BACKLIGHT_DUTY_MAX_PCT &&
                    LATTER_BACKLIGHT_DUTY_MAX_PCT <= 100,
                "LATTER_BACKLIGHT_DUTY_MIN_PCT/_MAX_PCT must satisfy 0 <= MIN < MAX <= 100");
  constexpr uint32_t kMaxDuty = (1UL << kPwmBits) - 1UL;
  constexpr uint32_t kOnMin = (kMaxDuty * static_cast<uint32_t>(LATTER_BACKLIGHT_DUTY_MIN_PCT)) / 100UL;
  constexpr uint32_t kOnMax = (kMaxDuty * static_cast<uint32_t>(LATTER_BACKLIGHT_DUTY_MAX_PCT)) / 100UL;
  const uint32_t on_duty = (bri_byte == 0)
      ? 0UL
      : kOnMin + (((kOnMax - kOnMin) * static_cast<uint32_t>(bri_byte)) + 127UL) / 255UL;
#if defined(LATTER_BACKLIGHT_INVERTED) && LATTER_BACKLIGHT_INVERTED
  const uint32_t hw_duty = kMaxDuty - on_duty;
  const int gpio_on_lvl = 0;
  const int gpio_off_lvl = 1;
#else
  const uint32_t hw_duty = on_duty;
  const int gpio_on_lvl = 1;
  const int gpio_off_lvl = 0;
#endif
  static bool ledc_timer_inited = false;
  static bool ledc_channel_inited = false;
  static int64_t last_hw_duty = -1;
  if (!ledc_timer_inited) {
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = static_cast<ledc_timer_bit_t>(kPwmBits);
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_HZ);
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&ledc_timer) == ESP_OK) {
      ledc_timer_inited = true;
    }
  }
  if (ledc_timer_inited) {
    if (!ledc_channel_inited) {
      ledc_channel_config_t ledc_channel = {};
      ledc_channel.gpio_num = LATTER_BACKLIGHT_PIN;
      ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
      ledc_channel.channel = LEDC_CHANNEL_0;
      ledc_channel.intr_type = LEDC_INTR_DISABLE;
      ledc_channel.timer_sel = LEDC_TIMER_0;
      ledc_channel.duty = hw_duty;
      ledc_channel.hpoint = 0;
      if (ledc_channel_config(&ledc_channel) == ESP_OK) {
        ledc_channel_inited = true;
        last_hw_duty = hw_duty;
      }
    } else if (last_hw_duty != static_cast<int64_t>(hw_duty)) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      last_hw_duty = hw_duty;
    }
  } else {
    gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), (bri_byte > 0) ? gpio_on_lvl : gpio_off_lvl);
  }
#if defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
  if (disp != nullptr) hal_panel_dcs_set_brightness(disp, bri_byte);
#else
  (void)disp;
#endif
#else
  static_assert(requires(DispT *d, uint8_t v) { d->set_brightness(v); },
                "Display driver lacks set_brightness() and no LATTER_BACKLIGHT_PIN is defined");
  if (disp != nullptr) hal_panel_dcs_set_brightness(disp, bri_byte);
#endif
}

template <typename DispT>
inline void hal_set_display_brightness(DispT *const *disp_pp, uint8_t bri) {
  if (disp_pp != nullptr) hal_set_display_brightness(*disp_pp, bri);
}

// Completes a pending DCS transition now, waiting out the SLPOUT settle time (OTA begin/end only).
template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void hal_panel_dcs_flush(DispT *disp) {
#if defined(USE_ESP32) && defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
  const auto &st = panel_dcs_state();
  const bool pending = (st.phase == PanelDcsState::WAKING) ||
                       (st.want_bri == 0 && st.phase != PanelDcsState::ASLEEP);
  if (!pending || disp == nullptr) return;
  const uint32_t left = panel_dcs_settle_left_ms(hal_millis_now());
  if (left > 0) esphome::delay(left);
  hal_panel_dcs_service(disp);
#else
  (void)disp;
#endif
}

template <typename DispT>
inline void hal_panel_dcs_flush(DispT *const *disp_pp) {
  if (disp_pp != nullptr) hal_panel_dcs_flush(*disp_pp);
}

// Non-blocking self-healing poll: completes any settled pending DCS transition (e.g. WAKING -> DISPON)
// without stalling the main loop, guarding against dropped or cancelled Component timeouts.
template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void hal_panel_dcs_poll(DispT *disp) {
#if defined(USE_ESP32) && defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
  const auto &st = panel_dcs_state();
  const bool pending = (st.phase == PanelDcsState::WAKING) ||
                       (st.want_bri == 0 && st.phase != PanelDcsState::ASLEEP);
  if (pending && disp != nullptr && panel_dcs_settle_left_ms(hal_millis_now()) == 0U) {
    hal_panel_dcs_service(disp);
  }
#else
  (void)disp;
#endif
}

template <typename DispT>
inline void hal_panel_dcs_poll(DispT *const *disp_pp) {
  if (disp_pp != nullptr) hal_panel_dcs_poll(*disp_pp);
}

// Switches FocalTech FT6336/FT3168 touch controllers (0x38) into low-power Period Monitor mode (REG 0x86=0x01)
// during screen-off standby on battery (~2.5 mA -> ~0.15 mA while preserving INT tap-to-wake), and back to
// active scan on wake. Safe no-op on other touch ICs if 0x38 does not ACK.
#ifdef USE_I2C
void hal_set_touch_sleep_mode_bus(esphome::i2c::I2CBus *bus, bool sleep);

template <typename I2cBusT>
  requires(!std::is_pointer_v<I2cBusT>)
inline void hal_set_touch_sleep_mode(I2cBusT *bus, bool sleep) {
  hal_set_touch_sleep_mode_bus(bus, sleep);
}

template <typename I2cBusT>
inline void hal_set_touch_sleep_mode(I2cBusT *const *bus_pp, bool sleep) {
  if (bus_pp != nullptr) hal_set_touch_sleep_mode_bus(*bus_pp, sleep);
}
#endif

// Pulses the touch controller hardware reset line cleanly before a warm software restart (or post-boot retry)
// so capacitive touch controllers (e.g. CST9217/CST9220) exit active finger-scan/I2C streaming cleanly.
inline void hal_pulse_touch_reset_pin(uint32_t settle_after_ms = 0U) {
#if defined(USE_ESP32) && defined(LATTER_TOUCH_RST_PIN)
  const auto rst_pin = static_cast<gpio_num_t>(LATTER_TOUCH_RST_PIN);
  gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(rst_pin, 0);
  esphome::delay(25);
  gpio_set_level(rst_pin, 1);
  if (settle_after_ms > 0U) esphome::delay(settle_after_ms);
#else
  (void)settle_after_ms;
#endif
}

inline void hal_prepare_touch_for_restart() {
  hal_pulse_touch_reset_pin(0U);
}

// Re-runs touchscreen setup (up to 2 attempts, >= 2 s apart) if post-reset I2C init failed during boot.
template <typename TouchT>
  requires(!std::is_pointer_v<TouchT>)
inline void hal_recover_touchscreen_if_failed(TouchT *ts, uint32_t now_ms, bool reset_counter = false) {
  static uint8_t s_attempts = 0;
  static uint32_t s_last_attempt_ms = 0;
  if (reset_counter) {
    s_attempts = 0;
    s_last_attempt_ms = 0;
    return;
  }
#if !defined(USE_ESP32) || (LATTER_HAS_ESPHOME_COMPONENT && LATTER_HAS_TOUCH)
  if (ts == nullptr || !ts->is_failed() || now_ms <= 1500U) return;
  if (s_attempts >= 2 || (s_last_attempt_ms != 0U && (now_ms - s_last_attempt_ms) < 2000U)) return;
  ++s_attempts;
  s_last_attempt_ms = now_ms;
#ifdef ESP_LOGW
  ESP_LOGW("touch", "Touch controller failed at boot; re-running setup (attempt %u)", static_cast<unsigned>(s_attempts));
#endif
  hal_pulse_touch_reset_pin(10U);
  ts->status_clear_error();
  ts->reset_to_construction_state();
  ts->call();
  ts->disable_loop();
  ts->enable_loop();
#else
  (void)ts;
  (void)now_ms;
#endif
}

template <typename TouchT>
inline void hal_recover_touchscreen_if_failed(TouchT *const *ts_pp, uint32_t now_ms, bool reset_counter = false) {
  if (ts_pp != nullptr) hal_recover_touchscreen_if_failed(*ts_pp, now_ms, reset_counter);
}

}  // namespace latter_ui
