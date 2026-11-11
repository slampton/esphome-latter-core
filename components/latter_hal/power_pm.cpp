#include "hal_power_pm.h"

#include "esphome/core/defines.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

#ifdef USE_ESP32
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_flash.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_pm.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#endif

#ifdef USE_ESP32_CRASH_HANDLER
#include "esphome/components/esp32/crash_handler.h"
#endif

#if defined(USE_ESP32) && defined(CONFIG_PM_ENABLE) && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
#define LATTER_PM_AUTO_SLEEP 1
#else
#define LATTER_PM_AUTO_SLEEP 0
#endif

#if defined(USE_ESP32) && defined(CONFIG_PM_ENABLE) && !defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && \
    defined(CONFIG_IDF_TARGET_ESP32P4) && LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
#define LATTER_PM_DFS_ONLY 1
#else
#define LATTER_PM_DFS_ONLY 0
#endif

#if LATTER_PM_AUTO_SLEEP
#if !defined(CONFIG_PM_LIGHT_SLEEP_CALLBACKS)
#error "power_management enable_light_sleep needs CONFIG_PM_LIGHT_SLEEP_CALLBACKS: \"y\" in the board HAL"
#endif
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Latter's light-sleep wake path is written for the ESP32-S3 GPIO block (bench-validate before enabling on another SoC)"
#endif
#include <hal/gpio_ll.h>
#include <hal/rtc_cntl_ll.h>
#include <soc/gpio_reg.h>
#endif

namespace latter_ui {

FlashWriteStats g_flash_write_stats;

uint32_t device_boot_id() {
  static uint32_t s_boot_id = 0;
  if (s_boot_id == 0) {
#ifdef USE_ESP32
    s_boot_id = esp_random() & 0x7FFFFFFFU;
#endif
    if (s_boot_id == 0) s_boot_id = 1U;
  }
  return s_boot_id;
}

// ------------------------------------------------------------------------------
// Main-Loop Stall Guard (parked during screen-off battery standby so it never wakes light sleep)
// ------------------------------------------------------------------------------
#ifdef USE_ESP32
static RTC_NOINIT_ATTR StallRecord s_stall_rtc_record;
static RTC_NOINIT_ATTR StallRecord s_twdt_rtc_record;
static TaskHandle_t g_stall_loop_task = nullptr;
static TaskHandle_t g_stall_guard_task_handle = nullptr;
static std::atomic<bool> g_stall_chip_awake{true};
#endif

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
#ifdef USE_ESP32
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
#endif
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

#ifdef USE_ESP32
static void stall_fill_cpus(StallRecord &rec) {
  stall_copy_str(rec.cpu0, sizeof(rec.cpu0), pcTaskGetName(xTaskGetCurrentTaskHandleForCore(0)));
#if !CONFIG_FREERTOS_UNICORE
  stall_copy_str(rec.cpu1, sizeof(rec.cpu1), pcTaskGetName(xTaskGetCurrentTaskHandleForCore(1)));
#endif
}

static char stall_loop_state() {
  if (g_stall_loop_task == nullptr) return '?';
  switch (eTaskGetState(g_stall_loop_task)) {
    case eRunning: return 'R';
    case eReady: return 'r';
    case eBlocked: return 'B';
    case eSuspended: return 'S';
    default: return '?';
  }
}

static void stall_clear_live() {
  if (s_stall_rtc_record.magic == kStallLiveMagic) s_stall_rtc_record.magic = 0;
}

static void stall_guard_task(void *) {
  auto &g = stall_guard();
  for (;;) {
    // Park cleanly in screen-off battery standby so a 2 s FreeRTOS delay never wakes light sleep.
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
#endif

void start_stall_guard() {
#ifdef USE_ESP32
  auto &g = stall_guard();
  if (g.started) return;
  g_stall_loop_task = xTaskGetCurrentTaskHandle();
  g.started = xTaskCreate(stall_guard_task, "latter_stall", 2560, nullptr, 15, &g_stall_guard_task_handle) == pdPASS;
  if (!g.started) {
    ESP_LOGW("latter", "Stall guard task could not start");
  }
#endif
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
#ifdef USE_ESP32
  if (g.previous_twdt.magic == kTwdtRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot task watchdog fired: main loop blocked %lums in %s (started at uptime %lus) CPU0=%s CPU1=%s",
             (unsigned long)g.previous_twdt.blocked_ms, g.previous_twdt.source,
             (unsigned long)g.previous_twdt.uptime_s, g.previous_twdt.cpu0, g.previous_twdt.cpu1);
  } else if (esp_reset_reason() == ESP_RST_TASK_WDT) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot task-watchdog reset without the watchdog interrupt's record (interrupt never ran)");
  }
#endif
  StallRecord longest{};
#ifdef USE_ESP32
  portENTER_CRITICAL(&g.mux);
  longest = g.longest;
  portEXIT_CRITICAL(&g.mux);
#endif
  if (longest.magic == kStallRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] longest main loop block this boot: %lums in %s (started at uptime %lus)",
             (unsigned long)longest.blocked_ms, longest.source, (unsigned long)longest.uptime_s);
  }
}

