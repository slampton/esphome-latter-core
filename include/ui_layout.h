#pragma once

// ==============================================================================
// Latter Core UI Layout & Hardware Portability Toolkit (ui_layout.h)
// Provides resolution- & shape-adaptive layout metrics and hit-testing across
// circular (466x466), rectangular watch (410x502), and square (480x480 / 720x720)
// ESP32-S3 and ESP32-P4 displays.
// ==============================================================================

#include "ui_core.h"
#include "ui_hit_table.h"
#include "hal_manifest.h"
#include "hal_panel_power.h"
#include "hal_power_pm.h"

namespace latter_ui {

struct UiLayout {
  int w = SCREEN_W;
  int h = SCREEN_H;
  int cx = SCREEN_CX;
  int cy = SCREEN_CY;
  bool is_round = (LATTER_ROUND_DISPLAY != 0);

  // Header & Footer Y coordinates (Adaptive for Round vs Wearable Watch vs Square/Rect vs 720p HD)
  int hdr_y = LATTER_ROUND_DISPLAY ? 48 : (LATTER_IS_WEARABLE ? 36 : ((SCREEN_H > 520) ? 48 : 28));
  int sub_hdr_y = LATTER_ROUND_DISPLAY ? 76 : (LATTER_IS_WEARABLE ? 64 : ((SCREEN_H > 520) ? 88 : 56));
  int back_y = kBackLabelY;
  int home_bar_y = kHomeBarY;
  int dock_y = LATTER_ROUND_DISPLAY ? 359 : (SCREEN_H - scale_y(66));
  int app_dock_y = LATTER_ROUND_DISPLAY ? 369 : (SCREEN_H - scale_y(66));
  int dock_pips_y = LATTER_ROUND_DISPLAY ? 404 : (SCREEN_H - scale_y(27));
  int app_dock_pips_y = LATTER_ROUND_DISPLAY ? 408 : (SCREEN_H - scale_y(27));
  int dock_r = (SCREEN_W < 360) ? 24 : ((SCREEN_W > 520) ? 40 : 27);

  // Consistent Upper-Left Celestial Sun/Moon Coordinates across all 5 displays
  int celestial_x = LATTER_ROUND_DISPLAY ? 100 : ((SCREEN_W < 380) ? 44 : (LATTER_IS_WEARABLE ? 68 : ((SCREEN_W > 520) ? 118 : 78)));
  int celestial_y = LATTER_ROUND_DISPLAY ? 92  : ((SCREEN_W < 380) ? 72 : ((SCREEN_H > 520) ? 118 : 78));

  // Corner-Safe Battery Indicator Coordinates for Non-Round Displays
  int battery_cx = LATTER_ROUND_DISPLAY ? SCREEN_CX : (LATTER_IS_WEARABLE ? (SCREEN_W - 68) : ((SCREEN_W < 380) ? (SCREEN_W - 36) : ((SCREEN_W > 520) ? (SCREEN_W - 86) : (SCREEN_W - 58))));
  int battery_y  = LATTER_ROUND_DISPLAY ? 435       : (LATTER_IS_WEARABLE ? 43 : ((SCREEN_W < 380) ? 24 : ((SCREEN_H > 520) ? 42 : 28)));

  // Standard Menu Card Width & Left Text Margin (232px on 320x480; scales up on >520px HD panels)
  int list_card_w = (SCREEN_W < 380) ? (SCREEN_W - 88) : ((SCREEN_W > 520) ? ((SCREEN_W * 276) / 466) : 276);
  int list_card_left = SCREEN_CX - (list_card_w / 2);

