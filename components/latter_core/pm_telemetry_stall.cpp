#if __has_include("hal_sleep_pm.h")
#include "hal_sleep_pm.h"
#include "hal_battery.h"
#include "hal_imu_qmi8658.h"
#include "hal_radio_policy.h"
#else
#include "../../include/hal_sleep_pm.h"
#include "../../include/hal_battery.h"
#include "../../include/hal_imu_qmi8658.h"
#include "../../include/hal_radio_policy.h"
#endif

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/application.h")
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#endif
#if __has_include("esphome/core/static_task.h")
#include "esphome/core/static_task.h"
#define LATTER_HAS_STATIC_TASK 1
#else
#define LATTER_HAS_STATIC_TASK 0
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef USE_ESP32
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_pm.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#ifdef USE_ESP32_CRASH_HANDLER
#include "esphome/components/esp32/crash_handler.h"
#endif

namespace latter_core {

FlashWriteStats g_flash_write_stats;

uint32_t device_boot_id() {
  static uint32_t s_boot_id = 0;
  if (s_boot_id == 0) {
    s_boot_id = esp_random() & 0x7FFFFFFFU;
    if (s_boot_id == 0) s_boot_id = 1U;
  }
  return s_boot_id;
}

static RTC_NOINIT_ATTR StallRecord s_stall_rtc_record;
static RTC_NOINIT_ATTR StallRecord s_twdt_rtc_record;
static TaskHandle_t g_stall_loop_task = nullptr;
static TaskHandle_t g_stall_guard_task_handle = nullptr;
static std::atomic<bool> g_stall_chip_awake{true};

static inline void stall_copy_str(char *dst, size_t n, const char *src) {
  size_t i = 0;
  if (src != nullptr) {
    for (; i + 1 < n && src[i] != '\0'; i++) dst[i] = src[i];
  }
  dst[i] = '\0';
}

static inline void stall_terminate(StallRecord &rec) {
  rec.source[sizeof(rec.source) - 1] = '\0';
  rec.cpu0[sizeof(rec.cpu0) - 1] = '\0';
  rec.cpu1[sizeof(rec.cpu1) - 1] = '\0';
}

StallGuardState::StallGuardState() {
  const esp_reset_reason_t rr = esp_reset_reason();
  const bool wdt_reset = rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT || rr == ESP_RST_PANIC;
  if (rr == ESP_RST_PANIC && s_stall_rtc_record.magic == kStallRecordMagic) {
    previous_boot = s_stall_rtc_record;
    stall_terminate(previous_boot);
  } else if (wdt_reset && s_stall_rtc_record.magic == kStallLiveMagic) {
    previous_live = s_stall_rtc_record;
    stall_terminate(previous_live);
  }
  if (wdt_reset && s_twdt_rtc_record.magic == kTwdtRecordMagic) {
    previous_twdt = s_twdt_rtc_record;
    stall_terminate(previous_twdt);
  }
  s_stall_rtc_record.magic = 0;
  s_twdt_rtc_record.magic = 0;
}

StallGuardState &stall_guard() {
  static StallGuardState s;
  return s;
}

bool previous_boot_stalled() { return stall_guard().previous_boot.magic == kStallRecordMagic; }

void stall_guard_pause(bool paused) {
  auto &g = stall_guard();
  if (!paused) g.resumed_ms.store(esphome::millis() | 1U, std::memory_order_relaxed);
  g.paused.store(paused, std::memory_order_relaxed);
}

void stall_guard_notify_chip_awake(bool stay_awake) noexcept {
  const bool was_stall_awake = g_stall_chip_awake.exchange(stay_awake, std::memory_order_relaxed);
  if (stay_awake && !was_stall_awake && g_stall_guard_task_handle != nullptr) {
    xTaskNotifyGive(g_stall_guard_task_handle);
  }
}

static void stall_current_source(char *dst, size_t n) {
  const esphome::LogString *src = esphome::App.get_current_source();
  esphome::Component *comp = esphome::App.get_current_component();
  const char *src_s = (src != nullptr) ? LOG_STR_ARG(src) : nullptr;
  const char *comp_s = (comp != nullptr) ? LOG_STR_ARG(comp->get_component_log_str()) : nullptr;
  if (src_s != nullptr && comp_s != nullptr && std::strcmp(src_s, comp_s) != 0) {
    std::snprintf(dst, n, "%s/%s", comp_s, src_s);
  } else {
    std::snprintf(dst, n, "%s", src_s != nullptr ? src_s : (comp_s != nullptr ? comp_s : "unknown"));
  }
}

static void stall_fill_cpus(StallRecord &rec) {
  stall_copy_str(rec.cpu0, sizeof(rec.cpu0), pcTaskGetName(xTaskGetCurrentTaskHandleForCore(0)));
#if !CONFIG_FREERTOS_UNICORE
  stall_copy_str(rec.cpu1, sizeof(rec.cpu1), pcTaskGetName(xTaskGetCurrentTaskHandleForCore(1)));
#endif
}

static char stall_loop_state() {
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY)
  if (g_stall_loop_task == nullptr) return '?';
  switch (eTaskGetState(g_stall_loop_task)) {
    case eRunning: return 'R';
    case eReady: return 'r';
    case eBlocked: return 'B';
    case eSuspended: return 'S';
    default: return '?';
  }
#else
  return '?';
#endif
}

