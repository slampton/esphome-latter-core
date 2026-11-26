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
#include "state/device_runtime_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#ifndef LATTER_BRI_PWM_GAMMA_X100
#ifdef LATTER_BRI_GAMMA_X100
#define LATTER_BRI_PWM_GAMMA_X100 LATTER_BRI_GAMMA_X100
#else
#define LATTER_BRI_PWM_GAMMA_X100 220
#endif
#endif
#ifndef LATTER_BRI_DCS_GAMMA_X100
#ifdef LATTER_BRI_GAMMA_X100
#define LATTER_BRI_DCS_GAMMA_X100 LATTER_BRI_GAMMA_X100
#else
#define LATTER_BRI_DCS_GAMMA_X100 150
#endif
#endif
#ifndef LATTER_BRI_DCS_MIN
#define LATTER_BRI_DCS_MIN 10
#endif
#ifndef LATTER_BRI_DCS_MAX
#define LATTER_BRI_DCS_MAX 255
#endif

#ifdef USE_ESP32
#include <driver/gpio.h>
#if defined(LATTER_BACKLIGHT_PIN)
#include <driver/ledc.h>
#endif
#include <sdkconfig.h>
#endif
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

namespace latter_core {

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

// Maps perceptual brightness fraction pct in [0.0f, 1.0f] onto hardware range [hw_min, hw_max]
// using a CIE/Weber-Fechner power curve (gamma = 2.20 for linear PWM IPS; 1.50 for DCS AMOLED).
// pct <= 0.0001f returns 0 (screen off); any positive pct maps into [max(1u, hw_min), hw_max].
inline uint32_t hal_map_perceptual_u32(float pct, uint32_t hw_min, uint32_t hw_max,
                                       float gamma = static_cast<float>(LATTER_BRI_PWM_GAMMA_X100) / 100.0f) {
  if (pct <= 0.0001f || hw_max == 0U) return 0U;
  if (hw_min > hw_max) hw_min = hw_max;
  const float clamped = std::clamp(pct, 0.01f, 1.0f);
  const float t = (clamped - 0.01f) / 0.99f;
  const float g = (gamma > 0.05f) ? gamma : 1.0f;
  const float curve = std::pow(t, g);
  const float raw = static_cast<float>(hw_min) + static_cast<float>(hw_max - hw_min) * curve;
  const uint32_t floor_val = (hw_min > 1U) ? hw_min : 1U;
  const uint32_t rounded = static_cast<uint32_t>(std::lround(raw));
  return std::clamp(rounded, floor_val, hw_max);
}

inline constexpr uint32_t hal_backlight_max_duty(uint32_t pwm_bits) {
  return (1UL << pwm_bits) - 1UL;
}

// duty_min_pm / duty_max_pm are in permille (0..1000 = 0.0%..100.0%) so 12-bit PWM FET boards
// can express sub-percent night floors (e.g. 3 permille = 0.3% = 12/4095) cleanly.
inline uint32_t hal_map_pwm_on_duty(float pct, uint32_t pwm_bits, uint32_t duty_min_pm, uint32_t duty_max_pm,
                                    float gamma = static_cast<float>(LATTER_BRI_PWM_GAMMA_X100) / 100.0f) {
  if (pct <= 0.0001f) return 0U;
  const uint32_t max_duty = hal_backlight_max_duty(pwm_bits);
  const uint32_t on_min = (max_duty * duty_min_pm) / 1000UL;
  const uint32_t on_max = (max_duty * duty_max_pm) / 1000UL;
  return hal_map_perceptual_u32(pct, on_min, on_max, gamma);
}

inline uint8_t hal_map_dcs_brightness(float pct,
                                      uint8_t dcs_min = static_cast<uint8_t>(LATTER_BRI_DCS_MIN),
                                      uint8_t dcs_max = static_cast<uint8_t>(LATTER_BRI_DCS_MAX),
                                      float gamma = static_cast<float>(LATTER_BRI_DCS_GAMMA_X100) / 100.0f) {
  return static_cast<uint8_t>(hal_map_perceptual_u32(pct, dcs_min, dcs_max, gamma));
}

#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_PIN)
inline void hal_ledc_apply_on_duty(uint32_t on_duty, bool is_on) {
  constexpr uint32_t kPwmBits = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_BITS);
  static_assert(kPwmBits >= 8 && kPwmBits <= 13, "LATTER_BACKLIGHT_PWM_BITS must be 8..13");
  constexpr uint32_t kMaxDuty = hal_backlight_max_duty(kPwmBits);
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
  static bool ledc_warned = false;
  static int64_t last_hw_duty = -1;
  if (!ledc_timer_inited) {
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = static_cast<ledc_timer_bit_t>(kPwmBits);
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_HZ);
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    const esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err == ESP_OK) {
      ledc_timer_inited = true;
    } else if (!ledc_warned) {
      ledc_warned = true;
#ifdef ESP_LOGW
      ESP_LOGW("power", "Backlight LEDC timer init failed (err=%d, hz=%lu, bits=%lu)",
               static_cast<int>(err), static_cast<unsigned long>(LATTER_BACKLIGHT_PWM_HZ),
               static_cast<unsigned long>(kPwmBits));
#endif
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
      const esp_err_t err = ledc_channel_config(&ledc_channel);
      if (err == ESP_OK) {
        ledc_channel_inited = true;
        last_hw_duty = hw_duty;
        LATTER_PROF_LOGI("[PROF:PANEL] bri_pwm init on=%lu/%lu hw=%lu",
                         static_cast<unsigned long>(on_duty), static_cast<unsigned long>(kMaxDuty),
                         static_cast<unsigned long>(hw_duty));
      } else if (!ledc_warned) {
        ledc_warned = true;
#ifdef ESP_LOGW
        ESP_LOGW("power", "Backlight LEDC channel init failed (err=%d, pin=%d)",
                 static_cast<int>(err), static_cast<int>(LATTER_BACKLIGHT_PIN));
#endif
      }
    } else if (last_hw_duty != static_cast<int64_t>(hw_duty)) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      last_hw_duty = hw_duty;
      LATTER_PROF_LOGI("[PROF:PANEL] bri_pwm on=%lu/%lu hw=%lu",
                       static_cast<unsigned long>(on_duty), static_cast<unsigned long>(kMaxDuty),
                       static_cast<unsigned long>(hw_duty));
    }
  } else {
    gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), is_on ? gpio_on_lvl : gpio_off_lvl);
  }
}
#endif