const char *last_reset_reason() {
#ifdef USE_ESP32
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
#endif
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
#ifdef USE_ESP32
  return esp_ota_get_last_invalid_partition() != nullptr;
#else
  return false;
#endif
}

// ------------------------------------------------------------------------------
// Automatic Light Sleep (S3 Battery Boards) & 40 MHz DFS (P4 Battery Boards)
// ------------------------------------------------------------------------------
#if LATTER_PM_AUTO_SLEEP
constexpr esp_pm_lock_type_t kPmAwakeLockType = ESP_PM_CPU_FREQ_MAX;
constexpr uint32_t kPmWakeCauseGpio = 1UL << 2;
constexpr uint32_t kPmWakeCauseTimer = 1UL << 3;
constexpr uint32_t kPmWakeCauseWifi = 1UL << 5;

struct PmWakePin {
  int8_t gpio;
  uint8_t armed_level;
  uint8_t saved_type;
  uint8_t saved_ena;
};

struct PmState {
  esp_pm_lock_handle_t lock;
  bool awake;
  bool dump_pending;
  uint32_t loop_interval_awake;
  volatile uint32_t arm_mask;
  uint32_t entered_mask;
  PmWakePin pins[PM_WAKE_SLOTS];
  volatile uint32_t sleeps;
  volatile uint32_t slept_us;
  volatile uint32_t gpio_wakes;
  volatile uint32_t synth;
  volatile uint32_t timer_wakes;
  volatile uint32_t wifi_wakes;
  volatile uint32_t other_wakes;
  volatile uint32_t pin_wakes[PM_WAKE_SLOTS];
  volatile uint32_t gpio_unattributed;
  uint32_t awake_ms_total;
  uint32_t awake_since_ms;
};

static RTC_NOINIT_ATTR PmCrumb s_pm_crumb;
static DRAM_ATTR esp_timer_handle_t s_pm_wake_loop_timer = nullptr;

static PmState g_pm_state = {nullptr, true, false, 16U, 0U, 0U,
                             {{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}},
                             0U, 0U, 0U, 0U,
                             0U, 0U, 0U, {0U, 0U, 0U, 0U}, 0U,
                             0U, 0U};

struct PmWakeHist {
  volatile uint32_t attempts;
  volatile uint32_t timer_bound;
  volatile uint32_t rtos_bound;
  volatile uint32_t req[5];
};
static PmWakeHist g_pm_wake_hist = {0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U}};

static void pm_wake_loop_timer_cb(void *) {
  [](auto &app) {
    if constexpr (requires { app.wake_loop_threadsafe(); }) {
      app.wake_loop_threadsafe();
    }
  }(esphome::App);
}

