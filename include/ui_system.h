#pragma once

// ==============================================================================
// Latter Core UI Module: System Settings, HSV Color Wheel & Diagnostics (ui_system.h)
// ==============================================================================

#include "ui_layout.h"
#include "ui_music.h"

#if defined(USE_ESP32)
#include <driver/rtc_io.h>
#include <esp_system.h>
#include <soc/soc_caps.h>
#endif

#ifndef LATTER_PWR_KEY_ACTIVE_HIGH
#define LATTER_PWR_KEY_ACTIVE_HIGH 1
#endif
#ifndef LATTER_PWR_KEY_DEEP_SLEEP_WAKE
#define LATTER_PWR_KEY_DEEP_SLEEP_WAKE 0
#endif
#ifndef LATTER_PMU_TS_DISABLE
#define LATTER_PMU_TS_DISABLE 0
#endif

namespace latter_ui {

// =============================================================================
// SECTION 18: SYSTEM SETTINGS & DIAGNOSTICS ENGINE (PAGE 4)
// =============================================================================

enum class PowerProfile : uint8_t {
  PORTABLE_CARRY = 0,
  HANDHELD_ROAMING = 1,
  WEARABLE_WATCH = 2,
  TABLETOP_DOCK = 3,
  ALWAYS_ON_STATIONARY = 4,
  CUSTOM = 5,
};

inline PowerProfile parse_power_profile(const std::string &opt) {
  if (opt == "Portable Carry") return PowerProfile::PORTABLE_CARRY;
  if (opt == "Handheld Roaming") return PowerProfile::HANDHELD_ROAMING;
  if (opt == "Wearable Watch") return PowerProfile::WEARABLE_WATCH;
  if (opt == "Tabletop Dock") return PowerProfile::TABLETOP_DOCK;
  if (opt == "Always-On Wall / Desk") return PowerProfile::ALWAYS_ON_STATIONARY;
  return PowerProfile::CUSTOM;
}

inline const char *power_profile_short_label(const std::string &opt) {
  switch (parse_power_profile(opt)) {
    case PowerProfile::PORTABLE_CARRY:       return "Portable";
    case PowerProfile::HANDHELD_ROAMING:     return "Roaming";
    case PowerProfile::WEARABLE_WATCH:       return "Watch";
    case PowerProfile::TABLETOP_DOCK:        return "Tabletop";
    case PowerProfile::ALWAYS_ON_STATIONARY: return "Wall/Desk";
    case PowerProfile::CUSTOM:               return "Custom";
  }
  return opt.c_str();
}

struct PowerProfileSpec {
  PowerProfile profile;
  const char *name;
  const char *va_wake_word_mode;
  const char *tap_wake_mode;
  const char *pickup_wake_mode;
  const char *display_off_timeout;
  const char *screensaver_docked_timeout;
  const char *hibernate_timeout_mode;
  bool docked_screensaver_enabled;
};

static constexpr PowerProfileSpec kPowerProfiles[] = {
    {PowerProfile::PORTABLE_CARRY,       "Portable Carry",        "Docked Only", "Docked Only", "Docked Only", "30s",   "1 min", "15 min",   true},
    {PowerProfile::HANDHELD_ROAMING,     "Handheld Roaming",      "Docked Only", "Always On",   "Always On",   "30s",   "1 min", "30 min",   true},
    {PowerProfile::WEARABLE_WATCH,       "Wearable Watch",        "Docked Only", "Always On",   "Always On",   "15s",   "1 min", "15 min",   true},
    {PowerProfile::TABLETOP_DOCK,        "Tabletop Dock",         "Docked Only", "Docked Only", "Disabled",    "30s",   "5 min", "15 min",   true},
    {PowerProfile::ALWAYS_ON_STATIONARY, "Always-On Wall / Desk", "Always On",   "Always On",   "Disabled",    "Never", "5 min", "Disabled", true},
};

inline const PowerProfileSpec *get_power_profile_spec(PowerProfile p) {
  for (const auto &spec : kPowerProfiles) {
    if (spec.profile == p) return &spec;
  }
  return nullptr;
}

inline const PowerProfileSpec *get_power_profile_spec(const std::string &opt) {
  return get_power_profile_spec(parse_power_profile(opt));
}

template <typename WwSelT, typename TapSelT, typename PickupSelT, typename BattToSelT,
          typename DockToSelT, typename HibToSelT, typename DockSwT>
inline bool apply_power_profile_spec(const std::string &opt,
                                     WwSelT *ww_sel, TapSelT *tap_sel, PickupSelT *pickup_sel,
                                     BattToSelT *batt_to_sel, DockToSelT *dock_to_sel,
                                     HibToSelT *hib_to_sel, DockSwT *dock_sw) {
  const PowerProfileSpec *spec = get_power_profile_spec(opt);
  if (spec == nullptr) return false;
  applying_power_profile() = true;
  auto set_sel = [](auto *sel, const char *val) {
    if (sel != nullptr && sel->current_option() != val) {
      auto call = sel->make_call();
      call.set_option(val);
      call.perform();
    }
  };
  set_sel(ww_sel, spec->va_wake_word_mode);
  set_sel(tap_sel, spec->tap_wake_mode);
  const char *eff_pickup = (LATTER_HAS_IMU && !LATTER_IS_STATIONARY) ? spec->pickup_wake_mode : "Disabled";
  set_sel(pickup_sel, eff_pickup);
  set_sel(batt_to_sel, spec->display_off_timeout);
  set_sel(dock_to_sel, spec->screensaver_docked_timeout);
  const char *eff_hib = (LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY) ? spec->hibernate_timeout_mode : "Disabled";
  set_sel(hib_to_sel, eff_hib);
  if (dock_sw != nullptr && !dock_sw->state && spec->docked_screensaver_enabled) {
    dock_sw->turn_on();
  }
  applying_power_profile() = false;
  return true;
}

template <typename PickupSelT, typename HibToSelT>
inline void clamp_power_capabilities(PickupSelT *pickup_sel, HibToSelT *hib_to_sel) {
  applying_power_profile() = true;
  auto set_sel = [](auto *sel, const char *val) {
    if (sel != nullptr && sel->current_option() != val) {
      auto call = sel->make_call();
      call.set_option(val);
      call.perform();
    }
  };
  if (!LATTER_HAS_IMU || LATTER_IS_STATIONARY) {
    set_sel(pickup_sel, "Disabled");
  }
  if (!LATTER_HAS_BATTERY || LATTER_IS_STATIONARY) {
    set_sel(hib_to_sel, "Disabled");
  }
  applying_power_profile() = false;
}

inline int32_t seconds_until_next_armed_alarm(const esphome::ESPTime &now, uint32_t snooze_until,
                                              int a1_h, int a1_m, bool a1_en, int a1_rec,
                                              int a2_h, int a2_m, bool a2_en, int a2_rec,
                                              int a3_h, int a3_m, bool a3_en, int a3_rec) {
  if (!now.is_valid()) return -1;
  int32_t best_sec = 43201;  // > 12h sentinel
  if (snooze_until > static_cast<uint32_t>(now.timestamp)) {
    int32_t s_rem = static_cast<int32_t>(snooze_until - static_cast<uint32_t>(now.timestamp));
    if (s_rem > 0 && s_rem <= 43200) best_sec = s_rem;
  }
  const int32_t now_tod = now.hour * 3600 + now.minute * 60 + now.second;
  auto eval_alarm = [&](int a_hr, int a_min, bool a_en, int a_rec) {
    if (!a_en) return;
    const int32_t alarm_tod = a_hr * 3600 + a_min * 60;
    int32_t delta_sec = 0;
    int target_dow = now.day_of_week;  // 1=SUN .. 7=SAT
    if (alarm_tod > now_tod) {
      delta_sec = alarm_tod - now_tod;
    } else {
      delta_sec = (86400 - now_tod) + alarm_tod;
      target_dow = (now.day_of_week % 7) + 1;
    }
    if (delta_sec <= 0 || delta_sec > 43200) return;
    if (a_rec == 1 && (target_dow < 2 || target_dow > 6)) return;
    if (a_rec == 2 && (target_dow != 1 && target_dow != 7)) return;
    if (delta_sec < best_sec) best_sec = delta_sec;
  };
  eval_alarm(a1_h, a1_m, a1_en, a1_rec);
  eval_alarm(a2_h, a2_m, a2_en, a2_rec);
  eval_alarm(a3_h, a3_m, a3_en, a3_rec);
  return (best_sec <= 43200) ? best_sec : -1;
}

// Returns the PWR key pad to the digital GPIO matrix after an ext0 deep-sleep wake (the wake pad
// stays in the RTC IO domain after wake until rtc_gpio_deinit(), which would freeze the
// binary_sensor reading). No-op on boards without a deep-sleep-capable PWR key.
inline void hal_release_pwr_key_wake_pad() {
#if defined(USE_ESP32) && defined(LATTER_PWR_KEY_PIN) && LATTER_PWR_KEY_DEEP_SLEEP_WAKE && SOC_RTCIO_INPUT_OUTPUT_SUPPORTED
  const gpio_num_t pin = static_cast<gpio_num_t>(LATTER_PWR_KEY_PIN);
  if (rtc_gpio_is_valid_gpio(pin)) {
    rtc_gpio_deinit(pin);
  }
#endif
}

template <typename I2cBusT>
inline void enter_axp2101_hibernate_or_alarm_sleep(I2cBusT *bus, int32_t sec_until_alarm) {
#ifdef USE_ESP32
#if defined(LATTER_PWR_KEY_PIN) && LATTER_PWR_KEY_DEEP_SLEEP_WAKE && SOC_PM_SUPPORT_EXT0_WAKEUP
  // Alarm-sleep keeps AXP2101 rails ON, so a PWR key press wakes the SoC through ext0.
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(LATTER_PWR_KEY_PIN), LATTER_PWR_KEY_ACTIVE_HIGH ? 1 : 0);
#endif
  if (sec_until_alarm > 0 && sec_until_alarm <= 43200) {
    uint64_t wake_sec = static_cast<uint64_t>((sec_until_alarm > 90) ? (sec_until_alarm - 90) : 15);
    ESP_LOGI("power", "Armed alarm in %lds: entering timer-wake deep sleep (wake in %llus, AXP2101 rails kept ON)",
             (long)sec_until_alarm, (unsigned long long)wake_sec);
    esp_sleep_enable_timer_wakeup(wake_sec * 1000000ULL);
#if defined(LATTER_C6_RESET_PIN) && !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
    // Rails stay on in alarm-sleep, so the ESP32-C6 co-processor would keep running (its CHIP_PU has a
    // pull-up). Hold CHIP_PU low through deep sleep (single-pad deep-sleep hold needs P4 rev >= 3.0);
    // latter_release_c6_reset_hold() drops the hold at the next boot before esp_hosted resets the C6.
    gpio_set_level(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN), 0);
    gpio_hold_en(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN));
