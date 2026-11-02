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

// Loudness model for alerts and voice replies, applied once through the DAC volume (the announcement
// mixer input is never ducked). ESPHome's ES8311 volume is dB-linear: volume v -> REG32 = v * 255,
// 0.5 dB per step, so 0.75 = 0 dB, 0.90 = +19 dB, 1.0 = +32 dB, and 1 dB = 2/255 of the scale.
//
// Every writer of physical_speaker's volume must use these helpers:
//   - esphome::rtttl re-applies its gain to the speaker chain at every song start, so alarm/chime paths
//     set the RTTTL gain to the intended DAC level before rtttl.play,
//   - speaker_source remaps its 0..1 volume into [volume_min, volume_max] before it reaches the DAC, so
//     music levels go through media_dac_volume().

// Must match volume_min / volume_max of latter_media_player in modules/audio_voice.yaml (latter-verify).
inline constexpr float kMediaVolumeMin = 0.10f;
inline constexpr float kMediaVolumeMax = 1.00f;

// The DAC level speaker_source applies for a media player volume.
inline float media_dac_volume(float player_volume) {
  if (std::isnan(player_volume)) player_volume = 0.60f;
  if (player_volume < 0.001f) return 0.0f;
  const float v = std::clamp(player_volume, 0.0f, 1.0f);
  return kMediaVolumeMin + v * (kMediaVolumeMax - kMediaVolumeMin);
}

// Alarms / timers / chimes: the Alert Volume slider sets the level; at 100% the DAC sits at 0.90 and
// each % below removes 0.18 dB (+15.4 dB at the 80% default). Never below the current music level.
inline float alarm_output_volume(float alert_vol_pct, float media_volume) {
  float a = alert_vol_pct;
  if (std::isnan(a) || a <= 0.0f) a = 80.0f;
  a = std::clamp(a, 20.0f, 100.0f);
  const float level = 0.90f - ((100.0f - a) * 0.18f * 2.0f / 255.0f);
  return std::clamp(std::max(level, media_dac_volume(media_volume)), 0.10f, 1.0f);
}

// Voice replies: the music level +3 dB, so a reply is a little louder than the music it interrupts and
// tracks the volume the user already chose.
inline float voice_output_volume(float media_volume) {
  return std::clamp(media_dac_volume(media_volume) + (6.0f / 255.0f), 0.50f, 1.0f);
}