inline constexpr int hal_backlight_en_gpio_level(
    bool is_on, bool en_inverted = (LATTER_BACKLIGHT_EN_INVERTED != 0)) noexcept {
  return en_inverted ? (is_on ? 0 : 1) : (is_on ? 1 : 0);
}

inline uint8_t hal_backlight_float_to_pct(float pct) noexcept {
  if (pct <= 0.0001f) return 0U;
  const float clamped = std::clamp(pct, 0.0f, 1.0f);
  const long rounded = std::lround(clamped * 100.0f);
  return static_cast<uint8_t>(std::clamp<long>(rounded, 1L, 100L));
}

template <bool CustomBacklight, typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void hal_set_display_brightness_impl(DispT *disp, float pct) {
  [[maybe_unused]] const bool is_on = (pct > 0.0001f);
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_EN_PIN)
  static int s_last_en = -1;
  const int want_en = hal_backlight_en_gpio_level(is_on);
  if (s_last_en != want_en) {
    gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), want_en);
    s_last_en = want_en;
  }
#endif
  if constexpr (CustomBacklight) {
    if (auto fn = runtime_state().hw_set_backlight_level; fn != nullptr) {
      fn(hal_backlight_float_to_pct(pct));
    }
#if defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
    const uint8_t dcs_wake = is_on
        ? static_cast<uint8_t>(std::max<uint32_t>(1U, static_cast<uint32_t>(std::lround(std::clamp(pct, 0.0f, 1.0f) * 255.0f))))
        : static_cast<uint8_t>(0U);
    if (disp != nullptr) hal_panel_dcs_set_brightness(disp, dcs_wake);
#else
    (void)disp;
#endif
  } else {
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_PIN)
    constexpr uint32_t kPwmBits = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_BITS);
    static_assert(kPwmBits >= 8 && kPwmBits <= 13, "LATTER_BACKLIGHT_PWM_BITS must be 8..13");
    static_assert(LATTER_BACKLIGHT_DUTY_MIN_PM >= 0 && LATTER_BACKLIGHT_DUTY_MIN_PM < LATTER_BACKLIGHT_DUTY_MAX_PM &&
                      LATTER_BACKLIGHT_DUTY_MAX_PM <= 1000,
                  "LATTER_BACKLIGHT_DUTY_MIN_PM/_MAX_PM must satisfy 0 <= MIN < MAX <= 1000");
    const uint32_t on_duty = hal_map_pwm_on_duty(
        pct, kPwmBits, static_cast<uint32_t>(LATTER_BACKLIGHT_DUTY_MIN_PM),
        static_cast<uint32_t>(LATTER_BACKLIGHT_DUTY_MAX_PM));
    hal_ledc_apply_on_duty(on_duty, is_on);
#if defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
    const uint8_t dcs_wake = is_on
        ? static_cast<uint8_t>(std::max<uint32_t>(1U, static_cast<uint32_t>(std::lround(std::clamp(pct, 0.0f, 1.0f) * 255.0f))))
        : static_cast<uint8_t>(0U);
    if (disp != nullptr) hal_panel_dcs_set_brightness(disp, dcs_wake);
#else
    (void)disp;
#endif
#else
    static_assert(requires(DispT *d, uint8_t v) { d->set_brightness(v); },
                  "Display driver lacks set_brightness() and neither LATTER_BACKLIGHT_PIN nor LATTER_BACKLIGHT_CUSTOM is configured");
    const uint8_t bri_byte = hal_map_dcs_brightness(pct);
    if (disp != nullptr) hal_panel_dcs_set_brightness(disp, bri_byte);
#endif
  }
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void hal_set_display_brightness_f(DispT *disp, float pct) {
  hal_set_display_brightness_impl<(LATTER_BACKLIGHT_CUSTOM != 0)>(disp, pct);
}

template <typename DispT>
inline void hal_set_display_brightness_f(DispT *const *disp_pp, float pct) {
  if (disp_pp != nullptr) hal_set_display_brightness_f(*disp_pp, pct);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void hal_set_display_brightness(DispT *disp, uint8_t bri_byte) {
  hal_set_display_brightness_f(disp, (bri_byte == 0U) ? 0.0f : (static_cast<float>(bri_byte) / 255.0f));
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

}  // namespace latter_core
