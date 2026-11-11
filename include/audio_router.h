#pragma once

// ==============================================================================
// Latter Core Audio Router & Wake-Word Arbitrator (audio_router.h)
// ==============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

#include "hal_types.h"

namespace latter_ui {

std::atomic<uint32_t> &echo_last_loud_ms();
std::atomic<bool> &echo_meter_active();

inline constexpr int64_t kEchoLoudMeanSquare = 33LL * 33LL;

// Zero-cost fast return outside an active VA reply echo-gate window.
inline void echo_meter_feed(const uint8_t *data, size_t len) {
  if (!echo_meter_active().load(std::memory_order_relaxed)) return;
  const size_t n = len / 2;
  if (n == 0) return;
  const int16_t *s = reinterpret_cast<const int16_t *>(data);
  int64_t acc = 0;
  for (size_t i = 0; i < n; i++) acc += static_cast<int32_t>(s[i]) * s[i];
  if (acc / static_cast<int64_t>(n) > kEchoLoudMeanSquare) {
    echo_last_loud_ms().store(esphome::millis(), std::memory_order_relaxed);
  }
}

inline bool wake_word_should_run(const std::string &ww_mode,
                                 bool usb_on,
                                 bool va_busy,
                                 bool media_busy,
                                 bool alert_or_tool_busy,
                                 bool sys_busy) {
  const bool mode_allowed = (ww_mode == "Always On") ||
                            (ww_mode == "Docked Only" && usb_on);
  return mode_allowed && !va_busy && !media_busy && !alert_or_tool_busy && !sys_busy;
}

// Must match volume_min / volume_max of latter_media_player in modules/audio_voice.yaml (latter-verify).
inline constexpr float kMediaVolumeMin = 0.10f;
inline constexpr float kMediaVolumeMax = 1.00f;

inline float media_dac_volume(float player_volume) {
  if (std::isnan(player_volume)) player_volume = 0.60f;
  if (player_volume < 0.001f) return 0.0f;
  const float v = std::clamp(player_volume, 0.0f, 1.0f);
  return kMediaVolumeMin + v * (kMediaVolumeMax - kMediaVolumeMin);
}

inline float alarm_output_volume(float alert_vol_pct, float media_volume) {
  float a = alert_vol_pct;
  if (std::isnan(a) || a <= 0.0f) a = 80.0f;
  a = std::clamp(a, 20.0f, 100.0f);
  const float level = 0.90f - ((100.0f - a) * 0.18f * 2.0f / 255.0f);
  return std::clamp(std::max(level, media_dac_volume(media_volume)), 0.10f, 1.0f);
}

inline float voice_output_volume(float media_volume) {
  return std::clamp(media_dac_volume(media_volume) + (18.0f / 255.0f), 0.50f, 1.0f);
}

#ifdef USE_I2C
void hal_set_es8311_es7210_standby(esphome::i2c::I2CBus *bus, bool standby, bool has_es7210 = true);
void hal_power_down_audio_and_imu_for_hibernate(esphome::i2c::I2CBus *bus, bool has_imu, bool has_es7210 = true);

template <typename I2cBusT>
inline void hal_set_es8311_es7210_standby(I2cBusT *const *bus_pp, bool standby, bool has_es7210 = true) {
  if (bus_pp != nullptr) hal_set_es8311_es7210_standby(*bus_pp, standby, has_es7210);
}

template <typename I2cBusT>
inline void hal_power_down_audio_and_imu_for_hibernate(I2cBusT *const *bus_pp, bool has_imu, bool has_es7210 = true) {
  if (bus_pp != nullptr) hal_power_down_audio_and_imu_for_hibernate(*bus_pp, has_imu, has_es7210);
}

class Es8311AudioControl final : public latter_hal::IAudioControl {
 public:
  explicit Es8311AudioControl(esphome::i2c::I2CBus *bus = nullptr, bool has_es7210 = true, bool has_imu = false)
      : bus_(bus), has_es7210_(has_es7210), has_imu_(has_imu) {}
  void configure(esphome::i2c::I2CBus *bus, bool has_es7210, bool has_imu) {
    bus_ = bus;
    has_es7210_ = has_es7210;
    has_imu_ = has_imu;
  }

  void set_standby(bool standby) override { hal_set_es8311_es7210_standby(bus_, standby, has_es7210_); }
  void power_down_for_hibernate() override {
    hal_power_down_audio_and_imu_for_hibernate(bus_, has_imu_, has_es7210_);
  }

 private:
  esphome::i2c::I2CBus *bus_{nullptr};
  bool has_es7210_{true};
  bool has_imu_{false};
};

inline Es8311AudioControl &es8311_audio_driver() {
  static Es8311AudioControl s_drv;
  return s_drv;
}
#endif  // USE_I2C

}  // namespace latter_ui