#endif
    esp_deep_sleep_start();
    return;
  }
  if (bus != nullptr) {
    uint8_t axp_pwr_off[2] = {0x10, 0x01};
    auto err = bus->write(0x34, axp_pwr_off, 2);
    if (err != esphome::i2c::ERROR_OK) {
      ESP_LOGW("power", "AXP2101 power-off write (0x10=0x01) failed (err=%d); falling back to ESP deep sleep", (int)err);
    }
  }
  esphome::delay(100);
  esp_deep_sleep_start();
#else
  (void)bus;
  (void)sec_until_alarm;
#endif
}

#if defined(USE_ESP32) && defined(LATTER_C6_RESET_PIN)
// Runs as a global constructor, before the FreeRTOS scheduler starts, so it precedes esp_hosted's C6
// reset pulse on every ESPHome version (2026.10: inside Wi-Fi setup; 2026.11+: the esp_hosted init task
// spawned from its own constructor). A held pad would latch CHIP_PU low and the C6 would never boot.
// Harmless when no hold is set.
__attribute__((constructor)) static void latter_release_c6_reset_hold() {
  gpio_hold_dis(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN));
}
#endif

template <typename I2cBusT>
inline void enter_axp2101_hibernate_or_alarm_sleep(I2cBusT *const *bus_pp, int32_t sec_until_alarm) {
  if (bus_pp != nullptr) enter_axp2101_hibernate_or_alarm_sleep(*bus_pp, sec_until_alarm);
}

