#pragma once

// ==============================================================================
// Latter Core QMI8658 Hardware Wake-on-Motion (hal_imu_qmi8658.h)
// ==============================================================================

#include <algorithm>
#include <cstdint>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#ifdef USE_I2C
#include "esphome/components/i2c/i2c_bus.h"
#endif

#include "hal_manifest.h"
#include "latter_prof.h"

namespace latter_ui {

struct ImuWomState {
  bool armed = false;
  bool failed = false;
  bool snap_valid = false;
  uint8_t snap[7] = {0};
  uint32_t qualify_until_ms = 0;
  uint32_t events = 0;
  uint8_t threshold_mg = 0;
};

#ifdef USE_ESP32
ImuWomState &imu_wom();
#else
inline ImuWomState &imu_wom() {
  static ImuWomState st;
  return st;
}
#endif

inline uint8_t imu_wom_threshold_mg(float software_thresh_g) {
  const int mg = static_cast<int>(software_thresh_g * 1000.0f * 0.5f);
  return static_cast<uint8_t>(std::max(32, std::min(255, mg)));
}

inline bool imu_wom_qualifying(uint32_t now_ms) {
  const ImuWomState &st = imu_wom();
  return st.qualify_until_ms != 0 && static_cast<int32_t>(st.qualify_until_ms - now_ms) > 0;
}

#ifdef USE_I2C
bool hal_imu_wom_enter(esphome::i2c::I2CBus *bus, uint8_t threshold_mg);
void hal_imu_wom_exit(esphome::i2c::I2CBus *bus);
bool hal_imu_wom_event(esphome::i2c::I2CBus *bus);

template <typename I2cBusT>
inline bool hal_imu_wom_enter(I2cBusT *const *bus_pp, uint8_t threshold_mg) {
  return bus_pp != nullptr && hal_imu_wom_enter(*bus_pp, threshold_mg);
}
template <typename I2cBusT>
inline void hal_imu_wom_exit(I2cBusT *const *bus_pp) {
  if (bus_pp != nullptr) hal_imu_wom_exit(*bus_pp);
}
template <typename I2cBusT>
inline bool hal_imu_wom_event(I2cBusT *const *bus_pp) {
  return bus_pp != nullptr && hal_imu_wom_event(*bus_pp);
}
#endif  // USE_I2C

}  // namespace latter_ui