static void stall_clear_live() {
  if (s_stall_rtc_record.magic == kStallLiveMagic) s_stall_rtc_record.magic = 0;
}

static void stall_guard_task(void *) {
  auto &g = stall_guard();
  for (;;) {
    if (!g_stall_chip_awake.load(std::memory_order_relaxed)) {
      stall_clear_live();
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      g.resumed_ms.store(esphome::millis() | 1U, std::memory_order_relaxed);
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (g.paused.load(std::memory_order_relaxed) || !g_stall_chip_awake.load(std::memory_order_relaxed)) {
      stall_clear_live();
      continue;
    }
    uint32_t start = esphome::App.get_loop_component_start_time();
    const uint32_t resumed = g.resumed_ms.load(std::memory_order_relaxed);
    if (resumed != 0 && static_cast<int32_t>(resumed - start) > 0) start = resumed;
    const uint32_t idle_ms = esphome::App.get_loop_interval();
    const uint32_t since = esphome::millis() - start;
    if (static_cast<int32_t>(since) < 0 || since <= idle_ms || since - idle_ms < kStallReportMs) {
      stall_clear_live();
      continue;
    }
    const uint32_t blocked = since - idle_ms;
    StallRecord rec{};
    rec.magic = kStallRecordMagic;
    rec.blocked_ms = blocked;
    rec.uptime_s = start / 1000U;
    stall_current_source(rec.source, sizeof(rec.source));
    stall_fill_cpus(rec);
    rec.loop_state = stall_loop_state();
    portENTER_CRITICAL(&g.mux);
    if (blocked > g.longest.blocked_ms) g.longest = rec;
    portEXIT_CRITICAL(&g.mux);
    if (blocked >= kStallAbortMs) {
      s_stall_rtc_record = rec;
      esp_system_abort("Latter stall guard: main loop blocked");
    }
    rec.magic = kStallLiveMagic;
    s_stall_rtc_record = rec;
  }
}

void start_stall_guard() {
  auto &g = stall_guard();
  if (g.started) return;
  g_stall_loop_task = xTaskGetCurrentTaskHandle();
#if LATTER_HAS_STATIC_TASK
  static esphome::StaticTask s_stall_task;
#if defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM) && !defined(CONFIG_IDF_TARGET_ESP32P4)
  g.started = s_stall_task.create(stall_guard_task, "latter_stall", 3072, nullptr, 15, /*use_psram=*/true);
#endif
  if (!g.started) {
    g.started = s_stall_task.create(stall_guard_task, "latter_stall", 2560, nullptr, 15, /*use_psram=*/false);
  }
  if (g.started) {
    g_stall_guard_task_handle = s_stall_task.get_handle();
  }
#else
  g.started = xTaskCreate(stall_guard_task, "latter_stall", 2560, nullptr, 15, &g_stall_guard_task_handle) == pdPASS;
#endif
  if (!g.started) {
    ESP_LOGW("latter", "Stall guard task could not start");
  }
}

void log_stall_summary() {
  auto &g = stall_guard();
  if (g.previous_boot.magic == kStallRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot aborted: main loop blocked %lums in %s (started at uptime %lus) loopTask=%c CPU0=%s CPU1=%s",
             (unsigned long)g.previous_boot.blocked_ms, g.previous_boot.source,
             (unsigned long)g.previous_boot.uptime_s, g.previous_boot.loop_state ? g.previous_boot.loop_state : '?',
             g.previous_boot.cpu0, g.previous_boot.cpu1);
  }
  if (g.previous_live.magic == kStallLiveMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot reset during a main loop block: %lums so far in %s (started at uptime %lus) loopTask=%c CPU0=%s CPU1=%s",
             (unsigned long)g.previous_live.blocked_ms, g.previous_live.source,
             (unsigned long)g.previous_live.uptime_s, g.previous_live.loop_state ? g.previous_live.loop_state : '?',
             g.previous_live.cpu0, g.previous_live.cpu1);
  }
  if (g.previous_twdt.magic == kTwdtRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot task watchdog fired: main loop blocked %lums in %s (started at uptime %lus) CPU0=%s CPU1=%s",
             (unsigned long)g.previous_twdt.blocked_ms, g.previous_twdt.source,
             (unsigned long)g.previous_twdt.uptime_s, g.previous_twdt.cpu0, g.previous_twdt.cpu1);
  } else if (esp_reset_reason() == ESP_RST_TASK_WDT) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot task-watchdog reset without the watchdog interrupt's record (interrupt never ran)");
  }
  StallRecord longest{};
  portENTER_CRITICAL(&g.mux);
  longest = g.longest;
  portEXIT_CRITICAL(&g.mux);
  if (longest.magic == kStallRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] longest main loop block this boot: %lums in %s (started at uptime %lus)",
             (unsigned long)longest.blocked_ms, longest.source, (unsigned long)longest.uptime_s);
  }
}

