#pragma once

// ==============================================================================
// Latter UI Frame Context, Backdrop Resolver & System Takeover State
// (include/ui_frame_context.h)
// Pure C++20 UI header — zero ESPHome or HAL includes.
// ==============================================================================

#include "state/device_runtime_state.h"
#include "ui_color.h"
#include "ui_panel_traits.h"
#include "ui_surface.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace latter_ui {

// Zero-allocation null-terminated string view for UiFrameContext & SystemTakeoverState (D-034(2)).
struct UiStrView {
  const char *ptr_{""};
  size_t len_{0};

  constexpr UiStrView() noexcept = default;
  constexpr UiStrView(const char *s) noexcept
      : ptr_(s != nullptr ? s : ""),
        len_(s != nullptr ? std::char_traits<char>::length(s) : 0U) {}
  constexpr UiStrView(const char *s, size_t n) noexcept
      : ptr_(s != nullptr ? s : ""),
        len_(s != nullptr ? n : 0U) {}
  UiStrView(const std::string &s) noexcept : ptr_(s.c_str()), len_(s.size()) {}
  UiStrView(std::string &&) = delete;

  constexpr const char *c_str() const noexcept { return ptr_; }
  constexpr const char *data() const noexcept { return ptr_; }
  constexpr size_t size() const noexcept { return len_; }
  constexpr size_t length() const noexcept { return len_; }
  constexpr bool empty() const noexcept { return len_ == 0U; }
  constexpr char operator[](size_t idx) const noexcept { return ptr_[idx]; }

  constexpr std::string_view sv() const noexcept { return std::string_view(ptr_, len_); }
  constexpr operator std::string_view() const noexcept { return std::string_view(ptr_, len_); }
  inline std::string str() const { return std::string(ptr_, len_); }

  constexpr size_t find(std::string_view needle, size_t pos = 0U) const noexcept {
    return sv().find(needle, pos);
  }
  constexpr size_t find(char ch, size_t pos = 0U) const noexcept {
    return sv().find(ch, pos);
  }
  constexpr size_t rfind(std::string_view needle, size_t pos = std::string_view::npos) const noexcept {
    return sv().rfind(needle, pos);
  }
  constexpr std::string_view substr(size_t pos = 0U, size_t count = std::string_view::npos) const noexcept {
    return sv().substr(pos, count);
  }

  friend constexpr bool operator==(UiStrView a, UiStrView b) noexcept { return a.sv() == b.sv(); }
  friend constexpr bool operator!=(UiStrView a, UiStrView b) noexcept { return a.sv() != b.sv(); }
  friend constexpr bool operator==(UiStrView a, std::string_view b) noexcept { return a.sv() == b; }
  friend constexpr bool operator!=(UiStrView a, std::string_view b) noexcept { return a.sv() != b; }
  friend constexpr bool operator==(std::string_view a, UiStrView b) noexcept { return a == b.sv(); }
  friend constexpr bool operator!=(std::string_view a, UiStrView b) noexcept { return a != b.sv(); }
  friend constexpr bool operator==(UiStrView a, const char *b) noexcept {
    return a.sv() == (b != nullptr ? std::string_view(b) : std::string_view{});
  }
  friend constexpr bool operator!=(UiStrView a, const char *b) noexcept { return !(a == b); }
  friend constexpr bool operator==(const char *a, UiStrView b) noexcept { return b == a; }
  friend constexpr bool operator!=(const char *a, UiStrView b) noexcept { return !(b == a); }
  friend inline bool operator==(UiStrView a, const std::string &b) noexcept {
    return a.sv() == std::string_view(b);
  }
  friend inline bool operator!=(UiStrView a, const std::string &b) noexcept {
    return a.sv() != std::string_view(b);
  }
  friend inline bool operator==(const std::string &a, UiStrView b) noexcept {
    return std::string_view(a) == b.sv();
  }
  friend inline bool operator!=(const std::string &a, UiStrView b) noexcept {
    return std::string_view(a) != b.sv();
  }
};