static esp_err_t IRAM_ATTR pm_light_sleep_enter_cb(int64_t sleep_time_us, void *arg) {
  (void) arg;
  PmState &st = g_pm_state;
  PmWakeHist &wh = g_pm_wake_hist;
  wh.attempts = wh.attempts + 1U;
  const int64_t until_timer = esp_timer_get_next_alarm_for_wake_up() - esp_timer_get_time();
  if (until_timer <= sleep_time_us + 1000) {
    wh.timer_bound = wh.timer_bound + 1U;
  } else {
    wh.rtos_bound = wh.rtos_bound + 1U;
  }
  const uint32_t bucket = sleep_time_us < 20000 ? 0U
                          : sleep_time_us < 50000 ? 1U
                          : sleep_time_us < 200000 ? 2U
                          : sleep_time_us < 1000000 ? 3U
                                                    : 4U;
  wh.req[bucket] = wh.req[bucket] + 1U;
  const uint32_t mask = st.arm_mask;
  uint32_t entered = 0;
  uint32_t levels = 0;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    const int g = st.pins[i].gpio;
    if (g < 0) continue;
    const uint32_t gpio_num = static_cast<uint32_t>(g);
    const int level = gpio_ll_get_level(&GPIO, gpio_num);
    if (level) levels |= 1UL << i;
    if ((mask & (1UL << i)) == 0) continue;
    st.pins[i].armed_level = static_cast<uint8_t>(level);
    st.pins[i].saved_type = static_cast<uint8_t>(GPIO.pin[gpio_num].int_type);
    st.pins[i].saved_ena = static_cast<uint8_t>(GPIO.pin[gpio_num].int_ena);
    GPIO.pin[gpio_num].int_ena = 0;
    gpio_ll_set_intr_type(&GPIO, gpio_num, level ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    gpio_ll_wakeup_enable(&GPIO, gpio_num);
    entered |= 1UL << i;
  }
  st.entered_mask = entered;
  uint32_t head = s_pm_crumb.ring_next;
  if (head >= kPmCrumbRing) head = 0;
  PmCrumbSleep &e = s_pm_crumb.ring[head];
  e.t_us = static_cast<uint32_t>(esp_timer_get_time());
  e.req_us = static_cast<uint32_t>(sleep_time_us);
  e.slept_us = 0;
  e.cause = 0;
  e.arm = static_cast<uint8_t>(entered);
  e.levels = static_cast<uint8_t>(levels);
  s_pm_crumb.ring_head = head;
  s_pm_crumb.magic = kPmCrumbMagic;
  s_pm_crumb.seq = st.sleeps;
  s_pm_crumb.phase = 1U;
  return ESP_OK;
}

