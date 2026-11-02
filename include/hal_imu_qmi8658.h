#pragma once

// ==============================================================================
// Latter Core QMI8658 Hardware Wake-on-Motion (hal_imu_qmi8658.h)
// ==============================================================================
// Screen-off raise-to-wake without polling: the accelerometer runs in its low-power
// Wake-on-Motion (WoM) mode and toggles INT1 on motion; the ESP32 only resumes IMU
// polling (and the orientation-qualified RaiseToWakeDetector) after an interrupt.
//
// Register facts (QMI8658A datasheet sections 5.10 / 6 / 12, QMI8658C sections 5.9 / 9,
// SensorLib SensorQMI8658, QST reference driver):
//   CTRL1 0x02  bit3 INT1_EN (push-pull when set, High-Z when clear), bit4 INT2_EN
//   CTRL2 0x03  aFS[6:4], aODR[3:0]; 11xx = low-power ODR (requires gEN = 0)
//   CTRL7 0x08  bit0 aEN, bit1 gEN
//   CTRL9 0x0A  command register; 0x08 = CTRL_CMD_WRITE_WOM_SETTING, 0x00 = ACK / NOP
//   CAL1_L 0x0B WoM threshold, 1 mg/LSB, 0 disables WoM
//   CAL1_H 0x0C [7:6] pin + initial level (00 = INT1 low), [5:0] blanking samples
//   STATUSINT 0x2D bit7 CmdDone; STATUS1 0x2F bit2 WoM (read clears it and resets the line)
// In WoM mode the chip produces no sensor data, so pollers stay stopped while armed. Each
// WoM event toggles INT1, so the GPIO is watched on both edges and STATUS1 bit2 filters out
// the edge caused by the STATUS1 read itself.
// ESPHome's qmi8658 driver writes configuration only in setup(); update_data() just reads
// 0x33..0x40, so the snapshot taken here is the only state that needs restoring.
// ==============================================================================

#include "esphome.h"

#include <algorithm>
#include <cstdint>

