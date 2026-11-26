#if __has_include("hal_sleep_pm.h")
#include "hal_sleep_pm.h"
#else
#include "../../include/hal_sleep_pm.h"
#endif

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if __has_include("esphome/core/application.h")
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#endif

#include <atomic>
#include <cstring>

#ifdef USE_ESP32
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_freertos_hooks.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>

#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_PM_ENABLE)
#error "ESP32-P4 must not enable esp32_power_management (40 MHz XTAL DFS retunes 200 MHz PSRAM across esp_hosted SDIO DMA; use fixed CPLL + quiet-loop pacing)"
#endif

#if defined(CONFIG_PM_ENABLE) && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
#define LATTER_PM_AUTO_SLEEP 1
#else
#define LATTER_PM_AUTO_SLEEP 0
#endif

#if !LATTER_PM_AUTO_SLEEP && defined(CONFIG_IDF_TARGET_ESP32P4) && LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
#define LATTER_PM_QUIET_LOOP_ONLY 1
#else
#define LATTER_PM_QUIET_LOOP_ONLY 0
#endif

#if LATTER_PM_AUTO_SLEEP
#if !defined(CONFIG_PM_LIGHT_SLEEP_CALLBACKS)
#error "power_management enable_light_sleep needs CONFIG_PM_LIGHT_SLEEP_CALLBACKS: \"y\" in the board HAL"
#endif
#if !LATTER_SOC_S3
#error "Latter's light-sleep wake path is written for the ESP32-S3 GPIO block (bench-validate before enabling on another SoC)"
#endif
#endif

// Isolate ESP32-S3 private LL/register headers strictly behind #if LATTER_SOC_S3.
#if LATTER_SOC_S3 && LATTER_PM_AUTO_SLEEP
#include <hal/gpio_ll.h>
#include <hal/rtc_cntl_ll.h>
#include <soc/gpio_reg.h>
#endif

namespace latter_core {

#if LATTER_PM_AUTO_SLEEP || LATTER_PM_QUIET_LOOP_ONLY
static void pm_wake_app_loop() {
  [](auto &app) {
    if constexpr (requires { app.wake_loop_threadsafe(); }) {
      app.wake_loop_threadsafe();
    }
  }(esphome::App);
}
#endif

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
  bool lock_held;
  bool dump_pending;
  uint32_t loop_interval_awake;
  PmWakeGraceWindow grace;
  volatile uint32_t arm_mask;
  uint32_t entered_mask;
  PmWakePin pins[PM_WAKE_SLOTS];
  volatile uint8_t wake_grace_isr;
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
static DRAM_ATTR esp_timer_handle_t s_pm_grace_end_timer = nullptr;
static std::atomic<uint8_t> s_pm_wake_grace_req{0U};
// ESP-IDF sets -mdisable-hardware-atomics when SPIRAM + S32C1I are active (is_always_lock_free == false);
// 1/2/4-byte atomics stay IRAM-safe via noflash stdatomic helpers.
static_assert(sizeof(s_pm_wake_grace_req) <= 4U,
              "PM wake-grace atomic must stay <=4 B for IRAM stdatomic fast path");

static PmState g_pm_state = {nullptr, true, false, false, 16U, {}, 0U, 0U,
                             {{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}},
                             0U, 0U, 0U, 0U, 0U,
                             0U, 0U, 0U, {0U, 0U, 0U, 0U}, 0U,
                             0U, 0U};

struct PmWakeHist {
  volatile uint32_t attempts;
  volatile uint32_t timer_bound;
  volatile uint32_t rtos_bound;
  volatile uint32_t req[5];
};
static PmWakeHist g_pm_wake_hist = {0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U}};

static void pm_grace_end_timer_cb(void *) {
  pm_wake_app_loop();
}

static void pm_wake_loop_timer_cb(void *) {
  PmState &st = g_pm_state;
  if (st.wake_grace_isr != 0U) {
    st.wake_grace_isr = 0U;
    s_pm_wake_grace_req.store(1U, std::memory_order_relaxed);
    if (s_pm_grace_end_timer != nullptr) {
      (void) esp_timer_stop(s_pm_grace_end_timer);
      (void) esp_timer_start_once(s_pm_grace_end_timer, static_cast<uint64_t>(kPmWakeGraceMs) * 1000ULL);
    }
  }
  pm_wake_app_loop();
}

