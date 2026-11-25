#pragma once

// ==============================================================================
// Latter HAL Hardware Capabilities & Manifest v2 Builders (hal_manifest.h)
// ==============================================================================
// GUI-agnostic hardware trait defaults, PMU boot telemetry, and Hardware Manifest v2
// JSON builders shared by screenless latter_core and display builds.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "latter_traits.h"
#include "state/state_tile_grid.h"

#ifndef LATTER_CORE_REV
#define LATTER_CORE_REV 2026112404L
#endif

namespace latter_core {

inline constexpr int kManifestProtocolContractVersion = 3;
inline constexpr uint16_t kVideoMaxViewportDim = static_cast<uint16_t>(LATTER_VIDEO_MAX_VIEWPORT_DIM);

inline constexpr const char *detect_chip_name() noexcept {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return "ESP32-P4";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  return "ESP32-S2";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  return "ESP32-C6";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  return "ESP32-C3";
#elif defined(CONFIG_IDF_TARGET_ESP32)
  return "ESP32";
#elif LATTER_CHIP_P4
  return "ESP32-P4";
#elif LATTER_SOC_S3
  return "ESP32-S3";
#else
  return "ESP32-S3";
#endif
}

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
  const int grid_cols = LATTER_HAS_DISPLAY
                            ? tile_grid_cols_for(disp_w, disp_h, LATTER_ROUND_DISPLAY != 0)
                            : 0;
  const int grid_rows = LATTER_HAS_DISPLAY
                            ? tile_grid_rows_for(disp_w, disp_h, LATTER_ROUND_DISPLAY != 0)
                            : 0;
  const char *shape = !LATTER_HAS_DISPLAY ? "none" : (LATTER_ROUND_DISPLAY ? "circle" : "square");
  const char *chip = detect_chip_name();
#if defined(USE_ESP32) && !defined(CONFIG_SPIRAM)
  const int psram_mb = 0;
#else
  const int psram_mb = static_cast<int>(LATTER_PSRAM_MB);
#endif
  const int video_hw_jpeg = (LATTER_HAS_VIDEO_STREAM && LATTER_HAS_HW_JPEG) ? 1 : 0;
  const int video_max_dim = static_cast<int>(kVideoMaxViewportDim);
  const int video_max_w = LATTER_HAS_VIDEO_STREAM
                              ? (((LATTER_SCREEN_W < video_max_dim) ? LATTER_SCREEN_W : video_max_dim) & ~15)
                              : 0;
  const int video_max_h = LATTER_HAS_VIDEO_STREAM
                              ? (((LATTER_SCREEN_H < video_max_dim) ? LATTER_SCREEN_H : video_max_dim) & ~15)
                              : 0;
  const int video_max_fps =
      !LATTER_HAS_VIDEO_STREAM ? 0 : (LATTER_HAS_HW_JPEG ? (LATTER_HAS_BATTERY ? 10 : 15) : 5);
  const int video_grid_fps =
      !LATTER_HAS_VIDEO_STREAM ? 0 : (LATTER_HAS_HW_JPEG ? (LATTER_HAS_BATTERY ? 5 : 8) : 2);
  char audio_buf[96];
  if (LATTER_HAS_AUDIO) {
    std::snprintf(audio_buf, sizeof(audio_buf),
                  "{\"out_rate\":%d,\"out_ch\":%d,\"out_bits\":%d,\"in_rate\":%d}",
                  static_cast<int>(LATTER_AUDIO_OUT_RATE), static_cast<int>(LATTER_AUDIO_OUT_CH),
                  static_cast<int>(LATTER_AUDIO_OUT_BITS), static_cast<int>(LATTER_AUDIO_IN_RATE));
  } else {
    std::snprintf(audio_buf, sizeof(audio_buf), "0");
  }
  char beacon_buf[112];
#if defined(USE_LATTER_BEACON)
  std::snprintf(beacon_buf, sizeof(beacon_buf),
                "{\"uuid\":\"4C415454-4552-434F-5245-000000000220\",\"major\":1,\"minor\":%d}",
                static_cast<int>(LATTER_BLE_MINOR));
#else
  std::snprintf(beacon_buf, sizeof(beacon_buf), "null");
#endif
  char buf[1024];
  const int len = std::snprintf(
      buf, sizeof(buf),
      "{\"v\":2,\"contract\":%d,\"fw\":\"3.0.0\",\"board\":\"%s\",\"boot_id\":%lu,"
      "\"w\":%d,\"h\":%d,\"shape\":\"%s\","
      "\"display\":%d,\"touch\":%d,\"audio\":%s,\"mic\":%d,\"spk\":%d,"
      "\"imu\":%d,\"battery\":%d,\"has_rtc\":%d,\"stationary\":%d,\"wearable\":%d,"
      "\"ips\":%d,\"chip\":\"%s\",\"psram_mb\":%d,"
      "\"video\":{\"hw_jpeg\":%d,\"max_w\":%d,\"max_h\":%d,\"max_fps\":%d,\"grid_fps\":%d},"
      "\"slot_count\":%d,\"card_slot_count\":%d,"
      "\"grid_cols\":%d,\"grid_rows\":%d,"
      "\"beacon\":%s,"
      "\"core_rev\":%ld,\"reset\":\"%s\",\"crash\":%d,\"rollback\":%d}",
      kManifestProtocolContractVersion, LATTER_BOARD_ID, static_cast<unsigned long>(device_boot_id()),
      disp_w, disp_h, shape,
      static_cast<int>(LATTER_HAS_DISPLAY), static_cast<int>(LATTER_HAS_TOUCH),
      audio_buf, static_cast<int>(LATTER_HAS_MIC), static_cast<int>(LATTER_HAS_SPEAKER),
      static_cast<int>(LATTER_HAS_IMU), static_cast<int>(LATTER_HAS_BATTERY), static_cast<int>(LATTER_HAS_RTC),
      static_cast<int>(LATTER_IS_STATIONARY), static_cast<int>(LATTER_IS_WEARABLE),
      static_cast<int>(LATTER_PANEL_IPS), chip, psram_mb,
      video_hw_jpeg, video_max_w, video_max_h, video_max_fps, video_grid_fps,
      static_cast<int>(LATTER_SLOT_COUNT), static_cast<int>(LATTER_CARD_SLOT_COUNT),
      grid_cols, grid_rows,
      beacon_buf,
      static_cast<long>(LATTER_CORE_REV), last_reset_reason(),
      static_cast<int>(crash_record_present()), static_cast<int>(firmware_rolled_back()));
  if (len < 0 || static_cast<size_t>(len) >= sizeof(buf)) {
    return "{}";
  }
  return std::string(buf, static_cast<size_t>(len));
}

}  // namespace latter_core
