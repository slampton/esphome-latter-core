#pragma once

// ==============================================================================
// Latter Board Bring-Up Probe helpers (examples/bringup/bringup.h)
// ==============================================================================
// Used by the generated examples/bringup/<board_id>.yaml probes (tools/new_board.py). No Latter core:
// the probe logs silicon, memory, I2C presence against the board facts ledger, PMU state and every
// key / interrupt edge, so a bench log can upgrade M/L facts to H (onboarding gate G2).
// ==============================================================================

#include "esphome.h"

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>

namespace latter_bringup {

inline void log_silicon() {
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  uint32_t flash_bytes = 0;
  esp_flash_get_size(nullptr, &flash_bytes);
  ESP_LOGI("bringup", "[BRINGUP:SILICON] model=%d rev=v%d.%d cores=%d flash=%luMB",
           static_cast<int>(ci.model), ci.revision / 100, ci.revision % 100, ci.cores,
           static_cast<unsigned long>(flash_bytes / (1024UL * 1024UL)));
  ESP_LOGI("bringup", "[BRINGUP:MEMORY] psram_total=%uKB psram_free=%uKB internal_free=%uKB",
           static_cast<unsigned>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024U),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U));
}

// Probes every 7-bit address; reports expected devices as present/missing and flags unexpected ones.
template <typename I2cBusT>
inline void probe_i2c(I2cBusT *bus, const uint8_t *expected, size_t n_expected) {
  if (bus == nullptr) return;
  int missing = 0, unexpected = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    const bool ack = bus->write_readv(addr, nullptr, 0, nullptr, 0) == esphome::i2c::ERROR_OK;
    bool is_expected = false;
    for (size_t i = 0; i < n_expected; i++) {
      if (expected[i] == addr) is_expected = true;
    }
    if (is_expected) {
      ESP_LOGI("bringup", "[BRINGUP:I2C] 0x%02X expected: %s", addr, ack ? "PRESENT" : "MISSING");
      if (!ack) missing++;
    } else if (ack) {
      ESP_LOGW("bringup", "[BRINGUP:I2C] 0x%02X present but NOT in the facts ledger", addr);
      unexpected++;
    }
  }
  ESP_LOGI("bringup", "[BRINGUP:I2C] summary expected=%u missing=%d unexpected=%d", static_cast<unsigned>(n_expected),
           missing, unexpected);
}
template <typename I2cBusT>
inline void probe_i2c(I2cBusT *const *bus_pp, const uint8_t *expected, size_t n_expected) {
  if (bus_pp != nullptr) probe_i2c(*bus_pp, expected, n_expected);
}

// AXP2101 snapshot: power status, charge state, VBAT, fuel gauge and rail enables.
template <typename I2cBusT>
inline void log_axp2101(I2cBusT *bus) {
  if (bus == nullptr) return;
  auto rd = [bus](uint8_t reg) -> int {
    uint8_t v = 0;
    return bus->write_readv(0x34, &reg, 1, &v, 1) == esphome::i2c::ERROR_OK ? v : -1;
  };
  const int s0 = rd(0x00), s1 = rd(0x01), vh = rd(0x34), vl = rd(0x35), gauge = rd(0xA4);
  const int dcdc = rd(0x80), ldo0 = rd(0x90), ldo1 = rd(0x91), on_src = rd(0x20), off_src = rd(0x21);
  const int vbat_mv = (vh < 0 || vl < 0) ? -1 : (((vh & 0x3F) << 8) | vl);
  ESP_LOGI("bringup",
           "[BRINGUP:PMU] status0=0x%02X status1=0x%02X vbat=%dmV gauge=%d%% dcdc=0x%02X ldo0=0x%02X ldo1=0x%02X "
           "last_on=0x%02X last_off=0x%02X",
           s0, s1, vbat_mv, gauge, dcdc, ldo0, ldo1, on_src, off_src);
}
template <typename I2cBusT>
inline void log_axp2101(I2cBusT *const *bus_pp) {
  if (bus_pp != nullptr) log_axp2101(*bus_pp);
}

}  // namespace latter_bringup