enum class BackdropMode : uint8_t {
  SolidBlack = 0,
  CustomWallpaper = 1,
  LiveWeatherSky = 2,
  DayNightSky = 3,
};

constexpr BackdropMode resolve_backdrop_mode(int gui_page, int home_subpage,
                                             bool home_weather_bg,
                                             bool weather_app_bg,
                                             bool weather_sim_active,
                                             bool has_custom_wallpaper,
                                             bool has_day_night_sky = false,
                                             bool tools_full_bleed = false,
                                             bool music_art_none = false) noexcept {
  const BackdropMode fallback_wall =
      has_day_night_sky
          ? BackdropMode::DayNightSky
          : (has_custom_wallpaper ? BackdropMode::CustomWallpaper : BackdropMode::SolidBlack);
  if (gui_page == 1 && home_subpage == 0) {
    if (home_weather_bg || weather_sim_active) {
      return BackdropMode::LiveWeatherSky;
    }
    return fallback_wall;
  }
  if (gui_page == 5) {
    if (weather_app_bg || weather_sim_active) {
      return BackdropMode::LiveWeatherSky;
    }
    return fallback_wall;
  }
  if (gui_page == 1) {
    return fallback_wall;
  }
  if (gui_page == 2) {
    return music_art_none ? fallback_wall : BackdropMode::SolidBlack;
  }
  if (gui_page == 4 || gui_page == 6 || gui_page == 7 ||
      ((gui_page == 8 || gui_page == 9 || gui_page == 12 || gui_page == 13) && !tools_full_bleed) ||
      gui_page == 10 || gui_page == 11) {
    return fallback_wall;
  }
  return BackdropMode::SolidBlack;
}

inline constexpr uint32_t kPhotoFrameLoadGraceMs = 2500U;

inline bool can_reuse_wallpaper_for_photo_frame(
    std::string_view screensaver_photo_url,
    std::string_view loaded_wallpaper_url,
    bool has_wallpaper_art) noexcept {
  return has_wallpaper_art && !screensaver_photo_url.empty() &&
         screensaver_photo_url == loaded_wallpaper_url;
}

inline bool is_photo_frame_initial_load_pending(
    uint32_t takeover_sig, std::string_view screensaver_style_opt,
    std::string_view screensaver_photo_url, bool has_photo_art,
    bool photo_fetch_failed, uint32_t standby_entered_ms,
    uint32_t now_ms) noexcept {
  if (takeover_sig != (1U << 3)) return false;
  if (screensaver_style_opt != "Photo Frame") return false;
  if (screensaver_photo_url.empty()) return false;
  if (has_photo_art || photo_fetch_failed) return false;
  if (standby_entered_ms == 0U) return false;
  return static_cast<uint32_t>(now_ms - standby_entered_ms) < kPhotoFrameLoadGraceMs;
}

inline ArtImageView resolve_photo_frame_art(
    const ArtImageView &screensaver_photo_art,
    const ArtImageView &home_wallpaper_art,
    std::string_view screensaver_photo_url,
    std::string_view loaded_wallpaper_url) noexcept {
  if (screensaver_photo_art.valid() && !screensaver_photo_url.empty()) {
    return screensaver_photo_art;
  }
  if (can_reuse_wallpaper_for_photo_frame(
          screensaver_photo_url, loaded_wallpaper_url, home_wallpaper_art.valid())) {
    return home_wallpaper_art;
  }
  return ArtImageView{};
}

inline float weather_temp_to_fahrenheit(
    float temp, std::string_view temp_unit) noexcept {
  if (std::isnan(temp)) return temp;
  if (!temp_unit.empty() &&
      (temp_unit.front() == 'C' || temp_unit.front() == 'c' ||
       temp_unit.back() == 'C' || temp_unit.back() == 'c')) {
    return temp * 1.8f + 32.0f;
  }
  return temp;
}

struct ParsedIsoTimestamp {
  int year{0};
  int month{0};
  int day{0};
  int hour{0};
  int minute{0};
  int second{0};
  bool has_tz{false};
  int tz_offset_sec{0};
  int64_t utc_epoch_sec{0};
};

