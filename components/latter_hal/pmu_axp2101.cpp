#include "hal_pmu_axp2101.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>
#endif

#if defined(USE_ESP32) && defined(LATTER_C6_RESET_PIN)
// Runs as a single global constructor in latter_hal, before FreeRTOS starts, preceding esp_hosted's
// C6 reset pulse so a deep-sleep pad hold on CHIP_PU never blocks C6 boot.
__attribute__((constructor)) static void latter_release_c6_reset_hold() {
  gpio_hold_dis(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN));
}
#endif

namespace latter_ui {

void hal_release_pwr_key_wake_pad() {
#if defined(USE_ESP32) && defined(LATTER_PWR_KEY_PIN) && LATTER_PWR_KEY_DEEP_SLEEP_WAKE && SOC_RTCIO_INPUT_OUTPUT_SUPPORTED
  const gpio_num_t pin = static_cast<gpio_num_t>(LATTER_PWR_KEY_PIN);
  if (rtc_gpio_is_valid_gpio(pin)) {
    rtc_gpio_deinit(pin);
  }
#endif
}

#ifdef USE_I2C
namespace {
constexpr uint8_t kAxpAddr = 0x34;
constexpr float kAxpThermalFoldbackEnterC = 65.0f;
constexpr float kAxpThermalFoldbackClearC = 58.0f;

bool axp_rd(esphome::i2c::I2CBus *bus, uint8_t reg, uint8_t *buf, size_t len = 1) {
  return bus != nullptr && bus->write_readv(kAxpAddr, &reg, 1, buf, len) == esphome::i2c::ERROR_OK;
}

bool axp_wr(esphome::i2c::I2CBus *bus, uint8_t reg, uint8_t val) {
  if (bus == nullptr) return false;
  const uint8_t buf[2] = {reg, val};
  return bus->write(kAxpAddr, buf, 2) == esphome::i2c::ERROR_OK;
}
}  // namespace

float hal_axp2101_read_die_temp_c(esphome::i2c::I2CBus *bus, bool &ok) {
  ok = false;
  uint8_t tbuf[2] = {0, 0};
  if (!axp_rd(bus, 0x56, tbuf, 2)) return 0.0f;
  const int raw = ((tbuf[0] & 0x3F) << 8) | tbuf[1];
  if (raw <= 0 || raw >= 0x3FFF) return 0.0f;
  // XPowersLib AXP2101::getTemperature(): 22.0 C + (7274 - raw) / 20.0f (0.05 C/LSB inverted NTC-like slope)
  const float temp_c = 22.0f + (7274.0f - static_cast<float>(raw)) / 20.0f;
  if (temp_c < -25.0f || temp_c > 125.0f) return 0.0f;
  ok = true;
  return temp_c;
}

void hal_axp2101_thermal_charge_foldback(esphome::i2c::I2CBus *bus, bool is_charging) {
#if defined(USE_ESP32) && LATTER_HAS_BATTERY
  if (bus == nullptr) return;
  auto &tel = pmu_telemetry();
  if (!is_charging) {
    if (tel.thermal_foldback_active) {
      uint8_t v = 0;
      if (axp_rd(bus, 0x62, &v)) {
        axp_wr(bus, 0x62, static_cast<uint8_t>((v & 0xE0) | axp2101_icc_code(kBattChargeMa)));
        tel.icc_ma = kBattChargeMa;
      }
      tel.thermal_foldback_active = false;
    }
    return;
  }
  bool ok = false;
  const float temp_c = hal_axp2101_read_die_temp_c(bus, ok);
  if (!ok) return;
  tel.die_temp_c = temp_c;
  if (!tel.thermal_foldback_active && temp_c >= kAxpThermalFoldbackEnterC && kBattChargeMa > kChargeThermalFoldbackMa) {
    uint8_t v = 0;
    if (axp_rd(bus, 0x62, &v)) {
      axp_wr(bus, 0x62, static_cast<uint8_t>((v & 0xE0) | axp2101_icc_code(kChargeThermalFoldbackMa)));
      tel.thermal_foldback_active = true;
      tel.icc_ma = kChargeThermalFoldbackMa;
      ESP_LOGW("PROF", "[PROF:POWER] trig=PMU_THERMAL_FOLDBACK die=%.1fC >= %.0fC -> ICC=%dmA",
               temp_c, kAxpThermalFoldbackEnterC, kChargeThermalFoldbackMa);
    }
  } else if (tel.thermal_foldback_active && temp_c <= kAxpThermalFoldbackClearC) {
    uint8_t v = 0;
    if (axp_rd(bus, 0x62, &v)) {
      axp_wr(bus, 0x62, static_cast<uint8_t>((v & 0xE0) | axp2101_icc_code(kBattChargeMa)));
      tel.thermal_foldback_active = false;
      tel.icc_ma = kBattChargeMa;
      LATTER_PROF_LOGI("[PROF:POWER] trig=PMU_THERMAL_RESTORE die=%.1fC <= %.0fC -> ICC=%dmA",
                       temp_c, kAxpThermalFoldbackClearC, kBattChargeMa);
    }
  }
#else
  (void)bus;
  (void)is_charging;
#endif
}

int hal_axp2101_read_gauge_pct(esphome::i2c::I2CBus *bus) {
  uint8_t gauge = 0xFF;
  if (!axp_rd(bus, 0xA4, &gauge, 1) || gauge > 100) return -1;
  return static_cast<int>(gauge);
}

void enter_axp2101_hibernate_or_alarm_sleep(esphome::i2c::I2CBus *bus, int32_t sec_until_alarm) {
#ifdef USE_ESP32
  BatteryModel &batt = battery_model();
  if (batt.cutoff_tripped && sec_until_alarm > 0) {
    ESP_LOGW("power", "Battery at cutoff: powering off instead of alarm-sleep (alarm in %lds)", (long)sec_until_alarm);
    sec_until_alarm = -1;
  }

  // Boards whose PWR key cannot wake ESP32 deep sleep (non-LP GPIO PWR keys):
  // On battery, never enter rail-on deep sleep (which would strand the user until the timer fires);
  // instead cut AXP2101 rails cleanly (0x10=0x01) so a single PWRON press cold-boots the board.
  // Docked on USB, timer-wake alarm standby stays active.
  if (!can_wake_for_on_battery_alarm() && sec_until_alarm > 0 && bus != nullptr) {
    uint8_t stat0 = 0;
    const bool vbus_present = axp_rd(bus, 0x00, &stat0, 1) && ((stat0 & 0x20) != 0);
    if (!vbus_present) {
      ESP_LOGI("power", "On battery without LP wake key: powering off cleanly via AXP2101 (skipping alarm in %lds)",
               (long)sec_until_alarm);
      sec_until_alarm = -1;
    }
  }

#if defined(LATTER_PWR_KEY_PIN) && LATTER_PWR_KEY_DEEP_SLEEP_WAKE && SOC_PM_SUPPORT_EXT0_WAKEUP
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(LATTER_PWR_KEY_PIN), LATTER_PWR_KEY_ACTIVE_HIGH ? 1 : 0);
#endif
  if (sec_until_alarm > 0 && sec_until_alarm <= 43200) {
    uint64_t wake_sec = static_cast<uint64_t>((sec_until_alarm > 90) ? (sec_until_alarm - 90) : 15);
    ESP_LOGI("power", "Armed alarm in %lds: entering timer-wake deep sleep (wake in %llus, AXP2101 rails kept ON)",
             (long)sec_until_alarm, (unsigned long long)wake_sec);
    esp_sleep_enable_timer_wakeup(wake_sec * 1000000ULL);
#if defined(LATTER_C6_RESET_PIN) && !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
    gpio_set_level(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN), 0);
    gpio_hold_en(static_cast<gpio_num_t>(LATTER_C6_RESET_PIN));
#endif
    esp_deep_sleep_start();
    return;
  }
  if (bus != nullptr) {
    const uint16_t rec_mv = batt.vraw_mv > 0 ? static_cast<uint16_t>(batt.vraw_mv) : 0U;
    const uint8_t rec_soc = std::isnan(batt.soc_display) ? 0xFF : static_cast<uint8_t>(std::lround(batt.soc_display));
    const uint8_t rec[4] = {static_cast<uint8_t>(kHibRecordMagic | (static_cast<uint8_t>(batt.hib_reason) & 0x0F)),
                            static_cast<uint8_t>(rec_mv >> 8), static_cast<uint8_t>(rec_mv & 0xFF), rec_soc};
    for (uint8_t i = 0; i < 4; i++) {
      axp_wr(bus, static_cast<uint8_t>(0x04 + i), rec[i]);
    }
    ESP_LOGI("power", "AXP2101 power-off: reason=%s vbat=%umV soc=%u", hibernate_reason_name(rec[0] & 0x0F),
             (unsigned)rec_mv, (unsigned)rec_soc);
    if (!axp_wr(bus, 0x10, 0x01)) {
      ESP_LOGW("power", "AXP2101 power-off write (0x10=0x01) failed; falling back to ESP deep sleep");
    }
  }
  esphome::delay(100);
  esp_deep_sleep_start();
