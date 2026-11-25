#pragma once

// ==============================================================================
// Latter Core: Versioned Persisted Device Preferences & Brightness/DND PODs
// (include/state/state_prefs.h)
// ==============================================================================

#include "latter_traits.h"
#include "state/state_common.h"

namespace latter_core {

// Versioned Persisted Device Preferences (Variable-length ESP-IDF NVS POD Blob)
// ==============================================================================
static constexpr uint32_t kPersistedPrefsMagic = 0x4C505246U;  // "LPRF"
static constexpr uint16_t kPersistedPrefsVersion = 1U;
static constexpr const char *kPersistedPrefsNvsNamespace = "latter_prefs";
static constexpr const char *kPersistedPrefsNvsKey = "prefs";

enum class PrefKey : uint8_t {
  WAKE_WORD_MODE = 0,
  TAP_TO_WAKE = 1,
  PICKUP_TO_WAKE = 2,
  BATTERY_SCREEN_TIMEOUT = 3,
  DOCKED_IDLE_TIMEOUT = 4,
  BATTERY_HIBERNATE_TIMEOUT = 5,
  SCREENSAVER_MODE = 6,
  SCREENSAVER_STYLE = 7,
  AUTO_BRIGHTNESS_MODE = 8,
  MANUAL_BRIGHTNESS = 9,
  DAY_BRIGHTNESS = 10,
  NIGHT_BRIGHTNESS = 11,
  DAY_START_HOUR = 12,
  NIGHT_START_HOUR = 13,
  COLOR_PROFILE = 14,
  VA_VISUAL_PROFILE = 15,
  START_SCREEN = 16,
  CLOCK_FACE = 17,
  TIME_FORMAT = 18,
  WEATHER_BACKGROUND = 19,
  HOME_SUN = 20,
  HOME_MOON = 21,
  WAKE_GREETING = 22,
  ALBUM_ART_STYLE = 23,
  MUSIC_OVERLAY_STYLE = 24,
  ALBUM_SORT_ORDER = 25,
  ARTIST_ALBUM_SORT_ORDER = 26,
  PODCAST_SORT_ORDER = 27,
  WW_SENS_OKAY_NABU = 28,
  WW_SENS_HEY_LUNA = 29,
  WW_SENS_HEY_JARVIS = 30,
  WW_SENS_HEY_MYCROFT = 31,
  WW_SENS_ALEXA = 32,
  WAKE_SCREEN_ON_ASSIST = 33,
  ALERT_VOLUME = 34,
  VOLUME_STEP_SIZE = 35,
  GAME_SOUND_EFFECTS = 36,
  BLE_BEACON_ENABLED = 37,
  BLE_BEACON_MODE = 38,
  HOME_SCREEN_ENABLED = 39,
  RETURN_TO_HOME_TIMEOUT = 40,
  HOME_CLOCK_TAP_ACTION = 41,
  SHOW_UTC_CLOCK = 42,
  WORLD_CLOCKS = 43,
  ALARMS_SUMMARY = 44,
  LOCAL_CLOCK_LABEL = 45,
  TOOLS_ITEMS = 46,
  GAMES_ITEMS = 47,
  HOME_DATE_TAP_ACTION = 48,
  CALENDAR_LOOKAHEAD_DAYS = 49,
  CALENDAR_WEEK_START = 50,
  BEDTIME_ACTIVE = 51,
  BEDTIME_DISPLAY = 52,
  BEDTIME_SCHEDULE = 53,
  BEDTIME_START_HOUR = 54,
  BEDTIME_END_HOUR = 55,
  BEDTIME_BRIGHTNESS = 56,
  CAMERA_DEFAULT_VIEW = 57,
  CAMERA_CAROUSEL_INTERVAL = 58,
  CAMERA_FRAMING = 59,
  CAMERA_STREAM_MODE = 60,
  CAMERA_DOORBELL_POPUP = 61,
  SCREENSAVER_PHOTO_CLOCK = 62,
  WEATHER_APP_BACKGROUND = 63,
  DO_NOT_DISTURB = 64,
  MUSIC_AUTO_OPEN = 65,
  GLASS_STYLE = 66,
  HOME_SHOW_DATE = 67,
  HOME_SHOW_TIME = 68,
  HOME_SHOW_WEATHER = 69,
  HOME_WEATHER_TAP_ACTION = 70,
  HOME_SHOW_BATTERY = 71,
  COUNT = 72,
};
static_assert(static_cast<uint8_t>(PrefKey::COUNT) < 128U,
              "PrefKey::COUNT must remain < 128 to fit in 128-bit PrefMask.");

struct PrefMask {
  uint64_t lo = 0ULL;
  uint64_t hi = 0ULL;

  constexpr PrefMask() noexcept = default;
  constexpr PrefMask(uint64_t lo_v, uint64_t hi_v = 0ULL) noexcept : lo(lo_v), hi(hi_v) {}

  constexpr bool empty() const noexcept { return lo == 0ULL && hi == 0ULL; }
  constexpr bool any() const noexcept { return !empty(); }
  constexpr void clear() noexcept { lo = 0ULL; hi = 0ULL; }

  constexpr PrefMask operator|(const PrefMask &o) const noexcept { return {lo | o.lo, hi | o.hi}; }
  constexpr PrefMask operator&(const PrefMask &o) const noexcept { return {lo & o.lo, hi & o.hi}; }
  constexpr PrefMask operator~() const noexcept { return {~lo, ~hi}; }
  constexpr PrefMask &operator|=(const PrefMask &o) noexcept {
    lo |= o.lo;
    hi |= o.hi;
    return *this;
  }
  constexpr PrefMask &operator&=(const PrefMask &o) noexcept {
    lo &= o.lo;
    hi &= o.hi;
    return *this;
  }
  constexpr bool operator==(const PrefMask &o) const noexcept = default;
  constexpr bool operator!=(const PrefMask &o) const noexcept = default;
  constexpr bool operator==(uint64_t v) const noexcept { return lo == v && hi == 0ULL; }
  constexpr bool operator!=(uint64_t v) const noexcept { return !(*this == v); }
  constexpr PrefMask &operator=(uint64_t v) noexcept {
    lo = v;
    hi = 0ULL;
    return *this;
  }
  constexpr explicit operator bool() const noexcept { return any(); }
};
static_assert(std::is_trivially_copyable_v<PrefMask>,
              "PrefMask must remain trivially copyable for PersistedDevicePrefs POD.");

inline constexpr PrefMask pref_key_bit(PrefKey k) noexcept {
  const uint8_t idx = static_cast<uint8_t>(k);
  return (idx < 64U) ? PrefMask{1ULL << idx, 0ULL}
                     : PrefMask{0ULL, 1ULL << (idx - 64U)};
}

inline constexpr PrefMask pref_mask_all(uint8_t count) noexcept {
  if (count == 0U) return {0ULL, 0ULL};
  if (count < 64U) return {(1ULL << count) - 1ULL, 0ULL};
  if (count == 64U) return {~0ULL, 0ULL};
  return {~0ULL, (count < 128U) ? ((1ULL << (count - 64U)) - 1ULL) : ~0ULL};
}

// Side-effect bitmask returned when device_prefs or local preferences mutate.
static constexpr uint32_t kPrefEffectNone              = 0U;
static constexpr uint32_t kPrefEffectWakeWordSync      = 1U << 0;
static constexpr uint32_t kPrefEffectWakeWordSens      = 1U << 1;
static constexpr uint32_t kPrefEffectBrightness        = 1U << 2;
static constexpr uint32_t kPrefEffectSleepTimer        = 1U << 3;
static constexpr uint32_t kPrefEffectScreensaverChange = 1U << 4;
static constexpr uint32_t kPrefEffectThemePreset       = 1U << 5;
static constexpr uint32_t kPrefEffectPowerPolicy       = 1U << 6;
static constexpr uint32_t kPrefEffectRedraw            = 1U << 7;
static constexpr uint32_t kPrefEffectNavPolicy         = 1U << 8;
static constexpr uint32_t kPrefEffectBedtime           = 1U << 9;
static constexpr uint32_t kPrefEffectWeatherSky        = 1U << 10;

enum class MusicAutoOpenMode : uint8_t {
  NowPlaying = 0,
  Lyrics = 1,
  Off = 2,
};

struct PersistedAlarmPref {
  uint8_t hour = 7;
  uint8_t minute = 0;
  bool enabled = false;
  uint8_t recurrence = 1;  // 0 = Daily, 1 = Weekdays, 2 = Weekends, 3 = Once
};

struct PersistedDevicePrefs {
  uint32_t magic = kPersistedPrefsMagic;
  uint16_t version = kPersistedPrefsVersion;
  bool clock_face_digital = (kPrefDefaultClockFaceIdx != 1U);
  bool is_24h_format = (kPrefDefaultTimeFormatIdx == 1U);
  bool defaults_seeded = false;