static esp_err_t IRAM_ATTR pm_light_sleep_exit_cb(int64_t slept_us, void *arg) {
  (void) arg;
  PmState &st = g_pm_state;
  s_pm_crumb.phase = 2U;
  bool gpio_woke = false;
  if (slept_us > 0) {
    st.sleeps = st.sleeps + 1U;
    st.slept_us = st.slept_us + static_cast<uint32_t>(slept_us);
    const uint32_t cause = rtc_cntl_ll_get_wakeup_cause();
    uint32_t head = s_pm_crumb.ring_head;
    if (head >= kPmCrumbRing) head = 0;
    uint32_t levels_out = 0;
    for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
      const int g = st.pins[i].gpio;
      if (g >= 0 && gpio_ll_get_level(&GPIO, static_cast<uint32_t>(g))) levels_out |= 1UL << (i + 4U);
    }
    PmCrumbSleep &e = s_pm_crumb.ring[head];
    e.slept_us = static_cast<uint32_t>(slept_us);
    e.cause = static_cast<uint16_t>(cause);
    e.levels = static_cast<uint8_t>((e.levels & 0x0FU) | levels_out);
    s_pm_crumb.ring_next = head + 1U == kPmCrumbRing ? 0U : head + 1U;
    gpio_woke = (cause & kPmWakeCauseGpio) != 0;
    if (gpio_woke) {
      st.gpio_wakes = st.gpio_wakes + 1U;
    } else if ((cause & kPmWakeCauseWifi) != 0) {
      st.wifi_wakes = st.wifi_wakes + 1U;
    } else if ((cause & kPmWakeCauseTimer) != 0) {
      st.timer_wakes = st.timer_wakes + 1U;
    } else {
      st.other_wakes = st.other_wakes + 1U;
    }
  }
  const uint32_t entered = st.entered_mask;
  if (entered == 0) {
    if (gpio_woke) {
      st.gpio_unattributed = st.gpio_unattributed + 1U;
      if (s_pm_wake_loop_timer != nullptr) (void) esp_timer_start_once(s_pm_wake_loop_timer, 50);
    }
    return ESP_OK;
  }
  st.entered_mask = 0;
  bool attributed = false;
  uint32_t changed_lo = 0, changed_hi = 0, isr_lo = 0, isr_hi = 0;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    if ((entered & (1UL << i)) == 0) continue;
    const uint32_t gpio_num = static_cast<uint32_t>(st.pins[i].gpio);
    gpio_ll_wakeup_disable(&GPIO, gpio_num);
    const uint32_t saved = st.pins[i].saved_type;
    gpio_ll_set_intr_type(&GPIO, gpio_num, static_cast<gpio_int_type_t>(saved));
    const uint32_t bit = 1UL << (gpio_num & 31U);
    if (gpio_num < 32U) {
      REG_WRITE(GPIO_STATUS_W1TC_REG, bit);
    } else {
      REG_WRITE(GPIO_STATUS1_W1TC_REG, bit);
    }
    GPIO.pin[gpio_num].int_ena = st.pins[i].saved_ena;
    const bool changed = static_cast<uint32_t>(gpio_ll_get_level(&GPIO, gpio_num)) != st.pins[i].armed_level;
    if (gpio_woke && changed) {
      st.pin_wakes[i] = st.pin_wakes[i] + 1U;
      attributed = true;
    }
    if (saved == static_cast<uint32_t>(GPIO_INTR_DISABLE) || st.pins[i].saved_ena == 0) continue;
    if (gpio_num < 32U) {
      isr_lo |= bit;
      if (changed) changed_lo |= bit;
    } else {
      isr_hi |= bit;
      if (changed) changed_hi |= bit;
    }
  }
  if (gpio_woke && !attributed) st.gpio_unattributed = st.gpio_unattributed + 1U;
  if ((changed_lo | changed_hi) == 0 && gpio_woke) {
    changed_lo = isr_lo;
    changed_hi = isr_hi;
  }
  if ((changed_lo | changed_hi) != 0) {
    st.synth = st.synth + 1U;
    if (changed_lo != 0) REG_WRITE(GPIO_STATUS_W1TS_REG, changed_lo);
    if (changed_hi != 0) REG_WRITE(GPIO_STATUS1_W1TS_REG, changed_hi);
  }
  if (gpio_woke || (changed_lo | changed_hi) != 0) {
    // Wake the ESPHome main loop immediately once cache is back up (IRAM-safe esp_timer_start_once).
    if (s_pm_wake_loop_timer != nullptr) (void) esp_timer_start_once(s_pm_wake_loop_timer, 50);
  }
  return ESP_OK;
}

static void IRAM_ATTR pm_tick_hook_cpu0() { s_pm_crumb.tick_us[0] = static_cast<uint32_t>(esp_timer_get_time()); }
static void IRAM_ATTR pm_tick_hook_cpu1() { s_pm_crumb.tick_us[1] = static_cast<uint32_t>(esp_timer_get_time()); }

static inline void pm_keep_raw_outputs_driven() {
#ifdef LATTER_TOUCH_RST_PIN
  gpio_sleep_sel_dis(static_cast<gpio_num_t>(LATTER_TOUCH_RST_PIN));
#endif
#ifdef LATTER_BACKLIGHT_EN_PIN
  gpio_sleep_sel_dis(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN));
