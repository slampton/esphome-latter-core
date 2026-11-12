#pragma once

// ==============================================================================
// Latter Core Battery Model & AXP2101 PMU Driver Declarations (hal_battery.h)
// ==============================================================================
// One canonical battery path for every battery board. The only board trait is LATTER_BATTERY_MAH, the
// fitted cell's rated capacity (boards/facts power.battery_mah); everything below derives from it.
//
// Charge profile (AXP2101 REG61-64, applied by hal_init_axp2101), for a 3.7 V LiPo:
//   constant current  <= 0.5C floored to the AXP2101 step (200 mA for 400/500 mAh, 500 mA for 1000 mAh),
//                     capped at 500 mA and folded back to 100 mA above 65 C AXP2101 die temperature
//   precharge         0.1C below the precharge threshold (floored to 25 mA steps, 25..200 mA)
//   termination       C/20, enabled (floored to 25 mA steps, 25..200 mA)
//   constant voltage  4.20 V (standard LiPo; no fitted cell is a 4.35 V HV type)
// ==============================================================================

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#ifdef USE_ESP32
#include <sdkconfig.h>
#endif
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

#ifndef LATTER_BATTERY_MAH
#define LATTER_BATTERY_MAH 0
#endif

#if defined(USE_ESP32) && defined(LATTER_HAS_BATTERY) && LATTER_HAS_BATTERY
static_assert(LATTER_BATTERY_MAH >= 100 && LATTER_BATTERY_MAH <= 5000,
              "Battery boards declare LATTER_BATTERY_MAH (the fitted cell, boards/facts power.battery_mah)");
#endif