  // Tier 2 + Tier 3 Device Preferences (defaults wired directly to generated protocol/latter_v3.json specs)
  uint8_t wake_word_mode = kPrefDefaultWakeWordModeIdx;
  uint8_t tap_to_wake = kPrefDefaultTapToWakeIdx;
  uint8_t pickup_to_wake = kPrefDefaultPickupToWakeIdx;
  uint8_t battery_screen_timeout = kPrefDefaultBatteryScreenTimeoutIdx;
  uint8_t docked_idle_timeout = kPrefDefaultDockedIdleTimeoutIdx;
  uint8_t battery_hibernate_timeout = kPrefDefaultBatteryHibernateTimeoutIdx;
  uint8_t screensaver_mode = kPrefDefaultScreensaverModeIdx;
  uint8_t screensaver_style = kPrefDefaultScreensaverStyleIdx;
  bool screensaver_photo_clock = kPrefDefaultScreensaverPhotoClock;
  uint8_t auto_brightness_mode = kPrefDefaultAutoBrightnessModeIdx;
  uint8_t manual_brightness = static_cast<uint8_t>(kPrefDefaultManualBrightness);
  uint8_t day_brightness = static_cast<uint8_t>(kPrefDefaultDayBrightness);
  uint8_t night_brightness = static_cast<uint8_t>(kPrefDefaultNightBrightness);
  uint8_t day_start_hour = static_cast<uint8_t>(kPrefDefaultDayStartHour);
  uint8_t night_start_hour = static_cast<uint8_t>(kPrefDefaultNightStartHour);
  uint8_t color_profile = kPrefDefaultColorProfileIdx;
  uint8_t va_visual_profile = kPrefDefaultVaVisualProfileIdx;
  uint8_t start_screen = kPrefDefaultStartScreenIdx;
  uint8_t clock_face = kPrefDefaultClockFaceIdx;
  uint8_t time_format = kPrefDefaultTimeFormatIdx;
  uint8_t home_clock_tap_action = kPrefDefaultHomeClockTapActionIdx;
  uint8_t home_date_tap_action = kPrefDefaultHomeDateTapActionIdx;
  uint8_t calendar_lookahead_days = static_cast<uint8_t>(kPrefDefaultCalendarLookaheadDays);
  uint8_t calendar_week_start = kPrefDefaultCalendarWeekStartIdx;
  bool show_utc_clock = kPrefDefaultShowUtcClock;
  char local_clock_label[33]{};            // Optional user-defined label shown above local time (<= 32 bytes)
  char world_clocks_spec[260]{};           // Up to 4 semicolon-delimited '<label>|<iana>' World Clock entries
  bool weather_background = kPrefDefaultWeatherBackground;
  bool weather_app_background = kPrefDefaultWeatherAppBackground;
  bool home_sun = kPrefDefaultHomeSun;
  bool home_moon = kPrefDefaultHomeMoon;
  bool wake_greeting = kPrefDefaultWakeGreeting;
  uint8_t album_art_style = kPrefDefaultAlbumArtStyleIdx;
  uint8_t music_overlay_style = kPrefDefaultMusicOverlayStyleIdx;
  uint8_t album_sort_order = kPrefDefaultAlbumSortOrderIdx;
  uint8_t artist_album_sort_order = kPrefDefaultArtistAlbumSortOrderIdx;
  uint8_t podcast_sort_order = kPrefDefaultPodcastSortOrderIdx;
  uint8_t ww_sens_okay_nabu = kPrefDefaultWwSensOkayNabuIdx;
  uint8_t ww_sens_hey_luna = kPrefDefaultWwSensHeyLunaIdx;
  uint8_t ww_sens_hey_jarvis = kPrefDefaultWwSensHeyJarvisIdx;
  uint8_t ww_sens_hey_mycroft = kPrefDefaultWwSensHeyMycroftIdx;
  uint8_t ww_sens_alexa = kPrefDefaultWwSensAlexaIdx;
  bool wake_screen_on_assist = kPrefDefaultWakeScreenOnAssist;
  uint8_t alert_volume = static_cast<uint8_t>(kPrefDefaultAlertVolume);
  uint8_t volume_step_size = static_cast<uint8_t>(kPrefDefaultVolumeStepSize);
  uint8_t speaker_volume_pct = 60;         // 10..100 (%) local speaker boot restore level
  bool game_sound_effects = kPrefDefaultGameSoundEffects;
  bool ble_beacon_enabled = false;         // Off until Latter enables native_ble_tracking
  uint8_t ble_beacon_mode = kPrefDefaultBleBeaconModeIdx;
  bool home_screen_enabled = kPrefDefaultHomeScreenEnabled;
  uint8_t return_to_home_timeout = kPrefDefaultReturnToHomeTimeoutIdx;

  // Two-way Latter Protocol v3 sync tracking
  uint32_t prefs_seq = 0;
  PrefMask dirty_mask{};

  uint32_t custom_theme_rgb = 0x3898FFU;
  int32_t high_score_marbles = 0;
  int32_t high_score_archery = 0;
  int32_t high_score_bricks = 0;
  int32_t high_score_chomp = 0;
  int32_t high_score_blocks = 0;
  int32_t high_score_snake = 0;
  PersistedAlarmPref alarms[3] = {
      {7, 0, false, 1},
      {8, 30, false, 2},
      {9, 0, false, 2},
  };
  char tools_items[80]{};
  char games_items[64]{};
  bool bedtime_active = kPrefDefaultBedtimeActive;
  uint8_t bedtime_display = kPrefDefaultBedtimeDisplayIdx;
  uint8_t bedtime_brightness = static_cast<uint8_t>(kPrefDefaultBedtimeBrightness);
  bool bedtime_schedule = kPrefDefaultBedtimeSchedule;
  uint8_t bedtime_start_hour = static_cast<uint8_t>(kPrefDefaultBedtimeStartHour);
  uint8_t bedtime_end_hour = static_cast<uint8_t>(kPrefDefaultBedtimeEndHour);
  uint8_t camera_default_view = kPrefDefaultCameraDefaultViewIdx;
  uint8_t camera_carousel_interval = kPrefDefaultCameraCarouselIntervalIdx;
  uint8_t camera_framing = kPrefDefaultCameraFramingIdx;
  uint8_t camera_stream_mode = kPrefDefaultCameraStreamModeIdx;
  uint8_t camera_doorbell_popup = kPrefDefaultCameraDoorbellPopupIdx;
  bool do_not_disturb = kPrefDefaultDoNotDisturb;
  uint8_t music_auto_open = kPrefDefaultMusicAutoOpenIdx;
  int32_t high_score_drop_four = 0;        // Appended v3.0+ tail field: Drop Four AI streak high score
  uint8_t glass_style = kPrefDefaultGlassStyleIdx;
  bool home_show_date = kPrefDefaultHomeShowDate;
  bool home_show_time = kPrefDefaultHomeShowTime;
  bool home_show_weather = kPrefDefaultHomeShowWeather;
  uint8_t home_weather_tap_action = kPrefDefaultHomeWeatherTapActionIdx;
  bool home_show_battery = kPrefDefaultHomeShowBattery;