  // 1. Adaptive 5-Slot Vertical List Geometry (System, Music, Area Picker, Calendar, Intercom)
  int slot5_stride = (SCREEN_H > 520) ? ((SCREEN_H * 63) / 466) : 63;
  int slot5_h      = (SCREEN_H > 520) ? ((SCREEN_H * 44) / 466) : 44;
  int slot5_cy     = (SCREEN_H > 520) ? SCREEN_CY : 236;
  constexpr int slot5_y(int idx_0_to_4) const {
    return slot5_cy + (idx_0_to_4 - 2) * slot5_stride;
  }
  template <typename TouchT>
  constexpr int hit_slot5(const TouchT &t, int x_min, int x_max) const {
    const int half_w = (w > 520) ? ((w * 150) / 466) : 150;
    const int scaled_min = (w < 380 && x_min < 60) ? 4 : scale_x(x_min);
    const int scaled_max = (w < 380) ? (w - 10) : scale_x(x_max);
    const int eff_min = (w == 466) ? std::min(x_min, 38) : std::max(4, std::min(scaled_min, cx - half_w));
    const int eff_max = (w == 466) ? x_max : std::min(w - 10, std::max(scaled_max, cx + half_w));
    if (t.x < eff_min || t.x > eff_max) return -1;
    const int half_s = slot5_stride / 2;
    for (int i = 0; i < 5; i++) {
      const int ym = slot5_y(i);
      const int y_top = ym - ((i == 0) ? (half_s + 4) : half_s);
      const int y_bot = ym + ((i == 4) ? (half_s + 10) : half_s);
      if (t.y >= y_top && t.y <= y_bot) return i;
    }
    return -1;
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
  // Home activity card (docs/device-ui-design.md D-028). The round panel keeps one line, centered
  // between the weather line and the dock; every other panel shows two lines, a named gap above the
  // dock (data above, controls below). weather_h is font_date's line height (home_weather_text_h()).
  constexpr bool home_activity_two_line() const { return !is_round; }
  constexpr int home_activity_w() const { return (w > 520) ? scale_x(380) : std::min(380, w - 32); }
  constexpr int home_activity_pitch() const { return scale_y(46); }  // two-line row pitch
  constexpr int home_activity_h() const { return is_round ? scale_y(68) : (2 * scale_y(46) + scale_y(12)); }
  constexpr int home_activity_dock_gap() const { return mm_to_px(3.0f); }  // clear space above the dock
  constexpr int home_activity_cy(int weather_h) const {
    return is_round ? ((home_weather_y + weather_h / 2) + (dock_y - dock_r)) / 2
                    : (dock_y - dock_r - home_activity_dock_gap() - home_activity_h() / 2);
  }
  // A dropdown column (opens the full activity list) when there are more items than lines.
  constexpr bool home_activity_has_drop(int count) const { return count > (is_round ? 1 : 2); }
  // Home dock swipe/tap zone top: below the activity card (plus 1 mm) while it shows, so a swipe on the
  // card never pages the dock, and never below the dock's own tap band (dock_y - dock_hit_r()).
  constexpr int home_dock_zone_top(int weather_h, bool card_shown) const {
    const int base = scale_y(320);
    if (!card_shown) return base;
    const int below_card = home_activity_cy(weather_h) + home_activity_h() / 2 + mm_to_px(1.0f);
    return std::min(dock_y - dock_hit_r(), std::max(base, below_card));
  }
  constexpr bool hit_home_activity(int tx, int ty, int weather_h) const {
    const int hw = home_activity_w() / 2;
    const int hh = home_activity_h() / 2 + scale_y(4);
    const int acy = home_activity_cy(weather_h);
    return (tx >= cx - hw && tx <= cx + hw && ty >= acy - hh && ty <= acy + hh);
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

  // 10. Touch thresholds in millimetres (docs/device-ui-design.md D-002), converted with LATTER_PANEL_PPI.
  //     Each value is the pixel distance the 466 px reference panel (266 ppi) used before, so that panel
  //     behaves exactly as it did; every other panel gets the same physical distance. TouchGestureEngine
  //     takes every gesture threshold from here (latter-verify [TOUCH MM]).
  constexpr int drag_start() const { return mm_to_px(0.6f); }          //  6 px: a card slider drag starts
  constexpr int flick_min() const { return mm_to_px(1.7f); }           // 18 px: music track-inspector flick
  constexpr int dock_release_swipe() const { return mm_to_px(1.9f); }  // 20 px: Home dock page swipe on release
  constexpr int swipe_short() const { return mm_to_px(2.1f); }         // 22 px: dock swipe while moving, music lists
  constexpr int swipe_min() const { return mm_to_px(2.3f); }           // 24 px: swipe-up home, pill-tap travel, mode bar, 2048
  constexpr int list_swipe() const { return mm_to_px(2.5f); }          // 26 px: vertical list and carousel paging
  constexpr int page_swipe() const { return mm_to_px(2.7f); }          // 28 px: in-app mode bars, map pan, cameras
  constexpr int tap_max() const { return mm_to_px(2.9f); }             // 30 px: a touch that moved less is a tap
  constexpr int wide_swipe() const { return mm_to_px(2.9f); }          // 30 px: Sky & Weather subpage swipe
  constexpr int canvas_swipe() const { return mm_to_px(3.25f); }       // 34 px: Area Control canvas category swipe
  constexpr int tap_max_loose() const { return mm_to_px(3.45f); }      // 36 px: music browser taps
  constexpr int fast_pan() const { return mm_to_px(10.5f); }           // 110 px: a map drag pans two tiles
  // Zones measured from the bottom edge (round panels keep their hand-placed rows).
  constexpr int edge_guard_y() const { return h - mm_to_px(1.5f); }                      // 16 px band below in-app content
  constexpr int home_zone_y() const { return is_round ? 412 : (h - mm_to_px(5.2f)); }   // 54 px: swipe-up home starts here
  constexpr int chin_guard_y() const { return is_round ? 412 : (h - mm_to_px(2.7f)); }  // 28 px: the home pill's chin
  // Area Control: drags above this line scrub card sliders; horizontal swipes below it step the mode bar.
  constexpr int mode_bar_zone_top() const { return app_dock_y - dock_r - scale_s(14); }
  // Home dock icon hit radius: the drawn icon plus 16 px, and never less than a 7 mm target.
  constexpr int dock_hit_r() const { return std::max(dock_r + 16, mm_to_px(3.6f)); }
  template <typename TouchT>
  constexpr bool is_home_pill_tap(const TouchT &t) const {
    const int pill_y = is_round ? 422 : (h - mm_to_px(2.8f));
    const int pill_hw = std::max(scale_x(64), mm_to_px(6.5f));
    return (t.y >= pill_y && std::abs(t.x - cx) <= pill_hw);
  }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
}

// Whether the Home renderer drew the activity card on the last Home frame (the dock zone starts below it).
inline bool &home_activity_card_shown() {
  static bool shown = false;
  return shown;
}

// font_date's line height, measured by the Home renderer (the activity card's position on the round
// panel depends on it); the default covers the first frame.
inline int &home_weather_text_h() {
  static int h = (SCREEN_H > 520) ? 45 : 32;
  return h;
}

// Page header (docs/device-ui-design.md, components): an UPPERCASE title in font_small and theme_primary,
// centered at layout().hdr_y. `shadow` adds a 1 px drop shadow for headers drawn over wallpaper or art.
template <typename DisplayT, typename FontT>
inline void render_page_header(DisplayT &it, int cx, const char *title, FontT *font, esphome::Color color,
                               bool shadow = false) {
  if (title == nullptr || title[0] == '\0' || font == nullptr) return;
  const int y = layout().hdr_y;
  if (shadow || backdrop_active()) {
    fast_print(it, cx + 1, y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, title);
  }
  fast_print(it, cx, y, font, color, esphome::display::TextAlign::CENTER, title);
}

// Header text that may be long (names from Home Assistant): fitted to the header width first.
template <typename DisplayT, typename FontT>
inline void render_page_header(DisplayT &it, int cx, const std::string &title, FontT *font, esphome::Color color,
                               bool shadow = false) {
  if (title.empty() || font == nullptr) return;
  const std::string fitted = fit_text_to_width_font(title, scale_x(280), font);
  render_page_header(it, cx, fitted.c_str(), font, color, shadow);
}

// Sub-header line at layout().sub_hdr_y: action feedback in theme_secondary, otherwise a context line in
// col_slate or col_white. Always font_small.
template <typename DisplayT, typename FontT>
inline void render_sub_header(DisplayT &it, int cx, const char *text, FontT *font, esphome::Color color) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  if (backdrop_active()) {
    fast_print(it, cx + 1, layout().sub_hdr_y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, text);
  }
  fast_print(it, cx, layout().sub_hdr_y, font, color, esphome::display::TextAlign::CENTER, text);
}

template <typename DisplayT, typename FontT>
inline void render_sub_header(DisplayT &it, int cx, const std::string &text, FontT *font, esphome::Color color) {
  render_sub_header(it, cx, text.c_str(), font, color);
}

// Empty or loading state: one sentence on a quiet card that says what's missing (or what's loading).
template <typename DisplayT, typename FontT>
inline void render_empty_state(DisplayT &it, int cx, int cy, const char *text, FontT *font, esphome::Color text_c) {
  render_modern_card(it, cx, cy, 296, 54, 14, kChromeBarTrack, kChromeCardBorder, text, text_c, font);
}

// The Offline pill shows once the Home Assistant API has been disconnected for 8 s, so a reconnect that
// completes within that time never shows it. Before the first connection the 8 s count from boot.
inline bool offline_pill_due(uint32_t now_ms) {
  auto &ls = link_state();
#ifdef USE_API
  if (esphome::api::global_api_server != nullptr &&
      esphome::api::global_api_server->is_connected_with_state_subscription()) {
    if (!ls.ha_connected) {
      ls.ha_connected = true;
      ls.last_ha_connect_ms = now_ms;
    }
    return false;
  }
#endif
  if (link_report().ha_clients > 0) return false;
  return !ls.ha_connected && (now_ms - ls.last_ha_disconnect_ms) >= 8000U;
}

// "Offline" pill (docs/device-ui-design.md D-026): top center, the word in font_micro and col_gold on the
// dark chrome card. Rectangular panels hang it from the top edge as a tab (rounded bottom corners), which
// keeps it above every page header; the round panel floats it inside the bezel where the circle is wider
// than the pill. gui.yaml draws it after the page and never over a takeover.
template <typename DisplayT, typename FontT>
inline void render_offline_pill(DisplayT &it, int cx, uint32_t now_ms, FontT *font, esphome::Color text_c) {
  if (font == nullptr || !offline_pill_due(now_ms)) return;
  static const char *const kLabel = "Offline";
  int text_w = 0, x_off = 0, base = 0, text_h = 0;
  font->measure(kLabel, &text_w, &x_off, &base, &text_h);
  // Visible glyphs of "Offline" run from the f/l ascender (0.75 em in Roboto, whose line height is
  // 2400/2048 em) down to the baseline; the card pads that ink, not the font's full line box.
  const int em = (text_h * 2048 + 1200) / 2400;
  const int glyph_h = (em * 3 + 2) / 4;
  const int pad_y = std::max(2, mm_to_px(0.2f));
  const int pad_x = mm_to_px(1.4f);
  const int pill_h = glyph_h + 2 * pad_y;
  const int pill_w = text_w + 2 * pad_x;
  const int r = pill_h / 2;
  const int x0 = cx - pill_w / 2;
#if LATTER_ROUND_DISPLAY
  const int margin = mm_to_px(0.6f);
  const int y0 = (SCREEN_R - chord_half_span_int(SCREEN_R, pill_w / 2 + margin)) + margin;
  const bool round_top = true;
#else
  const int y0 = 0;
  const bool round_top = false;
#endif
  // Translucent frosted-glass pill/tab; on rectangular panels the top corners lie above the screen edge.
  const float rad = 0.5f * static_cast<float>(pill_h);
  const int sy0 = round_top ? y0 : (y0 - r);
  const int sh = round_top ? pill_h : (pill_h + r);
  const GlassBackdrop bd = sample_glass_backdrop(x0 - 1, std::max(0, sy0 - 1), pill_w + 2, pill_h + 2);
  const int min_gold_a = bd.valid ? glass_alpha_for_text(kChromeGlassTint, bd.bright, text_c) : 184;
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, kChromeGlassTint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot, min_gold_a);
  fill_round_rect_glass(it, x0 - 1, sy0 - 1, pill_w + 2, sh + 2, rad + 1.0f, kChromeGlassSheen, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0, sy0, pill_w, sh, rad, eff_tint, a_top, a_bot);
  const int text_top = y0 + pad_y - (base - glyph_h);
  fast_print(it, cx + 1, text_top + 1, font, kChromeShadow, esphome::display::TextAlign::TOP_CENTER, kLabel);
  fast_print(it, cx, text_top, font, text_c, esphome::display::TextAlign::TOP_CENTER, kLabel);
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
  win_start = get_mode_bar_start_index(total_count, win_start);
  if (active_idx < win_start) {
    win_start = active_idx;
  } else if (active_idx >= win_start + 4) {
    win_start = active_idx - 3;
  }
  win_start = std::max(0, std::min(total_count - 4, win_start));
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

