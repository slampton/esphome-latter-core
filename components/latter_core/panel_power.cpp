#include "hal_panel_power.h"

#include "esphome/core/defines.h"

#ifdef USE_I2C
namespace latter_ui {

void hal_set_touch_sleep_mode_bus(esphome::i2c::I2CBus *bus, bool sleep) {
#if defined(USE_ESP32) && LATTER_HAS_TOUCH && LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY
  if (bus == nullptr) return;
  static int s_last_sleep = -1;
  const int want = sleep ? 1 : 0;
  if (s_last_sleep == want) return;
  // FocalTech FT6336 / FT3168 (I2C 0x38): REG 0x86 = 0x01 enables auto-entry to low-power monitor scan
  // (~0.15 mA vs ~2.5 mA active) while keeping INT active for tap-to-wake; 0x00 keeps active scan.
  // CST9217 resides at 0x5A so a probe/write to 0x38 simply NACKs once and disables further writes.
  static bool s_focaltech_present = true;
  if (!s_focaltech_present) {
    s_last_sleep = want;
    return;
  }
  uint8_t chip_reg = 0xA3;
  uint8_t chip_id = 0;
  if (s_last_sleep < 0) {
    if (bus->write_readv(0x38, &chip_reg, 1, &chip_id, 1) != esphome::i2c::ERROR_OK) {
      s_focaltech_present = false;
      s_last_sleep = want;
      return;
    }
  }
  const uint8_t wr[2] = {0x86, static_cast<uint8_t>(sleep ? 0x01 : 0x00)};
  if (bus->write(0x38, wr, 2) == esphome::i2c::ERROR_OK) {
    s_last_sleep = want;
    LATTER_PROF_LOGI("[PROF:TOUCH] monitor_mode=%d", want);
  }
#else
  (void) bus;
  (void) sleep;
#endif
}

}  // namespace latter_ui
#endif  // USE_I2C
