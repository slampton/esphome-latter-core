#pragma once

// ==============================================================================
// Latter HAL Hardware Capabilities & Manifest v2 Builders (hal_manifest.h)
// ==============================================================================
// GUI-agnostic hardware trait defaults, PMU boot telemetry, and Hardware Manifest v2
// JSON builders shared by headless latter_core and display builds.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#ifdef USE_ESP32
#include <sdkconfig.h>
#endif

#ifndef LATTER_SDK_REV
#define LATTER_SDK_REV 2026111401L
#endif

// Canonical Display Geometry & Hardware Capability Constants (LATTER_* standard)
#ifndef LATTER_BOARD_ID
#define LATTER_BOARD_ID "generic_esp32"
#endif
#ifndef LATTER_BLE_MINOR
#define LATTER_BLE_MINOR 1
#endif
#ifndef LATTER_SLOT_COUNT
#define LATTER_SLOT_COUNT 5
#endif
#ifndef LATTER_CARD_SLOT_COUNT
#define LATTER_CARD_SLOT_COUNT 3
#endif
#ifndef LATTER_SCREEN_W
#define LATTER_SCREEN_W 466
#endif
#ifndef LATTER_SCREEN_H
#define LATTER_SCREEN_H 466
#endif
#ifndef LATTER_ROUND_DISPLAY
#define LATTER_ROUND_DISPLAY 1
#endif
#ifndef LATTER_HAS_IMU
#define LATTER_HAS_IMU 1
#endif
#ifndef LATTER_HAS_BATTERY
#define LATTER_HAS_BATTERY 1
#endif
#ifndef LATTER_HAS_RTC
#define LATTER_HAS_RTC 0
#endif
#ifndef LATTER_IS_STATIONARY
#define LATTER_IS_STATIONARY 0
#endif
#ifndef LATTER_IS_WEARABLE
#define LATTER_IS_WEARABLE 0
#endif
#ifndef LATTER_HAS_TOUCH
#define LATTER_HAS_TOUCH 1
#endif
#ifndef LATTER_HAS_DISPLAY
#define LATTER_HAS_DISPLAY 1
#endif
#ifndef LATTER_HAS_AUDIO
#define LATTER_HAS_AUDIO 1
#endif
#ifndef LATTER_HAS_MIC
#define LATTER_HAS_MIC LATTER_HAS_AUDIO
#endif
#ifndef LATTER_HAS_SPEAKER
#define LATTER_HAS_SPEAKER LATTER_HAS_AUDIO
#endif
#ifndef LATTER_CHIP_P4
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_CHIP_P4 1
#else
#define LATTER_CHIP_P4 0
#endif
#endif
#ifndef LATTER_PSRAM_MB
#define LATTER_PSRAM_MB (LATTER_CHIP_P4 ? 32 : 8)
#endif
#ifndef LATTER_PANEL_IPS
#if defined(LATTER_BACKLIGHT_PIN)
#define LATTER_PANEL_IPS 1
#else
#define LATTER_PANEL_IPS 0
#endif
#endif
#ifndef LATTER_PANEL_PPI
#define LATTER_PANEL_PPI 266
#endif
#ifndef LATTER_AUDIO_OUT_RATE
#define LATTER_AUDIO_OUT_RATE 48000
#endif
#ifndef LATTER_AUDIO_OUT_CH
#define LATTER_AUDIO_OUT_CH 1
#endif
#ifndef LATTER_AUDIO_OUT_BITS
#define LATTER_AUDIO_OUT_BITS 16
#endif
#ifndef LATTER_AUDIO_IN_RATE
#define LATTER_AUDIO_IN_RATE 16000
#endif
#ifndef LATTER_BACKLIGHT_PWM_HZ
#define LATTER_BACKLIGHT_PWM_HZ 2000
#endif
#ifndef LATTER_BACKLIGHT_PWM_BITS
#define LATTER_BACKLIGHT_PWM_BITS 8
#endif
#ifndef LATTER_BACKLIGHT_DUTY_MIN_PCT
#define LATTER_BACKLIGHT_DUTY_MIN_PCT 0
#endif
#ifndef LATTER_BACKLIGHT_DUTY_MAX_PCT
#define LATTER_BACKLIGHT_DUTY_MAX_PCT 100
#endif
#ifndef LATTER_PWR_KEY_DEEP_SLEEP_WAKE
#define LATTER_PWR_KEY_DEEP_SLEEP_WAKE 0
#endif
#ifndef LATTER_PWR_KEY_ACTIVE_HIGH
#define LATTER_PWR_KEY_ACTIVE_HIGH 1
#endif
#ifndef LATTER_PMU_TS_DISABLE
#define LATTER_PMU_TS_DISABLE 0
#endif