  // Duration and curve come from the one motion system in ui_core.h (kMotionMs, ease_out_cubic).

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
      if (dt >= kMotionMs) {
        cur_start = static_cast<float>(target_start);
        win_animating = false;
      } else {
        float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kMotionMs));
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
      if (dt >= kMotionMs) {
        cur_active = static_cast<float>(target_active);
        act_animating = false;
      } else {
        float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kMotionMs));
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

inline bool any_dock_anim_active(int gui_page = -1, int home_subpage = 0) {
  if (gui_page >= 0) {
    if (gui_page != 1 || home_subpage != 0) {
      home_dock_anim().win_animating = false;
      home_dock_anim().act_animating = false;
    }
    if (gui_page != 5 && gui_page != 6 && gui_page != 9) {
      mode_bar_anim().win_animating = false;
      mode_bar_anim().act_animating = false;
    }
  }
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
    esphome::Color theme_primary, esphome::Color col_white, int explicit_win_start = -1) {
  using esphome::Color;
  if (vis_count <= 0) return;

  const uint32_t now_ms = esphome::millis();
  const int win_start_idx = (explicit_win_start >= 0)
                                ? explicit_win_start
                                : ((active_vis_idx >= 0) ? (active_global_idx - active_vis_idx) : 0);
  float anim_start  = mode_bar_anim().step_window(win_start_idx, now_ms);
  float anim_active = mode_bar_anim().step_active(active_global_idx, now_ms);

  const int by = layout().app_dock_y;
  const int dr = layout().dock_r;
  const int r_track = dr + scale_s(3);
  const int left_cx  = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride   = get_dock_slot_stride(win_xs, vis_count);
  const Color track_bg = kChromeBarTrack;

  // 1. Translucent frosted-glass stadium track from left_cx to right_cx (D-029, D-030). No slot separators.
  const int tx0 = left_cx - r_track;
  const int tw = (right_cx - left_cx) + 2 * r_track + 1;
  const int th = 2 * r_track + 1;
  const GlassBackdrop bd = sample_glass_backdrop(tx0, by - r_track, tw, th);
  const int track_alpha = bd.valid ? glass_alpha_for_text(kChromeGlassTint, bd.bright, kChromeGlassSheen) : 168;
  render_glass_card(it, tx0, by - r_track, tw, th, r_track, kChromeGlassTint, track_alpha);

  // 2. Smoothly gliding active-slot glass capsule: an anti-aliased 2 px ring with a tinted glass interior.
  const float rel_active = anim_active - anim_start;
  if (rel_active >= -0.55f && rel_active <= static_cast<float>(vis_count - 1) + 0.55f) {
    int abx = left_cx + static_cast<int>(std::roundf(rel_active * static_cast<float>(stride)));
    if (abx >= left_cx - scale_x(16) && abx <= right_cx + scale_x(16)) {
      float ring_alpha = 1.0f;
      if (rel_active < 0.0f) {
        ring_alpha = std::max(0.20f, 1.0f + rel_active * 1.6f);
      } else if (rel_active > static_cast<float>(vis_count - 1)) {
        ring_alpha = std::max(0.20f, 1.0f - (rel_active - static_cast<float>(vis_count - 1)) * 1.6f);
      }
      Color ring_col(static_cast<uint8_t>(track_bg.r + (theme_primary.r - track_bg.r) * ring_alpha),
                     static_cast<uint8_t>(track_bg.g + (theme_primary.g - track_bg.g) * ring_alpha),
                     static_cast<uint8_t>(track_bg.b + (theme_primary.b - track_bg.b) * ring_alpha));
      stroke_round_rect_aa(it, abx - dr, by - dr, 2 * dr + 1, 2 * dr + 1, static_cast<float>(dr) + 0.5f, ring_col, ring_col, 2);
      fill_round_rect_glass(it, abx - dr + 2, by - dr + 2, 2 * dr - 3, 2 * dr - 3,
                            static_cast<float>(dr) - 1.5f, theme_primary, 48, 28);
    }
  }

  // 4. Outer Left '<' and Right '>' Conveyor Chevrons when total_count > 4
  const int chev_off = dr + ((SCREEN_W < 380) ? 10 : scale_x(13));
  const int ch_s = scale_s(5);
  if (has_left_arrow) {
    int lx = left_cx - chev_off;
    fast_line(it, lx + ch_s / 2, by - ch_s, lx - ch_s / 2, by, col_white);
    fast_line(it, lx - ch_s / 2, by, lx + ch_s / 2, by + ch_s, col_white);
    fast_line(it, lx + ch_s / 2 + 1, by - ch_s, lx - ch_s / 2 + 1, by, theme_primary);
    fast_line(it, lx - ch_s / 2 + 1, by, lx + ch_s / 2 + 1, by + ch_s, theme_primary);
  }
  if (has_right_arrow) {
    int rx = right_cx + chev_off;
    fast_line(it, rx - ch_s / 2, by - ch_s, rx + ch_s / 2, by, col_white);
    fast_line(it, rx + ch_s / 2, by, rx - ch_s / 2, by + ch_s, col_white);
    fast_line(it, rx - ch_s / 2 - 1, by - ch_s, rx + ch_s / 2 - 1, by, theme_primary);
    fast_line(it, rx + ch_s / 2 - 1, by, rx - ch_s / 2 - 1, by + ch_s, theme_primary);
  }

  // 5. Proportional Viewport Scrollbar below the Mode Bar when total_count > 4
  if (total_count > 4) {
    render_viewport_scrollbar(it, cx, layout().app_dock_pips_y, total_count, anim_start,
                              theme_primary, false);
  }
}

