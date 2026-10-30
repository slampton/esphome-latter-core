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

  // Header & Footer Y coordinates (Adaptive for Round vs Wearable Watch vs Square/Rect vs 720p HD)
  int hdr_y = LATTER_ROUND_DISPLAY ? 48 : (LATTER_IS_WEARABLE ? 36 : ((SCREEN_H > 520) ? 48 : 28));
  int sub_hdr_y = LATTER_ROUND_DISPLAY ? 76 : (LATTER_IS_WEARABLE ? 64 : ((SCREEN_H > 520) ? 88 : 56));
  int back_y = LATTER_ROUND_DISPLAY ? 426 : (SCREEN_H - scale_y(28));
  int home_bar_y = LATTER_ROUND_DISPLAY ? 434 : (SCREEN_H - scale_y(11));
  int dock_y = LATTER_ROUND_DISPLAY ? 359 : (SCREEN_H - scale_y(66));
  int app_dock_y = LATTER_ROUND_DISPLAY ? 369 : (SCREEN_H - scale_y(66));
  int dock_pips_y = LATTER_ROUND_DISPLAY ? 404 : (SCREEN_H - scale_y(27));
  int app_dock_pips_y = LATTER_ROUND_DISPLAY ? 408 : (SCREEN_H - scale_y(27));
  int dock_r = (SCREEN_W < 360) ? 24 : ((SCREEN_W > 520) ? 40 : 27);

  // Consistent Upper-Left Celestial Sun/Moon Coordinates across all 5 displays
  int celestial_x = LATTER_ROUND_DISPLAY ? 106 : ((SCREEN_W < 380) ? 44 : (LATTER_IS_WEARABLE ? 68 : ((SCREEN_W > 520) ? 118 : 78)));
  int celestial_y = LATTER_ROUND_DISPLAY ? 86  : ((SCREEN_W < 380) ? 72 : ((SCREEN_H > 520) ? 118 : 78));

  // Corner-Safe Battery Indicator Coordinates for Non-Round Displays
  int battery_cx = LATTER_ROUND_DISPLAY ? SCREEN_CX : (LATTER_IS_WEARABLE ? (SCREEN_W - 68) : ((SCREEN_W < 380) ? (SCREEN_W - 36) : ((SCREEN_W > 520) ? (SCREEN_W - 86) : (SCREEN_W - 58))));
  int battery_y  = LATTER_ROUND_DISPLAY ? 435       : (LATTER_IS_WEARABLE ? 43 : ((SCREEN_W < 380) ? 24 : ((SCREEN_H > 520) ? 42 : 28)));

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
  constexpr int area_card_div_x() const { return area_card_left() + scale_x(64); }
  constexpr int area_card_orb_x() const { return area_card_right() - scale_x(28); }
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
    return (tx >= ox - scale_x(28) && tx <= area_card_right() + 8);
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
    return cx + (tool_card3_w / 2) - scale_x(93);
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
  int tools_col_off    = (SCREEN_W < 380) ? 61 : ((SCREEN_W > 520) ? ((SCREEN_W * 74) / 466) : 68);
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
  int music_art_cy = scale_y(233);
  constexpr int music_left_flank_x() const { return is_round ? 71 : ((w < 380) ? 42 : scale_x(48)); }
  constexpr int music_right_flank_x() const { return w - music_left_flank_x(); }
  constexpr int music_prev_x() const { return cx - (is_round ? 109 : ((w < 380) ? 96 : scale_x(104))); }
  constexpr int music_next_x() const { return cx + (is_round ? 109 : ((w < 380) ? 96 : scale_x(104))); }
  constexpr bool hit_music_header(int tx, int ty) const {
    return (ty <= scale_y(85) && (tx >= cx - scale_x(130) && tx <= cx + scale_x(130)));
  }
  constexpr bool hit_music_play_pause(int tx, int ty) const {
    int r = scale_s(48);
    return (tx >= cx - r && tx <= cx + r && ty >= music_art_cy - r && ty <= music_art_cy + r);
  }
  constexpr bool hit_music_prev(int tx, int ty) const {
    int r = scale_s(48);
    return (tx < cx - r && ty >= music_art_cy - r && ty < music_art_cy + r);
  }
  constexpr bool hit_music_next(int tx, int ty) const {
    int r = scale_s(48);
    return (tx > cx + r && ty >= music_art_cy - r && ty < music_art_cy + r);
  }
  constexpr bool hit_music_spk_shortcut(int tx, int ty) const {
    int r = scale_s(48);
    return (tx <= cx - r && ty >= scale_y(70) && ty < music_art_cy - r);
  }
  constexpr bool hit_music_lib_shortcut(int tx, int ty) const {
    int r = scale_s(48);
    return (tx <= cx - r && ty >= music_art_cy + r && ty < scale_y(405));
  }
  constexpr bool hit_music_vol_up(int tx, int ty) const {
    int r = scale_s(48);
    return (tx >= cx + r && ty >= scale_y(70) && ty < music_art_cy - r);
  }
  constexpr bool hit_music_vol_down(int tx, int ty) const {
    int r = scale_s(48);
    return (tx >= cx + r && ty >= music_art_cy + r && ty < scale_y(405));
  }
  constexpr bool hit_music_metadata(int tx, int ty) const {
    return (tx >= cx - scale_x(110) && tx <= cx + scale_x(110) && ty >= scale_y(338) && ty < scale_y(408));
  }
  constexpr int music_drill_play_cx() const { return (w < 380) ? 28 : scale_x(82); }
  constexpr int music_drill_split_x() const { return music_drill_play_cx() + scale_x(26); }
  constexpr bool hit_music_drill_play(int tx) const { return tx <= (music_drill_split_x() + scale_x(8)); }
  int music_scrub_y = scale_y(242);
  constexpr int music_seek_left_x() const { return is_round ? 50 : scale_x(34); }
  constexpr int music_seek_right_x() const { return w - music_seek_left_x(); }
  constexpr int music_scrub_x0() const { return is_round ? 95 : scale_x(72); }
  constexpr int music_scrub_x1() const { return w - music_scrub_x0(); }
  constexpr int music_scrub_w() const { return music_scrub_x1() - music_scrub_x0(); }
  constexpr float music_scrub_frac(int tx) const {
    int bw = music_scrub_w();
    if (bw <= 10) return 0.0f;
    float f = static_cast<float>(tx - music_scrub_x0()) / static_cast<float>(bw);
    return (f < 0.0f) ? 0.0f : ((f > 1.0f) ? 1.0f : f);
  }

  // 7. Single-Source Light Kelvin / Brightness Inspector Slider Geometry
  constexpr int light_slider_card_w() const { return (w < 380) ? (w - 24) : scale_x(316); }
  constexpr int light_kelvin_bar_w() const { return light_slider_card_w() - scale_x(32); }
  constexpr int light_kelvin_bar_x0() const { return cx - (light_kelvin_bar_w() / 2); }
  constexpr int light_bri_bar_w() const { return light_slider_card_w() - scale_x(16); }
  constexpr int light_bri_bar_x0() const { return cx - (light_bri_bar_w() / 2); }

  // 8. Single-Source System Color Swatch & Info Margin Geometry
  constexpr int sys_color_swatch_x() const { return cx + (list_card_w / 2) - scale_x(34); }
  constexpr bool hit_sys_color_swatch(int tx) const { return tx >= (sys_color_swatch_x() - scale_x(22)); }
  constexpr int sys_info_x() const { return is_round ? 88 : ((w < 380) ? 18 : scale_x(42)); }

  // 9. Single-Source Calendar Map Floating Zoom Button Geometry + Hit-Testers
  int map_zoom_y = scale_y(233);
  constexpr int map_zoom_left_x() const { return is_round ? 44 : scale_x(34); }
  constexpr int map_zoom_right_x() const { return w - map_zoom_left_x(); }
  constexpr bool hit_map_zoom_out(int tx, int ty) const {
    int r = scale_s(45);
    return (ty >= map_zoom_y - r && ty <= map_zoom_y + r && tx <= map_zoom_left_x() + r);
  }
  constexpr bool hit_map_zoom_in(int tx, int ty) const {
    int r = scale_s(45);
    return (ty >= map_zoom_y - r && ty <= map_zoom_y + r && tx >= map_zoom_right_x() - r);
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
  const int stride = (SCREEN_W < 380) ? 74 : scale_x(90);
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

// Hit-test the 4-column calculator grid (returns 0..3 or -1)
inline int hit_4col_calc(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 72 : scale_x(82);
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
#if defined(LATTER_BACKLIGHT_INVERTED) && LATTER_BACKLIGHT_INVERTED
  // P4-4B active-low backlight boost controller: 200Hz PWM, max_power capped at 80% (duty >= 51)
  const uint8_t scaled_bri = (bri_byte == 0) ? 0 : static_cast<uint8_t>(25 + (static_cast<uint16_t>(bri_byte) * 179U) / 255U);
  const uint8_t hw_duty = (scaled_bri == 0) ? 255 : static_cast<uint8_t>(255 - scaled_bri);
  const uint32_t pwm_freq_hz = 200;
  const int gpio_on_lvl = 0;
  const int gpio_off_lvl = 1;
#else
  const uint8_t hw_duty = bri_byte;
  const uint32_t pwm_freq_hz = 2000;
  const int gpio_on_lvl = 1;
  const int gpio_off_lvl = 0;
#endif
  static bool ledc_timer_inited = false;
  static bool ledc_channel_inited = false;
  static int last_hw_duty = -1;
  if (!ledc_timer_inited) {
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_TIMER_8_BIT;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = pwm_freq_hz;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&ledc_timer) == ESP_OK) {
      ledc_timer_inited = true;
    }
  }
  if (ledc_timer_inited) {
    if (!ledc_channel_inited) {
      ledc_channel_config_t ledc_channel = {};
      ledc_channel.gpio_num = LATTER_BACKLIGHT_PIN;
      ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
      ledc_channel.channel = LEDC_CHANNEL_0;
      ledc_channel.intr_type = LEDC_INTR_DISABLE;
      ledc_channel.timer_sel = LEDC_TIMER_0;
      ledc_channel.duty = hw_duty;
      ledc_channel.hpoint = 0;
      if (ledc_channel_config(&ledc_channel) == ESP_OK) {
        ledc_channel_inited = true;
        last_hw_duty = hw_duty;
      }
    } else if (last_hw_duty != static_cast<int>(hw_duty)) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      last_hw_duty = hw_duty;
    }
  } else {
    gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), (bri_byte > 0) ? gpio_on_lvl : gpio_off_lvl);
  }
