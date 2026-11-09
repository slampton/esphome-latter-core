#pragma once

// ==============================================================================
// Latter Core Automatic Light Sleep: Awake Lock, Wake Pins & Sleep Pad Config (hal_power_pm.h)
// ==============================================================================
// Boards whose HAL declares ESPHome's power_management (platform esp32_power_management,
// enable_light_sleep: true) let ESP-IDF enter automatic light sleep whenever no task is runnable and
// no power-management lock is held. Latter adds three things on top of the upstream component:
//
// 1. The awake lock ("latter_awake"). It is created held in pm_init() (the screen is on at boot) and
//    from then on only pm_apply() moves it, which only script apply_power_policy calls
//    (latter-verify enforces both). It is released only in screen-off quiet on battery. It is a
//    CPU_FREQ_MAX lock, so an awake device runs exactly as it did before light sleep existed (fixed
//    maximum CPU clock, no frequency scaling); scaling to min_frequency and light sleep happen only
//    while it is released.
//
// 2. Light-sleep wake pins from board traits: LATTER_PWR_KEY_PIN, LATTER_ACTION_BTN_PIN,
//    LATTER_TOUCH_INT_PIN, LATTER_IMU_INT1_PIN. GPIO wake is level-only and the GPIO edge detector
//    is not clocked in light sleep, while the ESPHome components that own these pins expect edge
//    interrupts. So the pins switch to level wake only inside ESP-IDF's light-sleep critical section
//    (CONFIG_PM_LIGHT_SLEEP_CALLBACKS), where interrupts are masked on the sleeping core:
//      enter: take the pin's interrupt away from every CPU (int_ena = 0), then wake level = the
//             opposite of the current level (any change wakes the chip)
//      exit:  restore the component's edge interrupt type, clear the status the level type latched,
//             give the interrupt back to its CPU, then raise the pin's interrupt status
//             (GPIO_STATUS_W1TS) when its level changed or a GPIO woke the chip, so the owning
//             component's ISR runs exactly once.
//    The critical section masks only the sleeping core. The other core keeps interrupts enabled
//    before ESP-IDF stalls it and after it is released on wake, and a component's ISR usually lives
//    there (the loop task's core). Masking int_ena while the pin is a level wake means no core can
//    take a level interrupt from it (no interrupt storm); GPIO wake-up itself doesn't need int_ena.
//    An edge that happened while the edge detector slept is still delivered. gpio_ll_wakeup_disable()
//    is used instead of gpio_wakeup_disable(), which would re-enable the pad's sleep isolation.
//    The arming mask is set by pm_apply() and is zero while the lock is held.
//
// 3. Sleep pad config for raw-IDF outputs: the S3 light-sleep GPIO reset workaround
//    (CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND, which selects CONFIG_PM_SLP_DISABLE_GPIO) isolates every
//    pad in light sleep unless gpio_sleep_sel_dis() was called for it. ESPHome already does that for
//    every pin it manages (esp32/gpio.cpp setup() under CONFIG_FREERTOS_USE_TICKLESS_IDLE); Latter
//    does it for the raw-IDF outputs named by board traits (LATTER_TOUCH_RST_PIN, backlight, C6 reset).
//
// The main loop interval stretches while the lock is released (kPmQuietLoopIntervalMs, or
// kPmQuietTouchLoopIntervalMs while touch is an armed wake pin; ESPHome's scheduler runs on its own
// deadlines, so timers and intervals still fire on time) and returns to the configured interval when
// it is taken again.
// ==============================================================================

#include "esphome.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef USE_ESP32
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#endif

#if defined(USE_ESP32) && defined(CONFIG_PM_ENABLE) && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
#define LATTER_PM_AUTO_SLEEP 1
#else
#define LATTER_PM_AUTO_SLEEP 0
#endif

#if LATTER_PM_AUTO_SLEEP
#if !defined(CONFIG_PM_LIGHT_SLEEP_CALLBACKS)
#error "power_management enable_light_sleep needs CONFIG_PM_LIGHT_SLEEP_CALLBACKS: \"y\" in the board HAL (light-sleep wake pins arm in the sleep callbacks)"
#endif
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Latter's light-sleep wake path is written for the ESP32-S3 GPIO block (bench-validate before enabling it on another SoC)"
#endif
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <hal/gpio_ll.h>
#include <hal/rtc_cntl_ll.h>
#include <soc/gpio_reg.h>
#endif