#else
  (void)bus;
  (void)sec_until_alarm;
#endif
}

void hal_axp2101_boot_battery_check(esphome::i2c::I2CBus *bus) {
#ifdef USE_ESP32
  if (bus == nullptr) return;
  auto &drv = axp2101_pmu_driver();
  drv.set_bus(bus);
  latter_hal::hal_registry().pmu = &drv;
  latter_hal::hal_registry().gauge = &drv;

  uint8_t stat0 = 0;
  if (!axp_rd(bus, 0x00, &stat0, 1)) return;
  const bool vbus_good = (stat0 & 0x20) != 0;
  const bool bat_present = (stat0 & 0x08) != 0;
  if (vbus_good || !bat_present) return;
  int best_mv = -1;
  for (int i = 0; i < 3; i++) {
    uint8_t vbuf[2] = {0, 0};
    if (axp_rd(bus, 0x34, vbuf, 2)) {
      const int mv = ((vbuf[0] & 0x3F) << 8) | vbuf[1];
      if (mv > best_mv) best_mv = mv;
    }
    if (i < 2) esphome::delay(20);
  }
  if (best_mv < BatteryModel::kVbatMinValidMv || best_mv > BatteryModel::kBootCutoffMv) return;
  ESP_LOGW("PROF", "[PROF:POWER] trig=CRITICAL_BATT_BOOT vbat=%dmV <= %dmV on battery -> power off", best_mv,
           BatteryModel::kBootCutoffMv);
  BatteryModel &batt = battery_model();
  batt.vraw_mv = best_mv;
  batt.soc_display = 0.0f;
  batt.cutoff_tripped = true;
  batt.hib_reason = HibernateReason::CRITICAL;
  enter_axp2101_hibernate_or_alarm_sleep(bus, -1);
#else
  (void)bus;
#endif
}

