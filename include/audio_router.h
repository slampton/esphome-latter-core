#pragma once

// ==============================================================================
// Latter Core Audio Router, I2S Clock Multiplexer & Wake-Word Arbitrator (audio_router.h)
// ==============================================================================
// Centralizes half-duplex I2S clock routing (symbolic ESP-IDF GPIO matrix indices
// from soc/gpio_sig_map.h), wake-word run-state arbitration, and mixer ducking
// calculation across all Latter display and voice hardware targets.
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#ifdef USE_ESP32
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#endif

namespace latter_ui {

// Routes shared BCLK, WS/LRCLK, and MCLK pins to I2S1 (ES8311 Speaker DAC) on
// dual-codec ESP32-S3 hardware using symbolic IDF GPIO matrix signal constants.
inline void hal_connect_s3_i2s_clocks_to_spk(uint32_t bclk_pin, uint32_t ws_pin, uint32_t mclk_pin) {
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
  esp_rom_gpio_connect_out_signal(bclk_pin, I2S1O_BCK_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(ws_pin, I2S1O_WS_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(mclk_pin, I2S1_MCLK_OUT_IDX, false, false);
#else
  (void) bclk_pin;
  (void) ws_pin;
  (void) mclk_pin;
#endif
}

// Routes shared BCLK, WS/LRCLK, and MCLK pins to I2S0 (ES7210 Microphone ADC) on
// dual-codec ESP32-S3 hardware using symbolic IDF GPIO matrix signal constants.
inline void hal_connect_s3_i2s_clocks_to_mic(uint32_t bclk_pin, uint32_t ws_pin, uint32_t mclk_pin) {
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
  esp_rom_gpio_connect_out_signal(bclk_pin, I2S0I_BCK_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(ws_pin, I2S0I_WS_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(mclk_pin, I2S0_MCLK_OUT_IDX, false, false);
#else
  (void) bclk_pin;
  (void) ws_pin;
  (void) mclk_pin;
#endif
}

// Canonical single-source predicate for whether on-board micro_wake_word should run.
// Shared by sync_wake_word_state (audio_voice.yaml) and the 1s supervisor (power_manager.yaml).
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

// Converts the 20..100% Alert Volume slider value into mixer decibel reduction.
inline float alert_volume_to_ducking_db(float alert_vol_pct, float scale = 0.25f) {
  float v = alert_vol_pct;
  if (std::isnan(v) || v <= 0.0f) v = 80.0f;
  v = std::clamp(v, 20.0f, 100.0f);
  return (100.0f - v) * scale;
}

// Transitions ES8311 DAC (0x18) and optional ES7210 ADC (0x40) between low-power standby and active mode.
template <typename I2cBusT>
inline void hal_set_es8311_es7210_standby(I2cBusT *bus, bool standby, bool has_es7210 = true) {
  if (bus == nullptr) return;
#if defined(LATTER_IS_STATIONARY) && LATTER_IS_STATIONARY
  // Mains-powered stationary displays keep ES8311/ES7210 in their ESPHome setup() state continuously
  (void)standby;
  (void)has_es7210;
  return;
#else
  // Codecs boot in active state (0) after ESPHome component setup(); initialize last_stby = 0
  // so boot-time hal_set_audio_codecs_standby(false) never overwrites ES7210's setup() registers.
  static int last_stby = 0;
  const int target = standby ? 1 : 0;
  if (target == last_stby) return;
  last_stby = target;
  if (standby) {
    uint8_t dac_lp[2] = {0x0D, 0xFC};
    bus->write(0x18, dac_lp, 2);
    if (has_es7210) {
      uint8_t adc_lp1[2] = {0x06, 0x7F};
      uint8_t adc_lp2[2] = {0x40, 0xFF};
      bus->write(0x40, adc_lp1, 2);
      bus->write(0x40, adc_lp2, 2);
    }
  } else {
    uint8_t dac_wake[2] = {0x0D, 0x01};
    bus->write(0x18, dac_wake, 2);
    if (has_es7210) {
      uint8_t adc_wake1[2] = {0x40, 0x43};
      uint8_t adc_wake2[2] = {0x06, 0x00};
      bus->write(0x40, adc_wake1, 2);
      bus->write(0x40, adc_wake2, 2);
    }
  }
#endif
}

template <typename I2cBusT>
inline void hal_set_es8311_es7210_standby(I2cBusT *const *bus_pp, bool standby, bool has_es7210 = true) {
  if (bus_pp != nullptr) hal_set_es8311_es7210_standby(*bus_pp, standby, has_es7210);
}

// Powers down QMI8658 IMU (0x6B), ES8311 DAC (0x18), and optional ES7210 ADC (0x40) prior to hibernate.
template <typename I2cBusT>
inline void hal_power_down_audio_and_imu_for_hibernate(I2cBusT *bus, bool has_imu, bool has_es7210 = true) {
  if (bus == nullptr) return;
  if (has_imu) {
    uint8_t qmi_pwr_down[2] = {0x08, 0x00};
    bus->write(0x6B, qmi_pwr_down, 2);
  }
  uint8_t dac_sleep1[2] = {0x0D, 0xFF};
  uint8_t dac_sleep2[2] = {0x00, 0x80};
  bus->write(0x18, dac_sleep1, 2);
  bus->write(0x18, dac_sleep2, 2);
  if (has_es7210) {
    uint8_t es7210_pwr[2] = {0x00, 0xFF};
    bus->write(0x40, es7210_pwr, 2);
  }
}

template <typename I2cBusT>
inline void hal_power_down_audio_and_imu_for_hibernate(I2cBusT *const *bus_pp, bool has_imu, bool has_es7210 = true) {
  if (bus_pp != nullptr) hal_power_down_audio_and_imu_for_hibernate(*bus_pp, has_imu, has_es7210);
}

}  // namespace latter_ui