namespace latter_ui {

// ---------------------------------------------------------------------------------------------------
// Charge profile (<= 0.5C standard rate, floored to AXP2101 ICC steps)
// ---------------------------------------------------------------------------------------------------
constexpr int kBattCapacityMah = LATTER_BATTERY_MAH;

constexpr int batt_clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// AXP2101 constant-charge current (REG62 bits 4:0): 25 mA steps up to 200 mA, then 100 mA steps to 1000 mA.
constexpr int axp2101_icc_floor_ma(int ma) {
  return ma < 25 ? 25 : (ma <= 200 ? (ma / 25) * 25 : (ma >= 1000 ? 1000 : 200 + ((ma - 200) / 100) * 100));
}
constexpr uint8_t axp2101_icc_code(int ma) {
  return static_cast<uint8_t>(ma <= 200 ? ma / 25 : 8 + (ma - 200) / 100);
}

constexpr int kChargeCapMa = 500;
constexpr int kChargeThermalFoldbackMa = 100;
constexpr float kAxpThermalFoldbackEnterC = 65.0f;
constexpr float kAxpThermalFoldbackClearC = 58.0f;

constexpr int charge_ma_for_capacity(int capacity_mah) {
  const int std_ma = axp2101_icc_floor_ma(capacity_mah > 0 ? capacity_mah / 2 : 200);
  const int max_raw = (capacity_mah * 8 / 10) < kChargeCapMa ? (capacity_mah * 8 / 10) : kChargeCapMa;
  const int max_ma = axp2101_icc_floor_ma(max_raw);
  return std_ma < max_ma ? std_ma : max_ma;
}
constexpr int precharge_ma_for_capacity(int capacity_mah) {
  return batt_clamp_int(((capacity_mah > 0 ? capacity_mah : 500) / 10) / 25 * 25, 25, 200);
}

constexpr int kBattChargeMaxMa = axp2101_icc_floor_ma(
    (kBattCapacityMah * 8 / 10) < kChargeCapMa ? (kBattCapacityMah * 8 / 10) : kChargeCapMa);  // <= 0.8C
constexpr int kBattChargeStdMa = axp2101_icc_floor_ma(kBattCapacityMah / 2);                    // <= 0.5C floor
constexpr int kBattChargeMa = kBattChargeStdMa < kBattChargeMaxMa ? kBattChargeStdMa : kBattChargeMaxMa;
constexpr int kBattPrechargeMa = batt_clamp_int((kBattCapacityMah / 10) / 25 * 25, 25, 200);   // 0.1C
constexpr int kBattTermMa = batt_clamp_int((kBattCapacityMah / 20) / 25 * 25, 25, 200);        // C/20
constexpr uint8_t kBattCvCode = 0x03;  // REG64 bits 2:0: 3 = 4.20 V

// ---------------------------------------------------------------------------------------------------
// Rest-voltage model & per-capacity OCV curves
// ---------------------------------------------------------------------------------------------------
constexpr int kPathFixedMilliohm = 80;
constexpr int kCellIrMilliohmAh = 120;
constexpr int path_mohm_for_capacity(int capacity_mah) {
  return kPathFixedMilliohm + (capacity_mah > 0 ? kCellIrMilliohmAh * 1000 / capacity_mah : 0);
}
constexpr int kBattPathMilliohm =
    kPathFixedMilliohm + (kBattCapacityMah > 0 ? kCellIrMilliohmAh * 1000 / kBattCapacityMah : 0);

enum class BattLoad : uint8_t { QUIET = 0, SCREEN = 1, AUDIO = 2 };

#if defined(USE_ESP32) && defined(CONFIG_PM_ENABLE) && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
constexpr int kLoadQuietMa = 8;   // Automatic light-sleep standby (~5-8 mA)
#else
constexpr int kLoadQuietMa = 55;  // Awake modem-sleep standby (~50-55 mA)
#endif
constexpr int kLoadScreenMa = 200;
constexpr int kLoadAudioMa = 300;

inline int batt_load_ma(BattLoad load) {
  switch (load) {
    case BattLoad::AUDIO: return kLoadAudioMa;
    case BattLoad::SCREEN: return kLoadScreenMa;
    default: return kLoadQuietMa;
  }
}

inline const char *batt_load_name(BattLoad load) {
  switch (load) {
    case BattLoad::AUDIO: return "audio";
    case BattLoad::SCREEN: return "screen";
    default: return "quiet";
  }
}

enum class ChargePhase : uint8_t { TRICKLE = 0, PRECHARGE = 1, CC = 2, CV = 3, DONE = 4, IDLE = 5 };

inline bool charge_phase_is_charging(ChargePhase p) {
  return p == ChargePhase::TRICKLE || p == ChargePhase::PRECHARGE || p == ChargePhase::CC ||
         p == ChargePhase::CV;
}

inline const char *battery_charge_state_text(bool on_battery, uint8_t reg01_chg_state) {
  if (on_battery) return "Discharging";
  const ChargePhase p = static_cast<ChargePhase>(reg01_chg_state & 0x07);
  if (p == ChargePhase::DONE) return "Full";
  if (charge_phase_is_charging(p)) return "Charging";
  return "Not Charging";
}

struct OcvPoint {
  int16_t mv;
  uint8_t pct;
};

// Small wearable cells (<= 450 mAh): slightly softer knee at low temperature / higher relative IR.
constexpr OcvPoint kOcvCurveSmall[] = {
    {3400, 0},  {3505, 5},  {3565, 10}, {3625, 15}, {3690, 20}, {3755, 25}, {3795, 30},
    {3835, 35}, {3860, 40}, {3880, 45}, {3895, 50}, {3915, 55}, {3955, 60}, {3995, 65},
    {4025, 70}, {4040, 75}, {4055, 80}, {4070, 85}, {4085, 90}, {4105, 95}, {4155, 100},
};
// Medium handheld cells (451..750 mAh): bench-measured 500 mAh Pocket Assistant discharge curve.
constexpr OcvPoint kOcvCurveMedium[] = {
    {3400, 0},  {3495, 5},  {3555, 10}, {3615, 15}, {3680, 20}, {3750, 25}, {3790, 30},
    {3830, 35}, {3855, 40}, {3875, 45}, {3890, 50}, {3910, 55}, {3950, 60}, {3990, 65},
    {4020, 70}, {4035, 75}, {4050, 80}, {4065, 85}, {4080, 90}, {4100, 95}, {4150, 100},
};
// Larger pouch cells (> 750 mAh, e.g. 1000 mAh Mobile Assistant): flatter mid-band plateau.
constexpr OcvPoint kOcvCurveLarge[] = {
    {3400, 0},  {3485, 5},  {3545, 10}, {3605, 15}, {3670, 20}, {3740, 25}, {3780, 30},
    {3815, 35}, {3840, 40}, {3860, 45}, {3880, 50}, {3900, 55}, {3940, 60}, {3980, 65},
    {4010, 70}, {4030, 75}, {4045, 80}, {4060, 85}, {4080, 90}, {4100, 95}, {4150, 100},
};

inline float ocv_lookup(const OcvPoint *curve, size_t n, float mv) {
  if (std::isnan(mv)) return NAN;
  if (mv <= curve[0].mv) return 0.0f;
  if (mv >= curve[n - 1].mv) return 100.0f;
  for (size_t i = 1; i < n; i++) {
    if (mv <= curve[i].mv) {
      const float v0 = curve[i - 1].mv, v1 = curve[i].mv;
      const float p0 = curve[i - 1].pct, p1 = curve[i].pct;
      return p0 + (p1 - p0) * (mv - v0) / (v1 - v0);
    }
  }
  return 100.0f;
}

inline float ocv_to_soc_for_capacity(float mv, int capacity_mah = kBattCapacityMah) {
  if (capacity_mah > 0 && capacity_mah <= 450) {
    return ocv_lookup(kOcvCurveSmall, sizeof(kOcvCurveSmall) / sizeof(kOcvCurveSmall[0]), mv);
  }
  if (capacity_mah > 750) {
    return ocv_lookup(kOcvCurveLarge, sizeof(kOcvCurveLarge) / sizeof(kOcvCurveLarge[0]), mv);
  }
  return ocv_lookup(kOcvCurveMedium, sizeof(kOcvCurveMedium) / sizeof(kOcvCurveMedium[0]), mv);
}

enum class HibernateReason : uint8_t { OTHER = 0, CRITICAL = 1, IDLE = 2, KEY = 3, BUTTON = 4 };
inline const char *hibernate_reason_name(uint8_t r) {
  static const char *const kNames[5] = {"other", "critical", "idle", "key", "button"};
  return r < 5 ? kNames[r] : "other";
}
constexpr uint8_t kHibRecordMagic = 0xB0;

struct HibernateRecord {
  bool valid = false;
  uint8_t reason = 0;
  uint16_t mv = 0;
  uint8_t soc = 0xFF;
};

struct BatteryModel {
  static constexpr uint32_t kSampleMs = 10000U;
  static constexpr uint32_t kSampleQuietMs = 60000U;
  static constexpr float kEmaTauMs = 60000.0f;
  static constexpr uint32_t kSettleMs = 180000U;
  static constexpr uint32_t kCvNominalMs = 40U * 60U * 1000U;
  static constexpr float kCvRampCeilPct = 99.0f;
  static constexpr float kLowPct = 10.0f;
  static constexpr float kLowClearPct = 15.0f;
  static constexpr int kCutoffRestMv = 3400;
  static constexpr int kCutoffLoadedMv = 3300;
  static constexpr uint32_t kCutoffHoldMs = 30000U;
  static constexpr uint8_t kCutoffMinSamples = 4;
  static constexpr int kFastSampleMv = 3500;
  static constexpr int kBootCutoffMv = kCutoffRestMv - 50;
  static constexpr uint32_t kDrainMinMs = 30U * 60U * 1000U;
  static constexpr int kVbatMinValidMv = 2500;
  static constexpr int kVbatMaxValidMv = 4600;