namespace latter_ui {

struct ImuWomState {
  bool armed = false;
  bool failed = false;          // CTRL9 handshake failed once: stay on polled raise-to-wake
  bool snap_valid = false;
  uint8_t snap[7] = {0};        // CTRL1..CTRL7 (0x02..0x08) as configured by ESPHome
  uint32_t qualify_until_ms = 0;  // polling window after a WoM event
  uint32_t events = 0;
  uint8_t threshold_mg = 0;
};

inline ImuWomState &imu_wom() {
  static ImuWomState st;
  return st;
}

// WoM gates the polled detector, so it is set below the software screen-off threshold.
inline uint8_t imu_wom_threshold_mg(float software_thresh_g) {
  const int mg = static_cast<int>(software_thresh_g * 1000.0f * 0.5f);
  return static_cast<uint8_t>(std::max(32, std::min(255, mg)));
}

inline bool imu_wom_qualifying(uint32_t now_ms) {
  const ImuWomState &st = imu_wom();
  return st.qualify_until_ms != 0 && static_cast<int32_t>(st.qualify_until_ms - now_ms) > 0;
}

namespace qmi8658_wom {
constexpr uint8_t kAddr = 0x6B;
constexpr uint8_t kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl7 = 0x08, kCtrl9 = 0x0A;
constexpr uint8_t kCal1L = 0x0B, kCal1H = 0x0C, kStatusInt = 0x2D, kStatus1 = 0x2F, kReset = 0x60;
constexpr uint8_t kCmdWriteWom = 0x08, kCmdAck = 0x00;
constexpr uint8_t kCtrl2WomAccel = 0x2D;  // aST 0, +/-8 g, low-power 21 Hz
constexpr uint8_t kBlankSamples = 0x10;   // ~0.76 s at 21 Hz: ignores the screen-off tap itself

template <typename I2cBusT> inline bool wr(I2cBusT *bus, uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return bus->write(kAddr, buf, 2) == esphome::i2c::ERROR_OK;
}

template <typename I2cBusT> inline bool rd(I2cBusT *bus, uint8_t reg, uint8_t *out, size_t n = 1) {
  return bus->write_readv(kAddr, &reg, 1, out, n) == esphome::i2c::ERROR_OK;
}

// CTRL9 protocol (works on both A and C silicon): command, wait CmdDone, ACK, wait clear.
template <typename I2cBusT> inline bool ctrl9(I2cBusT *bus, uint8_t cmd, uint32_t timeout_ms = 100) {
  if (!wr(bus, kCtrl9, cmd)) return false;
  uint8_t s = 0;
  uint32_t t0 = esphome::millis();
  while (true) {
    if (!rd(bus, kStatusInt, &s)) return false;
    if (s & 0x80) break;
    if (esphome::millis() - t0 > timeout_ms) return false;
    esphome::delay(1);
  }
  if (!wr(bus, kCtrl9, kCmdAck)) return false;
  t0 = esphome::millis();
  while (true) {
    if (!rd(bus, kStatusInt, &s)) return false;
    if (!(s & 0x80)) return true;
    if (esphome::millis() - t0 > timeout_ms) return false;
    esphome::delay(1);
  }
}

// Soft reset and replay ESPHome's setup() register order (CTRL1, 2, 3, 5, 7) from a snapshot.
template <typename I2cBusT> inline void restore_by_reset(I2cBusT *bus, const uint8_t *snap) {
  wr(bus, kReset, 0xB0);
  esphome::delay(15);
  wr(bus, kCtrl1, snap[0]);
  wr(bus, kCtrl2, snap[1]);
  wr(bus, 0x04, snap[2]);  // CTRL3
  wr(bus, 0x06, snap[4]);  // CTRL5
  wr(bus, kCtrl7, snap[6]);
}
}  // namespace qmi8658_wom

// Arms WoM on INT1 (initial level low). Caller has stopped the IMU pollers.
template <typename I2cBusT> inline bool hal_imu_wom_enter(I2cBusT *bus, uint8_t threshold_mg) {
  using namespace qmi8658_wom;
  ImuWomState &st = imu_wom();
  if (bus == nullptr || st.failed) return false;
  if (!st.armed) {
    if (!rd(bus, kCtrl1, st.snap, 7)) return false;  // ESPHome enables ADDR_AI, so a burst read works
    st.snap_valid = true;
  }
  bool ok = wr(bus, kCtrl1, st.snap[0] & ~0x18);  // INT pins High-Z during the CTRL9 handshake
  ok = ok && wr(bus, kCtrl7, 0x00);
  ok = ok && wr(bus, kCtrl2, kCtrl2WomAccel);
  ok = ok && wr(bus, kCal1L, threshold_mg);
  ok = ok && wr(bus, kCal1H, kBlankSamples & 0x3F);  // [7:6] = 00: INT1, initial level 0
  ok = ok && ctrl9(bus, kCmdWriteWom);
  ok = ok && wr(bus, kCtrl7, 0x01);                                   // accel only (low-power ODR needs gEN = 0)
  ok = ok && wr(bus, kCtrl1, (st.snap[0] | 0x08) & ~0x10);            // INT1 push-pull
  uint8_t s1 = 0;
  ok = ok && rd(bus, kStatus1, &s1);  // clear a stale WoM flag; INT1 returns to its initial level
  if (!ok) {
    st.failed = true;
    st.armed = false;
    restore_by_reset(bus, st.snap);
    ESP_LOGW("PROF", "[PROF:IMU] wom=fail: CTRL9 handshake failed, staying on polled raise-to-wake");
    return false;
  }
  st.armed = true;
  st.threshold_mg = threshold_mg;
  ESP_LOGI("PROF", "[PROF:IMU] wom=armed thr=%umg", static_cast<unsigned>(threshold_mg));
  return true;
}

// Disarms WoM and restores the ESPHome register configuration from the snapshot.
template <typename I2cBusT> inline void hal_imu_wom_exit(I2cBusT *bus) {
  using namespace qmi8658_wom;
  ImuWomState &st = imu_wom();
  if (bus == nullptr || !st.armed) return;
  st.armed = false;
  if (!st.snap_valid) return;
  wr(bus, kCtrl1, st.snap[0] & ~0x18);
  wr(bus, kCtrl7, 0x00);  // datasheet: clear CTRL7[1:0] before disabling WoM
  wr(bus, kCal1L, 0x00);
  wr(bus, kCal1H, 0x00);
  if (ctrl9(bus, kCmdWriteWom)) {
    wr(bus, kCtrl2, st.snap[1]);
    wr(bus, kCtrl7, st.snap[6]);
    wr(bus, kCtrl1, st.snap[0]);
  } else {
    restore_by_reset(bus, st.snap);
    st.failed = true;
    ESP_LOGW("PROF", "[PROF:IMU] wom=fail: disarm handshake failed, IMU reset and restored");
  }
  uint8_t s1 = 0;
  rd(bus, kStatus1, &s1);
}

// INT1 edge while armed: true when STATUS1 reports a real WoM event (the STATUS1 read
// resets INT1, and that reset edge reports bit2 = 0).
template <typename I2cBusT> inline bool hal_imu_wom_event(I2cBusT *bus) {
  if (bus == nullptr || !imu_wom().armed) return false;
  uint8_t s1 = 0;
  if (!qmi8658_wom::rd(bus, qmi8658_wom::kStatus1, &s1)) return false;
  return (s1 & 0x04) != 0;
}

template <typename I2cBusT> inline bool hal_imu_wom_enter(I2cBusT *const *bus_pp, uint8_t threshold_mg) {
  return bus_pp != nullptr && hal_imu_wom_enter(*bus_pp, threshold_mg);
}
template <typename I2cBusT> inline void hal_imu_wom_exit(I2cBusT *const *bus_pp) {
  if (bus_pp != nullptr) hal_imu_wom_exit(*bus_pp);
}
template <typename I2cBusT> inline bool hal_imu_wom_event(I2cBusT *const *bus_pp) {
  return bus_pp != nullptr && hal_imu_wom_event(*bus_pp);
}

}  // namespace latter_ui
