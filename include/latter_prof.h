#pragma once

// ==============================================================================
// Latter [PROF:*] Flight Recorder, Memory Watch & Render Section Profiler
// (latter_prof.h)
// ==============================================================================
// [PROF:*] diagnostics log at INFO only while Telemetry Profiler is on (auto-disabling after 24 h),
// so a device at the default INFO level stays quiet in steady state. Use LATTER_PROF_LOGI(fmt, ...)
// for every [PROF:*] INFO line (including [PROF:BOOT] and [PROF:LINK]; zero always-on exemptions).
// Genuine warnings ([PROF:LOWWATER], [PROF:STALL], [PROF:AUDIO_STALL], [PROF:BOOT] light-sleep crash
// breadcrumbs, IMU / battery faults) use ESP_LOGW("PROF", ...) and always log.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if defined(USE_ESP32) && __has_include("esphome/core/hal.h")
#include "esphome/core/hal.h"
#endif
#if __has_include("esphome/core/log.h")
#include "esphome/core/log.h"
#endif

template <typename... Args>
inline void latter_host_log_sink(const char *, const char *, Args &&...) {}

#ifndef ESP_LOGI
#define ESP_LOGI(tag, fmt, ...) latter_host_log_sink(tag, fmt, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGW
#define ESP_LOGW(tag, fmt, ...) latter_host_log_sink(tag, fmt, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGD
#define ESP_LOGD(tag, fmt, ...) latter_host_log_sink(tag, fmt, ##__VA_ARGS__)
#endif
#ifndef ESP_LOGE
#define ESP_LOGE(tag, fmt, ...) latter_host_log_sink(tag, fmt, ##__VA_ARGS__)
#endif

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#endif

namespace latter_core {

struct LoopProbeStats {
  uint32_t passes{0};
  uint32_t hf_passes{0};  // passes with a HighFrequencyLoopRequester held
  uint32_t gaps[5]{};     // gap since the previous pass: <20 ms, <50 ms, <200 ms, <1 s, >=1 s
  uint32_t last_ms{0};
};

inline LoopProbeStats &loop_probe_stats() {
  static LoopProbeStats s;
  return s;
}

inline uint32_t hal_millis_now() {
#if defined(USE_ESP32) && __has_include("esphome/core/hal.h")
  return esphome::millis();
#elif defined(USE_ESP32)
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
#else
  return 0U;
#endif
}

inline constexpr uint32_t kProfilerDefaultTimeoutMs = 24UL * 3600UL * 1000UL;

struct ProfilerTimerState {
  uint32_t started_ms{0U};
  uint32_t timeout_ms{kProfilerDefaultTimeoutMs};
  bool armed{false};
};

inline bool &profiler_enabled() {
  static bool s_enabled = false;
  return s_enabled;
}

inline ProfilerTimerState &profiler_timer_state() {
  static ProfilerTimerState s_timer{};
  return s_timer;
}

inline void profiler_start(uint32_t now_ms, uint32_t timeout_ms = kProfilerDefaultTimeoutMs) {
  auto &st = profiler_timer_state();
  st.started_ms = now_ms;
  st.timeout_ms = timeout_ms;
  st.armed = true;
  profiler_enabled() = true;
}

inline void profiler_stop() {
  auto &st = profiler_timer_state();
  profiler_enabled() = false;
  st.armed = false;
  st.started_ms = 0U;
  st.timeout_ms = kProfilerDefaultTimeoutMs;
}

inline bool profiler_is_expired(uint32_t now_ms) {
  if (!profiler_enabled()) return false;
  const auto &st = profiler_timer_state();
  if (!st.armed) return false;
  if (st.timeout_ms == 0U) return true;
  return static_cast<uint32_t>(now_ms - st.started_ms) >= st.timeout_ms;
}

inline uint32_t profiler_remaining_sec(uint32_t now_ms) {
  if (!profiler_enabled()) return 0U;
  const auto &st = profiler_timer_state();
  if (!st.armed) return 0U;
  const uint32_t elapsed = static_cast<uint32_t>(now_ms - st.started_ms);
  if (elapsed >= st.timeout_ms) return 0U;
  const uint32_t rem = st.timeout_ms - elapsed;
  return (rem / 1000U) + ((rem % 1000U) != 0U ? 1U : 0U);
}

}  // namespace latter_core

#define LATTER_PROF_LOGI(...)                    \
  do {                                           \
    if (::latter_core::profiler_enabled()) {     \
      ESP_LOGI("PROF", __VA_ARGS__);             \
    }                                            \
  } while (0)