// ==============================================================================
// 6. Universal Touchless / Rotary Encoder / Button Focus Navigation Engine
// ==============================================================================
// Home dock state, defined in ui_clock_home.h (the focus ring reads the same dock window).
inline int &home_dock_page();
inline int collect_home_dock_apps(int out_apps[8]);

inline int &ui_focus_index() {
  static int s_idx = 0;
  return s_idx;
}

inline uint32_t &ui_focus_last_ms() {
  static uint32_t s_ms = 0;
  return s_ms;
}

inline bool ui_focus_active(uint32_t now_ms) {
#if !LATTER_HAS_TOUCH
  (void) now_ms;
  return true;
#else
  const uint32_t last = ui_focus_last_ms();
  return (last > 0) && ((now_ms - last) < 8000U);
#endif
}

inline void activate_ui_focus(uint32_t now_ms) {
  ui_focus_last_ms() = (now_ms == 0) ? 1U : now_ms;
}

inline void clear_ui_focus() {
#if LATTER_HAS_TOUCH
  ui_focus_last_ms() = 0;
#endif
}

template<typename DisplayT>
inline void render_focus_ring_circle(DisplayT &it, int cx, int cy, int r, esphome::Color accent, esphome::Color inner_hi) {
  fast_circle_aa(it, cx, cy, static_cast<float>(r) + 2.5f, accent, 2.0f);
  fast_circle_aa(it, cx, cy, static_cast<float>(r) + 1.0f, inner_hi, 1.0f);
}

