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
//    (CONFIG_PM_LIGHT_SLEEP_CALLBACKS), where interrupts are masked:
//      enter: wake level = the opposite of the current level (any change wakes the chip)
//      exit:  restore the component's edge interrupt type, then raise the pin's interrupt status
//             (GPIO_STATUS_W1TS) when its level changed or a GPIO woke the chip, so the owning
//             component's ISR runs exactly once when interrupts unmask.
//    A level interrupt therefore never exists with interrupts enabled (no interrupt storm), and an
//    edge that happened while the edge detector slept is still delivered. gpio_ll_wakeup_disable()
//    is used instead of gpio_wakeup_disable(), which would re-enable the pad's sleep isolation.
//    The arming mask is set by pm_apply() and is zero while the lock is held.
//
// 3. Sleep pad config for raw-IDF outputs: the S3 light-sleep GPIO reset workaround
//    (CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND, which selects CONFIG_PM_SLP_DISABLE_GPIO) isolates every
//    pad in light sleep unless gpio_sleep_sel_dis() was called for it. ESPHome already does that for
//    every pin it manages (esp32/gpio.cpp setup() under CONFIG_FREERTOS_USE_TICKLESS_IDLE); Latter
//    does it for the raw-IDF outputs named by board traits (LATTER_TOUCH_RST_PIN, backlight, C6 reset).
//
// The main loop interval stretches to kPmQuietLoopIntervalMs while the lock is released (ESPHome's
// scheduler runs on its own deadlines, so timers and intervals still fire on time) and returns to
// the configured interval when it is taken again.
// ==============================================================================

#include "esphome.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef USE_ESP32
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
#include <esp_heap_caps.h>
#include <esp_pm.h>
#include <esp_sleep.h>
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

// Main loop interval while the awake lock is released (ESPHome's default is 16 ms).
constexpr uint32_t kPmQuietLoopIntervalMs = 100U;

#if LATTER_PM_AUTO_SLEEP
constexpr esp_pm_lock_type_t kPmAwakeLockType = ESP_PM_CPU_FREQ_MAX;
// ESP32-S3 RTC_CNTL wake-cause bit for GPIO wake: RTC_GPIO_TRIG_EN in esp_hw_support/port/esp32s3/include/soc/rtc.h
// (not included here: that header also defines global macros such as MHZ).
constexpr uint32_t kPmWakeCauseGpio = 1UL << 2;

struct PmWakePin {
  int8_t gpio;
  uint8_t armed_level;
  uint8_t saved_type;
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
  volatile uint32_t slept_us;      // wraps; [PROF:PM] uses deltas
  volatile uint32_t gpio_wakes;    // sleeps ended by a GPIO
  volatile uint32_t synth;         // exits that delivered a pin edge to its ISR
};

// Constant-initialized (no guard variable): the sleep callbacks read it from the idle task.
inline PmState g_pm_state = {nullptr, true, false, 16U, 0U, 0U,
                             {{-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}},
                             0U, 0U, 0U, 0U};

static esp_err_t IRAM_ATTR pm_light_sleep_enter_cb(int64_t sleep_time_us, void *arg) {
  (void) sleep_time_us;
  (void) arg;
  PmState &st = g_pm_state;
  const uint32_t mask = st.arm_mask;
  uint32_t entered = 0;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    const int g = st.pins[i].gpio;
    if (g < 0 || (mask & (1UL << i)) == 0) continue;
    const uint32_t gpio_num = static_cast<uint32_t>(g);
    const int level = gpio_ll_get_level(&GPIO, gpio_num);
    st.pins[i].armed_level = static_cast<uint8_t>(level);
    st.pins[i].saved_type = static_cast<uint8_t>(GPIO.pin[gpio_num].int_type);
    gpio_ll_set_intr_type(&GPIO, gpio_num, level ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    gpio_ll_wakeup_enable(&GPIO, gpio_num);
    entered |= 1UL << i;
  }
  st.entered_mask = entered;
  return ESP_OK;
}

static esp_err_t IRAM_ATTR pm_light_sleep_exit_cb(int64_t slept_us, void *arg) {
  (void) arg;
  PmState &st = g_pm_state;
  if (slept_us > 0) {
    st.sleeps = st.sleeps + 1U;
    st.slept_us = st.slept_us + static_cast<uint32_t>(slept_us);
  }
  const uint32_t entered = st.entered_mask;
  if (entered == 0) return ESP_OK;
  st.entered_mask = 0;
  const bool gpio_woke = slept_us > 0 && (rtc_cntl_ll_get_wakeup_cause() & kPmWakeCauseGpio) != 0;
  if (gpio_woke) st.gpio_wakes = st.gpio_wakes + 1U;
  uint32_t changed_lo = 0, changed_hi = 0, isr_lo = 0, isr_hi = 0;
  for (uint32_t i = 0; i < PM_WAKE_SLOTS; i++) {
    if ((entered & (1UL << i)) == 0) continue;
    const uint32_t gpio_num = static_cast<uint32_t>(st.pins[i].gpio);
    gpio_ll_wakeup_disable(&GPIO, gpio_num);
    const uint32_t saved = st.pins[i].saved_type;
    gpio_ll_set_intr_type(&GPIO, gpio_num, static_cast<gpio_int_type_t>(saved));
    // Polled pins (no ISR attached) have nothing to deliver.
    if (saved == static_cast<uint32_t>(GPIO_INTR_DISABLE) || GPIO.pin[gpio_num].int_ena == 0) continue;
    const uint32_t bit = 1UL << (gpio_num & 31U);
    const bool changed = static_cast<uint32_t>(gpio_ll_get_level(&GPIO, gpio_num)) != st.pins[i].armed_level;
    if (gpio_num < 32U) {
      isr_lo |= bit;
      if (changed) changed_lo |= bit;
    } else {
      isr_hi |= bit;
      if (changed) changed_hi |= bit;
    }
  }
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

// Once, from on_boot (before the components that own the wake pins finish setup). Creates the awake
// lock held, records the wake pins and registers the light-sleep callbacks. No-op without light sleep.
inline void pm_init() {
#if LATTER_PM_AUTO_SLEEP
  PmState &st = g_pm_state;
  if (st.lock != nullptr) return;
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
  ESP_LOGI("power", "Light sleep: awake lock held at boot, wake pins pwr=%d btn=%d touch=%d imu=%d, quiet loop %lums",
           st.pins[PM_WAKE_PWR_KEY].gpio, st.pins[PM_WAKE_ACTION_BTN].gpio, st.pins[PM_WAKE_TOUCH].gpio,
           st.pins[PM_WAKE_IMU].gpio, static_cast<unsigned long>(kPmQuietLoopIntervalMs));
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
    return;
  }
  if (stay_awake) {
    esp_pm_lock_acquire(st.lock);
    st.arm_mask = 0U;
    esphome::App.set_loop_interval(st.loop_interval_awake);
  } else {
    st.arm_mask = arm;  // armed before the first sleep can start
    esphome::App.set_loop_interval(kPmQuietLoopIntervalMs);
    esp_pm_lock_release(st.lock);
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

// Light sleeps that actually slept since boot (System: Link Report ls=); false without light sleep.
inline bool pm_sleep_count(uint32_t &out) {
#if LATTER_PM_AUTO_SLEEP
  if (g_pm_state.lock == nullptr) return false;
  out = g_pm_state.sleeps;
  return true;
#else
  (void) out;
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

}  // namespace latter_ui
