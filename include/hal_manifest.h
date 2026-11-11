#pragma once

// ==============================================================================
// Latter HAL Hardware Capabilities, Manifest & Stall Guard Declarations (hal_manifest.h)
// ==============================================================================
// GUI-agnostic hardware trait defaults, Hardware Manifest v2 JSON builders, and
// StallGuard / reset diagnostics shared by headless latter_core and display builds.
// ==============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <esp_attr.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#ifdef USE_ESP32_CRASH_HANDLER
#include "esphome/components/esp32/crash_handler.h"
#endif

#ifndef LATTER_SDK_REV
#define LATTER_SDK_REV 2026111036L
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

struct HardwareCapabilities {
  bool is_round = (LATTER_ROUND_DISPLAY != 0);
  bool has_display = (LATTER_HAS_DISPLAY != 0);
  bool has_touch = (LATTER_HAS_TOUCH != 0);
  bool has_audio = (LATTER_HAS_AUDIO != 0);
  bool has_mic = (LATTER_HAS_MIC != 0);
  bool has_speaker = (LATTER_HAS_SPEAKER != 0);
  bool has_imu = (LATTER_HAS_IMU != 0);
  bool has_battery = (LATTER_HAS_BATTERY != 0);
  bool is_stationary = (LATTER_IS_STATIONARY != 0);
  bool is_wearable = (LATTER_IS_WEARABLE != 0);
  bool is_ips_panel = (LATTER_PANEL_IPS != 0);
  bool is_chip_p4 = (LATTER_CHIP_P4 != 0);
  int psram_mb = LATTER_PSRAM_MB;
};

inline constexpr HardwareCapabilities caps() { return HardwareCapabilities{}; }

// PMIC telemetry shared by the PMU module and the profiler.
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

uint32_t device_boot_id();

// ------------------------------------------------------------------------------
// Main-loop stall guard (compiled implementation in components/latter_hal/power_pm.cpp)
// ------------------------------------------------------------------------------
constexpr uint32_t kStallAbortMs = 30000U;
constexpr uint32_t kStallReportMs = 2000U;
constexpr uint32_t kStallRecordMagic = 0x5354414CU;  // "STAL"
constexpr uint32_t kStallLiveMagic = 0x53544C56U;    // "STLV"
constexpr uint32_t kTwdtRecordMagic = 0x54574454U;   // "TWDT"

struct StallRecord {
  uint32_t magic;
  uint32_t blocked_ms;
  uint32_t uptime_s;
  char source[40];
  char cpu0[16];
  char cpu1[16];
  char loop_state;
};

struct StallGuardState {
  StallGuardState();

  std::atomic<bool> paused{false};
  std::atomic<uint32_t> resumed_ms{0};
  bool started{false};
  StallRecord previous_boot{};
  StallRecord previous_live{};
  StallRecord previous_twdt{};
  StallRecord longest{};
#ifdef USE_ESP32
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif
};

StallGuardState &stall_guard();
bool previous_boot_stalled();
void stall_guard_pause(bool paused);
void start_stall_guard();
void log_stall_summary();

const char *last_reset_reason();
bool crash_record_present();
bool firmware_rolled_back();

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
                "\"imu\":%d,\"battery\":%d,\"stationary\":%d,\"wearable\":%d,"
                "\"ips\":%d,\"chip\":\"%s\",\"psram_mb\":%d,"
                "\"slot_count\":%d,\"max_payload_bytes\":2048,"
                "\"beacon\":{\"uuid\":\"4C415454-4552-434F-5245-000000000220\",\"major\":1,\"minor\":%d},"
                "\"sdk\":%ld,\"reset\":\"%s\",\"crash\":%d,\"rollback\":%d}",
                kManifestProtocolContractVersion, LATTER_BOARD_ID, (unsigned long)device_boot_id(),
                disp_w, disp_h, shape,
                (int)LATTER_HAS_DISPLAY, (int)LATTER_HAS_TOUCH,
                audio_buf, (int)LATTER_HAS_MIC, (int)LATTER_HAS_SPEAKER,
                (int)LATTER_HAS_IMU, (int)LATTER_HAS_BATTERY,
                (int)LATTER_IS_STATIONARY, (int)LATTER_IS_WEARABLE,
                (int)LATTER_PANEL_IPS, chip, (int)LATTER_PSRAM_MB,
                (int)LATTER_SLOT_COUNT, (int)LATTER_BLE_MINOR,
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
