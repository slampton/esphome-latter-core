#pragma once

// ==============================================================================
// Latter Core Battery Model: Charge Profile, State of Charge, Low Warning & Cutoff (hal_battery.h)
// ==============================================================================
// One canonical battery path for every battery board. The only board trait is LATTER_BATTERY_MAH, the
// fitted cell's rated capacity (boards/facts power.battery_mah); everything below derives from it.
//
// Charge profile (AXP2101 REG61-64, applied by hal_init_axp2101 in ui_system.h), for a 3.7 V LiPo:
//   constant current  0.5C, the standard charge rate in LiPo cell datasheets (the condition cycle life
//                     is rated at; Sean's decision 2026-11-09), rounded up to the next AXP2101 step, never
//                     above 0.8C, capped at 500 mA: the AXP2101 charger is linear and dissipates
//                     (VBUS - VBAT) x I inside the enclosure
//   precharge         0.1C below the precharge threshold (floored to 25 mA steps, 25..200 mA)
//   termination       C/20, enabled (floored to 25 mA steps, 25..200 mA)
//   constant voltage  4.20 V (standard LiPo; no fitted cell is a 4.35 V HV type)
//
// State of charge. The AXP2101 fuel gauge (REG A4) needs a battery model (parameter table) loaded for the
// fitted cell, which X-Powers doesn't publish; with its default model it sat at 99 % from 4.11 to 4.19 V
// and fell 17 points in 2 h mid-range on the Mobile (2026-11-09). It is read only while the Telemetry
// Profiler is on, for the [PROF:POWER] gauge= comparison.
// The AXP2101 has no battery-current ADC and no coulomb counter (its ADC channels are VBAT, TS, VBUS,
// VSYS and die temperature), so state of charge comes from the battery voltage:
//   1. VBAT (REG34/35, 1 mV/LSB) is sampled by hal_poll_pmu_1s (pmu_axp2101.yaml) from the existing 1 s
//      supervisor tick: every kSampleMs while awake or docked, every kSampleQuietMs with the screen off on
//      battery (light-sleep quiet), and back to kSampleMs on the discharge knee (kFastSampleMv) or once a
//      sample is below the cutoff, so the cutoff debounce keeps its 30 s.
//   2. Load compensation estimates the rest voltage: VBAT + I_load x R_path on battery, VBAT -
//      I_charge x R_path while charging at constant current. I_load is a per-state class (BattLoad).
//   3. An EMA with a kEmaTauMs time constant (the weight follows the actual sample spacing) smooths it.
//   4. kOcvCurve maps the smoothed rest voltage to percent.
//   5. Display rules: on battery the value never rises (except in the kSettleMs window after boot or a
//      plug change, while the estimate converges). While charging it never falls and stops at 99 %; in
//      the constant-voltage phase (the voltage is pinned at 4.20 V and carries no charge information)
//      it ramps to 99 % over kCvNominalMs; 100 % comes only from the PMIC's charge-done state and holds
//      until unplug.
//
// Safe cutoff: hibernate cleanly (state saved, API disconnected, PMIC off) when the compensated rest
// voltage is <= kCutoffRestMv or the raw voltage is <= kCutoffLoadedMv on every sample for at least
// kCutoffHoldMs (kCutoffMinSamples samples), so a Wi-Fi or speaker sag never triggers it. The low-battery
// warning (binary sensor + an on-screen notice the next time the screen is on) fires at kLowPct.
// The reason for every PMIC power-off (and the voltage and percent at that moment) is kept in the
// AXP2101 data buffer (REG04-07, retained while a cell is connected) and printed in [PROF:BOOT] hib=.
// ==============================================================================

#include "esphome.h"
#include "latter_prof.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#ifndef LATTER_BATTERY_MAH
#define LATTER_BATTERY_MAH 0
#endif

#if defined(LATTER_HAS_BATTERY) && LATTER_HAS_BATTERY
static_assert(LATTER_BATTERY_MAH >= 100 && LATTER_BATTERY_MAH <= 5000,
              "Battery boards declare LATTER_BATTERY_MAH (the fitted cell, boards/facts power.battery_mah)");
#endif

