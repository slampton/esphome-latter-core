#pragma once

// ==============================================================================
// Latter Core UI Layout & Hardware Portability Toolkit (ui_layout.h)
// Provides resolution- & shape-adaptive layout metrics and hit-testing across
// circular (466x466), rectangular watch (410x502), and square (480x480 / 720x720)
// ESP32-S3 and ESP32-P4 displays.
// ==============================================================================

#include "ui_core.h"
#ifdef USE_ESP32
#include <driver/gpio.h>
#include <driver/ledc.h>
#endif

namespace latter_ui {

struct HardwareCapabilities {
  bool is_round = (LATTER_ROUND_DISPLAY != 0);
  bool has_imu = (LATTER_HAS_IMU != 0);
  bool has_battery = (LATTER_HAS_BATTERY != 0);
  bool is_stationary = (LATTER_IS_STATIONARY != 0);
  bool is_wearable = (LATTER_IS_WEARABLE != 0);
};

inline constexpr HardwareCapabilities caps() {
  return HardwareCapabilities{};
}

struct UiLayout {
  int w = SCREEN_W;
  int h = SCREEN_H;
  int cx = SCREEN_CX;
  int cy = SCREEN_CY;
  bool is_round = (LATTER_ROUND_DISPLAY != 0);

  // Header & Footer Y coordinates (Adaptive for Round vs Wearable Watch vs Square/Rect)
  int hdr_y = LATTER_ROUND_DISPLAY ? 48 : (LATTER_IS_WEARABLE ? 36 : 28);
  int sub_hdr_y = LATTER_ROUND_DISPLAY ? 76 : (LATTER_IS_WEARABLE ? 64 : 56);
  int back_y = LATTER_ROUND_DISPLAY ? 426 : (SCREEN_H - 28);
  int home_bar_y = LATTER_ROUND_DISPLAY ? 434 : (SCREEN_H - 12);
  int dock_y = LATTER_ROUND_DISPLAY ? 359 : (SCREEN_H - 58);
  int app_dock_y = LATTER_ROUND_DISPLAY ? 372 : (SCREEN_H - 58);
  int dock_pips_y = LATTER_ROUND_DISPLAY ? 405 : (SCREEN_H - 25);
  int app_dock_pips_y = LATTER_ROUND_DISPLAY ? 410 : (SCREEN_H - 25);
  int dock_r = (SCREEN_W < 360) ? 24 : 27;

  // Consistent Upper-Left Celestial Sun/Moon Coordinates across all 5 displays
  int celestial_x = LATTER_ROUND_DISPLAY ? 106 : ((SCREEN_W < 380) ? 44 : (LATTER_IS_WEARABLE ? 68 : 78));
  int celestial_y = LATTER_ROUND_DISPLAY ? 86  : ((SCREEN_W < 380) ? 72 : 78);

  // Corner-Safe Battery Indicator Coordinates for Non-Round Displays
  int battery_cx = LATTER_ROUND_DISPLAY ? SCREEN_CX : (LATTER_IS_WEARABLE ? (SCREEN_W - 86) : ((SCREEN_W < 380) ? (SCREEN_W - 36) : (SCREEN_W - 58)));
  int battery_y  = LATTER_ROUND_DISPLAY ? 435       : (LATTER_IS_WEARABLE ? 38 : ((SCREEN_W < 380) ? 24 : 28));

  // Standard Menu Card Width & Left Text Margin (232px on 320x480 leaves 26px gap before scroll arrows)
  int list_card_w = (SCREEN_W < 380) ? (SCREEN_W - 88) : 276;
  int list_card_left = SCREEN_CX - (list_card_w / 2);

  // 4-Column Stepper X centers (-5m, -1m, +1m, +5m / -1h, +1h, -5m, +5m)
  constexpr int stepper_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 74 : 90;
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  // 4-Column Calculator Keypad X centers
  constexpr int calc_col_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 72 : 82;
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
}

inline constexpr bool is_wearable_form_factor() {
  return (LATTER_IS_WEARABLE != 0);
}

inline constexpr bool is_wrist_watch_board() {
  return is_wearable_form_factor();
}

// Hit-test the 4-column stepper row (returns 0..3 or -1)
inline int hit_4col_stepper(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 74 : 90;
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

// Hit-test the 4-column calculator grid (returns 0..3 or -1)
inline int hit_4col_calc(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 72 : 82;
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

// Chip-Portable RTC Slow Clock Calibration & Wi-Fi Power-Save Helpers
// Safely guarded for ESP32-S3 and future ESP32-P4 (ESP-Hosted) targets
inline uint32_t hal_calibrate_rtc() {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  extern uint32_t rtc_clk_cal(int cal_clk, uint32_t slow_clk_cycles) __asm__("rtc_clk_cal");
  return rtc_clk_cal(0, 1024);
#else
  return 0;
#endif
}

inline void hal_set_wifi_power_save(int mode) {
#if defined(USE_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32P4)
  extern int esp_wifi_set_ps(int type) __asm__("esp_wifi_set_ps");
  esp_wifi_set_ps(mode);
#else
  (void)mode;
#endif
}

template <typename DispT>
inline void hal_set_display_brightness(DispT *disp, uint8_t bri_byte) {
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_EN_PIN)
  gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), (bri_byte > 0) ? 1 : 0);
#endif
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_PIN)
  static bool ledc_inited = false;
  if (!ledc_inited) {
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_TIMER_8_BIT;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&ledc_timer) == ESP_OK) {
      ledc_channel_config_t ledc_channel = {};
      ledc_channel.gpio_num = LATTER_BACKLIGHT_PIN;
      ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
      ledc_channel.channel = LEDC_CHANNEL_0;
      ledc_channel.intr_type = LEDC_INTR_DISABLE;
      ledc_channel.timer_sel = LEDC_TIMER_0;
      ledc_channel.duty = bri_byte;
      ledc_channel.hpoint = 0;
      if (ledc_channel_config(&ledc_channel) == ESP_OK) {
        ledc_inited = true;
      }
    }
  }
  if (ledc_inited) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, bri_byte);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  } else {
    gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), (bri_byte > 0) ? 1 : 0);
  }
#endif
  if constexpr (requires(DispT *d, uint8_t b) { d->set_brightness(b); }) {
    if (disp != nullptr) disp->set_brightness(bri_byte);
  } else {
    (void)disp;
    (void)bri_byte;
  }
}

}  // namespace latter_ui

namespace pocket_ui = latter_ui;