  // Zero-size constexpr typed preference accessors (preserves exact NVS POD layout)
  constexpr GlassStyle glass_style_enum() const noexcept {
    return static_cast<GlassStyle>((glass_style < kPrefOptsGlassStyle.size()) ? glass_style : kPrefDefaultGlassStyleIdx);
  }
  constexpr MusicAutoOpenMode music_auto_open_enum() const noexcept {
    return static_cast<MusicAutoOpenMode>((music_auto_open < kPrefOptsMusicAutoOpen.size()) ? music_auto_open : kPrefDefaultMusicAutoOpenIdx);
  }
  constexpr WakeWordMode wake_word_mode_enum() const noexcept {
    return static_cast<WakeWordMode>((wake_word_mode < kPrefOptsWakeWordMode.size()) ? wake_word_mode : kPrefDefaultWakeWordModeIdx);
  }
  constexpr TapWakeMode tap_to_wake_enum() const noexcept {
    return static_cast<TapWakeMode>((tap_to_wake < kPrefOptsTapToWake.size()) ? tap_to_wake : kPrefDefaultTapToWakeIdx);
  }
  constexpr WakePolicyMode pickup_to_wake_enum() const noexcept {
    return static_cast<WakePolicyMode>((pickup_to_wake < kPrefOptsPickupToWake.size()) ? pickup_to_wake : kPrefDefaultPickupToWakeIdx);
  }
  constexpr ScreensaverMode screensaver_mode_enum() const noexcept {
    return static_cast<ScreensaverMode>((screensaver_mode < kPrefOptsScreensaverMode.size()) ? screensaver_mode : kPrefDefaultScreensaverModeIdx);
  }
  constexpr ScreensaverStyle screensaver_style_enum() const noexcept {
    return static_cast<ScreensaverStyle>((screensaver_style < kPrefOptsScreensaverStyle.size()) ? screensaver_style : kPrefDefaultScreensaverStyleIdx);
  }
  constexpr AutoBrightnessMode auto_brightness_enum() const noexcept {
    return static_cast<AutoBrightnessMode>((auto_brightness_mode < kPrefOptsAutoBrightnessMode.size()) ? auto_brightness_mode : kPrefDefaultAutoBrightnessModeIdx);
  }
  constexpr ColorProfilePref color_profile_enum() const noexcept {
    return static_cast<ColorProfilePref>((color_profile < kPrefOptsColorProfile.size()) ? color_profile : kPrefDefaultColorProfileIdx);
  }
  constexpr VaVisualStyle va_style() const noexcept {
    return static_cast<VaVisualStyle>((va_visual_profile < kPrefOptsVaVisualProfile.size()) ? va_visual_profile : kPrefDefaultVaVisualProfileIdx);
  }
  constexpr ClockFaceStyle clock_face_style() const noexcept {
    return static_cast<ClockFaceStyle>((clock_face < kPrefOptsClockFace.size()) ? clock_face : kPrefDefaultClockFaceIdx);
  }
  constexpr uint32_t battery_screen_timeout_ms() const noexcept {
    constexpr uint32_t kMs[6] = {10000U, 15000U, 30000U, 60000U, 120000U, 0U};
    static_assert(kPrefOptsBatteryScreenTimeout.size() == 6U);
    return kMs[(battery_screen_timeout < kPrefOptsBatteryScreenTimeout.size())
                   ? battery_screen_timeout
                   : kPrefDefaultBatteryScreenTimeoutIdx];
  }
  constexpr uint32_t docked_idle_timeout_ms() const noexcept {
    constexpr uint32_t kMs[7] = {10000U, 15000U, 30000U, 60000U, 120000U, 300000U, 0U};
    static_assert(kPrefOptsDockedIdleTimeout.size() == 7U);
    return kMs[(docked_idle_timeout < kPrefOptsDockedIdleTimeout.size())
                   ? docked_idle_timeout
                   : kPrefDefaultDockedIdleTimeoutIdx];
  }
  constexpr uint32_t screen_timeout_ms(bool docked_or_stationary) const noexcept {
    return docked_or_stationary ? docked_idle_timeout_ms() : battery_screen_timeout_ms();
  }
  constexpr uint32_t hibernate_delay_s() const noexcept {
    constexpr uint32_t kSec[5] = {300U, 600U, 900U, 1800U, 0U};
    static_assert(kPrefOptsBatteryHibernateTimeout.size() == 5U);
    return kSec[(battery_hibernate_timeout < kPrefOptsBatteryHibernateTimeout.size())
                    ? battery_hibernate_timeout
                    : kPrefDefaultBatteryHibernateTimeoutIdx];
  }
  constexpr uint8_t manual_brightness_pct() const noexcept {
    return (manual_brightness >= static_cast<uint8_t>(kPrefMinManualBrightness) &&
            manual_brightness <= static_cast<uint8_t>(kPrefMaxManualBrightness))
               ? manual_brightness
               : static_cast<uint8_t>(kPrefDefaultManualBrightness);
  }
  constexpr uint8_t day_brightness_pct() const noexcept {
    return (day_brightness >= static_cast<uint8_t>(kPrefMinDayBrightness) &&
            day_brightness <= static_cast<uint8_t>(kPrefMaxDayBrightness))
               ? day_brightness
               : static_cast<uint8_t>(kPrefDefaultDayBrightness);
  }
  constexpr uint8_t night_brightness_pct() const noexcept {
    return (night_brightness >= static_cast<uint8_t>(kPrefMinNightBrightness) &&
            night_brightness <= static_cast<uint8_t>(kPrefMaxNightBrightness))
               ? night_brightness
               : static_cast<uint8_t>(kPrefDefaultNightBrightness);
  }
  constexpr uint8_t bedtime_brightness_pct() const noexcept {
    return (bedtime_brightness >= static_cast<uint8_t>(kPrefMinBedtimeBrightness) &&
            bedtime_brightness <= static_cast<uint8_t>(kPrefMaxBedtimeBrightness))
               ? bedtime_brightness
               : static_cast<uint8_t>(kPrefDefaultBedtimeBrightness);
  }
  constexpr uint32_t return_to_home_ms() const noexcept {
    constexpr uint32_t kMs[7] = {15000U, 30000U, 60000U, 120000U, 300000U, 600000U, 0U};
    static_assert(kPrefOptsReturnToHomeTimeout.size() == 7U);
    return kMs[(return_to_home_timeout < kPrefOptsReturnToHomeTimeout.size())
                   ? return_to_home_timeout
                   : kPrefDefaultReturnToHomeTimeoutIdx];
  }
  constexpr uint32_t camera_carousel_interval_ms() const noexcept {
    constexpr uint32_t kMs[4] = {10000U, 15000U, 30000U, 60000U};
    static_assert(kPrefOptsCameraCarouselInterval.size() == 4U);
    return kMs[(camera_carousel_interval < kPrefOptsCameraCarouselInterval.size())
                   ? camera_carousel_interval
                   : kPrefDefaultCameraCarouselIntervalIdx];
  }
};

static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs> &&
                  std::is_standard_layout_v<PersistedDevicePrefs>,
              "PersistedDevicePrefs must remain standard-layout and trivially copyable for NVS.");
static constexpr size_t kPersistedPrefsMinSize =
    offsetof(PersistedDevicePrefs, glass_style) + sizeof(PersistedDevicePrefs::glass_style);
static_assert(kPersistedPrefsMinSize > offsetof(PersistedDevicePrefs, high_score_drop_four) &&
                  kPersistedPrefsMinSize <= sizeof(PersistedDevicePrefs),
              "Append-only NVS tail fields must be placed strictly at the end of PersistedDevicePrefs.");
static_assert(std::char_traits<char>::length(kDefaultToolsItems) < sizeof(PersistedDevicePrefs::tools_items),
              "PersistedDevicePrefs::tools_items must fit kDefaultToolsItems.");
static_assert(std::char_traits<char>::length(kDefaultGamesItems) < sizeof(PersistedDevicePrefs::games_items),
              "PersistedDevicePrefs::games_items must fit kDefaultGamesItems.");

