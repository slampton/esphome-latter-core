#include "hal_imu_qmi8658.h"

#include "esphome/core/defines.h"
#include "esphome/core/hal.h"

namespace latter_ui {

ImuWomState &imu_wom() {
  static ImuWomState st;
  return st;
}

#ifdef USE_I2C
namespace qmi8658_wom {
constexpr uint8_t kAddr = 0x6B;
constexpr uint8_t kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl7 = 0x08, kCtrl9 = 0x0A;
constexpr uint8_t kCal1L = 0x0B, kCal1H = 0x0C, kStatusInt = 0x2D, kStatus1 = 0x2F, kReset = 0x60;
constexpr uint8_t kCmdWriteWom = 0x08, kCmdAck = 0x00;
constexpr uint8_t kCtrl2WomAccel = 0x2D;
constexpr uint8_t kBlankSamples = 0x10;

bool wr(esphome::i2c::I2CBus *bus, uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = {reg, val};
  return bus->write(kAddr, buf, 2) == esphome::i2c::ERROR_OK;
}

bool rd(esphome::i2c::I2CBus *bus, uint8_t reg, uint8_t *out, size_t n = 1) {
  return bus->write_readv(kAddr, &reg, 1, out, n) == esphome::i2c::ERROR_OK;
}

bool ctrl9(esphome::i2c::I2CBus *bus, uint8_t cmd, uint32_t timeout_ms = 100) {
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

void restore_by_reset(esphome::i2c::I2CBus *bus, const uint8_t *snap) {
  wr(bus, kReset, 0xB0);
  esphome::delay(15);
  wr(bus, kCtrl1, snap[0]);
  wr(bus, kCtrl2, snap[1]);
  wr(bus, 0x04, snap[2]);
  wr(bus, 0x06, snap[4]);
  wr(bus, kCtrl7, snap[6]);
}
}  // namespace qmi8658_wom

bool hal_imu_wom_enter(esphome::i2c::I2CBus *bus, uint8_t threshold_mg) {
  using namespace qmi8658_wom;
  ImuWomState &st = imu_wom();
  if (bus == nullptr || st.failed) return false;
  auto &drv = qmi8658_imu_driver();
  drv.set_bus(bus);
  latter_hal::hal_registry().imu = &drv;

  if (!st.armed) {
    if (!rd(bus, kCtrl1, st.snap, 7)) return false;
    st.snap_valid = true;
  }
  bool ok = wr(bus, kCtrl1, st.snap[0] & ~0x18);
  ok = ok && wr(bus, kCtrl7, 0x00);
  ok = ok && wr(bus, kCtrl2, kCtrl2WomAccel);
  ok = ok && wr(bus, kCal1L, threshold_mg);
  ok = ok && wr(bus, kCal1H, kBlankSamples & 0x3F);
  ok = ok && ctrl9(bus, kCmdWriteWom);
  ok = ok && wr(bus, kCtrl7, 0x01);
  ok = ok && wr(bus, kCtrl1, (st.snap[0] | 0x08) & ~0x10);
  uint8_t s1 = 0;
  ok = ok && rd(bus, kStatus1, &s1);
  if (!ok) {
    st.failed = true;
    st.armed = false;
    restore_by_reset(bus, st.snap);
    ESP_LOGW("PROF", "[PROF:IMU] wom=fail: CTRL9 handshake failed, staying on polled raise-to-wake");
    return false;
  }
  st.armed = true;
  st.threshold_mg = threshold_mg;
  LATTER_PROF_LOGI("[PROF:IMU] wom=armed thr=%umg", static_cast<unsigned>(threshold_mg));
  return true;
}

void hal_imu_wom_exit(esphome::i2c::I2CBus *bus) {
  using namespace qmi8658_wom;
  ImuWomState &st = imu_wom();
  if (bus == nullptr || !st.armed) return;
  st.armed = false;
  if (!st.snap_valid) return;
  wr(bus, kCtrl1, st.snap[0] & ~0x18);
  wr(bus, kCtrl7, 0x00);
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

bool hal_imu_wom_event(esphome::i2c::I2CBus *bus) {
  if (bus == nullptr || !imu_wom().armed) return false;
  uint8_t s1 = 0;
  if (!qmi8658_wom::rd(bus, qmi8658_wom::kStatus1, &s1)) return false;
  return (s1 & 0x04) != 0;
}
#endif  // USE_I2C

}  // namespace latter_ui
