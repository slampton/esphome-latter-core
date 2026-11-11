#pragma once

// ==============================================================================
// Latter HAL Driver Interfaces & Registry (hal_types.h)
// ==============================================================================
// Pure C++20 hardware abstraction interfaces with safe no-op defaults so community
// ESP32-S3 / ESP32-P4 boards (BQ25896, MAX17048, BMI270, PDM microphones, SPI/QSPI/DSI
// panels) can register custom drivers without modifying latter_core or latter_ui.
// ==============================================================================

#include <cstdint>

namespace latter_hal {

class IPmu {
 public:
  virtual ~IPmu() = default;
  virtual void boot_battery_check() {}
  virtual void init() {}
  virtual void thermal_foldback(bool /*is_charging*/) {}
  virtual void enter_hibernate_or_alarm_sleep(int32_t /*sec_until_alarm*/) {}
};

class IBatteryGauge {
 public:
  virtual ~IBatteryGauge() = default;
  // Returns hardware fuel-gauge SoC in 0..100, or -1 when unavailable/uncalibrated.
  virtual int read_soc_pct() { return -1; }
};

class IImu {
 public:
  virtual ~IImu() = default;
  virtual bool enter_wom(uint8_t /*threshold_mg*/) { return false; }
  virtual void exit_wom() {}
  virtual bool poll_wom_event() { return false; }
};

class IPanelSleep {
 public:
  virtual ~IPanelSleep() = default;
  virtual void set_brightness(uint8_t /*bri_byte*/) {}
  virtual void service() {}
  virtual void flush() {}
};

class IAudioControl {
 public:
  virtual ~IAudioControl() = default;
  virtual void set_standby(bool /*standby*/) {}
  virtual void power_down_for_hibernate() {}
};

struct HalRegistry {
  IPmu *pmu = nullptr;
  IBatteryGauge *gauge = nullptr;
  IImu *imu = nullptr;
  IPanelSleep *panel = nullptr;
  IAudioControl *audio = nullptr;

  IPmu &pmu_or_noop() const {
    static IPmu s_noop;
    return pmu != nullptr ? *pmu : s_noop;
  }
  IBatteryGauge &gauge_or_noop() const {
    static IBatteryGauge s_noop;
    return gauge != nullptr ? *gauge : s_noop;
  }
  IImu &imu_or_noop() const {
    static IImu s_noop;
    return imu != nullptr ? *imu : s_noop;
  }
  IPanelSleep &panel_or_noop() const {
    static IPanelSleep s_noop;
    return panel != nullptr ? *panel : s_noop;
  }
  IAudioControl &audio_or_noop() const {
    static IAudioControl s_noop;
    return audio != nullptr ? *audio : s_noop;
  }
};

inline HalRegistry &hal_registry() {
  static HalRegistry s_reg;
  return s_reg;
}

}  // namespace latter_hal