const char *last_reset_reason() {
  if (previous_boot_stalled()) return "stall";
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    default: break;
  }
  return "unknown";
}

bool crash_record_present() {
#ifdef USE_ESP32_CRASH_HANDLER
  return esphome::esp32::crash_handler_has_data();
#else
  return false;
#endif
}

bool firmware_rolled_back() {
  return esp_ota_get_last_invalid_partition() != nullptr;
}

[[maybe_unused]] static void pm_log_dump_lines(const char *label, size_t len, void (*dump)(FILE *)) {
  char *buf = static_cast<char *>(heap_caps_calloc(1, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return;
  FILE *f = fmemopen(buf, len - 1, "w");
  if (f != nullptr) {
    dump(f);
    fclose(f);
    char *save = nullptr;
    for (char *line = strtok_r(buf, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
      if (line[0] != '\0') {
        LATTER_PROF_LOGI("%s %s", label, line);
      }
    }
  }
  heap_caps_free(buf);
}

void pm_log_profile() {
  PmProfileSnapshot snap{};
  if (!pm_take_profile_snapshot(snap, /*clear_dump_pending=*/false)) return;
  static uint32_t last_ms = 0, last_slept_us = 0, last_sleeps = 0, last_gpio = 0, last_synth = 0, last_dump_ms = 0;
  const uint32_t now_ms = esphome::millis();
  uint32_t permille = 0;
  if (last_ms != 0 && now_ms != last_ms) {
    permille = (snap.slept_us - last_slept_us) / (now_ms - last_ms);
    if (permille > 1000U) permille = 1000U;
  }
  LATTER_PROF_LOGI("[PROF:PM] lock=%s ls=%lu.%lu%% sleeps=%lu gpio_wakes=%lu synth=%lu arm=0x%02lx loop=%lums",
                   snap.awake ? "held" : "released", static_cast<unsigned long>(permille / 10U),
                   static_cast<unsigned long>(permille % 10U), static_cast<unsigned long>(snap.sleeps - last_sleeps),
                   static_cast<unsigned long>(snap.gpio_wakes - last_gpio),
                   static_cast<unsigned long>(snap.synth - last_synth),
                   static_cast<unsigned long>(snap.arm_mask),
                   static_cast<unsigned long>(esphome::App.get_loop_interval()));
  last_ms = now_ms;
  last_slept_us = snap.slept_us;
  last_sleeps = snap.sleeps;
  last_gpio = snap.gpio_wakes;
  last_synth = snap.synth;
  if (snap.awake || (!snap.dump_pending && now_ms - last_dump_ms < 60000U)) return;
  (void) pm_take_profile_snapshot(snap, /*clear_dump_pending=*/true);
  last_dump_ms = now_ms;
  pm_log_dump_lines("[PROF:PM_LOCKS]", 2048, [](FILE *f) { esp_pm_dump_locks(f); });
}

void pm_log_wake_profile() {
  static uint32_t last_ms = 0;
  const uint32_t now_ms = esphome::millis();
  if (last_ms != 0 && now_ms - last_ms < 60000U) return;
  [[maybe_unused]] const uint32_t window_ms = (last_ms == 0) ? now_ms : now_ms - last_ms;
  last_ms = now_ms;

  PmProfileSnapshot snap{};
  if (pm_take_profile_snapshot(snap, /*clear_dump_pending=*/false)) {
    static uint32_t p_att = 0, p_tb = 0, p_rb = 0, p_sl = 0, p_req[5] = {0, 0, 0, 0, 0};
    LATTER_PROF_LOGI(
        "[PROF:WAKE] window=%lus sleeps: attempts=%lu slept=%lu bound_by esp_timer=%lu rtos=%lu "
        "req <20ms=%lu <50ms=%lu <200ms=%lu <1s=%lu >=1s=%lu lock=%s",
        (unsigned long)(window_ms / 1000U), (unsigned long)(snap.attempts - p_att),
        (unsigned long)(snap.sleeps - p_sl), (unsigned long)(snap.timer_bound - p_tb),
        (unsigned long)(snap.rtos_bound - p_rb), (unsigned long)(snap.req[0] - p_req[0]),
        (unsigned long)(snap.req[1] - p_req[1]), (unsigned long)(snap.req[2] - p_req[2]),
        (unsigned long)(snap.req[3] - p_req[3]), (unsigned long)(snap.req[4] - p_req[4]),
        snap.awake ? "held" : "released");
    p_att = snap.attempts;
    p_tb = snap.timer_bound;
    p_rb = snap.rtos_bound;
    p_sl = snap.sleeps;
    for (int i = 0; i < 5; i++) p_req[i] = snap.req[i];
  }

#ifdef USE_LATTER_LOOP_PROBE
  {
    static LoopProbeStats prev{};
    const LoopProbeStats &lp = loop_probe_stats();
    LATTER_PROF_LOGI(
        "[PROF:WAKE] loop: interval=%lums passes=%lu hf_held=%lu hf_now=%d gaps <20ms=%lu <50ms=%lu "
        "<200ms=%lu <1s=%lu >=1s=%lu",
        (unsigned long)esphome::App.get_loop_interval(), (unsigned long)(lp.passes - prev.passes),
        (unsigned long)(lp.hf_passes - prev.hf_passes),
        (int)esphome::HighFrequencyLoopRequester::is_high_frequency(),
        (unsigned long)(lp.gaps[0] - prev.gaps[0]), (unsigned long)(lp.gaps[1] - prev.gaps[1]),
        (unsigned long)(lp.gaps[2] - prev.gaps[2]), (unsigned long)(lp.gaps[3] - prev.gaps[3]),
        (unsigned long)(lp.gaps[4] - prev.gaps[4]));
    prev = lp;
  }
#endif

#if defined(CONFIG_ESP_TIMER_PROFILING)
  pm_log_dump_lines("[PROF:WAKE_TIMER]", 6144, [](FILE *f) { esp_timer_dump(f); });
#else
  LATTER_PROF_LOGI("[PROF:WAKE_TIMER] off (build with substitution wake_profiling: \"y\")");
#endif

#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
  {
    constexpr UBaseType_t kMaxTasks = 48;
    static TaskHandle_t prev_h[kMaxTasks] = {};
    static uint64_t prev_rt[kMaxTasks] = {};
    static UBaseType_t prev_n = 0;
    auto *ts = static_cast<TaskStatus_t *>(
        heap_caps_malloc(sizeof(TaskStatus_t) * kMaxTasks, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ts != nullptr) {
      configRUN_TIME_COUNTER_TYPE total = 0;
      const UBaseType_t n = uxTaskGetSystemState(ts, kMaxTasks, &total);
      uint64_t delta[kMaxTasks];
      for (UBaseType_t i = 0; i < n; i++) {
        const uint64_t rt = static_cast<uint64_t>(ts[i].ulRunTimeCounter);
        uint64_t prev = rt;
        for (UBaseType_t j = 0; j < prev_n; j++) {
          if (prev_h[j] == ts[i].xHandle) {
            prev = prev_rt[j];
            break;
          }
        }
        delta[i] = rt >= prev ? rt - prev : rt;
      }
      bool shown[kMaxTasks] = {};
      for (int k = 0; k < 14; k++) {
        int best = -1;
        for (UBaseType_t i = 0; i < n; i++) {
          if (!shown[i] && (best < 0 || delta[i] > delta[best])) best = static_cast<int>(i);
        }
        if (best < 0 || delta[best] == 0) break;
        shown[best] = true;
        const uint32_t pm = window_ms > 0 ? static_cast<uint32_t>(delta[best] / window_ms) : 0;
        LATTER_PROF_LOGI("[PROF:WAKE_TASK] %-16s prio=%u run=%lums (%lu.%lu%%)", ts[best].pcTaskName,
                         (unsigned) ts[best].uxCurrentPriority, (unsigned long)(delta[best] / 1000U),
                         (unsigned long)(pm / 10U), (unsigned long)(pm % 10U));
      }
      prev_n = n;
      for (UBaseType_t i = 0; i < n; i++) {
        prev_h[i] = ts[i].xHandle;
        prev_rt[i] = static_cast<uint64_t>(ts[i].ulRunTimeCounter);
      }
      heap_caps_free(ts);
    }
  }
#else
  LATTER_PROF_LOGI("[PROF:WAKE_TASK] off (build with substitution wake_profiling: \"y\")");
#endif
}

}  // namespace latter_core

#if !defined(LATTER_DIAG_TWDT_BREADCRUMB) || LATTER_DIAG_TWDT_BREADCRUMB
extern "C" void esp_task_wdt_isr_user_handler(void) {
  using namespace latter_core;
  StallRecord rec{};
  const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  const uint32_t start = esphome::App.get_loop_component_start_time();
  rec.blocked_ms = now_ms - start;
  rec.uptime_s = start / 1000U;
  const esphome::LogString *src = esphome::App.get_current_source();
  esphome::Component *comp = esphome::App.get_current_component();
  const char *src_s = (src != nullptr) ? LOG_STR_ARG(src) : nullptr;
  const char *comp_s = (comp != nullptr) ? LOG_STR_ARG(comp->get_component_log_str()) : nullptr;
  stall_copy_str(rec.source, sizeof(rec.source), comp_s != nullptr ? comp_s : (src_s != nullptr ? src_s : "unknown"));
  if (comp_s != nullptr && src_s != nullptr && std::strcmp(src_s, comp_s) != 0) {
    const size_t used = std::strlen(rec.source);
    if (used + 2 < sizeof(rec.source)) {
      rec.source[used] = '/';
      stall_copy_str(rec.source + used + 1, sizeof(rec.source) - used - 1, src_s);
    }
  }
  stall_fill_cpus(rec);
  rec.loop_state = '?';
  rec.magic = kTwdtRecordMagic;
  s_twdt_rtc_record = rec;
}
#endif

#if !defined(LATTER_DIAG_NVS_GUARD) || LATTER_DIAG_NVS_GUARD
extern "C" {
esp_err_t __real_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t __wrap_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
  const int64_t t0 = esp_timer_get_time();
  const esp_err_t err = __real_nvs_set_blob(handle, key, value, length);
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
  auto &fw = latter_core::g_flash_write_stats;
  fw.nvs_writes.fetch_add(1U, std::memory_order_relaxed);
  uint32_t prev = fw.nvs_max_us.load(std::memory_order_relaxed);
  while (us > prev && !fw.nvs_max_us.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
  }
  return err;
}

esp_err_t __real_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len);
esp_err_t __wrap_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len) {
  latter_core::g_flash_write_stats.erases.fetch_add(1U, std::memory_order_relaxed);
  return __real_esp_flash_erase_region(chip, start, len);
}
}  // extern "C"
#endif
#endif  // USE_ESP32

