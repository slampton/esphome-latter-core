#pragma once

// ==============================================================================
// Latter [PROF:*] Flight Recorder, Memory Watch & Render Section Profiler
// (latter_prof.h)
// ==============================================================================
// [PROF:*] diagnostics log at INFO only while "System: Telemetry Profiler" is on, so a device at the
// default INFO level stays quiet in steady state. Use LATTER_PROF_LOGI(fmt, ...) for every [PROF:*]
// INFO line. Exempt (plain ESP_LOGI("PROF", ...)): [PROF:BOOT], the once-per-boot summary, and
// [PROF:LINK], the Link Report. Genuine warnings ([PROF:LOWWATER], [PROF:STALL], [PROF:AUDIO_STALL],
// IMU / battery faults) use ESP_LOGW("PROF", ...) and always log.
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
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#endif

namespace esphome::latter_core {

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

}  // namespace esphome::latter_core

namespace latter_ui {

inline uint32_t hal_millis_now() {
#if defined(USE_ESP32) && __has_include("esphome/core/hal.h")
  return esphome::millis();
#elif defined(USE_ESP32)
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
#else
  return 0U;
#endif
}

inline bool &profiler_enabled() {
  static bool s_enabled = false;
  return s_enabled;
}

}  // namespace latter_ui

#define LATTER_PROF_LOGI(...)                    \
  do {                                           \
    if (::latter_ui::profiler_enabled()) {       \
      ESP_LOGI("PROF", __VA_ARGS__);             \
    }                                            \
  } while (0)

namespace latter_ui {

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
#elif __has_include("esphome/core/hal.h") && defined(USE_ESP32)
  return static_cast<uint64_t>(esphome::micros());
#else
  return 0ULL;
#endif
}

// Per-section render timing averaged per frame and logged as [PROF:SECT] with [PROF:FRAME_SUM].
enum class ProfSect : uint8_t { BG_SKY, BG_CELESTIAL, BG_CLOUDS, BG_PRECIP, HOME_TEXT, HOME_DOCK, BASE, ART, COUNT };

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

// On-demand memory report ("System: Log Memory Report"): heap totals per region plus FreeRTOS tasks.
inline void log_memory_report() {
#ifdef USE_ESP32
  auto region = [](const char *name, uint32_t caps) {
    multi_heap_info_t hi;
    heap_caps_get_info(&hi, caps);
    LATTER_PROF_LOGI("[PROF:HEAP] %s free=%uKB largest=%uKB min_ever=%uKB alloc=%uKB blocks=%u/%u", name,
                     (unsigned)(hi.total_free_bytes / 1024U), (unsigned)(hi.largest_free_block / 1024U),
                     (unsigned)(hi.minimum_free_bytes / 1024U), (unsigned)(hi.total_allocated_bytes / 1024U),
                     (unsigned)hi.allocated_blocks, (unsigned)hi.free_blocks);
  };
  region("internal", MALLOC_CAP_INTERNAL);
  region("dma", MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  region("psram", MALLOC_CAP_SPIRAM);
#if configUSE_TRACE_FACILITY
  UBaseType_t n = uxTaskGetNumberOfTasks();
  auto *ts = static_cast<TaskStatus_t *>(heap_caps_malloc(sizeof(TaskStatus_t) * (n + 4), MALLOC_CAP_SPIRAM));
  if (ts == nullptr) return;
  n = uxTaskGetSystemState(ts, n + 4, nullptr);
  unsigned int_stacks = 0;
  for (UBaseType_t i = 0; i < n; i++) {
    const bool ext = esp_ptr_external_ram(ts[i].pxStackBase);
    if (!ext) int_stacks++;
    LATTER_PROF_LOGI("[PROF:TASK] %-16s stack=%s unused=%uB prio=%u", ts[i].pcTaskName, ext ? "PSRAM" : "INT",
                     (unsigned)ts[i].usStackHighWaterMark, (unsigned)ts[i].uxCurrentPriority);
  }
  LATTER_PROF_LOGI("[PROF:TASK] %u tasks, %u with internal-RAM stacks", (unsigned)n, int_stacks);
  heap_caps_free(ts);
#else
  LATTER_PROF_LOGI("[PROF:TASK] task list needs CONFIG_FREERTOS_USE_TRACE_FACILITY");
#endif
#endif
}

}  // namespace latter_ui