static void pm_sync_loop_and_lock(uint32_t now_ms) {
  PmState &st = g_pm_state;
  if (st.lock == nullptr) return;
  if (!st.grace.active(now_ms)) {
    st.grace.clear();
  }
  const bool want_fast = st.awake || st.grace.active(now_ms);
  if (want_fast && !st.lock_held) {
    esp_pm_lock_acquire(st.lock);
    st.lock_held = true;
  }
  const uint32_t want_interval = want_fast ? st.loop_interval_awake : pm_quiet_loop_interval(st.arm_mask);
  if (esphome::App.get_loop_interval() != want_interval) {
    esphome::App.set_loop_interval(want_interval);
  }
  if (!want_fast && st.lock_held) {
    esp_pm_lock_release(st.lock);
    st.lock_held = false;
  }
}

#if LATTER_SOC_S3
static void IRAM_ATTR pm_s3_arm_wake_pins_iram(PmState &st, uint32_t mask, uint32_t &entered, uint32_t &levels) {
  entered = 0U;
  levels = 0U;
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
}

static uint32_t IRAM_ATTR pm_s3_wakeup_cause_iram() {
  return rtc_cntl_ll_get_wakeup_cause();
}

static uint32_t IRAM_ATTR pm_s3_exit_levels_iram(const PmState &st) {
  uint32_t levels_out = 0U;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    const int g = st.pins[i].gpio;
    if (g >= 0 && gpio_ll_get_level(&GPIO, static_cast<uint32_t>(g))) levels_out |= 1UL << (i + 4U);
  }
  return levels_out;
}

static bool IRAM_ATTR pm_s3_disarm_and_synth_wake_pins_iram(PmState &st, uint32_t entered, bool gpio_woke) {
  bool attributed = false;
  uint32_t changed_lo = 0U;
  uint32_t changed_hi = 0U;
  uint32_t isr_lo = 0U;
  uint32_t isr_hi = 0U;
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
  if ((changed_lo | changed_hi) == 0U && gpio_woke) {
    changed_lo = isr_lo;
    changed_hi = isr_hi;
  }
  if ((changed_lo | changed_hi) != 0U) {
    st.synth = st.synth + 1U;
    if (changed_lo != 0U) REG_WRITE(GPIO_STATUS_W1TS_REG, changed_lo);
    if (changed_hi != 0U) REG_WRITE(GPIO_STATUS1_W1TS_REG, changed_hi);
  }
  return gpio_woke || (changed_lo | changed_hi) != 0U;
}
#endif  // LATTER_SOC_S3

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
  uint32_t entered = 0U;
  uint32_t levels = 0U;
#if LATTER_SOC_S3
  pm_s3_arm_wake_pins_iram(st, st.arm_mask, entered, levels);
#endif
  st.entered_mask = entered;
  uint32_t head = s_pm_crumb.ring_next;
  if (head >= kPmCrumbRing) head = 0U;
  PmCrumbSleep &e = s_pm_crumb.ring[head];
  e.t_us = static_cast<uint32_t>(esp_timer_get_time());
  e.req_us = static_cast<uint32_t>(sleep_time_us);
  e.slept_us = 0U;
  e.cause = 0U;
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
    uint32_t cause = 0U;
    uint32_t levels_out = 0U;
#if LATTER_SOC_S3
    cause = pm_s3_wakeup_cause_iram();
    levels_out = pm_s3_exit_levels_iram(st);