template<typename DisplayT>
inline void render_focus_ring_rect(DisplayT &it, int cx, int cy, int w, int h, esphome::Color accent, esphome::Color inner_hi) {
  const int x0 = cx - w / 2;
  const int y0 = cy - h / 2;
  fast_rect_outline(it, x0 - 2, y0 - 2, w + 4, h + 4, accent);
  fast_rect_outline(it, x0 - 3, y0 - 3, w + 6, h + 6, accent);
  fast_rect_outline(it, x0 - 1, y0 - 1, w + 2, h + 2, inner_hi);
}

template<typename DisplayT>
inline void render_active_focus_overlay(DisplayT &it, int page, int subpage, uint32_t now_ms,
                                        esphome::Color theme_primary, esphome::Color col_white) {
  if (!ui_focus_active(now_ms)) return;
  const auto &ly = layout();
  const int f_idx = std::max(0, ui_focus_index());

  const int hit_fc = frame_hits().focus_count();
  if (hit_fc > 0 && !(page == 1 && subpage == 0)) {
    const HitRegion *hr = frame_hits().focus_at(f_idx % hit_fc);
    if (hr != nullptr) {
      if ((hr->flags & kHitFocusCircle) != 0) {
        render_focus_ring_circle(it, hr->cx(), hr->cy(), hr->radius(), col_white, theme_primary);
      } else {
        render_focus_ring_rect(it, hr->cx(), hr->cy(), hr->width(), hr->height(), col_white, theme_primary);
      }
      return;
    }
  }

  if (page == 1 && subpage == 0) {
    // Home Dock Icon Focus: the ring sits on the icon `select` opens (same window and slots as the dock).
    int dock_apps[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const int dock_count = collect_home_dock_apps(dock_apps);
    if (dock_count > 0) {
      int win_xs[4] = {ly.cx, ly.cx, ly.cx, ly.cx};
      bool has_l = false, has_r = false;
      get_home_dock_window_xs(dock_count, home_dock_page(), ly.cx, win_xs, has_l, has_r);
      const int vis_count = std::min(4, dock_count - get_home_dock_start_index(dock_count, home_dock_page()));
      const int vis_idx = f_idx % std::max(1, vis_count);
      render_focus_ring_circle(it, win_xs[vis_idx], ly.dock_y, ly.dock_r, col_white, theme_primary);
    }
  } else if (page == 2) {
    if (subpage == 0) {
      // Music Now Playing Transport Controls (0: Prev, 1: Play/Pause, 2: Next, 3: Library, 4: Speakers)
      const int idx = f_idx % 5;
      if (idx == 0) render_focus_ring_circle(it, ly.cx - scale_x(86), scale_y(310), scale_s(26), col_white, theme_primary);
      else if (idx == 1) render_focus_ring_circle(it, ly.cx, scale_y(310), scale_s(34), col_white, theme_primary);
      else if (idx == 2) render_focus_ring_circle(it, ly.cx + scale_x(86), scale_y(310), scale_s(26), col_white, theme_primary);
      else if (idx == 3) render_focus_ring_circle(it, ly.cx - scale_x(64), ly.app_dock_y, scale_s(24), col_white, theme_primary);
      else render_focus_ring_circle(it, ly.cx + scale_x(64), ly.app_dock_y, scale_s(24), col_white, theme_primary);
    } else {
      const int slot = f_idx % 5;
      render_focus_ring_rect(it, ly.cx, ly.slot5_y(slot), ly.list_card_w, ly.slot5_h, col_white, theme_primary);
    }
  } else if ((page == 3 && subpage == 0) || (page == 8 && subpage == 0)) {
    // 2x3 Launcher Grid Focus (Games & Tools)
    const int slot = f_idx % 6;
    const int col = slot % 2;
    const int row = slot / 2;
    const int gcx = ly.cx + ((col == 0) ? -ly.tools_col_off : ly.tools_col_off);
    const int gcy = ly.tools_row_y(row);
    const int gw = (SCREEN_W < 380) ? 110 : scale_x(122);
    render_focus_ring_rect(it, gcx, gcy, gw, scale_y(86), col_white, theme_primary);
  } else if (page == 4 || (page == 8 && (subpage >= 9 && subpage <= 11))) {
    // 5-Slot Vertical Menu List Focus
    const int slot = f_idx % 5;
    render_focus_ring_rect(it, ly.cx, ly.slot5_y(slot), ly.list_card_w, ly.slot5_h, col_white, theme_primary);
  } else if (page == 6) {
    if (subpage == 1) {
      const int slot = f_idx % 5;
      render_focus_ring_rect(it, ly.cx, ly.slot5_y(slot), ly.list_card_w, ly.slot5_h, col_white, theme_primary);
    } else {
      const int slot = f_idx % 7;
      if (slot < 3) {
        render_focus_ring_rect(it, ly.cx, ly.area_card3_y(slot), ly.area_card3_w, ly.area_card3_h, col_white, theme_primary);
      } else {
        const int m_idx = (slot - 3) % 4;
        const int stride = (SCREEN_W < 380) ? 56 : scale_x(68);
        const int fx = ly.cx + static_cast<int>((static_cast<float>(m_idx) - 1.5f) * static_cast<float>(stride));
        render_focus_ring_circle(it, fx, ly.app_dock_y, ly.dock_r, col_white, theme_primary);
      }
    }
  } else if (page == 5 || page == 9) {
    const int m_idx = f_idx % 4;
    const int stride = (SCREEN_W < 380) ? 56 : scale_x(68);
    const int fx = ly.cx + static_cast<int>((static_cast<float>(m_idx) - 1.5f) * static_cast<float>(stride));
    render_focus_ring_circle(it, fx, ly.app_dock_y, ly.dock_r, col_white, theme_primary);
  } else if (page == 7) {
    // TV Remote D-Pad & Action Focus
    const int slot = f_idx % 8;
    if (slot == 0) render_focus_ring_circle(it, ly.cx - scale_x(108), scale_y(118), scale_s(24), col_white, theme_primary);
    else if (slot == 1) render_focus_ring_circle(it, ly.cx, scale_y(158), scale_s(24), col_white, theme_primary);
    else if (slot == 2) render_focus_ring_circle(it, ly.cx - scale_x(68), scale_y(226), scale_s(24), col_white, theme_primary);
    else if (slot == 3) render_focus_ring_circle(it, ly.cx, scale_y(226), scale_s(32), col_white, theme_primary);
    else if (slot == 4) render_focus_ring_circle(it, ly.cx + scale_x(68), scale_y(226), scale_s(24), col_white, theme_primary);
    else if (slot == 5) render_focus_ring_circle(it, ly.cx, scale_y(294), scale_s(24), col_white, theme_primary);
    else if (slot == 6) render_focus_ring_circle(it, ly.cx - scale_x(82), scale_y(352), scale_s(24), col_white, theme_primary);
    else render_focus_ring_circle(it, ly.cx + scale_x(82), scale_y(352), scale_s(24), col_white, theme_primary);
  }
}

// ==============================================================================
// 7. Unified OTA Firmware Update Display & Poller Lifecycle Helpers
// ==============================================================================
template<typename DispT>
inline void handle_ota_begin(DispT *disp, bool &ota_in_progress, int &ota_progress_pct,
                             uint32_t &sleep_start_time, int &display_power_state) {
  ota_in_progress = true;
  stall_guard_pause(true);
  wifi_power_ota_begin();
  ota_progress_pct = 0;
  sleep_start_time = 0;
  screen_is_off_standby() = false;
  disp_poller_held() = true;
  display_power_state = 0;
  if (disp != nullptr) {
    disp->stop_poller();
    hal_set_display_brightness(disp, 230);
    hal_panel_dcs_flush(disp);  // OTA blocks the loop: finish a DCS wake now so the progress screen is lit
  }
}

template<typename DispT>
inline void handle_ota_begin(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct,
                             uint32_t &sleep_start_time, int &display_power_state) {
  if (disp_ptr != nullptr) {
    handle_ota_begin(*disp_ptr, ota_in_progress, ota_progress_pct, sleep_start_time, display_power_state);
  }
}

template<typename DispT>
inline void handle_ota_progress(DispT *disp, float raw_pct, int &ota_progress_pct, uint32_t now_m) {
  static uint32_t last_ota_draw_ms = 0;
  int pct = static_cast<int>(raw_pct);
#if defined(LATTER_DSI_IRAM_SAFE)
  constexpr int min_step_pct = 10;
  constexpr uint32_t min_step_ms = 800U;
#else
  constexpr int min_step_pct = 5;
  constexpr uint32_t min_step_ms = 400U;
#endif
  if (pct != ota_progress_pct &&
      (pct == 100 || (pct - ota_progress_pct >= min_step_pct && (now_m - last_ota_draw_ms >= min_step_ms)) || pct < ota_progress_pct)) {
    ota_progress_pct = pct;
    last_ota_draw_ms = now_m;
    if (disp != nullptr) disp->update();
  }
}

template<typename DispT>
inline void handle_ota_progress(DispT *const *disp_ptr, float raw_pct, int &ota_progress_pct, uint32_t now_m) {
  if (disp_ptr != nullptr) handle_ota_progress(*disp_ptr, raw_pct, ota_progress_pct, now_m);
}

template<typename DispT>
inline void handle_ota_end(DispT *disp, bool &ota_in_progress, int &ota_progress_pct) {
  if (ota_in_progress) {
    ota_progress_pct = 100;
  }
  disp_poller_held() = false;
  if (disp != nullptr) {
    disp->update();
    disp->start_poller();
    // A successful OTA reboots immediately. Hand the panel to the new firmware in the same state as the
    // "Restart Device" path (brightness 0 + DCS sleep / backlight off) rather than awake mid-frame;
    // AMOLED panels left awake across an OTA reboot would not come back from their next DCS sleep.
    hal_set_display_brightness(disp, 0);
    hal_panel_dcs_flush(disp);  // asleep before the reboot, not left waiting on a timeout
  }
  ota_in_progress = false;
  stall_guard_pause(false);
  wifi_power_ota_end();
}

template<typename DispT>
inline void handle_ota_end(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct) {
  if (disp_ptr != nullptr) handle_ota_end(*disp_ptr, ota_in_progress, ota_progress_pct);
}

template<typename DispT>
inline void handle_ota_error(DispT *disp, bool &ota_in_progress, int &ota_progress_pct,
                             bool &notif_active, std::string &notif_title, std::string &notif_message,
                             std::string &notif_priority, uint32_t &notif_duration_ms,
                             uint32_t &notif_start_ms, uint32_t now_m) {
  ota_in_progress = false;
  ota_progress_pct = 0;
  notif_title = "Update failed";
  notif_message = "Firmware update interrupted";
  notif_priority = "warning";
  notif_duration_ms = 5000U;
  notif_start_ms = now_m;
  notif_active = true;
  handle_ota_end(disp, ota_in_progress, ota_progress_pct);
}

template<typename DispT>
inline void handle_ota_error(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct,
                             bool &notif_active, std::string &notif_title, std::string &notif_message,
                             std::string &notif_priority, uint32_t &notif_duration_ms,
                             uint32_t &notif_start_ms, uint32_t now_m) {
  if (disp_ptr != nullptr) {
    handle_ota_error(*disp_ptr, ota_in_progress, ota_progress_pct,
                     notif_active, notif_title, notif_message,
                     notif_priority, notif_duration_ms, notif_start_ms, now_m);
  }
}

}  // namespace latter_ui


