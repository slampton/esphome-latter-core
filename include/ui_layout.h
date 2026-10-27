#pragma once

// ==============================================================================
// Latter Core UI Layout & Hardware Portability Toolkit (ui_layout.h)
// Provides resolution- & shape-adaptive layout metrics and hit-testing across
// circular (466x466), rectangular watch (410x502), and square (480x480 / 720x720)
// ESP32-S3 and ESP32-P4 displays.
// ==============================================================================

#include "ui_core.h"

namespace pocket_ui {

struct UiLayout {
  int w = SCREEN_W;
  int h = SCREEN_H;
  int cx = SCREEN_CX;
  int cy = SCREEN_CY;
  bool is_round = (POCKET_UI_ROUND_DISPLAY != 0);

  // Header & Footer Y coordinates
  int hdr_y = 48;
  int sub_hdr_y = 76;
  int back_y = 426;
  int home_bar_y = 434;
  int dock_y = 359;
  int dock_r = 27;

  // 4-Column Stepper X centers (-5m, -1m, +1m, +5m / -1h, +1h, -5m, +5m)
  // 90px stride (w=68) -> spans [cx - 169 .. cx + 169] (36..374 on 410px, 64..402 on 466px)
  constexpr int stepper_x(int col_0_to_3) const {
    return cx - 135 + (col_0_to_3 * 90);
  }

  // 4-Column Calculator Keypad X centers (82px stride, w=68)
  constexpr int calc_col_x(int col_0_to_3) const {
    return cx - 123 + (col_0_to_3 * 82);
  }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
}

inline constexpr bool is_wrist_watch_board() {
#if defined(POCKET_UI_BOARD_206) && POCKET_UI_BOARD_206
  return true;
#else
  return false;
#endif
}

// Hit-test the 4-column stepper row (returns 0..3 or -1)
inline int hit_4col_stepper(int tx, int cx = SCREEN_CX) {
  if (tx < cx - 180 || tx > cx + 180) return -1;
  if (tx < cx - 90) return 0;
  if (tx < cx)      return 1;
  if (tx < cx + 90) return 2;
  return 3;
}

// Hit-test the 4-column calculator grid (returns 0..3 or -1)
inline int hit_4col_calc(int tx, int cx = SCREEN_CX) {
  if (tx < cx - 165 || tx > cx + 165) return -1;
  if (tx < cx - 82) return 0;
  if (tx < cx)      return 1;
  if (tx < cx + 82) return 2;
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
  if constexpr (requires(DispT *d, uint8_t b) { d->set_brightness(b); }) {
    if (disp != nullptr) disp->set_brightness(bri_byte);
  } else {
    (void)disp;
    (void)bri_byte;
  }
}

}  // namespace pocket_ui