#endif
    uint32_t head = s_pm_crumb.ring_head;
    if (head >= kPmCrumbRing) head = 0U;
    PmCrumbSleep &e = s_pm_crumb.ring[head];
    e.slept_us = static_cast<uint32_t>(slept_us);
    e.cause = static_cast<uint16_t>(cause);
    e.levels = static_cast<uint8_t>((e.levels & 0x0FU) | levels_out);
    s_pm_crumb.ring_next = head + 1U == kPmCrumbRing ? 0U : head + 1U;
    gpio_woke = (cause & kPmWakeCauseGpio) != 0U;
    if (gpio_woke) {
      st.gpio_wakes = st.gpio_wakes + 1U;
    } else if ((cause & kPmWakeCauseWifi) != 0U) {
      st.wifi_wakes = st.wifi_wakes + 1U;
    } else if ((cause & kPmWakeCauseTimer) != 0U) {
      st.timer_wakes = st.timer_wakes + 1U;
    } else {
      st.other_wakes = st.other_wakes + 1U;
    }
  }
  const uint32_t entered = st.entered_mask;
  if (entered == 0U) {
    if (gpio_woke) {
      st.gpio_unattributed = st.gpio_unattributed + 1U;
      st.wake_grace_isr = 1U;
      if (s_pm_wake_loop_timer != nullptr) (void) esp_timer_start_once(s_pm_wake_loop_timer, 50);
    }
    return ESP_OK;
  }
  st.entered_mask = 0U;
  bool should_wake_loop = gpio_woke;
#if LATTER_SOC_S3
  should_wake_loop = pm_s3_disarm_and_synth_wake_pins_iram(st, entered, gpio_woke);
#endif
  if (should_wake_loop) {
    st.wake_grace_isr = 1U;
    if (s_pm_wake_loop_timer != nullptr) (void) esp_timer_start_once(s_pm_wake_loop_timer, 50);
  }
  return ESP_OK;
}

#ifdef CONFIG_ESP_TIMER_PROFILING
static void IRAM_ATTR pm_tick_hook_cpu0() { s_pm_crumb.tick_us[0] = static_cast<uint32_t>(esp_timer_get_time()); }
static void IRAM_ATTR pm_tick_hook_cpu1() { s_pm_crumb.tick_us[1] = static_cast<uint32_t>(esp_timer_get_time()); }
#endif

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

#if LATTER_PM_QUIET_LOOP_ONLY
static bool s_p4_quiet_inited = false;
static DRAM_ATTR std::atomic<bool> s_p4_quiet_awake{true};
static uint32_t s_p4_loop_interval_awake = 16U;
static uint32_t s_p4_wake_mask = 0U;
static PmWakeGraceWindow s_p4_grace{};
static DRAM_ATTR esp_timer_handle_t s_p4_wake_loop_timer = nullptr;
static std::atomic<uint8_t> s_p4_wake_grace_req{0U};
static_assert(sizeof(s_p4_quiet_awake) <= 4U && sizeof(s_p4_wake_grace_req) <= 4U,
              "PM P4 quiet-awake / wake-grace atomics must stay <=4 B for IRAM ISR/timer access");

static void p4_wake_loop_timer_cb(void *) {
  s_p4_wake_grace_req.store(1U, std::memory_order_relaxed);
  pm_wake_app_loop();
}
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
  std::memset(&s_pm_crumb, 0, sizeof(s_pm_crumb));
#if !defined(CONFIG_ESP_TIMER_IN_IRAM)
#error "Latter's light-sleep wake timer needs CONFIG_ESP_TIMER_IN_IRAM (selected by CONFIG_PM_SLP_IRAM_OPT)"
#endif
#ifdef CONFIG_ESP_TIMER_PROFILING
  esp_register_freertos_tick_hook_for_cpu(pm_tick_hook_cpu0, 0);
#if !CONFIG_FREERTOS_UNICORE
  esp_register_freertos_tick_hook_for_cpu(pm_tick_hook_cpu1, 1);