  bool has_sample = false;
  uint32_t last_sample_ms = 0;
  int vraw_mv = 0;
  int vrest_mv = 0;
  float ema_mv = NAN;
  float soc_model = NAN;
  float soc_display = NAN;
  bool on_battery = false;
  ChargePhase phase = ChargePhase::IDLE;
  BattLoad load = BattLoad::SCREEN;
  bool full_latched = false;
  uint32_t settle_until_ms = 0;
  uint32_t cv_start_ms = 0;
  float cv_start_soc = NAN;
  bool low = false;
  bool low_notice_pending = false;
  uint32_t cutoff_below_since_ms = 0;
  uint8_t cutoff_below_count = 0;
  bool cutoff_tripped = false;
  uint32_t drain_t0_ms = 0;
  float drain_soc0 = NAN;
  HibernateReason hib_reason = HibernateReason::OTHER;
  HibernateRecord prev_hib;

  uint32_t sample_interval_ms(bool quiet) const {
    const bool near_cutoff = cutoff_below_count > 0 || (has_sample && vrest_mv <= kFastSampleMv);
    return (quiet && !near_cutoff) ? kSampleQuietMs : kSampleMs;
  }
  bool sample_due(uint32_t now_ms, bool quiet) const {
    return !has_sample || (now_ms - last_sample_ms) >= sample_interval_ms(quiet);
  }

