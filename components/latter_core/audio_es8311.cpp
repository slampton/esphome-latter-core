#include "audio_router.h"

#include "esphome/core/defines.h"

namespace latter_ui {

#ifdef USE_ESP32
std::atomic<uint32_t> &echo_last_loud_ms() {
  static std::atomic<uint32_t> last{0};
  return last;
}

std::atomic<bool> &echo_meter_active() {
  static std::atomic<bool> active{false};
  return active;
}
#endif

#ifdef USE_I2C
void hal_set_es8311_es7210_standby(esphome::i2c::I2CBus *bus, bool standby, bool has_es7210) {
  if (bus == nullptr) return;

#if defined(LATTER_IS_STATIONARY) && LATTER_IS_STATIONARY
  if (standby) return;
  if (has_es7210) {
    const uint8_t adc_wake_seq[][2] = {
        {0x01, 0x00},
        {0x06, 0x00},
        {0x40, 0x43},
        {0x4B, 0x00},
        {0x4C, 0x00},
        {0x00, 0x71},
        {0x00, 0x41},
    };
    for (const auto &reg : adc_wake_seq) {
      bus->write(0x40, reg, 2);
    }
  }
#else
  static int last_stby = -1;
  static constexpr uint8_t kEs8311SnapRegs[] = {0x01, 0x02, 0x0D, 0x0E, 0x12, 0x14, 0x15, 0x17, 0x45};
  static uint8_t es8311_snap[sizeof(kEs8311SnapRegs)] = {};
  static bool es8311_snap_valid = false;
  static constexpr uint8_t kEs7210SnapRegs[] = {0x47, 0x48, 0x49, 0x4A};
  static uint8_t es7210_snap[sizeof(kEs7210SnapRegs)] = {};
  static bool es7210_snap_valid = false;
  auto snapshot = [bus](uint8_t addr, const uint8_t *regs, size_t n, uint8_t *out) -> bool {
    for (size_t i = 0; i < n; i++) {
      uint8_t reg = regs[i];
      if (bus->write_readv(addr, &reg, 1, &out[i], 1) != esphome::i2c::ERROR_OK) return false;
    }
    return true;
  };
  const int target = standby ? 1 : 0;
  if (standby) {
    if (target == last_stby) return;
    last_stby = target;
    es8311_snap_valid = snapshot(0x18, kEs8311SnapRegs, sizeof(kEs8311SnapRegs), es8311_snap);
    if (es8311_snap_valid) {
      const uint8_t dac_suspend_seq[][2] = {
          {0x17, 0x00},
          {0x0E, 0xFF},
          {0x12, 0x02},
          {0x14, 0x00},
          {0x0D, 0xFA},
          {0x15, 0x00},
          {0x02, 0x10},
          {0x01, 0x30},
          {0x01, 0x00},
          {0x45, 0x00},
          {0x0D, 0xFC},
          {0x02, 0x00},
      };
      for (const auto &reg : dac_suspend_seq) {
        bus->write(0x18, reg, 2);
      }
    } else {
      const uint8_t dac_lp[2] = {0x0D, 0xFC};
      bus->write(0x18, dac_lp, 2);
    }
    if (has_es7210) {
      es7210_snap_valid = snapshot(0x40, kEs7210SnapRegs, sizeof(kEs7210SnapRegs), es7210_snap);
      if (es7210_snap_valid) {
        for (uint8_t reg = 0x47; reg <= 0x4A; reg++) {
          const uint8_t lp[2] = {reg, 0xFF};
          bus->write(0x40, lp, 2);
        }
      }
      const uint8_t adc_lp_seq[][2] = {
          {0x4B, 0xFF},
          {0x4C, 0xFF},
          {0x40, 0xC0},
          {0x01, 0x7F},
          {0x06, 0x07},
      };
      for (const auto &reg : adc_lp_seq) {
        bus->write(0x40, reg, 2);
      }
    }
  } else {
    if (last_stby != 0) {
      last_stby = 0;
      if (es8311_snap_valid) {
        for (size_t i = 0; i < sizeof(kEs8311SnapRegs); i++) {
          const uint8_t reg[2] = {kEs8311SnapRegs[i], es8311_snap[i]};
          bus->write(0x18, reg, 2);
        }
        es8311_snap_valid = false;
      } else {
        const uint8_t dac_wake[2] = {0x0D, 0x01};
        bus->write(0x18, dac_wake, 2);
      }
      if (has_es7210) {
        if (es7210_snap_valid) {
          for (size_t i = 0; i < sizeof(kEs7210SnapRegs); i++) {
            const uint8_t reg[2] = {kEs7210SnapRegs[i], es7210_snap[i]};
            bus->write(0x40, reg, 2);
          }
          es7210_snap_valid = false;
        }
        const uint8_t adc_wake_seq[][2] = {
            {0x01, 0x00},
            {0x06, 0x00},
            {0x40, 0x43},
            {0x4B, 0x00},
            {0x4C, 0x00},
        };
        for (const auto &reg : adc_wake_seq) {
          bus->write(0x40, reg, 2);
        }
      }
    }
    if (has_es7210) {
      const uint8_t adc_csm_rst[2] = {0x00, 0x71};
      const uint8_t adc_csm_en[2] = {0x00, 0x41};
      bus->write(0x40, adc_csm_rst, 2);
      bus->write(0x40, adc_csm_en, 2);
    }
  }
#endif
}

void hal_power_down_audio_and_imu_for_hibernate(esphome::i2c::I2CBus *bus, bool has_imu, bool has_es7210) {
  if (bus == nullptr) return;
  if (has_imu) {
    const uint8_t qmi_pwr_down[2] = {0x08, 0x00};
    bus->write(0x6B, qmi_pwr_down, 2);
  }
  const uint8_t dac_sleep1[2] = {0x0D, 0xFF};
  const uint8_t dac_sleep2[2] = {0x00, 0x80};
  bus->write(0x18, dac_sleep1, 2);
  bus->write(0x18, dac_sleep2, 2);
  if (has_es7210) {
    const uint8_t es7210_pwr[2] = {0x00, 0xFF};
    bus->write(0x40, es7210_pwr, 2);
  }
}
#endif  // USE_I2C

}  // namespace latter_ui
