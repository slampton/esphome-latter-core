#include "hal_panel_power.h"

#include "esphome/core/defines.h"

#ifdef USE_I2C
namespace latter_ui {

void hal_set_touch_sleep_mode_bus(esphome::i2c::I2CBus *bus, bool sleep) {
#if defined(USE_ESP32) && LATTER_HAS_TOUCH && LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
  if (bus == nullptr) return;
  static int s_last_sleep = -1;
  const int want = sleep ? 1 : 0;
  // On wake (!sleep), always re-apply active scan registers so a prior touch hardware reset
  // or transient NACK cannot leave FocalTech stuck in low-power monitor scan.
  if (sleep && s_last_sleep == want) return;
  // FocalTech FT6336 / FT3168 (I2C 0x38): REG 0x86 = 0x01 enables auto-entry to low-power monitor scan
  // (~0.15 mA vs ~2.5 mA active) while keeping INT active for tap-to-wake; 0x00 keeps active scan.
  // Non-FocalTech boards (e.g., CST9217 @ 0x5A) disable 0x38 writes after 3 consecutive probe NACKs.
  static uint8_t s_probe_fail_count = 0;
  static bool s_focaltech_confirmed = false;
  if (!s_focaltech_confirmed && s_probe_fail_count >= 3U) {
    s_last_sleep = want;
    return;
  }
  if (!s_focaltech_confirmed) {
    uint8_t chip_reg = 0xA3;
    uint8_t chip_id = 0;
    if (bus->write_readv(0x38, &chip_reg, 1, &chip_id, 1) != esphome::i2c::ERROR_OK) {
      ++s_probe_fail_count;
      s_last_sleep = -1;
      return;
    }
    s_focaltech_confirmed = true;
    s_probe_fail_count = 0;
  }
  const uint8_t wr[2] = {0x86, static_cast<uint8_t>(sleep ? 0x01 : 0x00)};
  if (bus->write(0x38, wr, 2) == esphome::i2c::ERROR_OK) {
    if (!sleep) {
      const uint8_t trig_mode[2] = {0xA4, 0x01};
      const uint8_t act_rate[2] = {0x88, 0x06};
      (void) bus->write(0x38, trig_mode, 2);
      (void) bus->write(0x38, act_rate, 2);
    }
    if (s_last_sleep != want) {
      LATTER_PROF_LOGI("[PROF:TOUCH] monitor_mode=%d", want);
    }
    s_last_sleep = want;
  } else {
    s_last_sleep = -1;
  }
#else
  (void) bus;
  (void) sleep;
#endif
}

}  // namespace latter_ui
#endif  // USE_I2C