namespace latter_ui {

inline bool &profiler_enabled();

// Wake-pin slots, in arming-mask bit order (pm_wake_bit).
enum PmWakeSlot : uint8_t {
  PM_WAKE_PWR_KEY = 0,
  PM_WAKE_ACTION_BTN = 1,
  PM_WAKE_TOUCH = 2,
  PM_WAKE_IMU = 3,
  PM_WAKE_SLOTS = 4,
};

inline constexpr uint32_t pm_wake_bit(PmWakeSlot slot) { return 1UL << static_cast<uint32_t>(slot); }

// Main loop interval while the awake lock is released (ESPHome's default is 16 ms). Component loop()
// passes wait for it; scheduler items (intervals, timeouts) and ISR-driven components such as the
// GPIO binary sensors wake the loop on their own. The touchscreen ISR does not wake it, so while touch
// is an armed wake pin a shorter interval keeps a screen-off tap responsive.
constexpr uint32_t kPmQuietLoopIntervalMs = 1000U;
constexpr uint32_t kPmQuietTouchLoopIntervalMs = 250U;

inline constexpr uint32_t pm_quiet_loop_interval(uint32_t arm_mask) {
  return (arm_mask & pm_wake_bit(PM_WAKE_TOUCH)) != 0 ? kPmQuietTouchLoopIntervalMs : kPmQuietLoopIntervalMs;
}

// Light-sleep breadcrumb in RTC memory: it survives a watchdog or panic reset (not a power-on). An
// interrupt-watchdog reset without a panic record means a CPU couldn't take the watchdog interrupt at all
// (interrupts fully masked, or stalled by the other core), which in light-sleep quiet points into
// ESP-IDF's sleep path. The breadcrumb tells the next boot whether the last light sleep had entered
// (phase 1: between the enter and exit callbacks) or come back (phase 2), and keeps a ring of the last
// sleeps: the in-flight attempt at ring[head] while phase 1, the completed ones before it. The sleep
// callbacks write the phase and the ring, the main loop writes the IMU fields (pm_note_imu), and a
// FreeRTOS tick hook on each CPU stamps tick_us: the CPU whose ticks stopped first is the one that hung,
// and its last tick against the last ring entry says how long after that sleep it hung.
struct PmCrumbSleep {
  uint32_t t_us;      // esp_timer time at entry (low 32 bits, wraps ~71 min)
  uint32_t req_us;    // requested sleep (low 32 bits)
  uint32_t slept_us;  // 0 while in flight
  uint16_t cause;     // RTC wake cause (kPmWakeCause* bits)
  uint8_t arm;        // pins armed as level wakes (PmWakeSlot bits)
  uint8_t levels;     // wake-pin levels: bits 0-3 at entry, bits 4-7 at exit (PmWakeSlot order)
};
constexpr uint32_t kPmCrumbRing = 5U;  // the in-flight sleep plus the last 4 that slept
struct PmCrumb {
  uint32_t magic;
  uint32_t phase;       // 1 entered, 2 exited
  uint32_t seq;         // sleeps that had slept before the last entry
  uint32_t imu_armed;   // QMI8658 Wake-on-Motion armed when the main loop last noted it
  uint32_t imu_events;  // WoM events counted by then
  uint32_t ring_head;   // the last entry's slot
  uint32_t ring_next;   // the next entry's slot (ring_head again when the last attempt didn't sleep)
  uint32_t tick_us[2];  // last FreeRTOS tick per CPU (esp_timer low 32 bits)
  PmCrumbSleep ring[kPmCrumbRing];
};
constexpr uint32_t kPmCrumbMagic = 0x4C534C50UL;  // "LSLP"

#if LATTER_PM_AUTO_SLEEP
constexpr esp_pm_lock_type_t kPmAwakeLockType = ESP_PM_CPU_FREQ_MAX;
// ESP32-S3 RTC_CNTL wake-cause bits: RTC_GPIO_TRIG_EN / RTC_TIMER_TRIG_EN / RTC_WIFI_TRIG_EN in
// esp_hw_support/port/esp32s3/include/soc/rtc.h (not included here: that header also defines global
// macros such as MHZ).
constexpr uint32_t kPmWakeCauseGpio = 1UL << 2;
constexpr uint32_t kPmWakeCauseTimer = 1UL << 3;
constexpr uint32_t kPmWakeCauseWifi = 1UL << 5;

struct PmWakePin {
  int8_t gpio;
  uint8_t armed_level;
  uint8_t saved_type;
  uint8_t saved_ena;  // the pin's per-CPU interrupt enable bits (GPIO_PINn_INT_ENA)
};

struct PmState {
  esp_pm_lock_handle_t lock;
  bool awake;
  bool dump_pending;
  uint32_t loop_interval_awake;
  volatile uint32_t arm_mask;      // written by pm_apply (main loop), read by the sleep callbacks
  uint32_t entered_mask;           // pins switched to level wake by the current enter callback
  PmWakePin pins[PM_WAKE_SLOTS];
  volatile uint32_t sleeps;        // light sleeps that actually slept
  volatile uint32_t slept_us;      // wraps (~71 min); readers accumulate deltas
  volatile uint32_t gpio_wakes;    // sleeps ended by a GPIO
  volatile uint32_t synth;         // exits that delivered a pin edge to its ISR
  // Wake cause of each sleep (one bucket per sleep, GPIO > Wi-Fi > timer > other; they sum to sleeps).
  volatile uint32_t timer_wakes;
  volatile uint32_t wifi_wakes;
  volatile uint32_t other_wakes;
  // GPIO wakes per armed pin (the pins whose level left the armed level), and GPIO wakes where no
  // armed pin had changed by the exit (a pulse shorter than the wake-up, or an unarmed source).
  volatile uint32_t pin_wakes[PM_WAKE_SLOTS];
  volatile uint32_t gpio_unattributed;
  // Awake lock held time (main loop only, pm_init / pm_apply).
  uint32_t awake_ms_total;         // finished held periods
  uint32_t awake_since_ms;         // start of the current held period
};

static RTC_NOINIT_ATTR PmCrumb s_pm_crumb;

// Constant-initialized (no guard variable): the sleep callbacks read it from the idle task.
inline PmState g_pm_state = {nullptr, true, false, 16U, 0U, 0U,
                             {{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}},
                             0U, 0U, 0U, 0U,
                             0U, 0U, 0U, {0U, 0U, 0U, 0U}, 0U,
                             0U, 0U};

// Wake attribution for [PROF:WAKE], counted in the enter callback (IRAM, counters only). ESP-IDF sizes
// each light-sleep attempt to the nearest deadline (pm_impl.c vApplicationSleep): the next esp_timer
// alarm, or a FreeRTOS timeout (a task delay or a blocking call with a timeout).
struct PmWakeHist {
  volatile uint32_t attempts;
  volatile uint32_t timer_bound;  // the next esp_timer alarm set the length
  volatile uint32_t rtos_bound;   // a FreeRTOS timeout set it
  volatile uint32_t req[5];       // requested length: <20 ms, <50 ms, <200 ms, <1 s, >=1 s
};
inline PmWakeHist g_pm_wake_hist = {0U, 0U, 0U, {0U, 0U, 0U, 0U, 0U}};

static esp_err_t IRAM_ATTR pm_light_sleep_enter_cb(int64_t sleep_time_us, void *arg) {
  (void) arg;
  PmState &st = g_pm_state;
  // esp_timer_get_time / esp_timer_get_next_alarm_for_wake_up are IRAM-safe here: CONFIG_PM_SLP_IRAM_OPT
  // selects CONFIG_ESP_TIMER_IN_IRAM, and pm_impl.c calls the latter in this same critical section.
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
    GPIO.pin[gpio_num].int_ena = 0;  // no CPU takes the level interrupt (see the header comment)
    gpio_ll_set_intr_type(&GPIO, gpio_num, level ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    gpio_ll_wakeup_enable(&GPIO, gpio_num);
    entered |= 1UL << i;
  }
  st.entered_mask = entered;
  // RTC_NOINIT: pm_init() clears it, the bound check keeps a corrupt head inside the ring.
  uint32_t head = s_pm_crumb.ring_next;
  if (head >= kPmCrumbRing) head = 0;
  PmCrumbSleep &e = s_pm_crumb.ring[head];
  e.t_us = static_cast<uint32_t>(esp_timer_get_time());  // low 32 bits: no 64-bit math in IRAM
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

// Register reads, inline GPIO HAL calls and DRAM counters only: it runs from IRAM inside the light-sleep
// critical section (no flash or PSRAM code, no logging).
static esp_err_t IRAM_ATTR pm_light_sleep_exit_cb(int64_t slept_us, void *arg) {
  (void) arg;
  PmState &st = g_pm_state;
  s_pm_crumb.phase = 2U;
  bool gpio_woke = false;
  if (slept_us > 0) {
    st.sleeps = st.sleeps + 1U;
    st.slept_us = st.slept_us + static_cast<uint32_t>(slept_us);
    const uint32_t cause = rtc_cntl_ll_get_wakeup_cause();
    // Complete the ring entry (wake-pin levels before the exit restores any pin) and move the head on;
    // an attempt that didn't sleep keeps its slot for the next entry.
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
    if (gpio_woke) st.gpio_unattributed = st.gpio_unattributed + 1U;
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
    // Drop the status the level type latched, then give the interrupt back to its CPU. A real edge
    // in this window is not lost: a changed level is re-raised below.
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
    // Polled pins (no ISR attached) have nothing to deliver.
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
  // A GPIO woke the chip but every pin is back at its armed level (a pulse shorter than the wake-up):
  // hand the event to every armed ISR; each one re-reads its own source.
  if ((changed_lo | changed_hi) == 0 && gpio_woke) {
    changed_lo = isr_lo;
    changed_hi = isr_hi;
  }
  if ((changed_lo | changed_hi) != 0) {
    st.synth = st.synth + 1U;
    if (changed_lo != 0) REG_WRITE(GPIO_STATUS_W1TS_REG, changed_lo);
    if (changed_hi != 0) REG_WRITE(GPIO_STATUS1_W1TS_REG, changed_hi);
  }
  return ESP_OK;
}

// FreeRTOS tick hooks (tick interrupt, IRAM, also while the flash cache is disabled): one RTC store each.
// esp_timer_get_time is in IRAM here (CONFIG_PM_SLP_IRAM_OPT selects CONFIG_ESP_TIMER_IN_IRAM).
static void IRAM_ATTR pm_tick_hook_cpu0() { s_pm_crumb.tick_us[0] = static_cast<uint32_t>(esp_timer_get_time()); }
static void IRAM_ATTR pm_tick_hook_cpu1() { s_pm_crumb.tick_us[1] = static_cast<uint32_t>(esp_timer_get_time()); }

// Raw-IDF outputs (driven with gpio_set_level, not through an ESPHome pin) keep driving in light sleep.
inline void pm_keep_raw_outputs_driven() {
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

// The previous boot's breadcrumb, copied once at boot when that boot ended in a watchdog or panic reset.
inline PmCrumb &pm_previous_crumb() {
  static PmCrumb c{};
  return c;
}

// Once, from on_boot (before the components that own the wake pins finish setup). Creates the awake
// lock held, records the wake pins and registers the light-sleep callbacks, and keeps the previous
// boot's light-sleep breadcrumb. No-op without light sleep.
inline void pm_init() {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock != nullptr) return;
  const esp_reset_reason_t rr = esp_reset_reason();
  if (s_pm_crumb.magic == kPmCrumbMagic &&
      (rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_PANIC)) {
    pm_previous_crumb() = s_pm_crumb;
  }
  memset(&s_pm_crumb, 0, sizeof(s_pm_crumb));  // RTC_NOINIT: the ring indexes start inside the ring
#if !defined(CONFIG_ESP_TIMER_IN_IRAM)
#error "Latter's light-sleep tick hooks need CONFIG_ESP_TIMER_IN_IRAM (selected by CONFIG_PM_SLP_IRAM_OPT)"
#endif
  esp_register_freertos_tick_hook_for_cpu(pm_tick_hook_cpu0, 0);
#if !CONFIG_FREERTOS_UNICORE
  esp_register_freertos_tick_hook_for_cpu(pm_tick_hook_cpu1, 1);
#endif
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
    // Wake pads keep their awake configuration (input enabled) in light sleep.
    if (p.gpio >= 0) gpio_sleep_sel_dis(static_cast<gpio_num_t>(p.gpio));
  }
  esp_pm_lock_handle_t lock = nullptr;
  esp_err_t err = esp_pm_lock_create(kPmAwakeLockType, 0, "latter_awake", &lock);
  if (err != ESP_OK || lock == nullptr) {
    ESP_LOGE("power", "Light sleep: awake lock create failed (%s); light sleep stays unmanaged", esp_err_to_name(err));
    return;
  }
  esp_pm_lock_acquire(lock);  // the screen is on at boot; pm_apply owns every later transition
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
    // Without the callbacks no pin may be armed: a level wake outside the critical section would storm.
    for (PmWakePin &p : st.pins) p.gpio = -1;
    ESP_LOGE("power", "Light sleep: wake-pin callbacks unavailable (%s); waking on timers and Wi-Fi only",
             esp_err_to_name(err));
  }
  ESP_LOGI("power", "Light sleep: awake lock held at boot, wake pins pwr=%d btn=%d touch=%d imu=%d, quiet loop %lums (%lums with touch wake)",
           st.pins[PM_WAKE_PWR_KEY].gpio, st.pins[PM_WAKE_ACTION_BTN].gpio, st.pins[PM_WAKE_TOUCH].gpio,
           st.pins[PM_WAKE_IMU].gpio, static_cast<unsigned long>(kPmQuietLoopIntervalMs),
           static_cast<unsigned long>(kPmQuietTouchLoopIntervalMs));
#endif
}

// Single owner: script apply_power_policy. stay_awake holds the awake lock; otherwise the pins in
// wake_mask (pm_wake_bit) arm as light-sleep wake sources and the loop interval stretches.
inline void pm_apply(bool stay_awake, uint32_t wake_mask) {
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
    // Touch wake can be armed or disarmed while the lock stays released.
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
    st.arm_mask = arm;  // armed before the first sleep can start
    esphome::App.set_loop_interval(pm_quiet_loop_interval(arm));
    esp_pm_lock_release(st.lock);
    st.awake_ms_total += now_ms - st.awake_since_ms;
    st.dump_pending = true;
  }
  st.awake = stay_awake;
  if (profiler_enabled()) {
    ESP_LOGI("PROF", "[PROF:PM] lock=%s arm=0x%02lx loop=%lums", stay_awake ? "held" : "released",
             static_cast<unsigned long>(arm), static_cast<unsigned long>(esphome::App.get_loop_interval()));
  }
#else
  (void) stay_awake;
  (void) wake_mask;
#endif
}

