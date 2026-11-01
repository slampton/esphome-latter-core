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

}  // namespace latter_ui