#endif
#ifdef LATTER_BACKLIGHT_PIN
  gpio_sleep_sel_dis(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN));
#endif
#ifdef LATTER_C6_RESET_PIN
  gpio_sleep_sel_dis(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN));
#endif
}
#endif  // LATTER_PM_AUTO_SLEEP

#if LATTER_PM_DFS_ONLY
static esp_pm_lock_handle_t s_p4_dfs_lock = nullptr;
static bool s_p4_dfs_awake = true;
static uint32_t s_p4_loop_interval_awake = 16U;
#endif

PmCrumb &pm_previous_crumb() {
  static PmCrumb c{};
  return c;
}

void pm_init() {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock != nullptr) return;
  const esp_reset_reason_t rr = esp_reset_reason();
  if (s_pm_crumb.magic == kPmCrumbMagic &&
      (rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_PANIC)) {
    pm_previous_crumb() = s_pm_crumb;
  }
  memset(&s_pm_crumb, 0, sizeof(s_pm_crumb));
#if !defined(CONFIG_ESP_TIMER_IN_IRAM)
#error "Latter's light-sleep tick hooks need CONFIG_ESP_TIMER_IN_IRAM (selected by CONFIG_PM_SLP_IRAM_OPT)"
#endif
  esp_register_freertos_tick_hook_for_cpu(pm_tick_hook_cpu0, 0);
#if !CONFIG_FREERTOS_UNICORE
  esp_register_freertos_tick_hook_for_cpu(pm_tick_hook_cpu1, 1);
#endif
  if (s_pm_wake_loop_timer == nullptr) {
    esp_timer_create_args_t targs = {};
    targs.callback = &pm_wake_loop_timer_cb;
    targs.name = "latter_pm_wk";
    (void) esp_timer_create(&targs, &s_pm_wake_loop_timer);
  }
  pm_keep_raw_outputs_driven();
#ifdef LATTER_PWR_KEY_PIN
  st.pins[PM_WAKE_PWR_KEY].gpio = static_cast<int8_t>(LATTER_PWR_KEY_PIN);
#endif
#ifdef LATTER_ACTION_BTN_PIN
  st.pins[PM_WAKE_ACTION_BTN].gpio = static_cast<int8_t>(LATTER_ACTION_BTN_PIN);
#endif
#ifdef LATTER_TOUCH_INT_PIN
  st.pins[PM_WAKE_TOUCH].gpio = static_cast<int8_t>(LATTER_TOUCH_INT_PIN);
#endif
#ifdef LATTER_IMU_INT1_PIN
  st.pins[PM_WAKE_IMU].gpio = static_cast<int8_t>(LATTER_IMU_INT1_PIN);
#endif
  for (const PmWakePin &p : st.pins) {
    if (p.gpio >= 0) gpio_sleep_sel_dis(static_cast<gpio_num_t>(p.gpio));
  }
  esp_pm_lock_handle_t lock = nullptr;
  esp_err_t err = esp_pm_lock_create(kPmAwakeLockType, 0, "latter_awake", &lock);
  if (err != ESP_OK || lock == nullptr) {
    ESP_LOGE("power", "Light sleep: awake lock create failed (%s); light sleep stays unmanaged", esp_err_to_name(err));
    return;
  }
  esp_pm_lock_acquire(lock);
  st.awake = true;
  st.awake_since_ms = esphome::millis();
  st.loop_interval_awake = esphome::App.get_loop_interval();
  st.lock = lock;
  err = esp_sleep_enable_gpio_wakeup();
  esp_pm_sleep_cbs_register_config_t cbs = {};
  cbs.enter_cb = pm_light_sleep_enter_cb;
  cbs.exit_cb = pm_light_sleep_exit_cb;
  if (err == ESP_OK) err = esp_pm_light_sleep_register_cbs(&cbs);
  if (err != ESP_OK) {
    for (PmWakePin &p : st.pins) p.gpio = -1;
    ESP_LOGE("power", "Light sleep: wake-pin callbacks unavailable (%s); waking on timers and Wi-Fi only",
             esp_err_to_name(err));
  }
  ESP_LOGI("power", "Light sleep: awake lock held at boot, wake pins pwr=%d btn=%d touch=%d imu=%d, quiet loop %lums (%lums with touch wake)",
           st.pins[PM_WAKE_PWR_KEY].gpio, st.pins[PM_WAKE_ACTION_BTN].gpio, st.pins[PM_WAKE_TOUCH].gpio,
           st.pins[PM_WAKE_IMU].gpio, static_cast<unsigned long>(kPmQuietLoopIntervalMs),
           static_cast<unsigned long>(kPmQuietTouchLoopIntervalMs));