// Main loop only (script apply_power_policy and the QMI8658 WoM event handler): records the IMU
// Wake-on-Motion state in the light-sleep breadcrumb. No-op without light sleep.
inline void pm_note_imu(bool armed, uint32_t events) {
#if LATTER_PM_AUTO_SLEEP
  s_pm_crumb.imu_armed = armed ? 1U : 0U;
  s_pm_crumb.imu_events = events;
#else
  (void) armed;
  (void) events;
#endif
}

// Where the previous boot's watchdog / panic reset hit relative to light sleep: "ls-in" (inside a sleep,
// between the enter and exit callbacks), "ls-out" (after the last sleep came back), or "" (no breadcrumb:
// not a watchdog or panic reset, or no light sleep). [PROF:BOOT] and System: Link Report (rst=) show it.
inline const char *pm_crash_where() {
  const PmCrumb &c = pm_previous_crumb();
  if (c.magic != kPmCrumbMagic) return "";
  return c.phase == 1U ? "ls-in" : "ls-out";
}

// Light-sleep statistics since boot for System: Link Report. false without automatic light sleep.
struct PmSleepStats {
  uint32_t sleeps;
  uint64_t slept_us;
  uint32_t awake_ms;
  uint32_t timer_wakes;
  uint32_t gpio_wakes;
  uint32_t wifi_wakes;
  uint32_t other_wakes;
  uint32_t pin_wakes[PM_WAKE_SLOTS];  // pwr, btn, touch, imu (PmWakeSlot order)
  uint32_t gpio_unattributed;
};