// Canonical AXP2101 boot configuration (register map per XPowersLib AXP2101Constants.h).
//   1. Masks every IRQ source (REG40-42) and clears latched status (REG48-4A): no supported
//      board routes AXP_IRQ to the SoC; the PWR key is read from its own GPIO instead.
//   2. LATTER_PMU_TS_DISABLE=1: boards with no NTC on TS -> TS is an external fixed input that does
//      not gate the charger (REG50 = (v & 0xF0) | 0x10) and its ADC channel stops sampling (REG30 bit1).
//   3. LATTER_PMU_DCDC_KEEP_MASK (REG80 bits0-4 = DCDC1..5) and LATTER_PMU_LDO_KEEP_MASK
//      (bits0-7 = REG90 ALDO1-4/BLDO1-2/CPUSLDO/DLDO1, bit8 = REG91 bit0 DLDO2) only ever switch
//      rails OFF; rails outside the mask that have no load per the schematic are disabled.
//      Boards without a verified rail map leave both masks undefined (OTP defaults retained).
//   4. LATTER_PMU_CHARGE_MA (100..1000): constant-charge current (REG62), set per board from the
//      fitted cell capacity (<= 1C). Undefined = OTP default retained.
template <typename I2cBusT>
inline void hal_init_axp2101(I2cBusT *bus) {
#ifdef USE_ESP32
  if (bus == nullptr) return;
  constexpr uint8_t kAxpAddr = 0x34;
  auto rd = [bus](uint8_t reg, uint8_t &val) -> bool {
    return bus->write_readv(kAxpAddr, &reg, 1, &val, 1) == esphome::i2c::ERROR_OK;
  };
  auto wr = [bus](uint8_t reg, uint8_t val) -> bool {
    const uint8_t buf[2] = {reg, val};
    return bus->write(kAxpAddr, buf, 2) == esphome::i2c::ERROR_OK;
  };

  for (uint8_t reg = 0x40; reg <= 0x42; reg++) wr(reg, 0x00);
  for (uint8_t reg = 0x48; reg <= 0x4A; reg++) wr(reg, 0xFF);

  uint8_t v = 0;
#if LATTER_PMU_TS_DISABLE
  if (rd(0x50, v)) wr(0x50, static_cast<uint8_t>((v & 0xF0) | 0x10));
  if (rd(0x30, v)) wr(0x30, static_cast<uint8_t>(v & ~0x02));
#endif

#if defined(LATTER_PMU_DCDC_KEEP_MASK)
  static_assert((LATTER_PMU_DCDC_KEEP_MASK & 0x01) != 0, "LATTER_PMU_DCDC_KEEP_MASK must keep DCDC1 (SoC 3V3)");
  if (rd(0x80, v)) wr(0x80, static_cast<uint8_t>(v & (0xE0 | (LATTER_PMU_DCDC_KEEP_MASK & 0x1F))));
#endif
#if defined(LATTER_PMU_LDO_KEEP_MASK)
  if (rd(0x90, v)) wr(0x90, static_cast<uint8_t>(v & (LATTER_PMU_LDO_KEEP_MASK & 0xFF)));
  if (rd(0x91, v)) wr(0x91, static_cast<uint8_t>(v & (0xFE | ((LATTER_PMU_LDO_KEEP_MASK >> 8) & 0x01))));
#endif

#if defined(LATTER_PMU_CHARGE_MA)
  static_assert(LATTER_PMU_CHARGE_MA >= 100 && LATTER_PMU_CHARGE_MA <= 1000,
                "LATTER_PMU_CHARGE_MA must be within 100..1000 mA");
  constexpr uint8_t kIccCode = (LATTER_PMU_CHARGE_MA <= 200)
                                   ? static_cast<uint8_t>(LATTER_PMU_CHARGE_MA / 25)
                                   : static_cast<uint8_t>(8 + (LATTER_PMU_CHARGE_MA - 200) / 100);
  if (rd(0x62, v)) wr(0x62, static_cast<uint8_t>((v & 0xE0) | kIccCode));
#endif

  // Configuration snapshot for the board facts ledger / bring-up log.
  uint8_t r62 = 0, r64 = 0, r80 = 0, r90 = 0, r91 = 0, r30 = 0, r50 = 0, r27 = 0;
  rd(0x62, r62);
  rd(0x64, r64);
  rd(0x80, r80);
  rd(0x90, r90);
  rd(0x91, r91);
  rd(0x30, r30);
  rd(0x50, r50);
  rd(0x27, r27);
  const uint8_t icc = r62 & 0x1F;
  const int icc_ma = (icc <= 8) ? (icc * 25) : (200 + (icc - 8) * 100);
  auto &tel = pmu_telemetry();
  rd(0x20, tel.on_src);
  rd(0x21, tel.off_src);
  tel.cv_reg = r64;
  tel.icc_ma = icc_ma;
  tel.boot_valid = true;
  ESP_LOGI("pmu", "AXP2101: ICC=%dmA (0x62=0x%02X) CV=0x%02X DCDC=0x%02X LDO0=0x%02X LDO1=0x%02X ADC=0x%02X TS=0x%02X PWRON=0x%02X",
           icc_ma, r62, r64, r80, r90, r91, r30, r50, r27);
#else
  (void)bus;
#endif
}

template <typename I2cBusT>
inline void hal_init_axp2101(I2cBusT *const *bus_pp) {
  if (bus_pp != nullptr) hal_init_axp2101(*bus_pp);
}