namespace latter_ui {

inline constexpr int kManifestProtocolContractVersion = 3;

// PMIC telemetry shared by the PMU driver and the profiler.
struct PmuTelemetry {
  int gauge_raw = -1;
  bool boot_valid = false;
  uint8_t on_src = 0;
  uint8_t off_src = 0;
  uint8_t cv_reg = 0;
  int icc_ma = 0;
  float die_temp_c = 0.0f;
  bool thermal_foldback_active = false;
};

inline PmuTelemetry &pmu_telemetry() {
  static PmuTelemetry t;
  return t;
}

#ifdef USE_ESP32
uint32_t device_boot_id();
const char *last_reset_reason();
bool crash_record_present();
bool firmware_rolled_back();
#else
inline uint32_t device_boot_id() { return 1U; }
inline const char *last_reset_reason() { return "host"; }
inline bool crash_record_present() { return false; }
inline bool firmware_rolled_back() { return false; }
#endif

inline std::string build_hardware_manifest_json() {
  const int disp_w = LATTER_HAS_DISPLAY ? LATTER_SCREEN_W : 0;
  const int disp_h = LATTER_HAS_DISPLAY ? LATTER_SCREEN_H : 0;
  const char *shape = !LATTER_HAS_DISPLAY ? "none" : (LATTER_ROUND_DISPLAY ? "circle" : "square");
  const char *chip = LATTER_CHIP_P4 ? "ESP32-P4" : "ESP32-S3";
  char audio_buf[96];
  if (LATTER_HAS_AUDIO) {
    std::snprintf(audio_buf, sizeof(audio_buf),
                  "{\"out_rate\":%d,\"out_ch\":%d,\"out_bits\":%d,\"in_rate\":%d}",
                  (int)LATTER_AUDIO_OUT_RATE, (int)LATTER_AUDIO_OUT_CH,
                  (int)LATTER_AUDIO_OUT_BITS, (int)LATTER_AUDIO_IN_RATE);
  } else {
    std::snprintf(audio_buf, sizeof(audio_buf), "0");
  }
  char buf[640];
  std::snprintf(buf, sizeof(buf),
                "{\"v\":2,\"contract\":%d,\"fw\":\"3.0.0\",\"board\":\"%s\",\"boot_id\":%lu,"
                "\"w\":%d,\"h\":%d,\"shape\":\"%s\","
                "\"display\":%d,\"touch\":%d,\"audio\":%s,\"mic\":%d,\"spk\":%d,"
                "\"imu\":%d,\"battery\":%d,\"has_rtc\":%d,\"stationary\":%d,\"wearable\":%d,"
                "\"ips\":%d,\"chip\":\"%s\",\"psram_mb\":%d,"
                "\"slot_count\":%d,\"card_slot_count\":%d,\"max_payload_bytes\":2048,"
                "\"beacon\":{\"uuid\":\"4C415454-4552-434F-5245-000000000220\",\"major\":1,\"minor\":%d},"
                "\"sdk\":%ld,\"reset\":\"%s\",\"crash\":%d,\"rollback\":%d}",
                kManifestProtocolContractVersion, LATTER_BOARD_ID, (unsigned long)device_boot_id(),
                disp_w, disp_h, shape,
                (int)LATTER_HAS_DISPLAY, (int)LATTER_HAS_TOUCH,
                audio_buf, (int)LATTER_HAS_MIC, (int)LATTER_HAS_SPEAKER,
                (int)LATTER_HAS_IMU, (int)LATTER_HAS_BATTERY, (int)LATTER_HAS_RTC,
                (int)LATTER_IS_STATIONARY, (int)LATTER_IS_WEARABLE,
                (int)LATTER_PANEL_IPS, chip, (int)LATTER_PSRAM_MB,
                (int)LATTER_SLOT_COUNT, (int)LATTER_CARD_SLOT_COUNT, (int)LATTER_BLE_MINOR,
                (long)LATTER_SDK_REV, last_reset_reason(), (int)crash_record_present(), (int)firmware_rolled_back());
  return std::string(buf);
}

// Compact <=230-byte Hardware Manifest v2 state for Home Assistant's 255-char entity state limit
inline std::string build_hardware_manifest_state_json() {
  const int disp_w = LATTER_HAS_DISPLAY ? LATTER_SCREEN_W : 0;
  const int disp_h = LATTER_HAS_DISPLAY ? LATTER_SCREEN_H : 0;
  const char *shape = !LATTER_HAS_DISPLAY ? "none" : (LATTER_ROUND_DISPLAY ? "circle" : "square");
  const char *chip = LATTER_CHIP_P4 ? "ESP32-P4" : "ESP32-S3";
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\"v\":2,\"contract\":%d,\"board\":\"%s\","
                "\"w\":%d,\"h\":%d,\"shape\":\"%s\","
                "\"display\":%d,\"touch\":%d,\"audio\":%d,\"mic\":%d,\"spk\":%d,"
                "\"imu\":%d,\"battery\":%d,\"stationary\":%d,\"wearable\":%d,"
                "\"ips\":%d,\"chip\":\"%s\",\"psram_mb\":%d,\"minor\":%d}",
                kManifestProtocolContractVersion, LATTER_BOARD_ID,
                disp_w, disp_h, shape,
                (int)LATTER_HAS_DISPLAY, (int)LATTER_HAS_TOUCH,
                (int)LATTER_HAS_AUDIO, (int)LATTER_HAS_MIC, (int)LATTER_HAS_SPEAKER,
                (int)LATTER_HAS_IMU, (int)LATTER_HAS_BATTERY,
                (int)LATTER_IS_STATIONARY, (int)LATTER_IS_WEARABLE,
                (int)LATTER_PANEL_IPS, chip, (int)LATTER_PSRAM_MB,
                (int)LATTER_BLE_MINOR);
  return std::string(buf);
}

}  // namespace latter_ui