#elif LATTER_PM_DFS_ONLY
  if (s_p4_dfs_lock != nullptr) return;
  esp_pm_config_t pm_cfg = {};
#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
  pm_cfg.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#else
  pm_cfg.max_freq_mhz = 360;
#endif
  pm_cfg.min_freq_mhz = 40;
  pm_cfg.light_sleep_enable = false;
  if (esp_pm_configure(&pm_cfg) == ESP_OK &&
      esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "latter_awake", &s_p4_dfs_lock) == ESP_OK &&
      s_p4_dfs_lock != nullptr) {
    esp_pm_lock_acquire(s_p4_dfs_lock);
    s_p4_dfs_awake = true;
    s_p4_loop_interval_awake = esphome::App.get_loop_interval();
    ESP_LOGI("power", "ESP32-P4 DFS configured (%dMHz..40MHz, awake lock held at boot)", pm_cfg.max_freq_mhz);
  }
#endif
}

void pm_apply(bool stay_awake, uint32_t wake_mask) {
#ifdef USE_ESP32
  const bool was_stall_awake = g_stall_chip_awake.exchange(stay_awake, std::memory_order_relaxed);
  if (stay_awake && !was_stall_awake && g_stall_guard_task_handle != nullptr) {
    xTaskNotifyGive(g_stall_guard_task_handle);
  }
#endif
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock == nullptr) return;
  uint32_t valid = 0;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    if (st.pins[i].gpio >= 0) valid |= 1UL << i;
  }
  const uint32_t arm = stay_awake ? 0U : (wake_mask & valid);
  if (stay_awake == st.awake) {
    st.arm_mask = arm;
    if (!stay_awake && esphome::App.get_loop_interval() != pm_quiet_loop_interval(arm)) {
      esphome::App.set_loop_interval(pm_quiet_loop_interval(arm));
    }
    return;
  }
  const uint32_t now_ms = esphome::millis();
  if (stay_awake) {
    esp_pm_lock_acquire(st.lock);
    st.awake_since_ms = now_ms;
    st.arm_mask = 0U;
    esphome::App.set_loop_interval(st.loop_interval_awake);
  } else {
    st.arm_mask = arm;
    esphome::App.set_loop_interval(pm_quiet_loop_interval(arm));
    esp_pm_lock_release(st.lock);
    st.awake_ms_total += now_ms - st.awake_since_ms;
    st.dump_pending = true;
  }
  st.awake = stay_awake;
  if (profiler_enabled()) {
    LATTER_PROF_LOGI("[PROF:PM] lock=%s arm=0x%02lx loop=%lums", stay_awake ? "held" : "released",
             static_cast<unsigned long>(arm), static_cast<unsigned long>(esphome::App.get_loop_interval()));
  }