struct DevicePrefsState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  uint32_t last_effects = 0;
  uint8_t weather_sim_condition = kPrefDefaultWeatherSimConditionIdx;  // Volatile; resets to "Live (Home Assistant)" on boot
  bool loaded = false;
  bool nvs_save_pending = false;
  uint32_t nvs_dirty_since_ms = 0;
};

inline DevicePrefsState &device_prefs_state() {
  static DevicePrefsState st{};
  return st;
}

inline PersistedDevicePrefs &persisted_prefs() {
  static PersistedDevicePrefs prefs{};
  return prefs;
}

inline PersistedDevicePrefs &device_prefs() {
  return persisted_prefs();
}

inline constexpr uint8_t kStartScreenTimeAppIdx =
    static_cast<uint8_t>(find_pref_option_index(std::span<const std::string_view>{kPrefOptsStartScreen}, "Time App"));
static_assert(kStartScreenTimeAppIdx == 2U, "Start screen Time App option index mismatch");

inline constexpr uint8_t snap_calendar_lookahead_days(int v) noexcept {
  if (v <= 10) return 7U;
  if (v <= 21) return 14U;
  if (v <= 45) return 30U;
  return 60U;
}

template <size_t N>
inline constexpr const char *pref_opt_cstr(const std::array<std::string_view, N> &opts,
                                           size_t idx, size_t fallback = 0U) noexcept {
  const std::string_view sv = pref_option_at_clamped(std::span<const std::string_view>{opts}, idx, fallback);
  return sv.empty() ? "" : sv.data();
}

inline const char *pref_wake_word_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsWakeWordMode, p.wake_word_mode, kPrefDefaultWakeWordModeIdx); }
inline const char *pref_tap_to_wake_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsTapToWake, p.tap_to_wake, kPrefDefaultTapToWakeIdx); }
inline const char *pref_pickup_to_wake_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsPickupToWake, p.pickup_to_wake, kPrefDefaultPickupToWakeIdx); }
inline const char *pref_battery_screen_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsBatteryScreenTimeout, p.battery_screen_timeout, kPrefDefaultBatteryScreenTimeoutIdx); }
inline const char *pref_docked_idle_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsDockedIdleTimeout, p.docked_idle_timeout, kPrefDefaultDockedIdleTimeoutIdx); }
inline const char *pref_battery_hibernate_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsBatteryHibernateTimeout, p.battery_hibernate_timeout, kPrefDefaultBatteryHibernateTimeoutIdx); }
inline const char *pref_screensaver_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsScreensaverMode, p.screensaver_mode, kPrefDefaultScreensaverModeIdx); }
inline const char *pref_screensaver_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsScreensaverStyle, p.screensaver_style, kPrefDefaultScreensaverStyleIdx); }
inline const char *pref_bedtime_display_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsBedtimeDisplay, p.bedtime_display, kPrefDefaultBedtimeDisplayIdx); }
inline const char *pref_glass_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsGlassStyle, p.glass_style, kPrefDefaultGlassStyleIdx); }
inline const char *effective_screensaver_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) {
  return p.bedtime_active ? "Ambient Clock" : pref_screensaver_style_cstr(p);
}

inline constexpr bool is_hour_in_bedtime_window(uint8_t hour, uint8_t start_h, uint8_t end_h) noexcept {
  if (hour > 23U || start_h > 23U || end_h > 23U || start_h == end_h) return false;
  if (start_h < end_h) return hour >= start_h && hour < end_h;
  return hour >= start_h || hour < end_h;
}

inline constexpr bool should_suppress_alert_in_bedtime(bool bedtime_active,
                                                       AlertPriority prio) noexcept {
  return bedtime_active && (prio != AlertPriority::Critical);
}

// Under Do Not Disturb, suppress normal/warning notifications & chimes;
// doorbell rings (!camera_entity.empty() / !camera_url.empty()) still show the silent
// visual popup (chime = false), while true critical non-doorbell security/safety alerts
// break through with both visual popup and audible chime.
inline constexpr bool should_suppress_alert_in_dnd(bool dnd_active,
                                                   int priority_level,
                                                   bool is_doorbell) noexcept {
  if (!dnd_active) return false;
  if (is_doorbell) return false;
  return priority_level != static_cast<int>(AlertPriority::Critical);
}

inline constexpr bool should_suppress_alert_in_dnd(bool dnd_active,
                                                   AlertPriority prio,
                                                   bool is_doorbell = false) noexcept {
  return should_suppress_alert_in_dnd(dnd_active, static_cast<int>(prio), is_doorbell);
}

inline constexpr bool should_suppress_alert_chime_in_dnd(bool dnd_active,
                                                         int priority_level,
                                                         bool is_doorbell) noexcept {
  if (!dnd_active) return false;
  if (is_doorbell) return true;
  return priority_level != static_cast<int>(AlertPriority::Critical);
}

struct BedtimeScheduleTracker {
  bool initialized = false;
  bool prev_in_window = false;
  uint8_t prev_start_h = 255U;
  uint8_t prev_end_h = 255U;

  void reset() noexcept { *this = BedtimeScheduleTracker{}; }

  // Returns -1 when no schedule edge occurred, 1 when Bedtime should turn ON, 0 when Bedtime should turn OFF.
  constexpr int evaluate_hour(bool schedule_enabled, uint8_t start_h, uint8_t end_h,
                              int current_hour) noexcept {
    if (!schedule_enabled) {
      initialized = false;
      return -1;
    }
    if (current_hour < 0 || current_hour > 23 || start_h > 23U || end_h > 23U || start_h == end_h) {
      return -1;
    }
    const bool in_win = is_hour_in_bedtime_window(static_cast<uint8_t>(current_hour), start_h, end_h);
    if (!initialized || start_h != prev_start_h || end_h != prev_end_h) {
      initialized = true;
      prev_in_window = in_win;
      prev_start_h = start_h;
      prev_end_h = end_h;
      return in_win ? 1 : 0;
    }
    if (in_win != prev_in_window) {
      prev_in_window = in_win;
      return in_win ? 1 : 0;
    }
    return -1;
  }
};

inline BedtimeScheduleTracker &bedtime_schedule_tracker() noexcept {
  static BedtimeScheduleTracker tr{};
  return tr;
}
inline constexpr float resolve_screensaver_brightness(float active_bri,
                                                      bool photo_frame = false) noexcept {
  const float b = std::clamp(active_bri, 0.0f, 1.0f);
  if (photo_frame) {
    return std::min(b, std::max(0.08f, b * 0.70f));
  }
  return std::min(b, std::max(0.05f, b * 0.45f));
}

inline constexpr float resolve_bedtime_ambient_brightness(float bedtime_bri) noexcept {
  const float b = std::clamp(bedtime_bri, 0.0f, 1.0f);
  return std::min(b, std::max(0.02f, b * 0.35f));
}

inline const char *pref_auto_brightness_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsAutoBrightnessMode, p.auto_brightness_mode, kPrefDefaultAutoBrightnessModeIdx); }

enum class ActiveBrightnessTargetKind : uint8_t {
  MANUAL = 0,
  DAY = 1,
  NIGHT = 2,
  BEDTIME = 3,
  AMBIENT = 4,
  AMBIENT_OVERRIDE = 5,
};

struct ActiveBrightnessTarget {
  ActiveBrightnessTargetKind kind{ActiveBrightnessTargetKind::MANUAL};
  PrefKey pref_key{PrefKey::MANUAL_BRIGHTNESS};
  uint8_t pct{80};
  const char *context_label{"Manual"};  // "Bedtime", "Manual", "Auto · Day", "Auto · Night", "Ambient", "Ambient · Override"
};


bool parse_area_ambient_light(std::string_view raw, int8_t &out_pct, std::string &out_lux);