  bool settling(uint32_t now_ms) const { return static_cast<int32_t>(settle_until_ms - now_ms) > 0; }

  bool update(int vbat_mv, bool batt, ChargePhase ph, BattLoad ld, uint32_t now_ms,
              int hw_gauge_pct = -1, int capacity_mah = kBattCapacityMah) {
    if (now_ms == 0) now_ms = 1;
    const uint32_t dt_ms = has_sample ? now_ms - last_sample_ms : 0U;
    last_sample_ms = now_ms;
    if (vbat_mv < kVbatMinValidMv || vbat_mv > kVbatMaxValidMv) return false;
    const bool first = !has_sample;
    const bool plug_change = has_sample && batt != on_battery;
    const bool was_full = full_latched;
    has_sample = true;
    vraw_mv = vbat_mv;
    on_battery = batt;
    phase = ph;
    load = ld;

    const int path_mohm = kPathFixedMilliohm + (capacity_mah > 0 ? kCellIrMilliohmAh * 1000 / capacity_mah : 0);
    const int chg_ma = charge_ma_for_capacity(capacity_mah);
    const int pre_ma = precharge_ma_for_capacity(capacity_mah);

    int comp_mv = 0;
    if (batt) {
      comp_mv = batt_load_ma(ld) * path_mohm / 1000;
    } else if (ph == ChargePhase::CC) {
      comp_mv = -(chg_ma * path_mohm / 1000);
    } else if (ph == ChargePhase::PRECHARGE || ph == ChargePhase::TRICKLE) {
      comp_mv = -(pre_ma * path_mohm / 1000);
    }
    vrest_mv = vbat_mv + comp_mv;

    if (first || plug_change || std::isnan(ema_mv)) {
      ema_mv = static_cast<float>(vrest_mv);
      settle_until_ms = now_ms + kSettleMs;
    } else {
      const float alpha = 1.0f - std::exp(-static_cast<float>(dt_ms) / kEmaTauMs);
      ema_mv += alpha * (static_cast<float>(vrest_mv) - ema_mv);
    }
    soc_model = ocv_to_soc_for_capacity(ema_mv, capacity_mah);
    // Gentle fuel-gauge blend only when the hardware gauge agrees within +-15 % (rejects uncalibrated OTP 99 %).
    if (hw_gauge_pct >= 0 && hw_gauge_pct <= 100 && !std::isnan(soc_model) &&
        std::fabs(static_cast<float>(hw_gauge_pct) - soc_model) <= 15.0f) {
      soc_model = 0.85f * soc_model + 0.15f * static_cast<float>(hw_gauge_pct);
    }
    const bool settle = settling(now_ms);

    if (batt) {
      cv_start_ms = 0;
      full_latched = false;
      if (plug_change && was_full) {
        soc_display = 100.0f;
        settle_until_ms = now_ms;
      } else if (first || settle || std::isnan(soc_display)) {
        soc_display = soc_model;
      } else if (soc_model < soc_display) {
        soc_display = soc_model;
      }
      if (first || plug_change) drain_t0_ms = 0;
      if (drain_t0_ms == 0 && !settling(now_ms)) {
        drain_t0_ms = now_ms;
        drain_soc0 = soc_display;
      }
    } else {
      drain_t0_ms = 0;
      if (ph == ChargePhase::DONE) full_latched = true;
      if (full_latched) {
        soc_display = 100.0f;
      } else if (ph == ChargePhase::CV) {
        if (cv_start_ms == 0) {
          cv_start_ms = now_ms;
          const float base = std::isnan(soc_display) ? soc_model : soc_display;
          cv_start_soc = base > kCvRampCeilPct ? kCvRampCeilPct : base;
        }
        float frac = static_cast<float>(now_ms - cv_start_ms) / static_cast<float>(kCvNominalMs);
        if (frac > 1.0f) frac = 1.0f;
        const float ramp = cv_start_soc + (kCvRampCeilPct - cv_start_soc) * frac;
        if (std::isnan(soc_display) || ramp > soc_display) soc_display = ramp;
      } else if (charge_phase_is_charging(ph)) {
        cv_start_ms = 0;
        const float s = soc_model > kCvRampCeilPct ? kCvRampCeilPct : soc_model;
        if (first || settle || std::isnan(soc_display) || s > soc_display) soc_display = s;
      } else {
        cv_start_ms = 0;
        soc_display = soc_model;
      }
    }
    if (soc_display < 0.0f) soc_display = 0.0f;
    if (soc_display > 100.0f) soc_display = 100.0f;

    if (batt && !low && soc_display <= kLowPct) {
      low = true;
      low_notice_pending = true;
    } else if (low && (!batt || soc_display >= kLowClearPct)) {
      low = false;
      low_notice_pending = false;
    }

    bool tripped_now = false;
    if (!batt) {
      cutoff_below_since_ms = 0;
      cutoff_below_count = 0;
      cutoff_tripped = false;
    } else if (vrest_mv <= kCutoffRestMv || vraw_mv <= kCutoffLoadedMv) {
      if (cutoff_below_count == 0) cutoff_below_since_ms = now_ms;
      if (cutoff_below_count < 255) cutoff_below_count++;
      if (!cutoff_tripped && cutoff_below_count >= kCutoffMinSamples &&
          (now_ms - cutoff_below_since_ms) >= kCutoffHoldMs) {
        cutoff_tripped = true;
        tripped_now = true;
      }
    } else {
      cutoff_below_since_ms = 0;
      cutoff_below_count = 0;
    }
    return tripped_now;
  }