namespace latter_core {

struct FlightRecorderStats {
  uint32_t window_frames = 0;
  uint32_t window_render_us = 0;
  uint32_t max_render_us = 0;
  int max_render_page = 0;
  int max_render_sub = 0;
  uint32_t slow_frames = 0;
  uint32_t last_frame_ms = 0;
  uint32_t last_summary_ms = 0;
  int last_logged_page = -1;
  int last_logged_sub = -1;
  uint32_t va_session_start_ms = 0;
};

inline FlightRecorderStats &flight_recorder() {
  static FlightRecorderStats st;
  return st;
}

inline uint64_t prof_now_us() {
#ifdef USE_ESP32
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  return 0ULL;
#endif
}

// Per-section render timing averaged per frame and logged as [PROF:SECT] with [PROF:FRAME_SUM].
enum class ProfSect : uint8_t { BG_SKY, BG_CLOUDS, BG_PRECIP, HOME_TEXT, HOME_DOCK, BASE, ART, COUNT };

inline uint32_t *prof_sect_us() {
  static uint32_t acc[static_cast<size_t>(ProfSect::COUNT)] = {};
  return acc;
}

inline uint64_t prof_section_begin() { return profiler_enabled() ? prof_now_us() : 0; }

inline void prof_art_end(uint64_t t0) {
  if (t0 != 0) prof_sect_us()[static_cast<size_t>(ProfSect::ART)] += static_cast<uint32_t>(prof_now_us() - t0);
}

inline void log_audio_profile(const char *event_tag, const char *detail = "") {
  if (!profiler_enabled()) return;
  const uint32_t now_m = hal_millis_now();
  auto &fr = flight_recorder();
  if (event_tag != nullptr && std::strcmp(event_tag, "VA_LISTEN_START") == 0) {
    fr.va_session_start_ms = now_m;
  }
  const uint32_t va_el = (fr.va_session_start_ms > 0 && now_m >= fr.va_session_start_ms)
                             ? (now_m - fr.va_session_start_ms)
                             : 0U;
  LATTER_PROF_LOGI("[PROF:AUDIO] event=%s va_elapsed=%lums %s",
                   event_tag != nullptr ? event_tag : "", (unsigned long)va_el, detail ? detail : "");
}

// Internal RAM low-water watch, polled every second whether or not the profiler is on: when the
// lowest-ever internal free memory falls by 4 KB or more, log by how much and what was active.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
inline constexpr size_t kLowWaterWarnBytes = 128U * 1024U;
#else
inline constexpr size_t kLowWaterWarnBytes = 64U * 1024U;
#endif

inline void watch_internal_low_water(bool ble_act, bool mww_run, bool va_act, bool mp_play, int page) {
#ifdef USE_ESP32
  static size_t last_min = 0;
  const size_t cur_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  if (last_min == 0) {
    last_min = cur_min;
    return;
  }
  if (cur_min + 4096U <= last_min) {
    const unsigned long from_kb = static_cast<unsigned long>(last_min / 1024U);
    const unsigned long to_kb = static_cast<unsigned long>(cur_min / 1024U);
    const unsigned long free_kb = static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U);
    const unsigned long blk_kb =
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U);
    if (cur_min < kLowWaterWarnBytes) {
      ESP_LOGW("PROF",
               "[PROF:LOWWATER] internal min %luKB -> %luKB, below %luKB (free=%luKB max_blk=%luKB) "
               "ble=%d mww=%d va=%d mp=%d page=%d",
               from_kb, to_kb, static_cast<unsigned long>(kLowWaterWarnBytes / 1024U), free_kb, blk_kb,
               (int)ble_act, (int)mww_run, (int)va_act, (int)mp_play, page);
    } else {
      LATTER_PROF_LOGI("[PROF:LOWWATER] internal min %luKB -> %luKB (free=%luKB max_blk=%luKB) "
                       "ble=%d mww=%d va=%d mp=%d page=%d",
                       from_kb, to_kb, free_kb, blk_kb, (int)ble_act, (int)mww_run, (int)va_act, (int)mp_play, page);
    }
    last_min = cur_min;
  }
#else
  (void)ble_act;
  (void)mww_run;
  (void)va_act;
  (void)mp_play;
  (void)page;
#endif
}

// Compact 3-region heap summary emitted when Telemetry Profiler is enabled.
inline void log_heap_summary() {
#ifdef USE_ESP32
  if (!profiler_enabled()) return;
  auto region = [](const char *name, uint32_t caps) {
    multi_heap_info_t hi{};
    heap_caps_get_info(&hi, caps);
    LATTER_PROF_LOGI("[PROF:HEAP] %s free=%uKB largest=%uKB min_ever=%uKB alloc=%uKB blocks=%u/%u", name,
                     (unsigned)(hi.total_free_bytes / 1024U), (unsigned)(hi.largest_free_block / 1024U),
                     (unsigned)(hi.minimum_free_bytes / 1024U), (unsigned)(hi.total_allocated_bytes / 1024U),
                     (unsigned)hi.allocated_blocks, (unsigned)hi.free_blocks);
  };
  region("internal", MALLOC_CAP_INTERNAL);
  region("dma", MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  region("psram", MALLOC_CAP_SPIRAM);
#endif
}

}  // namespace latter_core