inline bool parse_iso_timestamp_components(
    std::string_view raw, ParsedIsoTimestamp &out) noexcept {
  while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
  while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
  if (raw.size() < 16U || raw == "unknown" || raw == "unavailable") return false;

  auto parse_digits = [&](size_t pos, size_t n, int &val) noexcept -> bool {
    if (pos + n > raw.size()) return false;
    int acc = 0;
    for (size_t i = 0; i < n; ++i) {
      const char ch = raw[pos + i];
      if (ch < '0' || ch > '9') return false;
      acc = acc * 10 + (ch - '0');
    }
    val = acc;
    return true;
  };

  int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
  if (!parse_digits(0, 4, yr) || raw[4] != '-' ||
      !parse_digits(5, 2, mo) || raw[7] != '-' ||
      !parse_digits(8, 2, dy)) {
    return false;
  }
  const char sep = raw[10];
  if (sep != 'T' && sep != 't' && sep != ' ') return false;
  if (!parse_digits(11, 2, hr) || raw[13] != ':' || !parse_digits(14, 2, mn)) {
    return false;
  }
  size_t idx = 16U;
  if (idx < raw.size() && raw[idx] == ':') {
    if (!parse_digits(idx + 1U, 2U, sc)) return false;
    idx += 3U;
  }
  if (idx < raw.size() && (raw[idx] == '.' || raw[idx] == ',')) {
    ++idx;
    if (idx >= raw.size() || raw[idx] < '0' || raw[idx] > '9') return false;
    while (idx < raw.size() && raw[idx] >= '0' && raw[idx] <= '9') ++idx;
  }

  bool has_tz = false;
  int tz_off_sec = 0;
  if (idx < raw.size()) {
    const char zc = raw[idx];
    if (zc == 'Z' || zc == 'z') {
      if (idx + 1U != raw.size()) return false;
      has_tz = true;
    } else if (zc == '+' || zc == '-') {
      const int sign = (zc == '-') ? -1 : 1;
      ++idx;
      int oh = 0, om = 0;
      if (!parse_digits(idx, 2U, oh)) return false;
      idx += 2U;
      if (idx < raw.size() && raw[idx] == ':') ++idx;
      if (idx < raw.size()) {
        if (!parse_digits(idx, 2U, om)) return false;
        idx += 2U;
      }
      if (idx != raw.size() || oh > 23 || om > 59) return false;
      has_tz = true;
      tz_off_sec = sign * (oh * 3600 + om * 60);
    } else {
      return false;
    }
  }

  if (mo < 1 || mo > 12 || dy < 1 || dy > 31 || hr < 0 || hr > 23 ||
      mn < 0 || mn > 59 || sc < 0 || sc > 60) {
    return false;
  }

  const int y = yr - (mo <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (mo > 2 ? mo - 3 : mo + 9) + 2) / 5 + dy - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097LL + static_cast<int64_t>(doe) - 719468LL;
  const int64_t wall_sec = days * 86400LL + static_cast<int64_t>(hr) * 3600LL +
                           static_cast<int64_t>(mn) * 60LL + static_cast<int64_t>(sc);

  out.year = yr;
  out.month = mo;
  out.day = dy;
  out.hour = hr;
  out.minute = mn;
  out.second = sc;
  out.has_tz = has_tz;
  out.tz_offset_sec = tz_off_sec;
  out.utc_epoch_sec = wall_sec - static_cast<int64_t>(tz_off_sec);
  return true;
}