inline ActiveBrightnessTarget resolve_active_brightness_target(
    const PersistedDevicePrefs &prefs,
    std::string_view sun_state,
    bool time_valid,
    int hour,
    int area_ambient_pct = -1,
    int ambient_override_pct = -1) noexcept {
  if (prefs.bedtime_active) {
    return {ActiveBrightnessTargetKind::BEDTIME, PrefKey::BEDTIME_BRIGHTNESS,
            prefs.bedtime_brightness_pct(), "Bedtime"};
  }
  const uint8_t lo_bri = std::min(prefs.night_brightness_pct(), prefs.day_brightness_pct());
  const uint8_t hi_bri = std::max(prefs.night_brightness_pct(), prefs.day_brightness_pct());
  switch (prefs.auto_brightness_enum()) {
    case AutoBrightnessMode::AmbientLight: {
      if (ambient_override_pct >= 1 && ambient_override_pct <= 100) {
        return {ActiveBrightnessTargetKind::AMBIENT_OVERRIDE, PrefKey::MANUAL_BRIGHTNESS,
                static_cast<uint8_t>(ambient_override_pct), "Ambient · Override"};
      }
      if (area_ambient_pct >= 0 && area_ambient_pct <= 100) {
        const int lo = static_cast<int>(lo_bri);
        const int hi = static_cast<int>(hi_bri);
        const int pct = std::clamp<int>(lo + (area_ambient_pct * (hi - lo) + 50) / 100, 1, 100);
        return {ActiveBrightnessTargetKind::AMBIENT, PrefKey::DAY_BRIGHTNESS,
                static_cast<uint8_t>(pct), "Ambient"};
      }
      // Missing/unavailable Area lux sensor: graceful Sun Synchronized / local-clock fallback
      const bool is_night = resolve_day_night_sky_is_night(sun_state, time_valid, hour);
      if (is_night) {
        return {ActiveBrightnessTargetKind::NIGHT, PrefKey::NIGHT_BRIGHTNESS,
                lo_bri, "Ambient · Night"};
      }
      return {ActiveBrightnessTargetKind::DAY, PrefKey::DAY_BRIGHTNESS,
              hi_bri, "Ambient · Day"};
    }
    case AutoBrightnessMode::SunSynchronized: {
      const bool is_night = resolve_day_night_sky_is_night(sun_state, time_valid, hour);
      if (is_night) {
        return {ActiveBrightnessTargetKind::NIGHT, PrefKey::NIGHT_BRIGHTNESS,
                lo_bri, "Auto · Night"};
      }
      return {ActiveBrightnessTargetKind::DAY, PrefKey::DAY_BRIGHTNESS,
              hi_bri, "Auto · Day"};
    }
    case AutoBrightnessMode::TimeSchedule: {
      const int day_h = static_cast<int>(prefs.day_start_hour);
      const int night_h = static_cast<int>(prefs.night_start_hour);
      bool is_day = true;
      if (time_valid) {
        if (day_h < night_h) {
          is_day = (hour >= day_h && hour < night_h);
        } else {
          is_day = (hour >= day_h || hour < night_h);
        }
      }
      if (is_day) {
        return {ActiveBrightnessTargetKind::DAY, PrefKey::DAY_BRIGHTNESS,
                hi_bri, "Auto · Day"};
      }
      return {ActiveBrightnessTargetKind::NIGHT, PrefKey::NIGHT_BRIGHTNESS,
              lo_bri, "Auto · Night"};
    }
    case AutoBrightnessMode::Disabled:
      break;
  }
  return {ActiveBrightnessTargetKind::MANUAL, PrefKey::MANUAL_BRIGHTNESS,
          prefs.manual_brightness_pct(), "Manual"};
}
inline const char *pref_color_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsColorProfile, p.color_profile, kPrefDefaultColorProfileIdx); }
inline const char *pref_va_visual_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsVaVisualProfile, p.va_visual_profile, kPrefDefaultVaVisualProfileIdx); }
inline const char *pref_start_screen_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsStartScreen, p.start_screen, kPrefDefaultStartScreenIdx); }
inline const char *pref_return_to_home_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsReturnToHomeTimeout, p.return_to_home_timeout, kPrefDefaultReturnToHomeTimeoutIdx); }
inline constexpr uint32_t return_to_home_timeout_ms(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return p.return_to_home_ms();
}
inline const char *pref_clock_face_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsClockFace, p.clock_face, kPrefDefaultClockFaceIdx); }
inline const char *pref_time_format_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsTimeFormat, p.time_format, kPrefDefaultTimeFormatIdx); }
inline const char *pref_home_clock_tap_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsHomeClockTapAction, p.home_clock_tap_action, kPrefDefaultHomeClockTapActionIdx); }
inline const char *pref_home_date_tap_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsHomeDateTapAction, p.home_date_tap_action, kPrefDefaultHomeDateTapActionIdx); }
inline const char *pref_home_weather_tap_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsHomeWeatherTapAction, p.home_weather_tap_action, kPrefDefaultHomeWeatherTapActionIdx); }
inline const char *pref_calendar_week_start_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsCalendarWeekStart, p.calendar_week_start, kPrefDefaultCalendarWeekStartIdx); }
inline const char *pref_album_art_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsAlbumArtStyle, p.album_art_style, kPrefDefaultAlbumArtStyleIdx); }
inline const char *pref_music_overlay_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsMusicOverlayStyle, p.music_overlay_style, kPrefDefaultMusicOverlayStyleIdx); }
inline const char *pref_music_auto_open_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsMusicAutoOpen, p.music_auto_open, kPrefDefaultMusicAutoOpenIdx); }
inline const char *pref_album_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsAlbumSortOrder, p.album_sort_order, kPrefDefaultAlbumSortOrderIdx); }
inline const char *pref_artist_album_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsArtistAlbumSortOrder, p.artist_album_sort_order, kPrefDefaultArtistAlbumSortOrderIdx); }
inline const char *pref_podcast_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsPodcastSortOrder, p.podcast_sort_order, kPrefDefaultPodcastSortOrderIdx); }
inline const char *pref_ww_sens_cstr(uint8_t idx) { return pref_opt_cstr(kPrefOptsWwSensOkayNabu, idx, kPrefDefaultWwSensOkayNabuIdx); }
inline const char *pref_ble_beacon_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsBleBeaconMode, p.ble_beacon_mode, kPrefDefaultBleBeaconModeIdx); }
inline const char *pref_weather_sim_cstr(const DevicePrefsState &st = device_prefs_state()) { return pref_opt_cstr(kPrefOptsWeatherSimCondition, st.weather_sim_condition, kPrefDefaultWeatherSimConditionIdx); }
inline const char *pref_camera_default_view_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsCameraDefaultView, p.camera_default_view, kPrefDefaultCameraDefaultViewIdx); }
inline const char *pref_camera_carousel_interval_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsCameraCarouselInterval, p.camera_carousel_interval, kPrefDefaultCameraCarouselIntervalIdx); }
inline const char *pref_camera_framing_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsCameraFraming, p.camera_framing, kPrefDefaultCameraFramingIdx); }
inline const char *pref_camera_stream_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsCameraStreamMode, p.camera_stream_mode, kPrefDefaultCameraStreamModeIdx); }
inline const char *pref_camera_doorbell_popup_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return pref_opt_cstr(kPrefOptsCameraDoorbellPopup, p.camera_doorbell_popup, kPrefDefaultCameraDoorbellPopupIdx); }


inline constexpr const char *const kCanonicalToolsItemTokens[6] = {
    "stopwatch", "level", "flashlight", "calculator", "dice", "coin_flip"};
inline constexpr const char *const kCanonicalGamesItemTokens[7] = {
    "blocks", "snake", "drop_four", "marbles", "archery", "bricks", "chomp"};

inline constexpr int tools_subpage_for_token(std::string_view tok) noexcept {
  if (tok == "stopwatch") return ToolsSubpage::STOPWATCH;
  if (tok == "level") return ToolsSubpage::LEVEL;
  if (tok == "flashlight") return ToolsSubpage::FLASHLIGHT;
  if (tok == "calculator") return ToolsSubpage::CALCULATOR;
  if (tok == "dice") return ToolsSubpage::DICE;
  if (tok == "coin_flip") return ToolsSubpage::COIN_FLIP;
  return 0;
}