namespace latter_ui {

// ---------------------------------------------------------------------------------------------------
// Charge profile
// ---------------------------------------------------------------------------------------------------
constexpr int kBattCapacityMah = LATTER_BATTERY_MAH;

constexpr int batt_clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// AXP2101 constant-charge current (REG62 bits 4:0): 25 mA steps up to 200 mA, then 100 mA steps to 1000 mA.
constexpr int axp2101_icc_floor_ma(int ma) {
  return ma < 25 ? 25 : (ma <= 200 ? (ma / 25) * 25 : (ma >= 1000 ? 1000 : 200 + ((ma - 200) / 100) * 100));
}
constexpr int axp2101_icc_ceil_ma(int ma) {
  return ma <= 25 ? 25 : (ma <= 200 ? ((ma + 24) / 25) * 25 : (ma >= 1000 ? 1000 : 200 + ((ma - 200 + 99) / 100) * 100));
}
constexpr uint8_t axp2101_icc_code(int ma) {
  return static_cast<uint8_t>(ma <= 200 ? ma / 25 : 8 + (ma - 200) / 100);
}
// The linear charger's heat cap and the USB 2.0 default port budget.
constexpr int kChargeCapMa = 500;
constexpr int kBattChargeMaxMa = axp2101_icc_floor_ma(
    (kBattCapacityMah * 8 / 10) < kChargeCapMa ? (kBattCapacityMah * 8 / 10) : kChargeCapMa);  // 0.8C
constexpr int kBattChargeStdMa = axp2101_icc_ceil_ma(kBattCapacityMah / 2);                     // 0.5C
constexpr int kBattChargeMa = kBattChargeStdMa < kBattChargeMaxMa ? kBattChargeStdMa : kBattChargeMaxMa;
constexpr int kBattPrechargeMa = batt_clamp_int((kBattCapacityMah / 10) / 25 * 25, 25, 200);   // 0.1C
constexpr int kBattTermMa = batt_clamp_int((kBattCapacityMah / 20) / 25 * 25, 25, 200);        // C/20
constexpr uint8_t kBattCvCode = 0x03;  // REG64 bits 2:0: 1 = 4.00 V, 2 = 4.10 V, 3 = 4.20 V, 4 = 4.35 V, 5 = 4.40 V
constexpr int kBattCvMv = 4200;

// ---------------------------------------------------------------------------------------------------
// Rest-voltage model
// ---------------------------------------------------------------------------------------------------
// Battery path resistance: cell DC internal resistance (small LiCoO2 pouch cells are about 120 mOhm at
// 1000 mAh, scaling with 1/capacity) plus the protection MOSFETs, connector and PCB (about 80 mOhm).
constexpr int kPathFixedMilliohm = 80;
constexpr int kCellIrMilliohmAh = 120;
constexpr int kBattPathMilliohm =
    kPathFixedMilliohm + (kBattCapacityMah > 0 ? kCellIrMilliohmAh * 1000 / kBattCapacityMah : 0);

// Average load per class (the cell's voltage follows the average draw, not the instant of the I2C read).
// QUIET (screen off on battery, light sleep) is measured: about 30-35 mA on the Mobile with light sleep
// (rev 2026110289; the pre-light-sleep 62-67 mA in boards/facts power_baseline no longer applies).
// SCREEN and AUDIO are ESTIMATES pending calibration: read the vraw step in the Link Report bat= / [PROF:BATT]
// when the screen or the speaker turns on or off, divide by kBattPathMilliohm, and set them from that.
enum class BattLoad : uint8_t { QUIET = 0, SCREEN = 1, AUDIO = 2 };
constexpr int kLoadQuietMa = 33;
constexpr int kLoadScreenMa = 200;  // estimate
constexpr int kLoadAudioMa = 300;   // estimate

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

// AXP2101 REG01 bits 2:0 (charger state).
enum class ChargePhase : uint8_t { TRICKLE = 0, PRECHARGE = 1, CC = 2, CV = 3, DONE = 4, IDLE = 5 };

inline bool charge_phase_is_charging(ChargePhase p) {
  return p == ChargePhase::TRICKLE || p == ChargePhase::PRECHARGE || p == ChargePhase::CC ||
         p == ChargePhase::CV;
}

// The Battery Charge State text sensor (pmu_axp2101.yaml): REG01 read each second while VBUS is present.
inline const char *battery_charge_state_text(bool on_battery, uint8_t reg01_chg_state) {
  if (on_battery) return "Discharging";
  const ChargePhase p = static_cast<ChargePhase>(reg01_chg_state & 0x07);
  if (p == ChargePhase::DONE) return "Full";
  if (charge_phase_is_charging(p)) return "Charging";
  return "Not Charging";
}

// Rest (open-circuit) voltage -> state of charge for a single 3.7 V LiCoO2 / LiPo cell at room temperature.
// Source: the widely published LiPo rest-voltage chart (4.20 V 100 %, 4.11 V 90 %, 3.95 V 70 %, 3.85 V 55 %,
// 3.80 V 40 %, 3.73 V 20 %, 3.69 V 10 %, 3.61 V 5 %, 3.27 V 0 %, in 5 % steps), with two Latter anchors:
// 0 % sits at kCutoffRestMv (3.40 V; the chart's 3.27-3.40 V band holds under 2 % and Latter powers off
// there), and 100 % at 4.16 V, the rest voltage a cell relaxes to after the charger terminates at
// 4.20 V / C/20 (4.20 V itself only exists under charge).
struct OcvPoint {
  int16_t mv;
  uint8_t pct;
};
constexpr OcvPoint kOcvCurve[] = {
    {3400, 0},  {3610, 5},  {3690, 10}, {3710, 15}, {3730, 20}, {3750, 25}, {3770, 30},
    {3790, 35}, {3800, 40}, {3820, 45}, {3840, 50}, {3850, 55}, {3870, 60}, {3910, 65},
    {3950, 70}, {3980, 75}, {4020, 80}, {4080, 85}, {4110, 90}, {4160, 100},
};
constexpr size_t kOcvPoints = sizeof(kOcvCurve) / sizeof(kOcvCurve[0]);

inline float ocv_to_soc(float mv) {
  if (std::isnan(mv)) return NAN;
  if (mv <= kOcvCurve[0].mv) return 0.0f;
  if (mv >= kOcvCurve[kOcvPoints - 1].mv) return 100.0f;
  for (size_t i = 1; i < kOcvPoints; i++) {
    if (mv <= kOcvCurve[i].mv) {
      const float v0 = kOcvCurve[i - 1].mv, v1 = kOcvCurve[i].mv;
      const float p0 = kOcvCurve[i - 1].pct, p1 = kOcvCurve[i].pct;
      return p0 + (p1 - p0) * (mv - v0) / (v1 - v0);
    }
  }
  return 100.0f;
}

// Why the PMIC was last switched off (AXP2101 data buffer REG04 low nibble).
enum class HibernateReason : uint8_t { OTHER = 0, CRITICAL = 1, IDLE = 2, KEY = 3, BUTTON = 4 };
inline const char *hibernate_reason_name(uint8_t r) {
  static const char *const kNames[5] = {"other", "critical", "idle", "key", "button"};
  return r < 5 ? kNames[r] : "other";
}
constexpr uint8_t kHibRecordMagic = 0xB0;  // REG04 high nibble: a Latter record is present

struct HibernateRecord {
  bool valid = false;
  uint8_t reason = 0;
  uint16_t mv = 0;
  uint8_t soc = 0xFF;  // 0xFF = unknown
};

// ---------------------------------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------------------------------
struct BatteryModel {
  static constexpr uint32_t kSampleMs = 10000U;       // awake, docked, low or near the cutoff
  static constexpr uint32_t kSampleQuietMs = 60000U;  // screen off on battery (light-sleep quiet)
  static constexpr float kEmaTauMs = 60000.0f;
  static constexpr uint32_t kSettleMs = 180000U;   // after boot or a plug change the estimate may move both ways
  static constexpr uint32_t kCvNominalMs = 40U * 60U * 1000U;  // typical CV phase, 0.5C down to C/20
  static constexpr float kCvRampCeilPct = 99.0f;   // only the PMIC's charge-done state shows 100 %
  // Low-battery warning, cleared once charging or after the estimate is back above kLowClearPct.
  static constexpr float kLowPct = 10.0f;
  static constexpr float kLowClearPct = 15.0f;
  // Cutoff. 3.40 V at rest: below about 3.6 V a LiPo is on its discharge knee (under 5 % left) and 3.40 V
  // leaves about 2 %, so the cell is never stored deeply discharged and the PMIC's off-state drain has
  // weeks of margin before the cell's own protection (2.5-3.0 V). 3.30 V raw is the floor under any load:
  // DCDC1 (the 3.3 V rail) is a buck from VSYS and is in dropout there; lower, a load step can trip the
  // AXP2101 DCDC under-voltage cutoff (an unclean power loss, [PROF:BOOT] last_off=dcdc-undervolt).
  static constexpr int kCutoffRestMv = 3400;
  static constexpr int kCutoffLoadedMv = 3300;
  static constexpr uint32_t kCutoffHoldMs = 30000U;
  static constexpr uint8_t kCutoffMinSamples = 4;  // 4 samples 10 s apart span 30 s
  static constexpr int kFastSampleMv = 3500;       // knee: back to kSampleMs below this rest voltage
  static constexpr uint32_t kDrainMinMs = 30U * 60U * 1000U;  // drain rate needs 30 min on battery
  static constexpr int kVbatMinValidMv = 2500;
  static constexpr int kVbatMaxValidMv = 4600;

