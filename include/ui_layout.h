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
  bool has_touch = (LATTER_HAS_TOUCH != 0);
  bool is_ips_panel = (LATTER_PANEL_IPS != 0);
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

  // Corner-Safe Battery Indicator Coordinates (Aligned with Date at y=52 on Wrist Assist & Mobile Assistant; Pocket & Remote unchanged)
  int battery_cx = LATTER_ROUND_DISPLAY ? SCREEN_CX : (LATTER_IS_WEARABLE ? (SCREEN_W - 52) : ((SCREEN_W < 380) ? (SCREEN_W - 36) : (SCREEN_W - 54)));
  int battery_y  = LATTER_ROUND_DISPLAY ? 435       : ((SCREEN_W < 380) ? 24 : 52);

  // Standard Menu Card Width & Left Text Margin (232px on 320x480; scales up on >520px HD panels)
  int list_card_w = (SCREEN_W < 380) ? (SCREEN_W - 88) : ((SCREEN_W > 520) ? ((SCREEN_W * 276) / 466) : 276);
  int list_card_left = SCREEN_CX - (list_card_w / 2);

  // 1. Adaptive 5-Slot Vertical List Geometry (System, Music, Area Picker, Calendar, Intercom, Macros)
  int slot5_stride = (SCREEN_H > 520) ? ((SCREEN_H * 63) / 466) : 63;
  int slot5_h      = (SCREEN_H > 520) ? ((SCREEN_H * 44) / 466) : 44;
  int slot5_cy     = (SCREEN_H > 520) ? SCREEN_CY : 236;
  constexpr int slot5_y(int idx_0_to_4) const {
    return slot5_cy + (idx_0_to_4 - 2) * slot5_stride;
  }

  // 2. Adaptive 3-Card Area Control & Whole-Home Geometry + Unified Hit-Testers
  int area_card3_stride = (SCREEN_H > 520) ? ((SCREEN_H * 76) / 466) : 76;
  int area_card3_h      = (SCREEN_H > 520) ? ((SCREEN_H * 64) / 466) : 64;
  int area_card3_w      = (SCREEN_W < 380) ? (SCREEN_W - 68) : ((SCREEN_W > 520) ? ((SCREEN_W * 316) / 466) : 316);
  int area_card3_cy     = (SCREEN_H > 520) ? ((SCREEN_H * 208) / 466) : 208;
  constexpr int area_card3_y(int idx_0_to_2) const {
    return area_card3_cy + (idx_0_to_2 - 1) * area_card3_stride;
  }
  constexpr int area_card_left() const { return cx - (area_card3_w / 2); }
  constexpr int area_card_right() const { return cx + (area_card3_w / 2); }
  constexpr int area_card_div_x() const { return area_card_left() + 64; }
  constexpr int area_card_orb_x() const { return area_card_right() - 28; }
  constexpr int hit_area_card3(int tx, int ty) const {
    int hw = (area_card3_w / 2) + 10;
    if (tx < cx - hw || tx > cx + hw) return -1;
    int hs = area_card3_stride / 2;
    for (int i = 0; i < 3; i++) {
      int ym = area_card3_y(i);
      if (ty >= ym - hs && ty <= ym + hs) return i;
    }
    return -1;
  }
  constexpr bool is_area_light_orb(int tx) const {
    int ox = area_card_orb_x();
    return (tx >= ox - 28 && tx <= area_card_right() + 8);
  }
  constexpr int area_slider_pct(int tx) const {
    int x0 = area_card_div_x() + 4;
    int x1 = area_card_right() - 8;
    if (x1 <= x0) return 100;
    int raw = ((tx - x0) * 100) / (x1 - x0);
    int stepped = ((raw + 2) / 5) * 5;
    return (stepped < 5) ? 5 : ((stepped > 100) ? 100 : stepped);
  }

  // 3. Adaptive 3-Card Timers & Alarms Landing Geometry + Unified Hit-Tester
  int tool_card3_stride = (SCREEN_H > 520) ? ((SCREEN_H * 100) / 466) : 100;
  int tool_card3_h      = (SCREEN_H > 520) ? ((SCREEN_H * 74) / 466) : 74;
  int tool_card3_w      = (SCREEN_W < 380) ? (SCREEN_W - 36) : ((SCREEN_W > 520) ? ((SCREEN_W * 316) / 466) : 316);
  int tool_card3_cy     = (SCREEN_H > 520) ? ((SCREEN_H * 222) / 466) : 222;
  constexpr int tool_card3_y(int idx_0_to_2) const {
    return tool_card3_cy + (idx_0_to_2 - 1) * tool_card3_stride;
  }
  constexpr int tool_card3_div_x() const {
    return cx + (tool_card3_w / 2) - 93;
  }
  constexpr int hit_tool_card3(int tx, int ty) const {
    int hw = (tool_card3_w / 2) + 12;
    if (tx < cx - hw || tx > cx + hw) return -1;
    int hs = tool_card3_stride / 2;
    for (int i = 0; i < 3; i++) {
      int ym = tool_card3_y(i);
      if (ty >= ym - hs && ty <= ym + hs) return i;
    }
    return -1;
  }

  // 4. Adaptive 2x3 Tools Launcher Grid Geometry + Unified Hit-Tester
  int tools_col_off    = (SCREEN_W < 380) ? 61 : ((SCREEN_W > 520) ? ((SCREEN_W * 68) / 466) : 68);
  int tools_row_stride = (SCREEN_H > 520) ? ((SCREEN_H * 105) / 466) : 105;
  int tools_grid_cy    = (SCREEN_H > 520) ? SCREEN_CY : 236;
  constexpr int tools_row_y(int row_0_to_2) const {
    return tools_grid_cy + (row_0_to_2 - 1) * tools_row_stride;
  }
  constexpr int hit_tools_row(int ty) const {
    int hs = tools_row_stride / 2;
    for (int r = 0; r < 3; r++) {
      int ym = tools_row_y(r);
      if (ty >= ym - hs && ty <= ym + hs) return r;
    }
    return -1;
  }
  constexpr int hit_tools_col(int tx) const {
    int max_dx = tools_col_off * 2 + 12;
    if (tx >= cx - max_dx && tx <= cx) return 0;
    if (tx > cx && tx <= cx + max_dx) return 1;
    return -1;
  }

  // 5. Adaptive Home Screen Vertical Zones + Unified Hit-Testers
  int home_date_y     = (SCREEN_H > 520) ? ((SCREEN_H * 52) / 466)  : 52;
  int home_clock_y    = (SCREEN_H > 520) ? ((SCREEN_H * 128) / 466) : 128;
  int home_weather_y  = (SCREEN_H > 520) ? ((SCREEN_H * 200) / 466) : 200;
  int home_activity_y = (SCREEN_H > 520) ? ((SCREEN_H * 266) / 466) : 266;
  constexpr bool hit_home_clock(int tx, int ty) const {
    int hw = (w > 520) ? 180 : 135;
    int hh = (h > 520) ? 64  : 48;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_clock_y - hh && ty <= home_clock_y + hh);
  }
  constexpr bool hit_home_weather(int tx, int ty) const {
    int hw = (w > 520) ? 210 : 165;
    int hh = (h > 520) ? 36  : 28;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_weather_y - hh && ty <= home_weather_y + hh);
  }
  constexpr bool hit_home_activity(int tx, int ty) const {
    int act_w = (w - 24 < 380) ? (w - 24) : ((w > 520) ? ((w * 380) / 466) : 380);
    int hw = act_w / 2;
    int hh = (h > 520) ? 54 : 44;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_activity_y - hh && ty <= home_activity_y + hh);
  }

  // 4-Column Stepper X centers (-5m, -1m, +1m, +5m / -1h, +1h, -5m, +5m)
  constexpr int stepper_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 74 : ((w > 520) ? ((w * 90) / 466) : 90);
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  // 4-Column Calculator Keypad X centers
  constexpr int calc_col_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 72 : ((w > 520) ? ((w * 82) / 466) : 82);
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  // 6. Single-Source Music Now Playing, Drilldown & Track Inspector Scrubber Geometry + Hit-Testers
  int music_art_cy = (SCREEN_H > 520) ? SCREEN_CY : 233;
  constexpr int music_left_flank_x() const { return is_round ? 71 : ((w < 380) ? 42 : 48); }
  constexpr int music_right_flank_x() const { return w - music_left_flank_x(); }
  constexpr int music_prev_x() const { return cx - (is_round ? 109 : ((w < 380) ? 96 : 104)); }
  constexpr int music_next_x() const { return cx + (is_round ? 109 : ((w < 380) ? 96 : 104)); }
  constexpr bool hit_music_header(int tx, int ty) const {
    return (ty <= 85 && (tx >= cx - 130 && tx <= cx + 130));
  }
  constexpr bool hit_music_play_pause(int tx, int ty) const {
    return (tx >= cx - 48 && tx <= cx + 48 && ty >= music_art_cy - 48 && ty <= music_art_cy + 48);
  }
  constexpr bool hit_music_prev(int tx, int ty) const {
    return (tx < cx - 48 && ty >= music_art_cy - 46 && ty < music_art_cy + 48);
  }
  constexpr bool hit_music_next(int tx, int ty) const {
    return (tx > cx + 48 && ty >= music_art_cy - 46 && ty < music_art_cy + 48);
  }
  constexpr bool hit_music_spk_shortcut(int tx, int ty) const {
    return (tx <= cx - 48 && ty >= 70 && ty < music_art_cy - 46);
  }
  constexpr bool hit_music_lib_shortcut(int tx, int ty) const {
    return (tx <= cx - 48 && ty >= music_art_cy + 48 && ty < 405);
  }
  constexpr bool hit_music_vol_up(int tx, int ty) const {
    return (tx >= cx + 48 && ty >= 70 && ty < music_art_cy - 46);
  }
  constexpr bool hit_music_vol_down(int tx, int ty) const {
    return (tx >= cx + 48 && ty >= music_art_cy + 48 && ty < 405);
  }
  constexpr bool hit_music_metadata(int tx, int ty) const {
    return (tx >= cx - 110 && tx <= cx + 110 && ty >= 338 && ty < 405);
  }
  constexpr int music_drill_play_cx() const { return (w < 380) ? 28 : 82; }
  constexpr int music_drill_split_x() const { return music_drill_play_cx() + 26; }
  int music_scrub_y = 242;
  constexpr int music_seek_left_x() const { return is_round ? 50 : 34; }
  constexpr int music_seek_right_x() const { return w - music_seek_left_x(); }
  constexpr int music_scrub_x0() const { return is_round ? 95 : 72; }
  constexpr int music_scrub_x1() const { return w - music_scrub_x0(); }
  constexpr int music_scrub_w() const { return music_scrub_x1() - music_scrub_x0(); }
  constexpr float music_scrub_frac(int tx) const {
    int bw = music_scrub_w();
    if (bw <= 10) return 0.0f;
    float f = static_cast<float>(tx - music_scrub_x0()) / static_cast<float>(bw);
    return (f < 0.0f) ? 0.0f : ((f > 1.0f) ? 1.0f : f);
  }

  // 7. Single-Source Light Kelvin / Brightness Inspector Slider Geometry
  constexpr int light_slider_card_w() const { return (w - 24 < 316) ? (w - 24) : 316; }
  constexpr int light_kelvin_bar_w() const { return light_slider_card_w() - 32; }
  constexpr int light_kelvin_bar_x0() const { return cx - (light_kelvin_bar_w() / 2); }
  constexpr int light_bri_bar_w() const { return light_slider_card_w() - 16; }
  constexpr int light_bri_bar_x0() const { return cx - (light_bri_bar_w() / 2); }

  // 8. Single-Source System Color Swatch & Info Margin Geometry
  constexpr int sys_color_swatch_x() const { return cx + (list_card_w / 2) - 34; }
  constexpr bool hit_sys_color_swatch(int tx) const { return tx >= (sys_color_swatch_x() - 18); }
  constexpr int sys_info_x() const { return is_round ? 88 : ((w < 380) ? 18 : 42); }

  // 9. Single-Source Calendar Map Floating Zoom Button Geometry + Hit-Testers
  int map_zoom_y = 233;
  constexpr int map_zoom_left_x() const { return is_round ? 44 : 34; }
  constexpr int map_zoom_right_x() const { return w - map_zoom_left_x(); }
  constexpr bool hit_map_zoom_out(int tx, int ty) const {
    return (ty >= map_zoom_y - 45 && ty <= map_zoom_y + 45 && tx <= map_zoom_left_x() + 42);
  }
  constexpr bool hit_map_zoom_in(int tx, int ty) const {
    return (ty >= map_zoom_y - 45 && ty <= map_zoom_y + 45 && tx >= map_zoom_right_x() - 42);
  }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
}

inline constexpr bool is_wearable_form_factor() {
  return (LATTER_IS_WEARABLE != 0);
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