inline constexpr const char *tools_launcher_label_for_subpage(int subpage_id) noexcept {
  switch (subpage_id) {
    case ToolsSubpage::STOPWATCH: return "Stopwatch";
    case ToolsSubpage::LEVEL: return "Level";
    case ToolsSubpage::FLASHLIGHT: return "Flashlight";
    case ToolsSubpage::CALCULATOR: return "Calculator";
    case ToolsSubpage::DICE: return "Dice Roll";
    case ToolsSubpage::COIN_FLIP: return "Coin Flip";
    default: return "";
  }
}

// 1-based game IDs matching NavRuntimeState::active_game:
// 1=Marbles (universal), 2=Archery (IMU), 3=Bricks (IMU), 4=Chomp (IMU),
// 5=Blocks (non-IMU), 6=Snake (non-IMU), 7=Drop Four (non-IMU).
inline constexpr int game_id_for_token(std::string_view tok) noexcept {
  if (tok == "marbles") return 1;
  if (tok == "archery") return 2;
  if (tok == "bricks") return 3;
  if (tok == "chomp") return 4;
  if (tok == "blocks") return 5;
  if (tok == "snake") return 6;
  if (tok == "drop_four") return 7;
  return 0;
}

inline constexpr bool game_supported_for_imu(int game_id, bool has_imu) noexcept {
  if (game_id == 1) return true;                    // Marbles: universal
  if (game_id >= 2 && game_id <= 4) return has_imu; // Archery, Bricks, Chomp: IMU-only
  if (game_id >= 5 && game_id <= 7) return !has_imu; // Blocks, Snake, Drop Four: non-IMU touch-only
  return false;
}


std::string normalize_tools_items_csv(std::string_view sv);
std::string normalize_games_items_csv(std::string_view sv);

inline std::string_view effective_tools_items_sv(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return (p.tools_items[0] != '\0') ? std::string_view(p.tools_items) : std::string_view(kDefaultToolsItems);
}

inline std::string_view effective_games_items_sv(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return (p.games_items[0] != '\0') ? std::string_view(p.games_items) : std::string_view(kDefaultGamesItems);
}


inline void clamp_prefs_to_capabilities(PersistedDevicePrefs &p = persisted_prefs()) {
  auto clamp_opt = []<size_t N>(uint8_t &slot, const std::array<std::string_view, N> &opts,
                                uint8_t def_idx) noexcept {
    if (slot >= opts.size()) slot = def_idx;
  };
  auto clamp_num = [](uint8_t &slot, DevicePrefsField field, int32_t lo, int32_t hi) noexcept {
    if (const auto *spec = find_pref_contract(field)) {
      slot = static_cast<uint8_t>(clamp_pref_number(*spec, static_cast<int32_t>(slot)));
    } else {
      slot = static_cast<uint8_t>(std::clamp<int32_t>(slot, lo, hi));
    }
  };
  auto clamp_hour = [](uint8_t &slot, int32_t max_h, int32_t def_h) noexcept {
    if (slot > static_cast<uint8_t>(max_h)) slot = static_cast<uint8_t>(def_h);
  };

  clamp_opt(p.wake_word_mode, kPrefOptsWakeWordMode, kPrefDefaultWakeWordModeIdx);
  clamp_opt(p.tap_to_wake, kPrefOptsTapToWake, kPrefDefaultTapToWakeIdx);
  clamp_opt(p.pickup_to_wake, kPrefOptsPickupToWake, kPrefDefaultPickupToWakeIdx);
  clamp_opt(p.battery_screen_timeout, kPrefOptsBatteryScreenTimeout, kPrefDefaultBatteryScreenTimeoutIdx);
  clamp_opt(p.docked_idle_timeout, kPrefOptsDockedIdleTimeout, kPrefDefaultDockedIdleTimeoutIdx);
  clamp_opt(p.battery_hibernate_timeout, kPrefOptsBatteryHibernateTimeout, kPrefDefaultBatteryHibernateTimeoutIdx);
  clamp_opt(p.screensaver_mode, kPrefOptsScreensaverMode, kPrefDefaultScreensaverModeIdx);
  clamp_opt(p.screensaver_style, kPrefOptsScreensaverStyle, kPrefDefaultScreensaverStyleIdx);
  clamp_opt(p.auto_brightness_mode, kPrefOptsAutoBrightnessMode, kPrefDefaultAutoBrightnessModeIdx);
  clamp_num(p.manual_brightness, DevicePrefsField::MANUAL_BRIGHTNESS, kPrefMinManualBrightness, kPrefMaxManualBrightness);
  clamp_num(p.day_brightness, DevicePrefsField::DAY_BRIGHTNESS, kPrefMinDayBrightness, kPrefMaxDayBrightness);
  clamp_num(p.night_brightness, DevicePrefsField::NIGHT_BRIGHTNESS, kPrefMinNightBrightness, kPrefMaxNightBrightness);
  clamp_num(p.bedtime_brightness, DevicePrefsField::BEDTIME_BRIGHTNESS, kPrefMinBedtimeBrightness, kPrefMaxBedtimeBrightness);
  clamp_hour(p.day_start_hour, kPrefMaxDayStartHour, kPrefDefaultDayStartHour);
  clamp_hour(p.night_start_hour, kPrefMaxNightStartHour, kPrefDefaultNightStartHour);
  clamp_opt(p.color_profile, kPrefOptsColorProfile, kPrefDefaultColorProfileIdx);
  clamp_opt(p.va_visual_profile, kPrefOptsVaVisualProfile, kPrefDefaultVaVisualProfileIdx);
  clamp_opt(p.start_screen, kPrefOptsStartScreen, kPrefDefaultStartScreenIdx);
  if (!p.home_screen_enabled && p.start_screen == kPrefDefaultStartScreenIdx) {
    p.start_screen = kStartScreenTimeAppIdx;  // Single-app mode cannot start on Home -> Time App
  }
  clamp_opt(p.return_to_home_timeout, kPrefOptsReturnToHomeTimeout, kPrefDefaultReturnToHomeTimeoutIdx);
  clamp_opt(p.clock_face, kPrefOptsClockFace, kPrefDefaultClockFaceIdx);
  clamp_opt(p.time_format, kPrefOptsTimeFormat, kPrefDefaultTimeFormatIdx);
  clamp_opt(p.home_clock_tap_action, kPrefOptsHomeClockTapAction, kPrefDefaultHomeClockTapActionIdx);
  clamp_opt(p.home_date_tap_action, kPrefOptsHomeDateTapAction, kPrefDefaultHomeDateTapActionIdx);
  clamp_opt(p.home_weather_tap_action, kPrefOptsHomeWeatherTapAction, kPrefDefaultHomeWeatherTapActionIdx);
  p.calendar_lookahead_days = snap_calendar_lookahead_days(p.calendar_lookahead_days);
  clamp_opt(p.calendar_week_start, kPrefOptsCalendarWeekStart, kPrefDefaultCalendarWeekStartIdx);
  clamp_opt(p.bedtime_display, kPrefOptsBedtimeDisplay, kPrefDefaultBedtimeDisplayIdx);
  clamp_hour(p.bedtime_start_hour, kPrefMaxBedtimeStartHour, kPrefDefaultBedtimeStartHour);
  clamp_hour(p.bedtime_end_hour, kPrefMaxBedtimeEndHour, kPrefDefaultBedtimeEndHour);
  p.local_clock_label[sizeof(p.local_clock_label) - 1] = '\0';
  p.world_clocks_spec[sizeof(p.world_clocks_spec) - 1] = '\0';
  p.tools_items[sizeof(p.tools_items) - 1] = '\0';
  p.games_items[sizeof(p.games_items) - 1] = '\0';
  copy_bounded_cstr(p.tools_items, normalize_tools_items_csv(p.tools_items));
  copy_bounded_cstr(p.games_items, normalize_games_items_csv(p.games_items));
  clamp_opt(p.album_art_style, kPrefOptsAlbumArtStyle, kPrefDefaultAlbumArtStyleIdx);
  clamp_opt(p.music_overlay_style, kPrefOptsMusicOverlayStyle, kPrefDefaultMusicOverlayStyleIdx);
  clamp_opt(p.music_auto_open, kPrefOptsMusicAutoOpen, kPrefDefaultMusicAutoOpenIdx);
  clamp_opt(p.album_sort_order, kPrefOptsAlbumSortOrder, kPrefDefaultAlbumSortOrderIdx);
  clamp_opt(p.artist_album_sort_order, kPrefOptsArtistAlbumSortOrder, kPrefDefaultArtistAlbumSortOrderIdx);
  clamp_opt(p.podcast_sort_order, kPrefOptsPodcastSortOrder, kPrefDefaultPodcastSortOrderIdx);
  clamp_opt(p.ww_sens_okay_nabu, kPrefOptsWwSensOkayNabu, kPrefDefaultWwSensOkayNabuIdx);
  clamp_opt(p.ww_sens_hey_luna, kPrefOptsWwSensHeyLuna, kPrefDefaultWwSensHeyLunaIdx);
  clamp_opt(p.ww_sens_hey_jarvis, kPrefOptsWwSensHeyJarvis, kPrefDefaultWwSensHeyJarvisIdx);
  clamp_opt(p.ww_sens_hey_mycroft, kPrefOptsWwSensHeyMycroft, kPrefDefaultWwSensHeyMycroftIdx);
  clamp_opt(p.ww_sens_alexa, kPrefOptsWwSensAlexa, kPrefDefaultWwSensAlexaIdx);
  clamp_num(p.alert_volume, DevicePrefsField::ALERT_VOLUME, kPrefMinAlertVolume, kPrefMaxAlertVolume);
  clamp_num(p.volume_step_size, DevicePrefsField::VOLUME_STEP_SIZE, kPrefMinVolumeStepSize, kPrefMaxVolumeStepSize);
  p.speaker_volume_pct = static_cast<uint8_t>(std::clamp<int>(p.speaker_volume_pct, 10, 100));
  clamp_opt(p.ble_beacon_mode, kPrefOptsBleBeaconMode, kPrefDefaultBleBeaconModeIdx);
  clamp_opt(p.camera_default_view, kPrefOptsCameraDefaultView, kPrefDefaultCameraDefaultViewIdx);
  clamp_opt(p.camera_carousel_interval, kPrefOptsCameraCarouselInterval, kPrefDefaultCameraCarouselIntervalIdx);
  clamp_opt(p.camera_framing, kPrefOptsCameraFraming, kPrefDefaultCameraFramingIdx);
  clamp_opt(p.camera_stream_mode, kPrefOptsCameraStreamMode, kPrefDefaultCameraStreamModeIdx);
  clamp_opt(p.camera_doorbell_popup, kPrefOptsCameraDoorbellPopup, kPrefDefaultCameraDoorbellPopupIdx);
  clamp_opt(p.glass_style, kPrefOptsGlassStyle, kPrefDefaultGlassStyleIdx);
#if !LATTER_HAS_HW_JPEG
  constexpr uint8_t kDoorbellSnapshotIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsCameraDoorbellPopup}, "Snapshot"));
  p.camera_doorbell_popup = kDoorbellSnapshotIdx;  // Live Video doorbell popup requires hardware JPEG decode