  float drain_pct_per_h(uint32_t now_ms) const {
    if (!has_sample || !on_battery || drain_t0_ms == 0 || std::isnan(drain_soc0) || std::isnan(soc_display)) {
      return NAN;
    }
    const uint32_t dt = now_ms - drain_t0_ms;
    if (dt < kDrainMinMs) return NAN;
    return (drain_soc0 - soc_display) * 3600000.0f / static_cast<float>(dt);
  }

  char state_char() const {
    if (on_battery) return 'd';
    if (full_latched || phase == ChargePhase::DONE) return 'f';
    if (phase == ChargePhase::CV) return 'v';
    if (charge_phase_is_charging(phase)) return 'c';
    return 'n';
  }
};

// Pure thermal foldback state machine step (65 C enter -> 100 mA, 58 C clear -> nominal_ma).
inline bool step_thermal_foldback_policy(bool is_charging, float die_temp_c, int nominal_ma,
                                         bool &foldback_active, int &active_icc_ma) {
  if (!is_charging) {
    const bool changed = foldback_active;
    foldback_active = false;
    active_icc_ma = nominal_ma;
    return changed;
  }
  if (!foldback_active && die_temp_c >= kAxpThermalFoldbackEnterC && nominal_ma > kChargeThermalFoldbackMa) {
    foldback_active = true;
    active_icc_ma = kChargeThermalFoldbackMa;
    return true;
  }
  if (foldback_active && die_temp_c <= kAxpThermalFoldbackClearC) {
    foldback_active = false;
    active_icc_ma = nominal_ma;
    return true;
  }
  if (!foldback_active) {
    active_icc_ma = nominal_ma;
  }
  return false;
}

// True when the board's PWR key can wake the SoC from ESP32 deep sleep (RTC/LP GPIO).
inline constexpr bool can_wake_for_on_battery_alarm_policy(int pwr_key_deep_sleep_wake, int has_battery) {
  return (pwr_key_deep_sleep_wake != 0) || (has_battery == 0);
}

inline constexpr bool can_wake_for_on_battery_alarm() {
  return can_wake_for_on_battery_alarm_policy(LATTER_PWR_KEY_DEEP_SLEEP_WAKE, LATTER_HAS_BATTERY);
}

inline constexpr int32_t resolve_alarm_sleep_seconds(int pwr_key_deep_sleep_wake, int has_battery,
                                                     bool cutoff_tripped, bool vbus_present,
                                                     int32_t sec_until_alarm) {
  if (cutoff_tripped && sec_until_alarm > 0) return -1;
  if (!can_wake_for_on_battery_alarm_policy(pwr_key_deep_sleep_wake, has_battery) &&
      sec_until_alarm > 0 && !vbus_present) {
    return -1;
  }
  return sec_until_alarm;
}

#ifdef USE_ESP32
BatteryModel &battery_model();
void battery_report_fragment(char *buf, size_t len, uint32_t now_ms);
void battery_log_profile(uint32_t now_ms);
void hal_release_pwr_key_wake_pad();
#else
inline BatteryModel &battery_model() {
  static BatteryModel m;
  return m;
}
inline void battery_report_fragment(char *buf, size_t len, uint32_t now_ms) {
  if (buf == nullptr || len == 0) return;
  buf[0] = '\0';
  const BatteryModel &b = battery_model();
  if (!b.has_sample) return;
  char dr[16];
  const float rate = b.drain_pct_per_h(now_ms);
  if (std::isnan(rate)) {
    std::snprintf(dr, sizeof(dr), "-");
  } else {
    std::snprintf(dr, sizeof(dr), "%.1f", rate);
  }
  std::snprintf(buf, len, " bat=%d/%d/%.0f/%c dr=%s", b.vraw_mv, b.vrest_mv,
                std::isnan(b.soc_display) ? -1.0f : b.soc_display, b.state_char(), dr);
}
inline void battery_log_profile(uint32_t /*now_ms*/) {}
inline void hal_release_pwr_key_wake_pad() {}
#endif

#ifdef USE_I2C
void hal_axp2101_boot_battery_check(esphome::i2c::I2CBus *bus);
void hal_init_axp2101(esphome::i2c::I2CBus *bus);
void enter_axp2101_hibernate_or_alarm_sleep(esphome::i2c::I2CBus *bus, int32_t sec_until_alarm);
float hal_axp2101_read_die_temp_c(esphome::i2c::I2CBus *bus, bool &ok);
void hal_axp2101_thermal_charge_foldback(esphome::i2c::I2CBus *bus, bool is_charging);
int hal_axp2101_read_gauge_pct(esphome::i2c::I2CBus *bus);

template <typename I2cBusT>
inline void hal_axp2101_boot_battery_check(I2cBusT *const *bus_pp) {
  if (bus_pp != nullptr) hal_axp2101_boot_battery_check(*bus_pp);
}
template <typename I2cBusT>
inline void hal_init_axp2101(I2cBusT *const *bus_pp) {
  if (bus_pp != nullptr) hal_init_axp2101(*bus_pp);
}
template <typename I2cBusT>
inline void enter_axp2101_hibernate_or_alarm_sleep(I2cBusT *const *bus_pp, int32_t sec_until_alarm) {
  if (bus_pp != nullptr) enter_axp2101_hibernate_or_alarm_sleep(*bus_pp, sec_until_alarm);
}
template <typename I2cBusT>
inline void hal_axp2101_thermal_charge_foldback(I2cBusT *const *bus_pp, bool is_charging) {
  if (bus_pp != nullptr) hal_axp2101_thermal_charge_foldback(*bus_pp, is_charging);
}
template <typename I2cBusT>
inline int hal_axp2101_read_gauge_pct(I2cBusT *const *bus_pp) {
  return (bus_pp != nullptr) ? hal_axp2101_read_gauge_pct(*bus_pp) : -1;
}
#endif  // USE_I2C

}  // namespace latter_ui