#endif
#if !defined(LATTER_BACKLIGHT_PIN)
  if constexpr (requires(DispT *d, uint8_t b) { d->set_brightness(b); }) {
    if (disp != nullptr) disp->set_brightness(bri_byte);
  } else {
    (void)disp;
    (void)bri_byte;
  }
#else
  (void)disp;
#endif
}

// =============================================================================
// FLUID 4-ICON HOME DOCK & UNIFIED IN-APP SEGMENTED MODE BAR GEOMETRY
// =============================================================================
inline int get_home_dock_start_index(int total_count, int page) {
  if (total_count <= 4 || page <= 0) return 0;
  int total_pages = (total_count + 3) / 4;
  int eff_page = (page >= total_pages) ? (total_pages - 1) : page;
  int raw_start = eff_page * 4;
  if (raw_start + 4 > total_count) {
    raw_start = total_count - 4;
  }
  return std::max(0, raw_start);
}

inline void get_home_dock_window_xs(int total_count, int page, int cx, int out_xs[4],
                                    bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  int start_idx = get_home_dock_start_index(total_count, page);
  int vis_count = std::min(4, total_count - start_idx);
  if (vis_count == 1) {
    out_xs[0] = cx;
  } else if (vis_count == 2) {
    out_xs[0] = cx - scale_x(44);
    out_xs[1] = cx + scale_x(44);
  } else if (vis_count == 3) {
    out_xs[0] = cx - scale_x(84);
    out_xs[1] = cx;
    out_xs[2] = cx + scale_x(84);
  } else {
#if LATTER_ROUND_DISPLAY
    out_xs[0] = 112;
    out_xs[1] = 193;
    out_xs[2] = 274;
    out_xs[3] = 355;
#else
    const int stride = (SCREEN_W < 380) ? 74 : scale_x(98);
    out_xs[0] = cx - (stride * 3) / 2;
    out_xs[1] = cx - (stride / 2);
    out_xs[2] = cx + (stride / 2);
    out_xs[3] = cx + (stride * 3) / 2;
#endif
  }
}