// Main loop only, at least once per ~71 min (the 60 s link report sample): folds the wrapping 32-bit
// slept-time counter into a 64-bit total.
inline bool pm_sleep_stats(PmSleepStats &out, uint32_t now_ms) {
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

// [PROF:PM] from the 10 s power profile: light-sleep share since the last line, sleeps, GPIO wakes,
// delivered pin edges and the arming mask. While the lock is released it also dumps ESP-IDF's lock
// table once per minute ([PROF:PM_LOCKS]; per-lock and per-mode time with the board's pm_profiling).
inline void pm_log_profile() {
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
  ESP_LOGI("PROF", "[PROF:PM] lock=%s ls=%lu.%lu%% sleeps=%lu gpio_wakes=%lu synth=%lu arm=0x%02lx loop=%lums",
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
  constexpr size_t kDumpLen = 2048;
  char *buf = static_cast<char *>(heap_caps_calloc(1, kDumpLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return;
  FILE *f = fmemopen(buf, kDumpLen - 1, "w");
  if (f != nullptr) {
    esp_pm_dump_locks(f);
    fclose(f);
    char *save = nullptr;
    for (char *line = strtok_r(buf, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
      if (line[0] != '\0') ESP_LOGI("PROF", "[PROF:PM_LOCKS] %s", line);
    }
  }
  heap_caps_free(buf);
#endif
}

// Writes an ESP-IDF dump function's text into a PSRAM buffer and logs it line by line under label.
inline void pm_log_dump_lines(const char *label, size_t len, void (*dump)(FILE *)) {
#ifdef USE_ESP32
  char *buf = static_cast<char *>(heap_caps_calloc(1, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return;
  FILE *f = fmemopen(buf, len - 1, "w");
  if (f != nullptr) {
    dump(f);
    fclose(f);
    char *save = nullptr;
    for (char *line = strtok_r(buf, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
      if (line[0] != '\0') ESP_LOGI("PROF", "%s %s", label, line);
    }
  }
  heap_caps_free(buf);
#else
  (void) label;
  (void) len;
  (void) dump;
#endif
}

// [PROF:WAKE], once a minute while the Telemetry Profiler is on (called from the 10 s power profile).
// Measures what wakes the chip instead of guessing:
//   sleeps     light-sleep attempts in the window, how many slept, which deadline sized them (an esp_timer
//              alarm or a FreeRTOS timeout) and their requested-length histogram (light-sleep boards)
//   loop       ESPHome's loop interval and component-phase passes, their gaps, and passes run while a
//              HighFrequencyLoopRequester was held (latter_core's loop probe)
//   WAKE_TIMER esp_timer_dump(): every esp_timer with its period, times armed / triggered and callback time
//              (CONFIG_ESP_TIMER_PROFILING)
//   WAKE_TASK  CPU time per FreeRTOS task in the window, busiest first (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
// Both options cost time in every build that sets them, so the boards leave them off (substitution
// wake_profiling: "n"); without them those two sections log one line saying how to turn them on.
inline void pm_log_wake_profile() {
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
    ESP_LOGI("PROF",
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
    ESP_LOGI("PROF",
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
  ESP_LOGI("PROF", "[PROF:WAKE_TIMER] off (build with substitution wake_profiling: \"y\")");
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
        uint64_t prev = rt;  // a task new in this window reports 0
        for (UBaseType_t j = 0; j < prev_n; j++) {
          if (prev_h[j] == ts[i].xHandle) {
            prev = prev_rt[j];
            break;
          }
        }
        delta[i] = rt >= prev ? rt - prev : rt;  // the 32-bit counter wraps after ~71 min
      }
      // Busiest 14 first (counter clock: esp_timer, 1 us).
      bool shown[kMaxTasks] = {};
      for (int k = 0; k < 14; k++) {
        int best = -1;
        for (UBaseType_t i = 0; i < n; i++) {
          if (!shown[i] && (best < 0 || delta[i] > delta[best])) best = static_cast<int>(i);
        }
        if (best < 0 || delta[best] == 0) break;
        shown[best] = true;
        const uint32_t pm = window_ms > 0 ? static_cast<uint32_t>(delta[best] / window_ms) : 0;  // us per ms = permille
        ESP_LOGI("PROF", "[PROF:WAKE_TASK] %-16s prio=%u run=%lums (%lu.%lu%%)", ts[best].pcTaskName,
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
  ESP_LOGI("PROF", "[PROF:WAKE_TASK] off (build with substitution wake_profiling: \"y\")");
#endif
}

}  // namespace latter_ui