#endif
#endif
  if (s_pm_wake_loop_timer == nullptr) {
    esp_timer_create_args_t targs = {};
    targs.callback = &pm_wake_loop_timer_cb;
    targs.name = "latter_pm_wk";
    (void) esp_timer_create(&targs, &s_pm_wake_loop_timer);
  }
  if (s_pm_grace_end_timer == nullptr) {
    esp_timer_create_args_t gargs = {};
    gargs.callback = &pm_grace_end_timer_cb;
    gargs.name = "latter_pm_gr";
    (void) esp_timer_create(&gargs, &s_pm_grace_end_timer);
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
  st.lock_held = true;
  st.grace.clear();
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
  ESP_LOGD("power",
           "Light sleep: awake lock held at boot, wake pins pwr=%d btn=%d touch=%d imu=%d, quiet loop %lums (%lums with touch wake)",
           st.pins[PM_WAKE_PWR_KEY].gpio, st.pins[PM_WAKE_ACTION_BTN].gpio, st.pins[PM_WAKE_TOUCH].gpio,
           st.pins[PM_WAKE_IMU].gpio, static_cast<unsigned long>(kPmQuietLoopIntervalMs),
           static_cast<unsigned long>(kPmQuietTouchLoopIntervalMs));
#elif LATTER_PM_QUIET_LOOP_ONLY
  if (s_p4_quiet_inited) return;
#if !defined(CONFIG_ESP_TIMER_IN_IRAM)
#error "ESP32-P4 battery quiet-loop touch wake timer needs CONFIG_ESP_TIMER_IN_IRAM in chip_esp32p4.yaml"
#endif
  if (s_p4_wake_loop_timer == nullptr) {
    esp_timer_create_args_t targs = {};
    targs.callback = &p4_wake_loop_timer_cb;
    targs.name = "latter_p4_wk";
    (void) esp_timer_create(&targs, &s_p4_wake_loop_timer);
  }
  s_p4_quiet_inited = true;
  s_p4_quiet_awake.store(true, std::memory_order_relaxed);
  s_p4_loop_interval_awake = esphome::App.get_loop_interval();
  ESP_LOGD("power",
           "ESP32-P4 battery quiet-loop pacing ready (fixed CPLL/200MHz PSRAM; quiet loop %lums, %lums with touch wake)",
           static_cast<unsigned long>(kPmQuietLoopIntervalMs),
           static_cast<unsigned long>(kPmQuietTouchLoopIntervalMs));
#endif
}

void pm_apply(bool stay_awake, uint32_t wake_mask) {
  stall_guard_notify_chip_awake(stay_awake);
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock == nullptr) return;
  uint32_t valid = 0U;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    if (st.pins[i].gpio >= 0) valid |= 1UL << i;
  }
  const uint32_t arm = stay_awake ? 0U : (wake_mask & valid);
  const uint32_t now_ms = esphome::millis();
  if (s_pm_wake_grace_req.exchange(0U, std::memory_order_relaxed) != 0U && !stay_awake && !st.awake) {
    st.grace.arm(now_ms, kPmWakeGraceMs);
  }
  if (stay_awake == st.awake) {
    st.arm_mask = arm;
    pm_sync_loop_and_lock(now_ms);
    return;
  }
  if (stay_awake) {
    st.awake_since_ms = now_ms;
    st.arm_mask = 0U;
    st.grace.clear();
    if (s_pm_grace_end_timer != nullptr) (void) esp_timer_stop(s_pm_grace_end_timer);
    st.awake = true;
    pm_sync_loop_and_lock(now_ms);
  } else {
    st.arm_mask = arm;
    st.grace.clear();
    if (s_pm_grace_end_timer != nullptr) (void) esp_timer_stop(s_pm_grace_end_timer);
    st.awake = false;
    pm_sync_loop_and_lock(now_ms);
    st.awake_ms_total += now_ms - st.awake_since_ms;
    st.dump_pending = true;
  }
  if (profiler_enabled()) {
    LATTER_PROF_LOGI("[PROF:PM] lock=%s arm=0x%02lx loop=%lums", st.lock_held ? "held" : "released",
                     static_cast<unsigned long>(arm), static_cast<unsigned long>(esphome::App.get_loop_interval()));
  }
#elif LATTER_PM_QUIET_LOOP_ONLY
  if (!s_p4_quiet_inited) return;
  s_p4_wake_mask = stay_awake ? 0U : wake_mask;
  const uint32_t now_p4 = esphome::millis();
  const bool was_awake = s_p4_quiet_awake.load(std::memory_order_relaxed);
  if (stay_awake) {
    s_p4_grace.clear();
    s_p4_wake_grace_req.store(0U, std::memory_order_relaxed);
  } else if (s_p4_wake_grace_req.exchange(0U, std::memory_order_relaxed) != 0U) {
    s_p4_grace.arm(now_p4, kPmWakeGraceMs);
  }
  const uint32_t target_loop =
      (stay_awake || s_p4_grace.active(now_p4)) ? s_p4_loop_interval_awake : pm_quiet_loop_interval(wake_mask);
  if (stay_awake == was_awake) {
    if (esphome::App.get_loop_interval() != target_loop) {
      esphome::App.set_loop_interval(target_loop);
    }
    return;
  }
  esphome::App.set_loop_interval(target_loop);
  s_p4_quiet_awake.store(stay_awake, std::memory_order_relaxed);
  LATTER_PROF_LOGI("[PROF:PM] p4_quiet=%s loop=%lums", stay_awake ? "awake" : "standby",
                   static_cast<unsigned long>(esphome::App.get_loop_interval()));