#elif LATTER_PM_DFS_ONLY
  (void) wake_mask;
  if (s_p4_dfs_lock == nullptr || stay_awake == s_p4_dfs_awake) return;
  if (stay_awake) {
    esp_pm_lock_acquire(s_p4_dfs_lock);
    esphome::App.set_loop_interval(s_p4_loop_interval_awake);
  } else {
    esphome::App.set_loop_interval(250U);
    esp_pm_lock_release(s_p4_dfs_lock);
  }
  s_p4_dfs_awake = stay_awake;
  LATTER_PROF_LOGI("[PROF:PM] p4_dfs=%s loop=%lums", stay_awake ? "max" : "40MHz",
                   static_cast<unsigned long>(esphome::App.get_loop_interval()));
#else
  (void) stay_awake;
  (void) wake_mask;
#endif
}

void pm_note_imu(bool armed, uint32_t events) {
#if LATTER_PM_AUTO_SLEEP
  s_pm_crumb.imu_armed = armed ? 1U : 0U;
  s_pm_crumb.imu_events = events;
#else
  (void) armed;
  (void) events;
#endif
}

const char *pm_crash_where() {
  const PmCrumb &c = pm_previous_crumb();
  if (c.magic != kPmCrumbMagic) return "";
  return c.phase == 1U ? "ls-in" : "ls-out";
}

bool pm_sleep_stats(PmSleepStats &out, uint32_t now_ms) {
#if LATTER_PM_AUTO_SLEEP
  const PmState &st = g_pm_state;
  if (st.lock == nullptr) return false;
  static uint64_t total_us = 0;
  static uint32_t last_us = 0;
  const uint32_t cur_us = st.slept_us;
  total_us += cur_us - last_us;
  last_us = cur_us;
  out.sleeps = st.sleeps;
  out.slept_us = total_us;
  out.awake_ms = st.awake_ms_total + (st.awake ? now_ms - st.awake_since_ms : 0U);
  out.timer_wakes = st.timer_wakes;
  out.gpio_wakes = st.gpio_wakes;
  out.wifi_wakes = st.wifi_wakes;
  out.other_wakes = st.other_wakes;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) out.pin_wakes[i] = st.pin_wakes[i];
  out.gpio_unattributed = st.gpio_unattributed;
  return true;
#else
  (void) out;
  (void) now_ms;
  return false;
#endif
}

static void pm_log_dump_lines(const char *label, size_t len, void (*dump)(FILE *)) {
#ifdef USE_ESP32
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
#else
  (void) label;
  (void) len;
  (void) dump;
#endif
}

void pm_log_profile() {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock == nullptr) return;
  static uint32_t last_ms = 0, last_slept_us = 0, last_sleeps = 0, last_gpio = 0, last_synth = 0, last_dump_ms = 0;
  const uint32_t now_ms = esphome::millis();
  const uint32_t slept_us = st.slept_us, sleeps = st.sleeps, gpio_wakes = st.gpio_wakes, synth = st.synth;
  uint32_t permille = 0;
  if (last_ms != 0 && now_ms != last_ms) {
    permille = (slept_us - last_slept_us) / (now_ms - last_ms);
    if (permille > 1000U) permille = 1000U;
  }
  LATTER_PROF_LOGI("[PROF:PM] lock=%s ls=%lu.%lu%% sleeps=%lu gpio_wakes=%lu synth=%lu arm=0x%02lx loop=%lums",
           st.awake ? "held" : "released", static_cast<unsigned long>(permille / 10U),
           static_cast<unsigned long>(permille % 10U), static_cast<unsigned long>(sleeps - last_sleeps),
           static_cast<unsigned long>(gpio_wakes - last_gpio), static_cast<unsigned long>(synth - last_synth),
           static_cast<unsigned long>(st.arm_mask), static_cast<unsigned long>(esphome::App.get_loop_interval()));
  last_ms = now_ms;
  last_slept_us = slept_us;
  last_sleeps = sleeps;
  last_gpio = gpio_wakes;
  last_synth = synth;
  if (st.awake || (!st.dump_pending && now_ms - last_dump_ms < 60000U)) return;
  st.dump_pending = false;
  last_dump_ms = now_ms;
  pm_log_dump_lines("[PROF:PM_LOCKS]", 2048, [](FILE *f) { esp_pm_dump_locks(f); });
