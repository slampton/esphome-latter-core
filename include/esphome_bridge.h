#pragma once

// ==============================================================================
// Isolated ESPHome Version Gate, Bridge Declarations & OTA Lifecycle Helpers
// (include/esphome_bridge.h)
// ==============================================================================
// Declares the VoiceAssistant session/wake-word helpers, MipiDsi framebuffer
// getter, firmware update metadata readers, and OTA display lifecycle hooks.
// All Rob<> private-member accessors live exclusively inside
// components/latter_core/esphome_bridge.cpp behind ESPHOME_VERSION_CODE guards.
// ==============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/version.h")
#include "esphome/core/version.h"
#endif

#if defined(ESPHOME_VERSION_CODE) && defined(VERSION_CODE)
static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 11, 0),
              "Latter requires ESPHome 2026.11.0b4 or newer (devices/*.yaml min_version).");
#endif

#ifdef USE_ESP32
#include <esp_sleep.h>
#endif

#include "hal_panel_power.h"
#include "hal_power_pm.h"
#include "latter_state.h"

#ifndef LATTER_SDK_REV
#define LATTER_SDK_REV 2026111301L
#endif

namespace esphome {
namespace voice_assistant {
class VoiceAssistant;
}  // namespace voice_assistant
namespace micro_wake_word {
class MicroWakeWord;
}  // namespace micro_wake_word
namespace mipi_dsi {
class MipiDsi;
}  // namespace mipi_dsi
}  // namespace esphome

namespace esphome::voice_assistant {

#ifdef USE_VOICE_ASSISTANT
void stop_va_microphone(VoiceAssistant *va);
void abort_voice_assistant(VoiceAssistant *va);
void on_api_client_authenticated(VoiceAssistant *va, const std::string &client_info,
                                 const std::string &client_address);
void clean_disconnect_api_before_hibernate(VoiceAssistant *va);
#else
inline void stop_va_microphone(VoiceAssistant *) {}
inline void abort_voice_assistant(VoiceAssistant *) {}
inline void on_api_client_authenticated(VoiceAssistant *, const std::string &, const std::string &) {}
inline void clean_disconnect_api_before_hibernate(VoiceAssistant *) {}
#endif

#if defined(USE_VOICE_ASSISTANT) && defined(USE_MICRO_WAKE_WORD)
std::string primary_wake_word_name(micro_wake_word::MicroWakeWord *mww);
void cycle_primary_wake_word(VoiceAssistant *va, micro_wake_word::MicroWakeWord *mww);
#else
inline std::string primary_wake_word_name(micro_wake_word::MicroWakeWord *) { return "None"; }
inline void cycle_primary_wake_word(VoiceAssistant *, micro_wake_word::MicroWakeWord *) {}
#endif

}  // namespace esphome::voice_assistant

namespace latter_hal {
#if defined(USE_ESP32_VARIANT_ESP32P4) && __has_include("esphome/components/mipi_dsi/mipi_dsi.h")
uint8_t *get_mipi_dsi_buffer(esphome::mipi_dsi::MipiDsi *d);
#else
inline uint8_t *get_mipi_dsi_buffer(esphome::mipi_dsi::MipiDsi *) { return nullptr; }
#endif
}  // namespace latter_hal