// Boot summary, re-logged to every API client a moment after it connects so an OTA log viewer sees
// why the device last went down and came back (boot-time logs are emitted before any client exists).
// AXP2101 REG20 / REG21 bit names follow XPowersLib (xpower_power_on/off_source_t).
inline void log_boot_summary() {
  log_stall_summary();
  uint32_t up_s = esphome::millis() / 1000U;
  const char *rr = last_reset_reason();
  const int crash = crash_record_present() ? 1 : 0;
  const int rollback = firmware_rolled_back() ? 1 : 0;
  const auto &tel = pmu_telemetry();
  if (!tel.boot_valid) {
    ESP_LOGI("PROF", "[PROF:BOOT] sdk=%ld uptime=%lus reset=%s crash=%d rollback=%d pmu=none", (long)LATTER_SDK_REV,
             (unsigned long)up_s, rr, crash, rollback);
    return;
  }
  static const char *const kOff[8] = {"pwrkey-long", "software", "pwrkey-low", "vsys-undervolt",
                                      "vbus-overvolt", "dcdc-undervolt", "dcdc-overvolt", "over-temp"};
  static const char *const kOn[6] = {"pwrkey", "irq", "vbus-insert", "batt-charge", "batt-insert", "en-mode"};
  char off_s[96] = {0};
  char on_s[64] = {0};
  for (int b = 0; b < 8; b++) {
    if (tel.off_src & (1U << b)) {
      std::strncat(off_s, off_s[0] ? "," : "", sizeof(off_s) - std::strlen(off_s) - 1);
      std::strncat(off_s, kOff[b], sizeof(off_s) - std::strlen(off_s) - 1);
    }
  }
  for (int b = 0; b < 6; b++) {
    if (tel.on_src & (1U << b)) {
      std::strncat(on_s, on_s[0] ? "," : "", sizeof(on_s) - std::strlen(on_s) - 1);
      std::strncat(on_s, kOn[b], sizeof(on_s) - std::strlen(on_s) - 1);
    }
  }
  ESP_LOGI("PROF",
           "[PROF:BOOT] sdk=%ld uptime=%lus reset=%s crash=%d rollback=%d last_off=0x%02X(%s) on=0x%02X(%s) icc=%dmA cv=0x%02X",
           (long)LATTER_SDK_REV, (unsigned long)up_s, rr, crash, rollback, tel.off_src, off_s[0] ? off_s : "-", tel.on_src,
           on_s[0] ? on_s : "-", tel.icc_ma, tel.cv_reg);
}
inline std::string &latter_site_id() {
  static std::string s = "primary";
  return s;
}

inline std::string &latter_ble_proxy() {
  static std::string s;
  return s;
}

inline std::string &latter_area_label() {
  static std::string s = "Home";
  return s;
}

