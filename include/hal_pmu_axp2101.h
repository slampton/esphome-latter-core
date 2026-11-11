#pragma once

// ==============================================================================
// Latter HAL AXP2101 PMU & Fuel-Gauge Driver Declarations (hal_pmu_axp2101.h)
// ==============================================================================

#include <cstdint>

#include "esphome/core/defines.h"
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

#include "hal_battery.h"
#include "hal_manifest.h"
#include "hal_types.h"

namespace latter_ui {

// True when the board's PWR key can wake the SoC from ESP32 deep sleep (RTC/LP GPIO).
// Boards without deep-sleep wake on the PWR key (e.g. non-LP GPIO PWR keys) disable
// on-battery alarm standby so battery Hibernate always powers off cleanly via AXP2101 0x10=0x01
// (waking reliably on a single key press via AXP PWRON) while docked USB alarms stay active.
inline constexpr bool can_wake_for_on_battery_alarm() {
  return (LATTER_PWR_KEY_DEEP_SLEEP_WAKE != 0) || (LATTER_HAS_BATTERY == 0);
}

void hal_release_pwr_key_wake_pad();

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

class Axp2101Pmu final : public latter_hal::IPmu, public latter_hal::IBatteryGauge {
 public:
  explicit Axp2101Pmu(esphome::i2c::I2CBus *bus = nullptr) : bus_(bus) {}
  void set_bus(esphome::i2c::I2CBus *bus) { bus_ = bus; }

  void boot_battery_check() override { hal_axp2101_boot_battery_check(bus_); }
  void init() override { hal_init_axp2101(bus_); }
  void thermal_foldback(bool is_charging) override { hal_axp2101_thermal_charge_foldback(bus_, is_charging); }
  void enter_hibernate_or_alarm_sleep(int32_t sec_until_alarm) override {
    enter_axp2101_hibernate_or_alarm_sleep(bus_, sec_until_alarm);
  }
  int read_soc_pct() override { return hal_axp2101_read_gauge_pct(bus_); }

 private:
  esphome::i2c::I2CBus *bus_{nullptr};
};

inline Axp2101Pmu &axp2101_pmu_driver() {
  static Axp2101Pmu s_drv;
  return s_drv;
}
#endif  // USE_I2C

}  // namespace latter_ui