#else
  (void) stay_awake;
  (void) wake_mask;
#endif
}

void pm_poll_loop(uint32_t now_ms) {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock == nullptr) return;
  if (s_pm_wake_grace_req.exchange(0U, std::memory_order_relaxed) != 0U && !st.awake) {
    st.grace.arm(now_ms, kPmWakeGraceMs);
  }
  if (!st.awake) {
    pm_sync_loop_and_lock(now_ms);
  }
#elif LATTER_PM_QUIET_LOOP_ONLY
  if (!s_p4_quiet_inited || s_p4_quiet_awake.load(std::memory_order_relaxed)) return;
  if (s_p4_wake_grace_req.exchange(0U, std::memory_order_relaxed) != 0U) {
    s_p4_grace.arm(now_ms, kPmWakeGraceMs);
  }
  if (!s_p4_grace.active(now_ms)) {
    s_p4_grace.clear();
  }
  const uint32_t want_loop =
      s_p4_grace.active(now_ms) ? s_p4_loop_interval_awake : pm_quiet_loop_interval(s_p4_wake_mask);
  if (esphome::App.get_loop_interval() != want_loop) {
    esphome::App.set_loop_interval(want_loop);
  }
#else
  (void) now_ms;
#endif
}

void pm_wake_grace_arm(uint32_t now_ms) {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock == nullptr || st.awake) return;
  st.grace.arm(now_ms, kPmWakeGraceMs);
  if (s_pm_grace_end_timer != nullptr) {
    (void) esp_timer_stop(s_pm_grace_end_timer);
    (void) esp_timer_start_once(s_pm_grace_end_timer, static_cast<uint64_t>(kPmWakeGraceMs) * 1000ULL);
  }
  pm_sync_loop_and_lock(now_ms);
#elif LATTER_PM_QUIET_LOOP_ONLY
  if (!s_p4_quiet_inited || s_p4_quiet_awake.load(std::memory_order_relaxed)) return;
  s_p4_grace.arm(now_ms, kPmWakeGraceMs);
  if (esphome::App.get_loop_interval() != s_p4_loop_interval_awake) {
    esphome::App.set_loop_interval(s_p4_loop_interval_awake);
  }
#else
  (void) now_ms;
#endif
}

void IRAM_ATTR pm_wake_loop_from_isr() noexcept {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (!st.awake && s_pm_wake_loop_timer != nullptr) {
    st.wake_grace_isr = 1U;
    (void) esp_timer_start_once(s_pm_wake_loop_timer, 50);
  }
#elif LATTER_PM_QUIET_LOOP_ONLY
  if (!s_p4_quiet_awake.load(std::memory_order_relaxed) && s_p4_wake_loop_timer != nullptr) {
    (void) esp_timer_start_once(s_p4_wake_loop_timer, 50);
  }
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

bool pm_take_profile_snapshot(PmProfileSnapshot &out, bool clear_dump_pending) noexcept {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock == nullptr) {
    out = PmProfileSnapshot{};
    return false;
  }
  const PmWakeHist &wh = g_pm_wake_hist;
  out.valid = true;
  out.awake = st.awake;
  out.dump_pending = st.dump_pending;
  if (clear_dump_pending) {
    st.dump_pending = false;
  }
  out.slept_us = st.slept_us;
  out.sleeps = st.sleeps;
  out.gpio_wakes = st.gpio_wakes;
  out.synth = st.synth;
  out.arm_mask = st.arm_mask;
  out.attempts = wh.attempts;
  out.timer_bound = wh.timer_bound;
  out.rtos_bound = wh.rtos_bound;
  for (int i = 0; i < 5; i++) out.req[i] = wh.req[i];
  return true;
#else
  (void) clear_dump_pending;
  out = PmProfileSnapshot{};
  return false;
#endif
}

}  // namespace latter_core
#endif  // USE_ESP32