template <typename DisplayT, typename FontDateT, typename FontSmallT>
inline void render_system_page(
    DisplayT &it, int cx, uint32_t now_ms, const char *version_str,
    int system_menu_mode, int system_dashboard_page,
    int sleep_menu_page, int display_menu_page,
    bool menu_scroll_active, int menu_scroll_step, int menu_scroll_dir, int menu_scroll_target_page,
    int &update_check_state, uint32_t &update_check_timer,
    const std::string &ha_fw_latest, const std::string &ha_fw_summary,
    float batt, bool is_charging, float batt_v,
    float wifi_sig, const std::string &ip_str, float imu_temp_c,
    uint32_t free_heap_kb, bool ha_time_valid, bool ble_active,
    // Sleep & Wake values
    const std::string &power_profile_opt,
    const std::string &pickup_wake_opt, const std::string &tap_wake_opt,
    const std::string &screen_off_opt, bool docked_ss_on, const std::string &ss_style_opt,
    const std::string &dock_inact_opt, const std::string &hibernate_to_opt,
    bool instant_crown_hib, bool stay_awake_on,
    // Display & Clock values
    bool clock_face_digital, bool is_24h_format,
    const std::string &palette_opt, const std::string &default_app_opt,
    const std::string &auto_brightness_opt, const std::string &manual_bright_opt,
    int day_start_hr, int night_start_hr,
    const std::string &day_brightness_opt, const std::string &night_brightness_opt,
    // Sound values
    int spk_vol_pct, int alert_vol_pct, int vol_step_pct,
    // Music values
    const std::string &art_mode_opt, const std::string &overlay_style_opt,
    const std::string &album_sort_opt, const std::string &artist_sort_opt,
    const std::string &podcast_sort_opt,
    // Voice & Per-Model Wake Word Sensitivity values
    const std::string &va_ww_mode_opt, const std::string &va_ww_active_opt,
    const std::string &sens_nabu_opt, const std::string &sens_luna_opt,
    const std::string &sens_jarvis_opt, const std::string &sens_mycroft_opt,
    const std::string &sens_alexa_opt,
    const std::string &va_profile_opt, bool va_wake_scr,
    // Fonts & Colors
    FontDateT *font_date, FontSmallT *font_small,
    const esphome::Color &theme_primary, const esphome::Color &theme_secondary,
    const esphome::Color &col_bg, const esphome::Color &col_black,
    const esphome::Color &col_white, const esphome::Color &col_slate,
    const esphome::Color &col_dark_gray, const esphome::Color &col_gold,
    const esphome::Color &col_red, bool game_sound_on = true) {
  (void)font_date; (void)col_dark_gray;
  char buf[64];

  const esphome::Color card_bg_on(22, 27, 36);
  const esphome::Color card_bg_off(15, 18, 24);
  const esphome::Color card_brd_on(52, 62, 78);
  const esphome::Color card_brd_off(34, 40, 50);

  auto draw_back = [&]() {
    render_back_button(it, cx, font_small, 426, "< Back");
  };

  // Multi-item cycle card (3+ options): renders squared-off modern card + subtle < > flank chevrons
  auto draw_cycle_card_y = [&](int y_mid, const char *label, bool enabled = true) {
    esphome::Color bg_c  = enabled ? card_bg_on  : card_bg_off;
    esphome::Color brd_c = enabled ? card_brd_on : card_brd_off;
    esphome::Color txt_c = enabled ? col_white   : col_slate;
    esphome::Color chv_c = enabled ? theme_secondary : col_slate;
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
    if (enabled) {
      const int c_l = layout().list_card_left + 15;
      const int c_r = cx + (layout().list_card_w / 2) - 15;
      fast_line(it, c_l, y_mid, c_l + 5, y_mid - 5, chv_c);
      fast_line(it, c_l, y_mid, c_l + 5, y_mid + 5, chv_c);
      fast_line(it, c_r, y_mid, c_r - 5, y_mid - 5, chv_c);
      fast_line(it, c_r, y_mid, c_r - 5, y_mid + 5, chv_c);
    }
  };

  // Binary toggle card (2 states): renders squared-off modern card (white when active, dimmed when off, zero arrows)
  auto draw_toggle_card_y = [&](int y_mid, const char *label, bool is_on) {
    esphome::Color bg_c  = is_on ? card_bg_on  : card_bg_off;
    esphome::Color brd_c = is_on ? card_brd_on : card_brd_off;
    esphome::Color txt_c = is_on ? col_white   : col_slate;
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
  };

  // Action / Submenu drill-down card
  auto draw_action_card_y = [&](int y_mid, const char *label, esphome::Color brd_c,
                                esphome::Color txt_c, esphome::Color bg_c) {
    render_modern_card(it, cx, y_mid, 276, 44, 12, bg_c, brd_c, label, txt_c, font_small);
  };

  auto draw_right_chevrons = [&](bool show_up, bool show_down) {
    render_vertical_page_pips(it, show_up, show_down, col_bg, theme_secondary, col_slate);
  };

  const int row0_y = layout().slot5_y(0);
  const int row_s  = layout().slot5_stride;
  const int mask_top_h = scale_y(82);
  const int mask_btm_y = scale_y(388);
  const int min_vis_y  = scale_y(60);
  const int max_vis_y  = scale_y(395);

  auto compute_scroll_offset = [&](int cur_page, const int16_t ease_lut[5], int max_shift_466) -> int {
    const int max_shift = scale_y(max_shift_466);
    if (menu_scroll_active) {
      int step = std::max(0, std::min(4, menu_scroll_step));
      int shift = scale_y(ease_lut[step]);
      return (menu_scroll_dir == 1) ? shift : (max_shift - shift);
    }
    return (cur_page == 2) ? max_shift : 0;
  };

  if (system_menu_mode == 0) {
    static constexpr int16_t SCROLL_EASE_189[5] = {0, 41, 102, 161, 189};
    int current_offset = compute_scroll_offset(system_dashboard_page, SCROLL_EASE_189, 189);
    static constexpr const char *kSysLabels[8] = {
      "Display", "Sleep & Wake", "Sound",   "Voice",
      "Music",   "Home",         "Actions", "Info"
    };
    for (int i = 0; i < 8; i++) {
      int y = row0_y + i * row_s - current_offset;
      if (menu_scroll_active ? (y >= min_vis_y && y <= max_vis_y) : (y >= row0_y - 10 && y <= layout().slot5_y(4) + 10)) {
        render_modern_card(it, cx, y, 276, 44, 12, card_bg_on, card_brd_on, kSysLabels[i], col_white, font_small);
      }
    }

    if (menu_scroll_active) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    fast_print(it, cx, layout().hdr_y, font_small, theme_primary, esphome::display::TextAlign::CENTER, "SYSTEM");

    int active_page = menu_scroll_active ? menu_scroll_target_page : system_dashboard_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 1) {
    static constexpr int16_t SCROLL_EASE_315[5] = {0, 68, 170, 268, 315};
    int current_offset = compute_scroll_offset(sleep_menu_page, SCROLL_EASE_315, 315);
    char sleep_labels[10][36];
    const char *short_p = power_profile_short_label(power_profile_opt);
    snprintf(sleep_labels[0], sizeof(sleep_labels[0]), "Profile: %s", short_p);
#if LATTER_HAS_IMU
    snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Pickup Wake: %s", pickup_wake_opt.c_str());
#else
    snprintf(sleep_labels[1], sizeof(sleep_labels[1]), "Pickup Wake: N/A");
#endif
    snprintf(sleep_labels[2], sizeof(sleep_labels[2]), "Tap Wake: %s", tap_wake_opt.c_str());
#if LATTER_HAS_BATTERY
    snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Battery Timeout: %s", screen_off_opt.c_str());
#else
    snprintf(sleep_labels[3], sizeof(sleep_labels[3]), "Battery Timeout: N/A");
#endif
    snprintf(sleep_labels[4], sizeof(sleep_labels[4]), "Docked Saver: %s", docked_ss_on ? "On" : "Off");
    const char *short_s = (ss_style_opt == "Aurora Borealis") ? "Aurora"
                          : ((ss_style_opt == "Matrix Rain") ? "Matrix" : ss_style_opt.c_str());
    snprintf(sleep_labels[5], sizeof(sleep_labels[5]), "Saver Style: %s", short_s);
    snprintf(sleep_labels[6], sizeof(sleep_labels[6]), "Docked Timeout: %s", dock_inact_opt.c_str());
#if LATTER_HAS_BATTERY
    snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Hibernate: %s", hibernate_to_opt.c_str());
    snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Hold Crown: %s", instant_crown_hib ? "Instant" : "5s Delay");
    snprintf(sleep_labels[9], sizeof(sleep_labels[9]), "Stay Awake: %s", stay_awake_on ? "On" : "Off");
#else
    snprintf(sleep_labels[7], sizeof(sleep_labels[7]), "Hibernate: N/A");
    snprintf(sleep_labels[8], sizeof(sleep_labels[8]), "Hold Crown: N/A");
    snprintf(sleep_labels[9], sizeof(sleep_labels[9]), "Stay Awake: N/A");
#endif

    for (int i = 0; i < 10; i++) {
      int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        if (i == 4) {
          draw_toggle_card_y(y, sleep_labels[i], docked_ss_on);
        } else if (i == 8) {
          draw_toggle_card_y(y, sleep_labels[i], LATTER_HAS_BATTERY != 0);
        } else if (i == 9) {
          draw_toggle_card_y(y, sleep_labels[i], (LATTER_HAS_BATTERY != 0) && stay_awake_on);
        } else {
          bool en = (i == 0) ? true
                  : (i == 1) ? ((LATTER_HAS_IMU != 0) && (pickup_wake_opt != "Disabled"))
                  : (i == 2) ? (tap_wake_opt != "Disabled")
                  : (i == 3) ? ((LATTER_HAS_BATTERY != 0) && (screen_off_opt != "Never"))
                  : (i == 5) ? docked_ss_on
                  : (i == 6) ? (dock_inact_opt != "Never")
                  : (i == 7) ? ((LATTER_HAS_BATTERY != 0) && (hibernate_to_opt != "Disabled"))
                  : true;
          draw_cycle_card_y(y, sleep_labels[i], en);
        }
      }
    }

    if (menu_scroll_active) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_menu_header(it, cx, 48, "SLEEP & WAKE", font_small, theme_primary);
    draw_back();
    int active_page = menu_scroll_active ? menu_scroll_target_page : sleep_menu_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 2) {
    int current_offset = compute_scroll_offset(display_menu_page, SHARED_SCROLL_EASE_252, 252);
    char disp_labels[9][36];
    snprintf(disp_labels[0], sizeof(disp_labels[0]), "Start Screen: %s",
             (default_app_opt == "Clock") ? "Clock" : "Home");
    snprintf(disp_labels[1], sizeof(disp_labels[1]), "Clock Face: %s", clock_face_digital ? "Digital" : "Analog");
    snprintf(disp_labels[2], sizeof(disp_labels[2]), "Format: %s", is_24h_format ? "24-Hour" : "12-Hour");
    const char *pal_s = palette_opt.c_str();
    snprintf(disp_labels[3], sizeof(disp_labels[3]), "Color: %s", pal_s);
    const char *ab_s = (auto_brightness_opt == "Sun Synchronized") ? "Sun"
                       : ((auto_brightness_opt == "Time Schedule") ? "Sched" : "Off");
    snprintf(disp_labels[4], sizeof(disp_labels[4]), "Auto Bright: %s", ab_s);
    snprintf(disp_labels[5], sizeof(disp_labels[5]), "Brightness: %s",
             manual_bright_opt.empty() ? "90%" : manual_bright_opt.c_str());
    int nh12 = (night_start_hr > 12) ? (night_start_hr - 12) : night_start_hr;
    snprintf(disp_labels[6], sizeof(disp_labels[6]), "Schedule: %dAM-%dPM", day_start_hr, nh12);
    snprintf(disp_labels[7], sizeof(disp_labels[7]), "Day Bri: %s", day_brightness_opt.c_str());
    snprintf(disp_labels[8], sizeof(disp_labels[8]), "Night Bri: %s", night_brightness_opt.c_str());

    bool ab_off = (auto_brightness_opt == "Disabled" || auto_brightness_opt.empty());
    bool ab_sched = (auto_brightness_opt == "Time Schedule");

    for (int i = 0; i < 9; i++) {
      int y = row0_y + i * row_s - current_offset;
      if (y >= min_vis_y && y <= max_vis_y) {
        if (i <= 2) {
          draw_toggle_card_y(y, disp_labels[i], true);
        } else {
          bool active = !( (i == 5 && !ab_off) || (i == 6 && !ab_sched) || ((i == 7 || i == 8) && ab_off) );
          draw_cycle_card_y(y, disp_labels[i], active);
          if (i == 3) {
            const int swatch_x = layout().sys_color_swatch_x();
            const int sw_r = scale_s(9);
            fast_filled_circle(it, swatch_x, y, sw_r, theme_primary);
            fast_circle(it, swatch_x, y, sw_r, col_white);
          }
        }
      }
    }

    if (menu_scroll_active) {
      fill_rect_row_major(it, 0, 0, SCREEN_W, mask_top_h, col_bg);
      fill_rect_row_major(it, 0, mask_btm_y, SCREEN_W, SCREEN_H - mask_btm_y, col_bg);
    }
    render_menu_header(it, cx, 48, "DISPLAY", font_small, theme_primary);
    draw_back();
    int active_page = menu_scroll_active ? menu_scroll_target_page : display_menu_page;
    draw_right_chevrons(active_page == 2, active_page == 1);

  } else if (system_menu_mode == 3) {
    render_menu_header(it, cx, 48, "ACTIONS", font_small, theme_primary);
    draw_action_card_y(layout().slot5_y(0), "Screensaver Now",   card_brd_on, col_white, card_bg_on);
#if LATTER_HAS_BATTERY
    draw_action_card_y(layout().slot5_y(1), "Hibernate Now",     card_brd_on, col_white, card_bg_on);
#else
    draw_action_card_y(layout().slot5_y(1), "Screen Off Now",    card_brd_on, col_white, card_bg_on);
#endif
    draw_action_card_y(layout().slot5_y(2), "Software Update >", card_brd_on, col_white, card_bg_on);
    snprintf(buf, sizeof(buf), "BLE Beacon: %s", ble_active ? "On" : "Off");
    draw_toggle_card_y(layout().slot5_y(3), buf, ble_active);
    draw_action_card_y(layout().slot5_y(4), "Restart Device",    col_red,     col_white, esphome::Color(28, 18, 22));
    draw_back();

  } else if (system_menu_mode == 4) {
    fast_print(it, cx, scale_y(130), font_small, theme_primary, esphome::display::TextAlign::CENTER, "RESTART?");
    render_modern_card(it, cx - scale_x(73), scale_y(230), 112, 44, 12, card_bg_on, card_brd_on, "Cancel", col_white, font_small);
    render_modern_card(it, cx + scale_x(73), scale_y(230), 112, 44, 12, esphome::Color(28, 18, 22), col_red, "Restart", col_white, font_small);

  } else if (system_menu_mode == 5) {
    render_menu_header(it, cx, 48, "UPDATE", font_small, theme_primary);
    snprintf(buf, sizeof(buf), "Installed: v%s", version_str);
    fast_print(it, cx, scale_y(95), font_small, col_slate, esphome::display::TextAlign::CENTER, buf);

    bool has_update = is_newer_semver(ha_fw_latest, version_str);
    if (update_check_state == 1) {
      if (now_ms - update_check_timer > 1500U) {
        update_check_state = has_update ? 0 : 2;
        update_check_timer = now_ms;
      }
    } else if (update_check_state == 2) {
      if (now_ms - update_check_timer > 3500U) {
        update_check_state = 0;
      }
    }

    if (has_update) {
      snprintf(buf, sizeof(buf), "Available: v%s", ha_fw_latest.empty() ? "2.0.x" : ha_fw_latest.c_str());
      fast_print(it, cx, scale_y(125), font_small, theme_secondary, esphome::display::TextAlign::CENTER, buf);
      std::string summ = ha_fw_summary;
      if (summ.empty() || summ == "unavailable" || summ == "unknown") {
        summ = "Firmware update ready.";
      }
      summ = fit_text_to_width_font(summ, scale_x(280), font_small);
      fast_print(it, cx, scale_y(160), font_small, col_white, esphome::display::TextAlign::CENTER, summ.c_str());
      draw_action_card_y(scale_y(215), "Update Now", theme_primary, theme_secondary, card_bg_on);
    } else {
      snprintf(buf, sizeof(buf), "Latest: v%s", version_str);
      fast_print(it, cx, scale_y(125), font_small, theme_secondary, esphome::display::TextAlign::CENTER, buf);
      if (update_check_state == 1) {
        fast_print(it, cx, scale_y(160), font_small, theme_secondary, esphome::display::TextAlign::CENTER, "Checking Home Assistant...");
        draw_action_card_y(scale_y(215), "Checking...", theme_primary, theme_secondary, card_bg_on);
      } else if (update_check_state == 2) {
        fast_print(it, cx, scale_y(160), font_small, col_slate, esphome::display::TextAlign::CENTER, "No updates available");
        draw_action_card_y(scale_y(215), "Up to date", card_brd_on, col_white, card_bg_on);
      } else {
        fast_print(it, cx, scale_y(160), font_small, col_slate, esphome::display::TextAlign::CENTER, "Firmware is up to date");
        draw_action_card_y(scale_y(215), "Check for Updates", theme_primary, col_white, card_bg_on);
      }
    }
    draw_back();

  } else if (system_menu_mode == 6) {
    fast_print(it, cx, scale_y(100), font_small, theme_primary, esphome::display::TextAlign::CENTER, "INSTALL UPDATE?");
    fast_print(it, cx, scale_y(145), font_small, col_white, esphome::display::TextAlign::CENTER, "Download & flash firmware");
    fast_print(it, cx, scale_y(175), font_small, theme_secondary, esphome::display::TextAlign::CENTER, "Keep device powered on");
    render_modern_card(it, cx - scale_x(73), scale_y(240), 112, 44, 12, card_bg_on, card_brd_on, "Cancel", col_white, font_small);
    render_modern_card(it, cx + scale_x(73), scale_y(240), 112, 44, 12, card_bg_on, theme_primary, "Install", col_white, font_small);

  } else if (system_menu_mode == 7) {
    render_menu_header(it, cx, 48, "SOUND", font_small, theme_primary);
    snprintf(buf, sizeof(buf), "Speaker Vol: %d%%", spk_vol_pct);
    draw_cycle_card_y(layout().slot5_y(0), buf, true);
    snprintf(buf, sizeof(buf), "Alert Vol: %d%%", alert_vol_pct);
    draw_cycle_card_y(layout().slot5_y(1), buf, true);
    snprintf(buf, sizeof(buf), "Vol Step: %d%%", vol_step_pct);
    draw_cycle_card_y(layout().slot5_y(2), buf, true);
    snprintf(buf, sizeof(buf), "Game Sound: %s", game_sound_on ? "On" : "Muted");
    draw_toggle_card_y(layout().slot5_y(3), buf, game_sound_on);
    draw_back();

  } else if (system_menu_mode == 8) {
    render_menu_header(it, cx, 48, "SYSTEM INFO", font_small, theme_primary);
    const int info_x = layout().sys_info_x();
    auto print_info_row = [&](int y, const char *lbl, const char *val, esphome::Color val_col) {
      int ey = scale_y(y);
      int w_lbl = 0, x_off = 0, base = 0, th = 0;
      font_small->measure(lbl, &w_lbl, &x_off, &base, &th);
      fast_print(it, info_x, ey, font_small, col_white, esphome::display::TextAlign::LEFT, lbl);
      fast_print(it, info_x + w_lbl + 8, ey, font_small, val_col, esphome::display::TextAlign::LEFT, val);
    };

    bool has_update = is_newer_semver(ha_fw_latest, version_str);
    if (has_update) {
      snprintf(buf, sizeof(buf), "v%s (Update: v%s)", version_str, ha_fw_latest.c_str());
      print_info_row(84, "Version:", buf, col_gold);
    } else {
      snprintf(buf, sizeof(buf), "v%s (Up to date)", version_str);
      print_info_row(84, "Version:", buf, col_slate);
    }

    uint32_t up_sec = now_ms / 1000U;
    uint32_t up_d = up_sec / 86400U;
    uint32_t up_h = (up_sec % 86400U) / 3600U;
    uint32_t up_m = (up_sec % 3600U) / 60U;
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum",
             (unsigned long)up_d, (unsigned long)up_h, (unsigned long)up_m);
    print_info_row(116, "Uptime:", buf, col_slate);

    if (!latter_site_id().empty() && latter_site_id() != "primary") {
      snprintf(buf, sizeof(buf), "Site: %s", latter_site_id().c_str());
      print_info_row(148, "Latter:", buf, theme_secondary);
    } else {
      print_info_row(148, "Home Assistant:", ha_time_valid ? "Connected" : "Syncing...", col_slate);
    }

    if (std::isnan(wifi_sig)) {
      snprintf(buf, sizeof(buf), "%s dBm  •  BLE: %s", kUnknownGlyph, ble_active ? "Active" : "Off");
    } else {
      snprintf(buf, sizeof(buf), "%.0f dBm  •  BLE: %s", wifi_sig, ble_active ? "Active" : "Off");
    }
    print_info_row(180, "Wi-Fi:", buf, col_slate);

    print_info_row(212, "IP:", ip_str.empty() ? "Connecting..." : ip_str.c_str(), col_slate);

    std::string area_str = latter_area_label().empty() ? "Home" : latter_area_label();
    if (!latter_ble_proxy().empty() && latter_ble_proxy() != "unknown" && latter_ble_proxy() != "unavailable") {
      area_str += " (" + latter_ble_proxy() + ")";
    }
    area_str = fit_text_to_width_font(area_str, 220, font_small);
    print_info_row(244, "Area:", area_str.c_str(), col_slate);

    snprintf(buf, sizeof(buf), "%.0f%% (%.2fV)%s",
             std::isnan(batt) ? 100.0f : batt,
             std::isnan(batt_v) ? 4.15f : batt_v,
             is_charging ? " • Chg" : "");
    print_info_row(276, "Battery:", buf, col_slate);

    float temp_c = std::isnan(imu_temp_c) ? 30.0f : imu_temp_c;
    float temp_f = temp_c * 1.8f + 32.0f;
    snprintf(buf, sizeof(buf), "%.1fC (%.1fF) • %luKB", temp_c, temp_f, (unsigned long)free_heap_kb);
    print_info_row(308, "HW / RAM:", buf, col_slate);

    draw_back();

  } else if (system_menu_mode == 9) {
    render_menu_header(it, cx, 48, "MUSIC", font_small, theme_primary);
    snprintf(buf, sizeof(buf), "Art: %s", art_mode_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(0), buf, true);
    snprintf(buf, sizeof(buf), "Overlay: %s", overlay_style_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(1), buf, true);
    snprintf(buf, sizeof(buf), "Album Sort: %s", album_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(2), buf, true);
    snprintf(buf, sizeof(buf), "Artist Discog: %s", artist_sort_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(3), buf, true);
    snprintf(buf, sizeof(buf), "Podcast Sort: %s", podcast_sort_opt.c_str());
    draw_toggle_card_y(layout().slot5_y(4), buf, true);
    draw_back();

  } else if (system_menu_mode == 11) {
    render_menu_header(it, cx, 48, "VOICE", font_small, theme_primary);
    const bool ww_en = (va_ww_mode_opt != "Disabled");
    snprintf(buf, sizeof(buf), "Wake Mode: %s", va_ww_mode_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(0), buf, ww_en);
    snprintf(buf, sizeof(buf), "Wake Word: %s", va_ww_active_opt.empty() ? "Okay Nabu" : va_ww_active_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(1), buf, ww_en);
    draw_action_card_y(layout().slot5_y(2), "Sensitivity (Per Model) >",
                       ww_en ? card_brd_on : card_brd_off,
                       ww_en ? col_white   : col_slate,
                       ww_en ? card_bg_on  : card_bg_off);
    snprintf(buf, sizeof(buf), "Profile: %s", va_profile_opt.c_str());
    draw_cycle_card_y(layout().slot5_y(3), buf, true);
    snprintf(buf, sizeof(buf), "Wake Screen: %s", va_wake_scr ? "On" : "Off");
    draw_toggle_card_y(layout().slot5_y(4), buf, va_wake_scr);
    draw_back();

  } else if (system_menu_mode == 12) {
    render_menu_header(it, cx, 48, "SENSITIVITY", font_small, theme_primary);
    auto short_sens = [](const std::string &s) -> const char * {
      if (s == "Slightly sensitive") return "Low";
      if (s == "Very sensitive") return "High";
      return "Medium";
    };
    snprintf(buf, sizeof(buf), "Okay Nabu: %s", short_sens(sens_nabu_opt));
    draw_cycle_card_y(layout().slot5_y(0), buf, true);
    snprintf(buf, sizeof(buf), "Hey Luna: %s", short_sens(sens_luna_opt));
    draw_cycle_card_y(layout().slot5_y(1), buf, true);
    snprintf(buf, sizeof(buf), "Hey Jarvis: %s", short_sens(sens_jarvis_opt));
    draw_cycle_card_y(layout().slot5_y(2), buf, true);
    snprintf(buf, sizeof(buf), "Hey Mycroft: %s", short_sens(sens_mycroft_opt));
    draw_cycle_card_y(layout().slot5_y(3), buf, true);
    snprintf(buf, sizeof(buf), "Alexa: %s", short_sens(sens_alexa_opt));
    draw_cycle_card_y(layout().slot5_y(4), buf, true);
    draw_back();

  } else if (system_menu_mode == 13) {
    const int wheel_cy = scale_y(180);
    const float ws = (SCREEN_MIN_DIM > 520) ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f) : 1.0f;
    fast_circle(it, cx, wheel_cy, scale_s(166), card_brd_off);

    for (int a = 0; a < 60; a++) {
      float hue_01 = static_cast<float>(a) * (1.0f / 60.0f);
      float c_a = DIAL_COS[a];
      float s_a = DIAL_SIN[a];

      int x1 = cx + static_cast<int>(c_a * 148.0f * ws);
      int y1 = wheel_cy + static_cast<int>(s_a * 148.0f * ws);
      fast_filled_circle(it, x1, y1, scale_s(16), hsv_to_rgb(hue_01, 1.00f, 1.0f));

      int x2 = cx + static_cast<int>(c_a * 124.0f * ws);
      int y2 = wheel_cy + static_cast<int>(s_a * 124.0f * ws);
      fast_filled_circle(it, x2, y2, scale_s(14), hsv_to_rgb(hue_01, 0.84f, 1.0f));

      int x3 = cx + static_cast<int>(c_a * 101.0f * ws);
      int y3 = wheel_cy + static_cast<int>(s_a * 101.0f * ws);
      fast_filled_circle(it, x3, y3, scale_s(12), hsv_to_rgb(hue_01, 0.68f, 1.0f));

      int x4 = cx + static_cast<int>(c_a * 79.0f * ws);
      int y4 = wheel_cy + static_cast<int>(s_a * 79.0f * ws);
      fast_filled_circle(it, x4, y4, scale_s(11), hsv_to_rgb(hue_01, 0.52f, 1.0f));

      int x5 = cx + static_cast<int>(c_a * 59.0f * ws);
      int y5 = wheel_cy + static_cast<int>(s_a * 59.0f * ws);
      fast_filled_circle(it, x5, y5, scale_s(9), hsv_to_rgb(hue_01, 0.36f, 1.0f));
    }

    fast_filled_circle(it, cx, wheel_cy, scale_s(49), col_black);
    fast_filled_circle(it, cx, wheel_cy, scale_s(44), theme_primary);
    fast_circle(it, cx, wheel_cy, scale_s(44), col_white);
    fast_circle(it, cx, wheel_cy, scale_s(45), theme_secondary);

    float rad = color_wheel_hue_deg() * (PI_F / 180.0f);
    float r_sel = (59.0f + ((color_wheel_sat() - 0.35f) / 0.65f) * 89.0f) * ws;
    r_sel = std::max(57.0f * ws, std::min(150.0f * ws, r_sel));
    int sel_x = cx + static_cast<int>(fast_cos(rad) * r_sel);
    int sel_y = wheel_cy + static_cast<int>(fast_sin(rad) * r_sel);
    fast_filled_circle(it, sel_x, sel_y, scale_s(12), theme_primary);
    fast_circle(it, sel_x, sel_y, scale_s(12), col_white);
    fast_circle(it, sel_x, sel_y, scale_s(13), col_white);
    fast_circle(it, sel_x, sel_y, scale_s(14), col_black);

    render_modern_card(it, cx, scale_y(376), 224, 38, 12, card_bg_on, theme_primary, "Save Custom Color", col_white, font_small);
    draw_back();
  }
}


// ==============================================================================

}  // namespace latter_ui

using namespace latter_ui;
