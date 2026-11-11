#include "hal_battery.h"

namespace latter_ui {

BatteryModel &battery_model() {
  static BatteryModel m;
  return m;
}

void battery_report_fragment(char *buf, size_t len, uint32_t now_ms) {
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

void battery_log_profile(uint32_t now_ms) {
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
      b.state_char(), batt_load_name(b.load), kBattPathMilliohm, (int)b.low,
      (unsigned)b.cutoff_below_count, (unsigned long)below_s,
      (long)(settle_left > 0 ? settle_left / 1000 : 0), std::isnan(rate) ? 0.0f : rate,
      std::isnan(rate) ? 0.0f : rate * static_cast<float>(kBattCapacityMah) / 100.0f, kBattCapacityMah);
}

}  // namespace latter_ui