namespace latter_core {

void log_power_and_mem_profile(const char *trigger, int pwr_state, bool usb, bool chg,
                               float batt_pct, float batt_v, bool spk_pa, bool mww_run,
                               bool va_act, bool mp_play, bool ble_act, int game_id,
                               uint32_t sleep_start_ms) {
  if (!profiler_enabled()) return;
  const uint32_t now_m = hal_millis_now();
  const uint32_t sleep_age_s = (sleep_start_ms > 0 && now_m >= sleep_start_ms)
                                   ? ((now_m - sleep_start_ms) / 1000U)
                                   : 0U;
  LATTER_PROF_LOGI(
      "[PROF:POWER] trig=%s pwr=%d usb=%d chg=%d batt=%.0f%%(%.2fV) gauge=%d%% spk_pa=%d mww=%d va=%d mp=%d ble=%d game=%d sleep_age=%lus wom=%d/%lu wifi_ps=%d",
      trigger, pwr_state, (int)usb, (int)chg,
      std::isnan(batt_pct) ? -1.0f : batt_pct,
      std::isnan(batt_v) ? 0.0f : batt_v, pmu_telemetry().gauge_raw,
      (int)spk_pa, (int)mww_run, (int)va_act, (int)mp_play, (int)ble_act, game_id,
      (unsigned long)sleep_age_s, (int)imu_wom().armed, (unsigned long)imu_wom().events,
      wifi_ps_mode_now());
  battery_log_profile(now_m);
  pm_log_profile();
  pm_log_wake_profile();
#ifdef USE_ESP32
  size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t int_max  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024U;
  size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t ps_min   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  size_t int_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024U;
  size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024U;
  LATTER_PROF_LOGI("[PROF:MEM] int_free=%luKB int_max_blk=%luKB int_min=%luKB dma_free=%luKB psram_free=%luKB psram_min=%luKB",
                   (unsigned long)int_free, (unsigned long)int_max, (unsigned long)int_min, (unsigned long)dma_free,
                   (unsigned long)ps_free, (unsigned long)ps_min);
#endif
}

void log_boot_summary() {
  log_stall_summary();
  const PmCrumb &crumb = pm_previous_crumb();
  if (crumb.magic == kPmCrumbMagic && crumb.ring_head < kPmCrumbRing) {
    const PmCrumbSleep &last_entry = crumb.ring[crumb.ring_head];
    ESP_LOGW("PROF", "[PROF:BOOT] previous reset at light sleep %s: seq=%lu imu_wom=%lu imu_events=%lu tick0=%+ldus tick1=%+ldus",
             pm_crash_where(), (unsigned long)crumb.seq, (unsigned long)crumb.imu_armed,
             (unsigned long)crumb.imu_events, (long)(int32_t)(crumb.tick_us[0] - last_entry.t_us),
             (long)(int32_t)(crumb.tick_us[1] - last_entry.t_us));
    for (uint32_t k = 0; k < kPmCrumbRing; k++) {
      const uint32_t idx = (crumb.ring_head + 1U + k) % kPmCrumbRing;
      const PmCrumbSleep &e = crumb.ring[idx];
      const bool last = idx == crumb.ring_head;
      if (e.t_us == 0 && e.req_us == 0) continue;
      ESP_LOGW("PROF", "[PROF:BOOT]   %s t=%luus req=%luus slept=%luus cause=0x%04x arm=0x%02x lv=0x%02x",
               last ? (crumb.phase == 1U ? "in-flight" : "last") : "sleep", (unsigned long)e.t_us,
               (unsigned long)e.req_us, (unsigned long)e.slept_us, (unsigned)e.cause, (unsigned)e.arm,
               (unsigned)e.levels);
    }
  }
  const char *rr = last_reset_reason();
  const int crash = crash_record_present() ? 1 : 0;
  const int rollback = firmware_rolled_back() ? 1 : 0;
  if (crash != 0 || rollback != 0) {
    ESP_LOGW("latter", "Previous boot fault detected (reset=%s crash=%d rollback=%d)", rr, crash, rollback);
  }
  if (!profiler_enabled()) return;
  const uint32_t up_s = hal_millis_now() / 1000U;
  const auto &tel = pmu_telemetry();
  if (!tel.boot_valid) {
    LATTER_PROF_LOGI("[PROF:BOOT] core_rev=%ld uptime=%lus reset=%s crash=%d rollback=%d pmu=none", (long)LATTER_CORE_REV,
                     (unsigned long)up_s, rr, crash, rollback);
    return;
  }
  static const char *const kOff[8] = {"pwrkey-long", "software", "pwrkey-low", "vsys-undervolt",
                                      "vbus-overvolt", "dcdc-undervolt", "dcdc-overvolt", "over-temp"};
  static const char *const kOn[6] = {"pwrkey", "irq", "vbus-insert", "batt-charge", "batt-insert", "en-mode"};
  char off_s[96] = {0};
  char on_s[64] = {0};
  size_t off_len = 0;
  for (int b = 0; b < 8; b++) {
    if (tel.off_src & (1U << b)) {
      int n = std::snprintf(off_s + off_len, sizeof(off_s) - off_len, "%s%s", off_len ? "," : "", kOff[b]);
      if (n > 0) off_len = std::min(sizeof(off_s) - 1, off_len + static_cast<size_t>(n));
    }
  }
  size_t on_len = 0;
  for (int b = 0; b < 6; b++) {
    if (tel.on_src & (1U << b)) {
      int n = std::snprintf(on_s + on_len, sizeof(on_s) - on_len, "%s%s", on_len ? "," : "", kOn[b]);
      if (n > 0) on_len = std::min(sizeof(on_s) - 1, on_len + static_cast<size_t>(n));
    }
  }
  char hib_s[48] = "-";
  const HibernateRecord &prev = battery_model().prev_hib;
  if (prev.valid) {
    if (prev.soc <= 100) {
      std::snprintf(hib_s, sizeof(hib_s), "%s@%umV/%u%%", hibernate_reason_name(prev.reason), (unsigned)prev.mv,
                    (unsigned)prev.soc);
    } else {
      std::snprintf(hib_s, sizeof(hib_s), "%s@%umV", hibernate_reason_name(prev.reason), (unsigned)prev.mv);
    }
  }
  LATTER_PROF_LOGI(
      "[PROF:BOOT] core_rev=%ld uptime=%lus reset=%s crash=%d rollback=%d last_off=0x%02X(%s) hib=%s on=0x%02X(%s) icc=%dmA cv=0x%02X",
      (long)LATTER_CORE_REV, (unsigned long)up_s, rr, crash, rollback, tel.off_src, off_s[0] ? off_s : "-", hib_s,
      tel.on_src, on_s[0] ? on_s : "-", tel.icc_ma, tel.cv_reg);
}

}  // namespace latter_core