// Unified In-App Segmented Mode Bar (Option A: Stadium Pill + 1-Step Conveyor Window)
// Floating circles on Page 1 launch apps; connected stadium pill tracks inside
// apps (Sky & Weather, Area Control, Whole-Home Hub) switch the middle view.
inline int get_mode_bar_start_index(int total_count, int win_start) {
  if (total_count <= 4 || win_start <= 0) return 0;
  return std::max(0, std::min(total_count - 4, win_start));
}

inline void sync_mode_bar_window(int &win_start, int active_idx, int total_count) {
  if (total_count <= 4) {
    win_start = 0;
    return;
  }
  win_start = std::max(0, std::min(total_count - 4, active_idx - 1));
}

inline void get_mode_bar_window_xs(int total_count, int win_start, int cx, int out_xs[4],
                                   bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  int start_idx = get_mode_bar_start_index(total_count, win_start);
  int vis_count = std::min(4, total_count - start_idx);
  bool dl = false, dr = false;
  get_home_dock_window_xs(vis_count, 0, cx, out_xs, dl, dr);
  out_has_left_arrow  = (start_idx > 0);
  out_has_right_arrow = (start_idx + 4 < total_count);
}

// =============================================================================
// SMOOTH HORIZONTAL CONVEYOR SLIDE & CAPSULE GLIDE ANIMATOR (ESP32-P4 & S3)
// =============================================================================
struct DockSlideAnimator {
  float cur_start = 0.0f;
  float from_start = 0.0f;
  int target_start = -1;
  uint32_t win_start_ms = 0;
  bool win_animating = false;