inline void format_hhmm_clock_to(int hr, int mn, bool is_24h, char *out, size_t out_sz) noexcept {
  if (out == nullptr || out_sz == 0U) return;
  const int h = ((hr % 24) + 24) % 24;
  const int m = std::clamp(mn, 0, 59);
  if (is_24h) {
    std::snprintf(out, out_sz, "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(out, out_sz, "%d:%02d %s", h12, m, (h >= 12) ? "PM" : "AM");
  }
}

inline bool format_iso_time_with_offset(
    std::string_view iso_str, bool is_24h, int local_utc_offset_min,
    char *out, size_t out_sz) noexcept {
  if (out == nullptr || out_sz == 0U) return false;
  ParsedIsoTimestamp ts{};
  if (!parse_iso_timestamp_components(iso_str, ts)) {
    std::snprintf(out, out_sz, "%s", kUnknownGlyph);
    return false;
  }
  if (!ts.has_tz) {
    format_hhmm_clock_to(ts.hour, ts.minute, is_24h, out, out_sz);
    return true;
  }
  const int64_t local_sec = ts.utc_epoch_sec + static_cast<int64_t>(local_utc_offset_min) * 60LL;
  const int rem = static_cast<int>(((local_sec % 86400LL) + 86400LL) % 86400LL);
  format_hhmm_clock_to(rem / 3600, (rem % 3600) / 60, is_24h, out, out_sz);
  return true;
}

// Concrete Per-Frame Render Context (pure C++20; passed to all compiled page_*.cpp & host tests)
struct UiFrameContext {
  UiWallTime now{};
  uint32_t now_ms{0};
  int cx{SCREEN_CX};
  int cy{SCREEN_CY};
  const char *friendly_name{""};
  const char *fw_version{""};

  // Fonts (pure C++20 UiFontView; constructible from ESPHome Font* or make_host_font)
  UiFontView font_time{};
  UiFontView font_chrono_time{};
  UiFontView font_date{};
  UiFontView font_btn_bold{};
  UiFontView font_small{};
  UiFontView font_micro{};
  UiFontView font_games_title{};

  // Decoded RGB565 image views
  ArtImageView album_art{};
  ArtImageView remote_album_art{};
  ArtImageView home_wallpaper_art{};
  ArtImageView screensaver_photo_art{};
  ArtImageView camera_snapshot_art{};

  // Palette colors
  UiColor theme_primary{0x6B8E9EU};
  UiColor theme_secondary{0xC0C0C0U};
  UiColor col_bg{0x000000U};
  UiColor col_black{0x000000U};
  UiColor col_white{0xFFFFFFU};
  UiColor col_slate{0x9E9E9EU};
  UiColor col_gray{0x616161U};
  UiColor col_dark_gray{0x212121U};
  UiColor col_gold{0xC59B27U};
  UiColor col_warm_gold{0xA88424U};
  UiColor col_solaris_gold{0xFFDC1EU};
  UiColor col_cyan{0x456E8AU};
  UiColor col_accent_cyan{0x1EC8E6U};
  UiColor col_green{0x529668U};
  UiColor col_orange{0xB85C24U};
  UiColor col_red{0xE06A5EU};
  UiColor col_purple{0x6D4B7DU};
  UiColor col_magenta{0x8E4A7DU};

  // Live link, battery & media snapshots (zero-copy UiStrView — zero heap allocs per frame)
  bool panel_is_on{true};
  bool ha_connected{true};
  uint32_t ha_disconnect_ms{0};
  bool local_media_playing{false};
  int spk_vol_pct{60};
  int alert_vol_pct{80};
  int vol_step_pct{5};
  int free_heap_kb{0};
  float wifi_rssi{NAN};
  UiStrView ip_addr{};
  bool ble_active{false};
  UiStrView track_title{};
  UiStrView track_artist{};
  UiStrView track_album{};
  UiStrView fw_latest_ver{};
  UiStrView fw_release_summary{};
  UiStrView primary_wake_word{"Okay Nabu"};

  // Select options
  UiStrView clock_face_style_opt{"Analog"};
  UiStrView weather_sim_opt{"Live (Home Assistant)"};
  UiStrView music_overlay_style_opt{"Art + Metadata"};
  UiStrView music_auto_open_opt{"Now Playing"};
  UiStrView music_fullscreen_art_opt{"Full Screen"};
  UiStrView music_album_sort_opt{"A-Z"};
  UiStrView music_artist_album_sort_opt{"Release Year"};
  UiStrView music_podcast_sort_opt{"Latest Episode"};
  UiStrView theme_palette_opt{"Steel & Silver"};
  UiStrView tap_wake_opt{"Always On"};
  UiStrView pickup_wake_opt{"Medium"};
  UiStrView display_off_timeout_opt{"30 Seconds"};
  UiStrView screensaver_mode_opt{"Docked Only"};
  UiStrView screensaver_style_opt{"Ambient Clock"};
  UiStrView screensaver_docked_timeout_opt{"5 Minutes"};
  UiStrView hibernate_timeout_opt{"30 Minutes"};
  UiStrView bedtime_display_opt{"Screen Off"};
  UiStrView default_start_app_opt{"Home"};
  UiStrView auto_brightness_opt{"Time Schedule"};
  UiStrView va_wake_word_mode_opt{"Always On"};
  UiStrView va_visual_profile_opt{"Nebula"};
  UiStrView ww_sens_okay_nabu_opt{"Medium"};
  UiStrView ww_sens_hey_luna_opt{"Off"};
  UiStrView ww_sens_hey_jarvis_opt{"Off"};
  UiStrView ww_sens_hey_mycroft_opt{"Off"};
  UiStrView ww_sens_alexa_opt{"Off"};

  // Numbers
  int manual_brightness_pct{80};
  int day_brightness_pct{90};
  int night_brightness_pct{35};
  int day_start_hr{7};
  int night_start_hr{21};

  // Switches
  bool home_weather_bg{true};
  bool weather_app_bg{true};
  bool home_wake_greeting{true};
  bool home_sun_arc{true};
  bool home_moon_phase{true};
  bool game_sound_enabled{true};
  bool va_wake_screen{true};
  bool screensaver_photo_clock{false};
  bool bedtime_active{false};
  bool do_not_disturb{false};
};

struct SystemTakeoverState {
  int cx{SCREEN_CX};
  int cy{SCREEN_CY};
  uint32_t now_ms{0};
  bool is_24h_format{false};
  bool ota_in_progress{false};
  int ota_progress_pct{0};
  bool is_hibernating{false};
  int hibernate_countdown_val{0};
  bool panel_is_on{true};
  int display_power_state{0};
  bool alarm_ringing{false};
  int alarm_ringing_idx{0};
  int alarm1_recurrence{1};
  int alarm2_recurrence{2};
  int alarm3_recurrence{2};
  bool timer_ringing{false};
  bool find_my_active{false};
  UiStrView va_timer_name{};
  bool va_active{false};
  UiStrView va_status_text{};
  bool va_tts_received{false};
  bool va_vad_active{false};
  bool va_intercom_turn{false};
  UiStrView va_visual_profile_opt{};
  bool notif_active{false};
  UiStrView notif_title{};
  UiStrView notif_message{};
  UiStrView notif_priority{};
  UiStrView notif_camera_url{};
  UiStrView notif_camera_entity{};
  UiStrView current_loaded_camera_url{};
  UiStrView screensaver_style_opt{};
  bool screensaver_photo_clock{false};
  bool quick_actions_open{false};
  UiColor theme_primary{};
  UiColor theme_secondary{};
  UiColor col_bg{};
  UiColor col_black{};
  UiColor col_white{};
  UiColor col_slate{};
  UiColor col_gray{};
  UiColor col_dark_gray{};
  UiColor col_gold{};
  UiColor col_red{};
  UiColor col_solaris_gold{};

  static inline SystemTakeoverState from_context(const UiFrameContext &ctx) {
    const auto &rs = runtime_state();
    const auto &prefs = persisted_prefs();
    SystemTakeoverState st{};
    st.cx = ctx.cx;
    st.cy = ctx.cy;
    st.now_ms = ctx.now_ms;
    st.is_24h_format = prefs.is_24h_format;
    st.ota_in_progress = rs.nav.ota_in_progress;
    st.ota_progress_pct = rs.nav.ota_progress_pct;
    st.is_hibernating = rs.nav.is_hibernating;
    st.hibernate_countdown_val = rs.nav.hibernate_countdown_val;
    st.panel_is_on = ctx.panel_is_on;
    st.display_power_state = rs.nav.display_power_state;
    const bool sc_on = rs.showcase.is_active(ctx.now_ms);
    st.alarm_ringing = rs.alarms.ringing || (sc_on && rs.showcase.silent_alarm);
    st.alarm_ringing_idx = rs.alarms.ringing_idx;
    st.alarm1_recurrence = prefs.alarms[0].recurrence;
    st.alarm2_recurrence = prefs.alarms[1].recurrence;
    st.alarm3_recurrence = prefs.alarms[2].recurrence;
    st.timer_ringing = rs.timers.ringing;
    st.find_my_active = rs.alarms.find_my_active || (sc_on && rs.showcase.silent_find_my);
    st.va_timer_name = rs.timers.va_timer_name;
    st.va_active = rs.voice.active;
    st.va_status_text = rs.voice.status_text;
    st.va_tts_received = rs.voice.tts_received;
    st.va_vad_active = rs.voice.vad_active;
    st.va_intercom_turn = rs.voice.intercom_turn;
    if (!rs.voice.active && rs.voice.status_text.empty()) {
      const uint8_t sc_va = rs.showcase.effective_va_phase(ctx.now_ms);
      if (sc_va != 0U) {
        st.va_active = true;
        st.va_vad_active = (sc_va == static_cast<uint8_t>(ShowcaseVaStage::VAD));
        st.va_tts_received = (sc_va == static_cast<uint8_t>(ShowcaseVaStage::SPEAKING));
        if (sc_va == static_cast<uint8_t>(ShowcaseVaStage::THINKING)) {
          st.va_status_text = UiStrView("Thinking...");
        } else if (sc_va == static_cast<uint8_t>(ShowcaseVaStage::SPEAKING)) {
          st.va_status_text = UiStrView("Responding...");
        } else {
          st.va_status_text = UiStrView("Listening...");
        }
      }
    }
    st.va_visual_profile_opt =
        (sc_on && rs.showcase.va_profile_override >= 0 && rs.showcase.va_profile_override < 5)
            ? UiStrView(kVaVisualProfileOpts[rs.showcase.va_profile_override])
            : ctx.va_visual_profile_opt;
    st.notif_active = rs.alert.active;
    st.notif_title = rs.alert.title;
    st.notif_message = rs.alert.message;
    st.notif_priority = rs.alert.priority;
    st.notif_camera_url = rs.alert.camera_url;
    st.notif_camera_entity = rs.alert.camera_entity;
    st.current_loaded_camera_url = rs.alert.loaded_camera_url;
    const bool sc_ss_override =
        sc_on && rs.showcase.screensaver_style_override >= 0 && rs.showcase.screensaver_style_override < 6;
    st.screensaver_style_opt = sc_ss_override
                                   ? UiStrView(kScreensaverStyleOpts[rs.showcase.screensaver_style_override])
                                   : (prefs.bedtime_active ? UiStrView("Ambient Clock") : ctx.screensaver_style_opt);
    st.screensaver_photo_clock = (!prefs.bedtime_active || sc_ss_override) && prefs.screensaver_photo_clock;
    st.quick_actions_open = rs.nav.quick_actions_open;
    st.theme_primary = ctx.theme_primary;
    st.theme_secondary = ctx.theme_secondary;
    st.col_bg = ctx.col_bg;
    st.col_black = ctx.col_black;
    st.col_white = ctx.col_white;
    st.col_slate = ctx.col_slate;
    st.col_gray = ctx.col_gray;
    st.col_dark_gray = ctx.col_dark_gray;
    st.col_gold = ctx.col_gold;
    st.col_red = ctx.col_red;
    st.col_solaris_gold = ctx.col_solaris_gold;
    return st;
  }
};

UiFrameContext &acquire_frame_context();
const SystemTakeoverState &acquire_takeover_state(const UiFrameContext &ctx);

inline int compute_hhmm_key(const UiWallTime &now) noexcept {
  return now.is_valid()
      ? (static_cast<int>(now.day_of_year) * 1440 + static_cast<int>(now.hour) * 60 + static_cast<int>(now.minute))
      : -1;
}

}  // namespace latter_ui
