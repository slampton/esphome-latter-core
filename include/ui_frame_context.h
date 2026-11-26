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

inline bool has_screensaver_photo_frame_art(
    bool photo_art_loaded,
    bool wallpaper_loaded,
    const latter_core::DeviceRuntimeState &rs = latter_core::runtime_state(),
    const latter_core::AreaContextState &ac = latter_core::area_ctx_state()) noexcept {
  return (photo_art_loaded && !ac.screensaver_photo_url.empty()) ||
         can_reuse_wallpaper_for_photo_frame(
             ac.screensaver_photo_url, rs.music.loaded_wallpaper_url, wallpaper_loaded);
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

bool parse_iso_timestamp_components(std::string_view raw, ParsedIsoTimestamp &out) noexcept;
void format_hhmm_clock_to(int hr, int mn, bool is_24h, char *out, size_t out_sz) noexcept;
bool format_iso_time_with_offset(
    std::string_view iso_str, bool is_24h, int local_utc_offset_min,
    char *out, size_t out_sz) noexcept;

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
  UiColor col_magenta{0x8E4A7DU};

  // Live link, battery & media snapshots (zero-copy UiStrView — zero heap allocs per frame)
  bool panel_is_on{true};
  bool ha_connected{true};
  uint32_t ha_disconnect_ms{0};
  bool local_media_playing{false};
  int spk_vol_pct{60};
  int alert_vol_pct{latter_core::kPrefDefaultAlertVolume};
  int vol_step_pct{latter_core::kPrefDefaultVolumeStepSize};
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

  // Select options (wired directly to generated protocol/latter_v3.json defaults)
  UiStrView clock_face_style_opt{latter_core::kPrefDefaultClockFace.data(), latter_core::kPrefDefaultClockFace.size()};
  UiStrView weather_sim_opt{latter_core::kPrefDefaultWeatherSimCondition.data(), latter_core::kPrefDefaultWeatherSimCondition.size()};
  UiStrView music_overlay_style_opt{latter_core::kPrefDefaultMusicOverlayStyle.data(), latter_core::kPrefDefaultMusicOverlayStyle.size()};
  UiStrView music_auto_open_opt{latter_core::kPrefDefaultMusicAutoOpen.data(), latter_core::kPrefDefaultMusicAutoOpen.size()};
  UiStrView music_fullscreen_art_opt{latter_core::kPrefDefaultAlbumArtStyle.data(), latter_core::kPrefDefaultAlbumArtStyle.size()};
  UiStrView music_album_sort_opt{latter_core::kPrefDefaultAlbumSortOrder.data(), latter_core::kPrefDefaultAlbumSortOrder.size()};
  UiStrView music_artist_album_sort_opt{latter_core::kPrefDefaultArtistAlbumSortOrder.data(), latter_core::kPrefDefaultArtistAlbumSortOrder.size()};
  UiStrView music_podcast_sort_opt{latter_core::kPrefDefaultPodcastSortOrder.data(), latter_core::kPrefDefaultPodcastSortOrder.size()};
  UiStrView theme_palette_opt{latter_core::kPrefDefaultColorProfile.data(), latter_core::kPrefDefaultColorProfile.size()};
  UiStrView glass_style_opt{latter_core::kPrefDefaultGlassStyle.data(), latter_core::kPrefDefaultGlassStyle.size()};
  UiStrView tap_wake_opt{latter_core::kPrefDefaultTapToWake.data(), latter_core::kPrefDefaultTapToWake.size()};
  UiStrView pickup_wake_opt{latter_core::kPrefDefaultPickupToWake.data(), latter_core::kPrefDefaultPickupToWake.size()};
  UiStrView display_off_timeout_opt{latter_core::kPrefDefaultBatteryScreenTimeout.data(), latter_core::kPrefDefaultBatteryScreenTimeout.size()};
  UiStrView screensaver_mode_opt{latter_core::kPrefDefaultScreensaverMode.data(), latter_core::kPrefDefaultScreensaverMode.size()};
  UiStrView screensaver_style_opt{latter_core::kPrefDefaultScreensaverStyle.data(), latter_core::kPrefDefaultScreensaverStyle.size()};
  UiStrView screensaver_docked_timeout_opt{latter_core::kPrefDefaultDockedIdleTimeout.data(), latter_core::kPrefDefaultDockedIdleTimeout.size()};
  UiStrView hibernate_timeout_opt{latter_core::kPrefDefaultBatteryHibernateTimeout.data(), latter_core::kPrefDefaultBatteryHibernateTimeout.size()};
  UiStrView bedtime_display_opt{latter_core::kPrefDefaultBedtimeDisplay.data(), latter_core::kPrefDefaultBedtimeDisplay.size()};
  UiStrView default_start_app_opt{latter_core::kPrefDefaultStartScreen.data(), latter_core::kPrefDefaultStartScreen.size()};
  UiStrView auto_brightness_opt{latter_core::kPrefDefaultAutoBrightnessMode.data(), latter_core::kPrefDefaultAutoBrightnessMode.size()};
  UiStrView va_wake_word_mode_opt{latter_core::kPrefDefaultWakeWordMode.data(), latter_core::kPrefDefaultWakeWordMode.size()};
  UiStrView va_visual_profile_opt{latter_core::kPrefDefaultVaVisualProfile.data(), latter_core::kPrefDefaultVaVisualProfile.size()};
  UiStrView ww_sens_okay_nabu_opt{latter_core::kPrefDefaultWwSensOkayNabu.data(), latter_core::kPrefDefaultWwSensOkayNabu.size()};
  UiStrView ww_sens_hey_luna_opt{latter_core::kPrefDefaultWwSensHeyLuna.data(), latter_core::kPrefDefaultWwSensHeyLuna.size()};
  UiStrView ww_sens_hey_jarvis_opt{latter_core::kPrefDefaultWwSensHeyJarvis.data(), latter_core::kPrefDefaultWwSensHeyJarvis.size()};
  UiStrView ww_sens_hey_mycroft_opt{latter_core::kPrefDefaultWwSensHeyMycroft.data(), latter_core::kPrefDefaultWwSensHeyMycroft.size()};
  UiStrView ww_sens_alexa_opt{latter_core::kPrefDefaultWwSensAlexa.data(), latter_core::kPrefDefaultWwSensAlexa.size()};

  // Numbers
  int manual_brightness_pct{latter_core::kPrefDefaultManualBrightness};
  int day_brightness_pct{latter_core::kPrefDefaultDayBrightness};
  int night_brightness_pct{latter_core::kPrefDefaultNightBrightness};
  int day_start_hr{latter_core::kPrefDefaultDayStartHour};
  int night_start_hr{latter_core::kPrefDefaultNightStartHour};

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

  const latter_core::DeviceRuntimeState *rs_ptr{nullptr};
  const latter_core::PersistedDevicePrefs *prefs_ptr{nullptr};

  inline const latter_core::DeviceRuntimeState &state() const noexcept {
    return rs_ptr ? *rs_ptr : latter_core::runtime_state();
  }
  inline const latter_core::PersistedDevicePrefs &prefs() const noexcept {
    return prefs_ptr ? *prefs_ptr : latter_core::persisted_prefs();
  }
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
  UiColor col_dark_gray{};
  UiColor col_gold{};
  UiColor col_red{};
  UiColor col_solaris_gold{};

  static SystemTakeoverState from_context(const UiFrameContext &ctx);
};

UiFrameContext &acquire_frame_context();
const SystemTakeoverState &acquire_takeover_state(const UiFrameContext &ctx);

inline int compute_hhmm_key(const UiWallTime &now) noexcept {
  return now.is_valid()
      ? (static_cast<int>(now.day_of_year) * 1440 + static_cast<int>(now.hour) * 60 + static_cast<int>(now.minute))
      : -1;
}

}  // namespace latter_ui