  float cur_active = 0.0f;
  float from_active = 0.0f;
  int target_active = -1;
  uint32_t act_start_ms = 0;
  bool act_animating = false;

  static constexpr uint32_t kDurationMs = 210U;

  static inline float ease_out_cubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float inv = 1.0f - t;
    return 1.0f - (inv * inv * inv);
  }

  void reset() {
    target_start = -1;
    target_active = -1;
    win_animating = false;
    act_animating = false;
  }

  float step_window(int new_start, uint32_t now_ms) {
    if (target_start < 0 || std::abs(new_start - target_start) > 4) {
      target_start = new_start;
      from_start = static_cast<float>(new_start);
      cur_start = from_start;
      win_animating = false;
      return cur_start;
    }
    if (new_start != target_start) {
      from_start = cur_start;
      target_start = new_start;
      win_start_ms = now_ms;
      win_animating = true;
    }
    if (win_animating) {
      uint32_t dt = now_ms - win_start_ms;
      if (dt >= kDurationMs) {
        cur_start = static_cast<float>(target_start);
        win_animating = false;
      } else {
        float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kDurationMs));
        cur_start = from_start + (static_cast<float>(target_start) - from_start) * e;
      }
    } else {
      cur_start = static_cast<float>(target_start);
    }
    return cur_start;
  }

  float step_active(int new_active, uint32_t now_ms) {
    if (target_active < 0 || std::abs(new_active - target_active) > 6) {
      target_active = new_active;
      from_active = static_cast<float>(new_active);
      cur_active = from_active;
      act_animating = false;
      return cur_active;
    }
    if (new_active != target_active) {
      from_active = cur_active;
      target_active = new_active;
      act_start_ms = now_ms;
      act_animating = true;
    }
    if (act_animating) {
      uint32_t dt = now_ms - act_start_ms;
      if (dt >= kDurationMs) {
        cur_active = static_cast<float>(target_active);
        act_animating = false;
      } else {
        float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kDurationMs));
        cur_active = from_active + (static_cast<float>(target_active) - from_active) * e;
      }
    } else {
      cur_active = static_cast<float>(target_active);
    }
    return cur_active;
  }

  bool is_animating() const { return win_animating || act_animating; }
};

inline DockSlideAnimator &home_dock_anim() {
  static DockSlideAnimator anim;
  return anim;
}

inline DockSlideAnimator &mode_bar_anim() {
  static DockSlideAnimator anim;
  return anim;
}

inline bool any_dock_anim_active() {
  return home_dock_anim().is_animating() || mode_bar_anim().is_animating();
}

inline int get_dock_slot_stride(const int *win_xs, int vis_count) {
  if (vis_count >= 2) return win_xs[1] - win_xs[0];
#if LATTER_ROUND_DISPLAY
  return 81;
#else
  return (SCREEN_W < 380) ? 74 : scale_x(98);
#endif
}