#endif
}

void pm_log_wake_profile() {
  static uint32_t last_ms = 0;
  const uint32_t now_ms = esphome::millis();
  if (last_ms != 0 && now_ms - last_ms < 60000U) return;
  const uint32_t window_ms = (last_ms == 0) ? now_ms : now_ms - last_ms;
  last_ms = now_ms;

#if LATTER_PM_AUTO_SLEEP
  {
    static uint32_t p_att = 0, p_tb = 0, p_rb = 0, p_sl = 0, p_req[5] = {0, 0, 0, 0, 0};
    const PmWakeHist &wh = g_pm_wake_hist;
    const uint32_t att = wh.attempts, tb = wh.timer_bound, rb = wh.rtos_bound, sl = g_pm_state.sleeps;
    uint32_t req[5];
    for (int i = 0; i < 5; i++) req[i] = wh.req[i];
    LATTER_PROF_LOGI(
             "[PROF:WAKE] window=%lus sleeps: attempts=%lu slept=%lu bound_by esp_timer=%lu rtos=%lu "
             "req <20ms=%lu <50ms=%lu <200ms=%lu <1s=%lu >=1s=%lu lock=%s",
             (unsigned long)(window_ms / 1000U), (unsigned long)(att - p_att), (unsigned long)(sl - p_sl),
             (unsigned long)(tb - p_tb), (unsigned long)(rb - p_rb), (unsigned long)(req[0] - p_req[0]),
             (unsigned long)(req[1] - p_req[1]), (unsigned long)(req[2] - p_req[2]),
             (unsigned long)(req[3] - p_req[3]), (unsigned long)(req[4] - p_req[4]),
             g_pm_state.awake ? "held" : "released");
    p_att = att;
    p_tb = tb;
    p_rb = rb;
    p_sl = sl;
    for (int i = 0; i < 5; i++) p_req[i] = req[i];
  }
#endif

#ifdef USE_LATTER_LOOP_PROBE
  {
    static esphome::latter_core::LoopProbeStats prev{};
    const esphome::latter_core::LoopProbeStats &lp = esphome::latter_core::loop_probe_stats();
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

#if defined(USE_ESP32) && defined(CONFIG_ESP_TIMER_PROFILING)
  pm_log_dump_lines("[PROF:WAKE_TIMER]", 6144, [](FILE *f) { esp_timer_dump(f); });
#elif defined(USE_ESP32)
  LATTER_PROF_LOGI("[PROF:WAKE_TIMER] off (build with substitution wake_profiling: \"y\")");
#endif

#if defined(USE_ESP32) && defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
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
#elif defined(USE_ESP32)
  LATTER_PROF_LOGI("[PROF:WAKE_TASK] off (build with substitution wake_profiling: \"y\")");
#endif
}

}  // namespace latter_ui

#ifdef USE_ESP32
extern "C" void esp_task_wdt_isr_user_handler(void) {
  using namespace latter_ui;
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

extern "C" {
esp_err_t __real_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t __wrap_nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
  const int64_t t0 = esp_timer_get_time();
  const esp_err_t err = __real_nvs_set_blob(handle, key, value, length);
  const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
  auto &fw = latter_ui::g_flash_write_stats;
  fw.nvs_writes.fetch_add(1U, std::memory_order_relaxed);
  uint32_t prev = fw.nvs_max_us.load(std::memory_order_relaxed);
  while (us > prev && !fw.nvs_max_us.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
  }
  return err;
}

esp_err_t __real_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len);
esp_err_t __wrap_esp_flash_erase_region(esp_flash_t *chip, uint32_t start, uint32_t len) {
  latter_ui::g_flash_write_stats.erases.fetch_add(1U, std::memory_order_relaxed);
  return __real_esp_flash_erase_region(chip, start, len);
}
}  // extern "C"
#endif