void hal_init_axp2101(esphome::i2c::I2CBus *bus) {
#ifdef USE_ESP32
  if (bus == nullptr) return;
  auto &drv = axp2101_pmu_driver();
  drv.set_bus(bus);
  latter_hal::hal_registry().pmu = &drv;
  latter_hal::hal_registry().gauge = &drv;

  for (uint8_t reg = 0x40; reg <= 0x42; reg++) axp_wr(bus, reg, 0x00);
  for (uint8_t reg = 0x48; reg <= 0x4A; reg++) axp_wr(bus, reg, 0xFF);

  uint8_t v = 0;
  // Enable VBAT (bit 0) + internal die temperature ADC (bit 2) for 65 C charge foldback.
  if (axp_rd(bus, 0x30, &v)) {
    uint8_t adc_en = static_cast<uint8_t>(v | 0x05);
#if LATTER_PMU_TS_DISABLE
    adc_en = static_cast<uint8_t>(adc_en & ~0x02);
#endif
    axp_wr(bus, 0x30, adc_en);
  }
#if LATTER_PMU_TS_DISABLE
  if (axp_rd(bus, 0x50, &v)) axp_wr(bus, 0x50, static_cast<uint8_t>((v & 0xF0) | 0x10));
#endif

#if defined(LATTER_PMU_DCDC_KEEP_MASK)
  static_assert((LATTER_PMU_DCDC_KEEP_MASK & 0x01) != 0, "LATTER_PMU_DCDC_KEEP_MASK must keep DCDC1 (SoC 3V3)");
  if (axp_rd(bus, 0x80, &v)) axp_wr(bus, 0x80, static_cast<uint8_t>(v & (0xE0 | (LATTER_PMU_DCDC_KEEP_MASK & 0x1F))));
#endif
#if defined(LATTER_PMU_LDO_KEEP_MASK)
  if (axp_rd(bus, 0x90, &v)) axp_wr(bus, 0x90, static_cast<uint8_t>(v & (LATTER_PMU_LDO_KEEP_MASK & 0xFF)));
  if (axp_rd(bus, 0x91, &v)) axp_wr(bus, 0x91, static_cast<uint8_t>(v & (0xFE | ((LATTER_PMU_LDO_KEEP_MASK >> 8) & 0x01))));
#endif

#if LATTER_HAS_BATTERY
  static_assert(kBattChargeMa >= 100 && kBattChargeMa <= 1000, "charge current outside the AXP2101 range");
  if (axp_rd(bus, 0x62, &v)) axp_wr(bus, 0x62, static_cast<uint8_t>((v & 0xE0) | axp2101_icc_code(kBattChargeMa)));
  if (axp_rd(bus, 0x61, &v)) axp_wr(bus, 0x61, static_cast<uint8_t>((v & 0xF0) | (kBattPrechargeMa / 25)));
  if (axp_rd(bus, 0x63, &v)) axp_wr(bus, 0x63, static_cast<uint8_t>((v & 0xE0) | 0x10 | (kBattTermMa / 25)));
  if (axp_rd(bus, 0x64, &v)) axp_wr(bus, 0x64, static_cast<uint8_t>((v & 0xF8) | kBattCvCode));
#endif

  {
    uint8_t rec[4] = {0, 0, 0, 0};
    bool rec_ok = true;
    for (uint8_t i = 0; i < 4; i++) rec_ok = axp_rd(bus, static_cast<uint8_t>(0x04 + i), &rec[i]) && rec_ok;
    HibernateRecord &prev = battery_model().prev_hib;
    if (rec_ok && (rec[0] & 0xF0) == kHibRecordMagic) {
      prev.valid = true;
      prev.reason = rec[0] & 0x0F;
      prev.mv = static_cast<uint16_t>((rec[1] << 8) | rec[2]);
      prev.soc = rec[3];
    }
    for (uint8_t i = 0; i < 4; i++) axp_wr(bus, static_cast<uint8_t>(0x04 + i), 0x00);
  }

  uint8_t r61 = 0, r62 = 0, r63 = 0, r64 = 0, r80 = 0, r90 = 0, r91 = 0, r30 = 0, r50 = 0, r27 = 0, r16 = 0, r18 = 0,
          r24 = 0;
  axp_rd(bus, 0x61, &r61);
  axp_rd(bus, 0x62, &r62);
  axp_rd(bus, 0x63, &r63);
  axp_rd(bus, 0x64, &r64);
  axp_rd(bus, 0x16, &r16);
  axp_rd(bus, 0x18, &r18);
  axp_rd(bus, 0x24, &r24);
  axp_rd(bus, 0x80, &r80);
  axp_rd(bus, 0x90, &r90);
  axp_rd(bus, 0x91, &r91);
  axp_rd(bus, 0x30, &r30);
  axp_rd(bus, 0x50, &r50);
  axp_rd(bus, 0x27, &r27);
  const uint8_t icc = r62 & 0x1F;
  const int icc_ma = (icc <= 8) ? (icc * 25) : (200 + (icc - 8) * 100);
  auto &tel = pmu_telemetry();
  axp_rd(bus, 0x20, &tel.on_src);
  axp_rd(bus, 0x21, &tel.off_src);
  tel.cv_reg = r64;
  tel.icc_ma = icc_ma;
  tel.boot_valid = true;
  static const int kCvMv[8] = {0, 4000, 4100, 4200, 4350, 4400, 0, 0};
  ESP_LOGI("pmu",
           "AXP2101: ICC=%dmA (0x62=0x%02X) IPRE=%dmA ITERM=%dmA%s CV=%dmV (0x%02X) cell=%dmAh "
           "DCDC=0x%02X LDO0=0x%02X LDO1=0x%02X ADC=0x%02X TS=0x%02X PWRON=0x%02X ILIM=0x%02X CHG=0x%02X VOFF=0x%02X",
           icc_ma, r62, (r61 & 0x0F) * 25, (r63 & 0x0F) * 25, (r63 & 0x10) ? "" : "(off)", kCvMv[r64 & 0x07], r64,
           kBattCapacityMah, r80, r90, r91, r30, r50, r27, r16, r18, r24);
#else
  (void)bus;
#endif
}
#endif  // USE_I2C

}  // namespace latter_ui