template <typename DisplayT>
inline void render_in_app_mode_bar_track(
    DisplayT &it, int cx, const int *win_xs, int vis_count, int active_vis_idx,
    bool has_left_arrow, bool has_right_arrow, int total_count, int active_global_idx,
    esphome::Color theme_primary, esphome::Color col_white) {
  using esphome::Color;
  if (vis_count <= 0) return;

  const uint32_t now_ms = esphome::millis();
  const int win_start_idx = (active_vis_idx >= 0) ? (active_global_idx - active_vis_idx) : 0;
  float anim_start  = mode_bar_anim().step_window(win_start_idx, now_ms);
  float anim_active = mode_bar_anim().step_active(active_global_idx, now_ms);

  const int by = layout().app_dock_y;
  const int dr = layout().dock_r;
  const int r_track = dr + scale_s(3);
  const int left_cx  = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride   = get_dock_slot_stride(win_xs, vis_count);
  const Color track_bg(16, 20, 27);
  const Color track_brd(52, 64, 82);
  const Color cap_bg(16, 20, 27);

  // 1. Continuous Scanline Stadium Pill Track connecting left_cx .. right_cx
  for (int dy = -r_track; dy <= r_track; dy++) {
    int py = by + dy;
    if (py < 0 || py >= SCREEN_H) continue;
    int dx_cap = static_cast<int>(std::sqrt(static_cast<float>(r_track * r_track - dy * dy)));
    int x0 = left_cx - dx_cap;
    int x1 = right_cx + dx_cap;
    if (x1 > x0) {
      it.horizontal_line(x0, py, x1 - x0 + 1, track_bg);
      if (std::abs(dy) == r_track) {
        it.horizontal_line(left_cx, py, right_cx - left_cx + 1, track_brd);
      } else {
        it.draw_pixel_at(x0, py, track_brd);
        it.draw_pixel_at(x1, py, track_brd);
      }
    }
  }

  // 2. Subtle vertical slot dividers between inactive slots inside the track
  const int div_h = dr - scale_s(6);
  const Color div_col(30, 38, 50);
  const float rel_active = anim_active - anim_start;
  for (int i = 0; i < vis_count - 1; i++) {
    if (std::fabs(static_cast<float>(i) - rel_active) < 0.65f ||
        std::fabs(static_cast<float>(i + 1) - rel_active) < 0.65f) {
      continue;
    }
    int mid_x = (win_xs[i] + win_xs[i + 1]) / 2;
    it.vertical_line(mid_x, by - div_h, div_h * 2 + 1, div_col);
  }

  // 3. Smoothly Gliding Active Slot Capsule Highlight inside the track
  if (rel_active >= -0.35f && rel_active <= static_cast<float>(vis_count - 1) + 0.35f) {
    int abx = left_cx + static_cast<int>(std::roundf(rel_active * static_cast<float>(stride)));
    abx = std::max(left_cx, std::min(right_cx, abx));
    it.filled_circle(abx, by, dr - 1, cap_bg);
    it.circle(abx, by, dr, theme_primary);
    it.circle(abx, by, dr - 1, theme_primary);
  }

  // 4. Outer Left '<' and Right '>' Conveyor Chevrons when total_count > 4
  const int chev_off = dr + ((SCREEN_W < 380) ? 10 : scale_x(13));
  const int ch_s = scale_s(5);
  if (has_left_arrow) {
    int lx = left_cx - chev_off;
    it.line(lx + ch_s / 2, by - ch_s, lx - ch_s / 2, by, col_white);
    it.line(lx - ch_s / 2, by, lx + ch_s / 2, by + ch_s, col_white);
    it.line(lx + ch_s / 2 + 1, by - ch_s, lx - ch_s / 2 + 1, by, theme_primary);
    it.line(lx - ch_s / 2 + 1, by, lx + ch_s / 2 + 1, by + ch_s, theme_primary);
  }
  if (has_right_arrow) {
    int rx = right_cx + chev_off;
    it.line(rx - ch_s / 2, by - ch_s, rx + ch_s / 2, by, col_white);
    it.line(rx + ch_s / 2, by, rx - ch_s / 2, by + ch_s, col_white);
    it.line(rx - ch_s / 2 - 1, by - ch_s, rx + ch_s / 2 - 1, by, theme_primary);
    it.line(rx + ch_s / 2 - 1, by, rx - ch_s / 2 - 1, by + ch_s, theme_primary);
  }

  // 5. Proportional Viewport Scrollbar below the Mode Bar when total_count > 4
  if (total_count > 4) {
    render_viewport_scrollbar(it, cx, layout().app_dock_pips_y, total_count, anim_start,
                              theme_primary, false);
  }
}
}  // namespace latter_ui


