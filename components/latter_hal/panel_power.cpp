#include "hal_panel_power.h"

#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4) && defined(LATTER_DSI_IRAM_SAFE)
#include <esp_attr.h>
#include <esp_lcd_mipi_dsi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ESPHome 2026.10+'s mipi_dsi component registers a non-IRAM callback (notify_refresh_ready),
// which causes esp_lcd_dpi_panel_register_event_callbacks() to fail with ESP_ERR_INVALID_ARG
// when CONFIG_LCD_DSI_ISR_CACHE_SAFE=y is enabled. Wrapping registration to substitute an
// IRAM_ATTR semaphore-give trampoline keeps DW-GDMA DPI scanout running cleanly across OTA erases.
extern "C" {
static DRAM_ATTR SemaphoreHandle_t s_latter_dpi_io_lock = nullptr;

static bool IRAM_ATTR __attribute__((noinline, used))
latter_iram_dpi_color_trans_done_cb(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *user_ctx) {
  (void) panel;
  (void) edata;
  (void) user_ctx;
  SemaphoreHandle_t sem = s_latter_dpi_io_lock;
  BaseType_t need_yield = pdFALSE;
  if (sem != nullptr) {
    xSemaphoreGiveFromISR(sem, &need_yield);
  }
  return need_yield == pdTRUE;
}

esp_err_t __real_esp_lcd_dpi_panel_register_event_callbacks(
    esp_lcd_panel_handle_t dpi_panel,
    const esp_lcd_dpi_panel_event_callbacks_t *cbs,
    void *user_ctx);

esp_err_t __wrap_esp_lcd_dpi_panel_register_event_callbacks(
    esp_lcd_panel_handle_t dpi_panel,
    const esp_lcd_dpi_panel_event_callbacks_t *cbs,
    void *user_ctx) {
  if (cbs == nullptr) {
    return __real_esp_lcd_dpi_panel_register_event_callbacks(dpi_panel, nullptr, user_ctx);
  }
  s_latter_dpi_io_lock = static_cast<SemaphoreHandle_t>(user_ctx);
  esp_lcd_dpi_panel_event_callbacks_t iram_cbs = *cbs;
  if (iram_cbs.on_color_trans_done != nullptr) {
    iram_cbs.on_color_trans_done = &latter_iram_dpi_color_trans_done_cb;
  }
  return __real_esp_lcd_dpi_panel_register_event_callbacks(dpi_panel, &iram_cbs, nullptr);
}
}  // extern "C"
#endif

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