#endif
#if !LATTER_HAS_IMU || LATTER_IS_STATIONARY
  constexpr uint8_t kPickupDisabledIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsPickupToWake}, "Disabled"));
  p.pickup_to_wake = kPickupDisabledIdx;  // Disabled
#endif
#if !LATTER_HAS_TOUCH
  constexpr uint8_t kTapWakeDisabledIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsTapToWake}, "Disabled"));
  p.tap_to_wake = kTapWakeDisabledIdx;  // Disabled on touchless displays
#endif
#if !LATTER_HAS_BATTERY || LATTER_IS_STATIONARY
  constexpr uint8_t kHibernateDisabledIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsBatteryHibernateTimeout}, "Disabled"));
  p.battery_hibernate_timeout = kHibernateDisabledIdx;  // Disabled
#endif
  constexpr uint8_t kClockAnalogIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsClockFace}, "Modern Analog"));
  constexpr uint8_t kClockMinimalIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsClockFace}, "Minimal Digital"));
  constexpr uint8_t kTime24hIdx = static_cast<uint8_t>(
      find_pref_option_index(std::span<const std::string_view>{kPrefOptsTimeFormat}, "24-Hour"));
#if !LATTER_ROUND_DISPLAY
  if (p.clock_face == kClockAnalogIdx) p.clock_face = kClockMinimalIdx;  // Rectangular panels clamp Modern Analog -> Minimal Digital
#endif
  (void)kClockMinimalIdx;
  p.clock_face_digital = (p.clock_face != kClockAnalogIdx);
  p.is_24h_format = (p.time_format == kTime24hIdx);
}

inline void seed_hardware_sleep_defaults_for_traits(PersistedDevicePrefs &p, bool is_stationary,
                                                    bool is_wearable, bool is_round_display,
                                                    bool has_battery) noexcept {
  p.clock_face = is_round_display ? 1U : 0U;  // Modern Analog on round displays; Minimal Digital elsewhere
  p.clock_face_digital = (p.clock_face != 1U);
  if (is_stationary) {
    p.wake_word_mode = 1;
    p.tap_to_wake = 0;
    p.pickup_to_wake = 2;
    p.battery_screen_timeout = 5;
    p.docked_idle_timeout = 6;
    p.battery_hibernate_timeout = 4;
    p.ble_beacon_mode = 0;
    p.screensaver_mode = 2;  // "Never"
  } else if (is_wearable) {
    p.wake_word_mode = 0;
    p.tap_to_wake = 1;
    p.pickup_to_wake = 0;
    p.battery_screen_timeout = 1;
    p.docked_idle_timeout = 3;
    p.battery_hibernate_timeout = 2;
    p.ble_beacon_mode = 1;
    p.screensaver_mode = 0;  // "Docked Only"
    p.day_brightness = 80;
    p.night_brightness = 25;
  } else if (is_round_display && has_battery) {
    p.wake_word_mode = 0;
    p.tap_to_wake = 2;
    p.pickup_to_wake = 2;
    p.battery_screen_timeout = 2;
    p.docked_idle_timeout = 3;
    p.battery_hibernate_timeout = 2;
    p.ble_beacon_mode = 1;
    p.screensaver_mode = 0;  // "Docked Only"
  } else {
    p.wake_word_mode = 0;
    p.tap_to_wake = 0;
    p.pickup_to_wake = 0;
    p.battery_screen_timeout = 2;
    p.docked_idle_timeout = 3;
    p.battery_hibernate_timeout = 3;
    p.ble_beacon_mode = 1;
    p.screensaver_mode = 0;  // "Docked Only"
  }
}

inline void seed_hardware_sleep_defaults(PersistedDevicePrefs &p) {
  seed_hardware_sleep_defaults_for_traits(p, LATTER_IS_STATIONARY != 0, LATTER_IS_WEARABLE != 0,
                                          LATTER_ROUND_DISPLAY != 0, LATTER_HAS_BATTERY != 0);
}


// Append-only prefix loader: accepts any NVS blob in [kPersistedPrefsMinSize, 4096]
// (copies min(len, sizeof(PersistedDevicePrefs)), default-initializing newly appended tail fields
// on v3.0 -> v3.1+ upgrades and safely reading the v3.0 prefix across OTA rollbacks).
inline bool load_persisted_prefs_from_buffer(const void *buf, size_t len,
                                             PersistedDevicePrefs &out = persisted_prefs()) {
  if (buf == nullptr || len < kPersistedPrefsMinSize || len > 4096U) {
    return false;
  }
  PersistedDevicePrefs loaded{};
  std::memcpy(&loaded, buf, std::min(len, sizeof(PersistedDevicePrefs)));
  if (loaded.magic != kPersistedPrefsMagic || loaded.version != kPersistedPrefsVersion) {
    return false;
  }
  for (auto &a : loaded.alarms) {
    if (a.hour > 23) a.hour = 7;
    if (a.minute > 59) a.minute = 0;
    if (a.recurrence > 3) a.recurrence = 0;
  }
  clamp_prefs_to_capabilities(loaded);
  out = loaded;
  return true;
}


bool load_persisted_prefs();
void ensure_persisted_prefs_loaded();
bool save_persisted_prefs();