  bool has_sample = false;
  uint32_t last_sample_ms = 0;
  int vraw_mv = 0;    // last VBAT reading
  int vrest_mv = 0;   // last compensated (rest-voltage estimate) reading
  float ema_mv = NAN;
  float soc_model = NAN;    // the OCV curve at ema_mv
  float soc_display = NAN;  // Battery Level
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
  HibernateReason hib_reason = HibernateReason::OTHER;  // set by whoever starts a hibernate
  HibernateRecord prev_hib;                              // read from the AXP2101 at boot

  // quiet = screen off on battery with no audio. The slow cadence stops on the discharge knee (rest voltage
  // <= kFastSampleMv, about 2-3 % left) and as soon as a sample is below the cutoff, so the cutoff still trips
  // 30 s (4 samples) after the voltage gets there.
  uint32_t sample_interval_ms(bool quiet) const {
    const bool near_cutoff = cutoff_below_count > 0 || (has_sample && vrest_mv <= kFastSampleMv);
    return (quiet && !near_cutoff) ? kSampleQuietMs : kSampleMs;
  }
  bool sample_due(uint32_t now_ms, bool quiet) const {
    return !has_sample || (now_ms - last_sample_ms) >= sample_interval_ms(quiet);
  }

  bool settling(uint32_t now_ms) const { return static_cast<int32_t>(settle_until_ms - now_ms) > 0; }