namespace latter_ui {

template <typename UpdT>
  requires(!std::is_pointer_v<UpdT>)
inline std::string fw_update_latest_version(const UpdT *upd) {
  if (upd == nullptr) return "";
  if constexpr (requires { upd->update_info.latest_version; }) {
    return upd->update_info.latest_version;
  }
  return "";
}

template <typename UpdT>
inline std::string fw_update_latest_version(UpdT *const *upd_ptr) {
  return (upd_ptr != nullptr) ? fw_update_latest_version(*upd_ptr) : std::string{};
}

template <typename UpdT>
  requires(!std::is_pointer_v<UpdT>)
inline std::string fw_update_release_summary(const UpdT *upd) {
  if (upd == nullptr) return "";
  if constexpr (requires { upd->update_info.summary; }) {
    return upd->update_info.summary;
  }
  return "";
}

template <typename UpdT>
inline std::string fw_update_release_summary(UpdT *const *upd_ptr) {
  return (upd_ptr != nullptr) ? fw_update_release_summary(*upd_ptr) : std::string{};
}

inline bool is_newer_semver(const std::string &lat_str, const std::string &cur_str) {
  if (lat_str.empty() || lat_str == "unavailable" || lat_str == "unknown") return false;
  auto parse_ver = [](const std::string &raw, int &maj, int &min_v, int &pat) -> bool {
    const char *p = raw.c_str();
    if (*p == 'v' || *p == 'V') ++p;
    char *endp = nullptr;
    maj = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p || *endp != '.') return false;
    p = endp + 1;
    min_v = static_cast<int>(std::strtol(p, &endp, 10));
    if (endp == p) return false;
    pat = 0;
    if (*endp == '.') {
      p = endp + 1;
      pat = static_cast<int>(std::strtol(p, &endp, 10));
    }
    return true;
  };
  const char *lp = lat_str.c_str();
  if (*lp == 'v' || *lp == 'V') ++lp;
  if (std::strncmp(lp, "202", 3) == 0) return false;
  int l_maj = 0, l_min = 0, l_pat = 0;
  int c_maj = 0, c_min = 0, c_pat = 0;
  if (!parse_ver(lat_str, l_maj, l_min, l_pat)) return false;
  if (!parse_ver(cur_str, c_maj, c_min, c_pat)) return false;
  if (l_maj > c_maj) return true;
  if (l_maj < c_maj) return false;
  if (l_min > c_min) return true;
  if (l_min < c_min) return false;
  return l_pat > c_pat;
}

inline bool is_newer_semver(const std::string &lat_str, const char *cur_str) {
  return is_newer_semver(lat_str, std::string(cur_str != nullptr ? cur_str : ""));
}

inline bool &speaker_volume_boot_synced() {
  static bool synced = false;
  return synced;
}

inline bool &boot_speaker_off_done() {
  static bool done = false;
  return done;
}

// ==============================================================================
// Unified OTA Firmware Update Display & Poller Lifecycle Helpers
// ==============================================================================
template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void handle_ota_begin(DispT *disp, bool &ota_in_progress, int &ota_progress_pct,
                             uint32_t &sleep_start_time, int &display_power_state) {
  ota_in_progress = true;
  stall_guard_pause(true);
  wifi_power_ota_begin();
  ota_progress_pct = 0;
  sleep_start_time = 0;
  screen_is_off_standby() = false;
  disp_poller_held() = true;
  anim_poller_suppressed() = false;
  display_power_state = 0;
  if (disp != nullptr) {
    disp->stop_poller();
    hal_set_display_brightness(disp, 230);
    hal_panel_dcs_flush(disp);
  }
}

template <typename DispT>
inline void handle_ota_begin(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct,
                             uint32_t &sleep_start_time, int &display_power_state) {
  if (disp_ptr != nullptr) {
    handle_ota_begin(*disp_ptr, ota_in_progress, ota_progress_pct, sleep_start_time, display_power_state);
  }
}