inline void schedule_persisted_prefs_save(uint32_t now_ms) {
  auto &st = device_prefs_state();
  if (!st.nvs_save_pending) {
    st.nvs_save_pending = true;
    st.nvs_dirty_since_ms = now_ms;
  }
}


bool flush_persisted_prefs_if_dirty();
bool tick_persisted_prefs_flush(uint32_t now_ms, uint32_t debounce_ms = 1500U);

void seed_default_prefs_if_needed(PersistedDevicePrefs &p = persisted_prefs());

inline constexpr std::array<DevicePrefsField, static_cast<size_t>(PrefKey::COUNT)> kPrefKeyDeviceFields = {{
    DevicePrefsField::WAKE_WORD_MODE,
    DevicePrefsField::TAP_TO_WAKE,
    DevicePrefsField::PICKUP_TO_WAKE,
    DevicePrefsField::BATTERY_SCREEN_TIMEOUT,
    DevicePrefsField::DOCKED_IDLE_TIMEOUT,
    DevicePrefsField::BATTERY_HIBERNATE_TIMEOUT,
    DevicePrefsField::SCREENSAVER_MODE,
    DevicePrefsField::SCREENSAVER_STYLE,
    DevicePrefsField::AUTO_BRIGHTNESS_MODE,
    DevicePrefsField::MANUAL_BRIGHTNESS,
    DevicePrefsField::DAY_BRIGHTNESS,
    DevicePrefsField::NIGHT_BRIGHTNESS,
    DevicePrefsField::DAY_START_HOUR,
    DevicePrefsField::NIGHT_START_HOUR,
    DevicePrefsField::COLOR_PROFILE,
    DevicePrefsField::VA_VISUAL_PROFILE,
    DevicePrefsField::START_SCREEN,
    DevicePrefsField::CLOCK_FACE,
    DevicePrefsField::TIME_FORMAT,
    DevicePrefsField::WEATHER_BACKGROUND,
    DevicePrefsField::HOME_SUN,
    DevicePrefsField::HOME_MOON,
    DevicePrefsField::WAKE_GREETING,
    DevicePrefsField::ALBUM_ART_STYLE,
    DevicePrefsField::MUSIC_OVERLAY_STYLE,
    DevicePrefsField::ALBUM_SORT_ORDER,
    DevicePrefsField::ARTIST_ALBUM_SORT_ORDER,
    DevicePrefsField::PODCAST_SORT_ORDER,
    DevicePrefsField::WW_SENS_OKAY_NABU,
    DevicePrefsField::WW_SENS_HEY_LUNA,
    DevicePrefsField::WW_SENS_HEY_JARVIS,
    DevicePrefsField::WW_SENS_HEY_MYCROFT,
    DevicePrefsField::WW_SENS_ALEXA,
    DevicePrefsField::WAKE_SCREEN_ON_ASSIST,
    DevicePrefsField::ALERT_VOLUME,
    DevicePrefsField::VOLUME_STEP_SIZE,
    DevicePrefsField::GAME_SOUND_EFFECTS,
    DevicePrefsField::BLE_BEACON_ENABLED,
    DevicePrefsField::BLE_BEACON_MODE,
    DevicePrefsField::HOME_SCREEN_ENABLED,
    DevicePrefsField::RETURN_TO_HOME_TIMEOUT,
    DevicePrefsField::HOME_CLOCK_TAP_ACTION,
    DevicePrefsField::SHOW_UTC_CLOCK,
    DevicePrefsField::WORLD_CLOCKS,
    DevicePrefsField::ALARMS_SUMMARY,
    DevicePrefsField::LOCAL_CLOCK_LABEL,
    DevicePrefsField::TOOLS_ITEMS,
    DevicePrefsField::GAMES_ITEMS,
    DevicePrefsField::HOME_DATE_TAP_ACTION,
    DevicePrefsField::CALENDAR_LOOKAHEAD_DAYS,
    DevicePrefsField::CALENDAR_WEEK_START,
    DevicePrefsField::BEDTIME_ACTIVE,
    DevicePrefsField::BEDTIME_DISPLAY,
    DevicePrefsField::BEDTIME_SCHEDULE,
    DevicePrefsField::BEDTIME_START_HOUR,
    DevicePrefsField::BEDTIME_END_HOUR,
    DevicePrefsField::BEDTIME_BRIGHTNESS,
    DevicePrefsField::CAMERA_DEFAULT_VIEW,
    DevicePrefsField::CAMERA_CAROUSEL_INTERVAL,
    DevicePrefsField::CAMERA_FRAMING,
    DevicePrefsField::CAMERA_STREAM_MODE,
    DevicePrefsField::CAMERA_DOORBELL_POPUP,
    DevicePrefsField::SCREENSAVER_PHOTO_CLOCK,
    DevicePrefsField::WEATHER_APP_BACKGROUND,
    DevicePrefsField::DO_NOT_DISTURB,
    DevicePrefsField::MUSIC_AUTO_OPEN,
    DevicePrefsField::GLASS_STYLE,
    DevicePrefsField::HOME_SHOW_DATE,
    DevicePrefsField::HOME_SHOW_TIME,
    DevicePrefsField::HOME_SHOW_WEATHER,
    DevicePrefsField::HOME_WEATHER_TAP_ACTION,
    DevicePrefsField::HOME_SHOW_BATTERY,
}};

inline constexpr const char *pref_key_name(PrefKey key) noexcept {
  const size_t idx = static_cast<size_t>(key);
  if (idx >= kPrefKeyDeviceFields.size()) return "";
  if (key == PrefKey::BLE_BEACON_ENABLED) return "ble_beacon_enabled";
  if (key == PrefKey::TOOLS_ITEMS) return "tools_items";
  if (key == PrefKey::GAMES_ITEMS) return "games_items";
  const DevicePrefsField field = kPrefKeyDeviceFields[idx];
  for (const auto &spec : kPrefContractSpecs) {
    if (spec.field == field) return spec.key.empty() ? "" : spec.key.data();
  }
  return "";
}
static_assert(std::string_view(pref_key_name(PrefKey::WAKE_WORD_MODE)) == "wake_word_mode");
static_assert(std::string_view(pref_key_name(PrefKey::BLE_BEACON_ENABLED)) == "ble_beacon_enabled");
static_assert(std::string_view(pref_key_name(PrefKey::TOOLS_ITEMS)) == "tools_items");
static_assert(std::string_view(pref_key_name(PrefKey::GAMES_ITEMS)) == "games_items");
static_assert(std::string_view(pref_key_name(PrefKey::GLASS_STYLE)) == "glass_style");
static_assert(std::string_view(pref_key_name(PrefKey::HOME_SHOW_DATE)) == "home_show_date");
static_assert(std::string_view(pref_key_name(PrefKey::HOME_SHOW_TIME)) == "home_show_time");
static_assert(std::string_view(pref_key_name(PrefKey::HOME_SHOW_WEATHER)) == "home_show_weather");
static_assert(std::string_view(pref_key_name(PrefKey::HOME_WEATHER_TAP_ACTION)) == "home_weather_tap_action");
static_assert(std::string_view(pref_key_name(PrefKey::HOME_SHOW_BATTERY)) == "home_show_battery");
std::string get_pref_value_str(PrefKey key, const PersistedDevicePrefs &p = persisted_prefs());
bool set_pref_by_key_str(PersistedDevicePrefs &p, PrefKey key, std::string_view sv,
                         PrefMask &changed_mask, uint32_t &effects_mask);
std::string encode_prefs_mask(PrefMask mask, const PersistedDevicePrefs &p = persisted_prefs());
std::string format_dirty_prefs(const PersistedDevicePrefs &p = persisted_prefs());
std::string build_hello_dirty_prefs(const PersistedDevicePrefs &p = persisted_prefs());
bool parse_v3_device_prefs_payload(std::string_view payload,
                                   uint32_t &out_effects,
                                   uint32_t now_ms = 0,
                                   PersistedDevicePrefs &p = persisted_prefs(),
                                   DevicePrefsState &st = device_prefs_state());

}  // namespace latter_core