// Transitions ES8311 DAC (0x18) and optional ES7210 ADC (0x40) between low-power standby and active mode,
// and re-synchronizes ES7210's state machine once I2S MCLK/LRCK clocks are active.
template <typename I2cBusT>
inline void hal_set_es8311_es7210_standby(I2cBusT *bus, bool standby, bool has_es7210 = true) {
  if (bus == nullptr) return;
#if defined(LATTER_IS_STATIONARY) && LATTER_IS_STATIONARY
  // Mains-powered stationary displays keep ES8311/ES7210 powered continuously.
  // Note: ESPHome's ES7210::setup() writes ES7210_POWER_ON at the end of setup(), which leaves
  // REG40=0xC3 (analog power down) and REG4B/REG4C=0x0F (MIC1-4 bias/ADC/PGA powered down).
  // Explicitly power on analog + MIC1-4 stages and re-sync CSM once I2S MCLK/LRCK are active.
  if (standby) return;
  if (has_es7210) {
    const uint8_t adc_wake_seq[][2] = {
        {0x01, 0x00},  // ES7210_CLOCK_OFF_REG01: enable all ADC/MIC clocks
        {0x06, 0x00},  // ES7210_POWER_DOWN_REG06: digital & DLL power on
        {0x40, 0x43},  // ES7210_ANALOG_REG40: analog power on, VDDA=3.3V, VMID=5kOhm
        {0x4B, 0x00},  // ES7210_MIC12_POWER_REG4B: MIC1/2 bias, ADC1/2, PGA1/2 on
        {0x4C, 0x00},  // ES7210_MIC34_POWER_REG4C: MIC3/4 bias, ADC3/4, PGA3/4 on
        {0x00, 0x71},  // ES7210_RESET_REG00: CSM reset
        {0x00, 0x41},  // ES7210_RESET_REG00: CSM active
    };
    for (const auto &reg : adc_wake_seq) {
      bus->write(0x40, reg, 2);
    }
  }
#else
  // Battery boards: full low-power suspend modeled on Espressif esp_codec_dev es8311_suspend() /
  // es7210_stop(). Registers the suspend sequence changes are snapshotted while the codecs are active
  // and restored verbatim on resume, so ESPHome's configured clocks/gains come back exactly.
  // REG00 digital resets are omitted (a snapshot restore cannot replay a reset), and the DAC volume
  // (REG32) is never touched because ESPHome may change it while the codecs are suspended.
  static int last_stby = -1;
  static constexpr uint8_t kEs8311SnapRegs[] = {0x01, 0x02, 0x0D, 0x0E, 0x12, 0x14, 0x15, 0x17, 0x45};
  static uint8_t es8311_snap[sizeof(kEs8311SnapRegs)] = {};
  static bool es8311_snap_valid = false;
  static constexpr uint8_t kEs7210SnapRegs[] = {0x47, 0x48, 0x49, 0x4A};
  static uint8_t es7210_snap[sizeof(kEs7210SnapRegs)] = {};
  static bool es7210_snap_valid = false;
  auto snapshot = [bus](uint8_t addr, const uint8_t *regs, size_t n, uint8_t *out) -> bool {
    for (size_t i = 0; i < n; i++) {
      uint8_t reg = regs[i];
      if (bus->write_readv(addr, &reg, 1, &out[i], 1) != esphome::i2c::ERROR_OK) return false;
    }
    return true;
  };
  const int target = standby ? 1 : 0;
  if (standby) {
    if (target == last_stby) return;
    last_stby = target;
    es8311_snap_valid = snapshot(0x18, kEs8311SnapRegs, sizeof(kEs8311SnapRegs), es8311_snap);
    if (es8311_snap_valid) {
      const uint8_t dac_suspend_seq[][2] = {
          {0x17, 0x00},  // ADC volume 0
          {0x0E, 0xFF},  // ADC/PGA analog power down
          {0x12, 0x02},  // DAC power down
          {0x14, 0x00},  // mic bias / PGA input off
          {0x0D, 0xFA},  // analog power down (stage 1)
          {0x15, 0x00},  // ADC ramp/HPF off
          {0x02, 0x10},  // clock divider hold
          {0x01, 0x30},  // clock manager: gate MCLK/BCLK paths
          {0x01, 0x00},  // all codec clocks off
          {0x45, 0x00},  // GP control off
          {0x0D, 0xFC},  // analog power down (VMID off)
          {0x02, 0x00},
      };
      for (const auto &reg : dac_suspend_seq) {
        bus->write(0x18, reg, 2);
      }
    } else {
      uint8_t dac_lp[2] = {0x0D, 0xFC};
      bus->write(0x18, dac_lp, 2);
    }
    if (has_es7210) {
      es7210_snap_valid = snapshot(0x40, kEs7210SnapRegs, sizeof(kEs7210SnapRegs), es7210_snap);
      if (es7210_snap_valid) {
        for (uint8_t reg = 0x47; reg <= 0x4A; reg++) {
          const uint8_t lp[2] = {reg, 0xFF};  // MIC1-4 low-power
          bus->write(0x40, lp, 2);
        }
      }
      const uint8_t adc_lp_seq[][2] = {
          {0x4B, 0xFF},  // MIC1/2 bias, ADC, PGA off
          {0x4C, 0xFF},  // MIC3/4 bias, ADC, PGA off
          {0x40, 0xC0},  // analog power down
          {0x01, 0x7F},  // all ADC/MIC clocks off
          {0x06, 0x07},  // digital power down
      };
      for (const auto &reg : adc_lp_seq) {
        bus->write(0x40, reg, 2);
      }
    }
  } else {
    if (last_stby != 0) {
      last_stby = 0;
      if (es8311_snap_valid) {
        for (size_t i = 0; i < sizeof(kEs8311SnapRegs); i++) {
          const uint8_t reg[2] = {kEs8311SnapRegs[i], es8311_snap[i]};
          bus->write(0x18, reg, 2);
        }
        es8311_snap_valid = false;
      } else {
        uint8_t dac_wake[2] = {0x0D, 0x01};
        bus->write(0x18, dac_wake, 2);
      }
      if (has_es7210) {
        if (es7210_snap_valid) {
          for (size_t i = 0; i < sizeof(kEs7210SnapRegs); i++) {
            const uint8_t reg[2] = {kEs7210SnapRegs[i], es7210_snap[i]};
            bus->write(0x40, reg, 2);
          }
          es7210_snap_valid = false;
        }
        const uint8_t adc_wake_seq[][2] = {
            {0x01, 0x00},  // ES7210_CLOCK_OFF_REG01: enable all ADC/MIC clocks
            {0x06, 0x00},  // ES7210_POWER_DOWN_REG06: digital & DLL power on
            {0x40, 0x43},  // ES7210_ANALOG_REG40: analog power on, VDDA=3.3V, VMID=5kOhm
            {0x4B, 0x00},  // ES7210_MIC12_POWER_REG4B: MIC1/2 bias, ADC1/2, PGA1/2 on
            {0x4C, 0x00},  // ES7210_MIC34_POWER_REG4C: MIC3/4 bias, ADC3/4, PGA3/4 on
        };
        for (const auto &reg : adc_wake_seq) {
          bus->write(0x40, reg, 2);
        }
      }
    }
    if (has_es7210) {
      uint8_t adc_csm_rst[2] = {0x00, 0x71};
      uint8_t adc_csm_en[2] = {0x00, 0x41};
      bus->write(0x40, adc_csm_rst, 2);
      bus->write(0x40, adc_csm_en, 2);
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