template <typename DispT>
inline void handle_ota_begin(DispT *disp_or_ptr, DeviceRuntimeState &rt = runtime_state()) {
  handle_ota_begin(disp_or_ptr, rt.nav.ota_in_progress, rt.nav.ota_progress_pct,
                   rt.nav.sleep_start_time, rt.nav.display_power_state);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void handle_ota_progress(DispT *disp, float raw_pct, int &ota_progress_pct, uint32_t now_m) {
  static uint32_t last_ota_draw_ms = 0;
  int pct = static_cast<int>(raw_pct);
#if defined(LATTER_DSI_IRAM_SAFE)
  constexpr int min_step_pct = 10;
  constexpr uint32_t min_step_ms = 800U;
#else
  constexpr int min_step_pct = 5;
  constexpr uint32_t min_step_ms = 400U;
#endif
  if (pct != ota_progress_pct &&
      (pct == 100 || (pct - ota_progress_pct >= min_step_pct && (now_m - last_ota_draw_ms >= min_step_ms)) ||
       pct < ota_progress_pct)) {
    ota_progress_pct = pct;
    last_ota_draw_ms = now_m;
    if (disp != nullptr) disp->update();
  }
}

template <typename DispT>
inline void handle_ota_progress(DispT *const *disp_ptr, float raw_pct, int &ota_progress_pct, uint32_t now_m) {
  if (disp_ptr != nullptr) handle_ota_progress(*disp_ptr, raw_pct, ota_progress_pct, now_m);
}

template <typename DispT>
inline void handle_ota_progress(DispT *disp_or_ptr, float raw_pct, uint32_t now_m,
                                DeviceRuntimeState &rt = runtime_state()) {
  handle_ota_progress(disp_or_ptr, raw_pct, rt.nav.ota_progress_pct, now_m);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void handle_ota_end(DispT *disp, bool &ota_in_progress, int &ota_progress_pct) {
  if (ota_in_progress) {
    ota_progress_pct = 100;
  }
  disp_poller_held() = false;
  anim_poller_suppressed() = false;
  if (disp != nullptr) {
    disp->update();
    disp->start_poller();
    hal_set_display_brightness(disp, 0);
    hal_panel_dcs_flush(disp);
  }
  ota_in_progress = false;
  stall_guard_pause(false);
  wifi_power_ota_end();
}

template <typename DispT>
inline void handle_ota_end(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct) {
  if (disp_ptr != nullptr) handle_ota_end(*disp_ptr, ota_in_progress, ota_progress_pct);
}

template <typename DispT>
inline void handle_ota_end(DispT *disp_or_ptr, DeviceRuntimeState &rt = runtime_state()) {
  handle_ota_end(disp_or_ptr, rt.nav.ota_in_progress, rt.nav.ota_progress_pct);
}

template <typename DispT>
  requires(!std::is_pointer_v<DispT>)
inline void handle_ota_error(DispT *disp, bool &ota_in_progress, int &ota_progress_pct,
                             bool &notif_active, std::string &notif_title, std::string &notif_message,
                             std::string &notif_priority, uint32_t &notif_duration_ms,
                             uint32_t &notif_start_ms, uint32_t now_m) {
  ota_in_progress = false;
  ota_progress_pct = 0;
  notif_title = "Update failed";
  notif_message = "Firmware update interrupted";
  notif_priority = "warning";
  notif_duration_ms = 5000U;
  notif_start_ms = now_m;
  notif_active = true;
  handle_ota_end(disp, ota_in_progress, ota_progress_pct);
}

template <typename DispT>
inline void handle_ota_error(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct,
                             bool &notif_active, std::string &notif_title, std::string &notif_message,
                             std::string &notif_priority, uint32_t &notif_duration_ms,
                             uint32_t &notif_start_ms, uint32_t now_m) {
  if (disp_ptr != nullptr) {
    handle_ota_error(*disp_ptr, ota_in_progress, ota_progress_pct,
                     notif_active, notif_title, notif_message,
                     notif_priority, notif_duration_ms, notif_start_ms, now_m);
  }
}

template <typename DispT>
inline void handle_ota_error(DispT *disp_or_ptr, uint32_t now_m, DeviceRuntimeState &rt = runtime_state()) {
  handle_ota_error(disp_or_ptr, rt.nav.ota_in_progress, rt.nav.ota_progress_pct,
                   rt.alert.active, rt.alert.title, rt.alert.message,
                   rt.alert.priority, rt.alert.duration_ms, rt.alert.start_ms, now_m);
}

}  // namespace latter_ui