  // Feeds one VBAT sample. Returns true exactly once, when the cutoff trips.
  bool update(int vbat_mv, bool batt, ChargePhase ph, BattLoad ld, uint32_t now_ms) {
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

    int comp_mv = 0;
    if (batt) {
      comp_mv = batt_load_ma(ld) * kBattPathMilliohm / 1000;
    } else if (ph == ChargePhase::CC) {
      comp_mv = -(kBattChargeMa * kBattPathMilliohm / 1000);
    } else if (ph == ChargePhase::PRECHARGE || ph == ChargePhase::TRICKLE) {
      comp_mv = -(kBattPrechargeMa * kBattPathMilliohm / 1000);
    }
    vrest_mv = vbat_mv + comp_mv;

    if (first || plug_change || std::isnan(ema_mv)) {
      ema_mv = static_cast<float>(vrest_mv);
      settle_until_ms = now_ms + kSettleMs;
    } else {
      const float alpha = 1.0f - std::exp(-static_cast<float>(dt_ms) / kEmaTauMs);
      ema_mv += alpha * (static_cast<float>(vrest_mv) - ema_mv);
    }
    soc_model = ocv_to_soc(ema_mv);
    const bool settle = settling(now_ms);

    if (batt) {
      cv_start_ms = 0;
      full_latched = false;
      if (plug_change && was_full) {
        // Unplugged after charge done: start at 100 % and only fall from here.
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
        // On USB but not charging (charger paused or disabled): the cell rests while VBUS runs the system,
        // so the rest voltage reads true.
        cv_start_ms = 0;
        soc_display = soc_model;
      }
    }
    if (soc_display < 0.0f) soc_display = 0.0f;
    if (soc_display > 100.0f) soc_display = 100.0f;

    // Low-battery warning.
    if (batt && !low && soc_display <= kLowPct) {
      low = true;
      low_notice_pending = true;
    } else if (low && (!batt || soc_display >= kLowClearPct)) {
      low = false;
      low_notice_pending = false;
    }

    // Cutoff.
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

  // Average discharge since the estimate settled on battery, in %/h (NAN until kDrainMinMs have passed).
  // Derived from the voltage model, not measured current; mA = %/h x capacity / 100.
  float drain_pct_per_h(uint32_t now_ms) const {
    if (!has_sample || !on_battery || drain_t0_ms == 0 || std::isnan(drain_soc0) || std::isnan(soc_display)) {
      return NAN;
    }
    const uint32_t dt = now_ms - drain_t0_ms;
    if (dt < kDrainMinMs) return NAN;
    return (drain_soc0 - soc_display) * 3600000.0f / static_cast<float>(dt);
  }

  // d discharging, c charging (precharge or constant current), v constant voltage, f full, n not charging.
  char state_char() const {
    if (on_battery) return 'd';
    if (full_latched || phase == ChargePhase::DONE) return 'f';
    if (phase == ChargePhase::CV) return 'v';
    if (charge_phase_is_charging(phase)) return 'c';
    return 'n';
  }
};

inline BatteryModel &battery_model() {
  static BatteryModel m;
  return m;
}

// Link Report tail (hal_link_report.h): " bat=<vraw mV>/<vrest mV>/<percent>/<state> dr=<%/h>", or nothing
// before the first sample (boards without a battery never sample).
inline void battery_report_fragment(char *buf, size_t len, uint32_t now_ms) {
  if (buf == nullptr || len == 0) return;
  buf[0] = '\0';
  const BatteryModel &b = battery_model();
  if (!b.has_sample) return;
  char dr[16];
  const float rate = b.drain_pct_per_h(now_ms);
  if (std::isnan(rate)) {
    snprintf(dr, sizeof(dr), "-");
  } else {
    snprintf(dr, sizeof(dr), "%.1f", rate);
  }
  snprintf(buf, len, " bat=%d/%d/%.0f/%c dr=%s", b.vraw_mv, b.vrest_mv,
           std::isnan(b.soc_display) ? -1.0f : b.soc_display, b.state_char(), dr);
}

// [PROF:BATT], logged with every [PROF:POWER] line (Telemetry Profiler).
inline void battery_log_profile(uint32_t now_ms) {
  const BatteryModel &b = battery_model();
  if (!b.has_sample) return;
  const float rate = b.drain_pct_per_h(now_ms);
  const uint32_t below_s = b.cutoff_below_count > 0 ? (now_ms - b.cutoff_below_since_ms) / 1000U : 0U;
  const int32_t settle_left = static_cast<int32_t>(b.settle_until_ms - now_ms);
  LATTER_PROF_LOGI(
           "[PROF:BATT] vraw=%dmV vrest=%dmV ema=%.0fmV soc=%.1f%% disp=%.0f%% state=%c load=%s R=%dmOhm "
           "low=%d cut=%u/%lus settle=%lds dr=%.1f%%/h (~%.0fmA) cap=%dmAh",
           b.vraw_mv, b.vrest_mv, std::isnan(b.ema_mv) ? 0.0f : b.ema_mv,
           std::isnan(b.soc_model) ? -1.0f : b.soc_model, std::isnan(b.soc_display) ? -1.0f : b.soc_display,
           b.state_char(), batt_load_name(b.load), kBattPathMilliohm, (int) b.low,
           (unsigned) b.cutoff_below_count, (unsigned long) below_s,
           (long) (settle_left > 0 ? settle_left / 1000 : 0), std::isnan(rate) ? 0.0f : rate,
           std::isnan(rate) ? 0.0f : rate * static_cast<float>(kBattCapacityMah) / 100.0f, kBattCapacityMah);
}

}  // namespace latter_ui
