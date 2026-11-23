#pragma once

// ==============================================================================
// Latter Core SDK: Versioned Persisted Device Preferences & Brightness/DND PODs
// (include/state/state_prefs.h)
// ==============================================================================

#include "state/state_common.h"

namespace latter_ui {

// Versioned Persisted Device Preferences (Single ESPPreferenceObject POD Blob)
// ==============================================================================
static constexpr uint32_t kPersistedPrefsMagic = 0x4C505246U;  // "LPRF"
static constexpr uint16_t kPersistedPrefsVersion = 1U;
static constexpr uint32_t kPersistedPrefsHash = 0x4C415450U;   // "LATP"

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
  COUNT = 66,
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
  bool clock_face_digital = true;
  bool is_24h_format = false;
  bool defaults_seeded = false;

  // Tier 2 + Tier 3 Device Preferences (compact enum indices / integers / bools)
  uint8_t wake_word_mode = 0;              // 0..2 ("Docked Only", "Always On", "Disabled")
  uint8_t tap_to_wake = 2;                 // 0..3 ("Single Tap", "Double Tap", "Docked Only", "Disabled")
  uint8_t pickup_to_wake = 1;              // 0..2 ("Always On", "Docked Only", "Disabled")
  uint8_t battery_screen_timeout = 2;      // 0..5 ("10s", "15s", "30s", "1 min", "2 min", "Never")
  uint8_t docked_idle_timeout = 3;         // 0..6 ("10s", "15s", "30s", "1 min", "2 min", "5 min", "Never")
  uint8_t battery_hibernate_timeout = 2;   // 0..4 ("5 min", "10 min", "15 min", "30 min", "Disabled")
  uint8_t screensaver_mode = 0;            // 0..2 ("Docked Only", "Always", "Never")
  uint8_t screensaver_style = 0;           // 0..5 ("Ambient Clock", "Photo Frame", "Aurora", "Starfield", "Digital Rain", "Fireworks")
  bool screensaver_photo_clock = false;    // Optional subtle clock on Photo Frame screensaver
  uint8_t auto_brightness_mode = 1;        // 0..2 ("Disabled", "Sun Synchronized", "Time Schedule")
  uint8_t manual_brightness = 80;          // 1..100 (%)
  uint8_t day_brightness = 90;             // 1..100 (%)
  uint8_t night_brightness = 35;           // 1..100 (%)
  uint8_t day_start_hour = 7;              // 0..23
  uint8_t night_start_hour = 20;           // 0..23
  uint8_t color_profile = 0;               // 0..4 ("Steel Blue", "Pink", "Blue", "Green", "Custom")
  uint8_t va_visual_profile = 0;           // 0..4 ("Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave")
  uint8_t start_screen = 0;                // 0..12 ("Home", "Pinned", "Time App", "Calendar", "Clock Face", "Media", "Home Hub", "Area Controls", "Remote", "Weather", "Cameras", "Games", "Tools")
  uint8_t clock_face = 0;                  // 0..2 ("Minimal Digital", "Modern Analog", "Hybrid Digital")
  uint8_t time_format = 0;                 // 0..1 ("12-Hour (AM/PM)", "24-Hour")
  uint8_t home_clock_tap_action = 0;       // 0..1 ("Time App", "Clock Face")
  uint8_t home_date_tap_action = 1;        // 0..1 ("Day View", "Overview")
  uint8_t calendar_lookahead_days = 30;    // 7, 14, 30, or 60 days
  uint8_t calendar_week_start = 0;         // 0..1 ("Sunday", "Monday")
  bool show_utc_clock = false;             // Pin dedicated UTC World Clock row on Time App -> Clock tab
  char local_clock_label[33]{};            // Optional user-defined label shown above local time (<= 32 bytes)
  char world_clocks_spec[260]{};           // Up to 4 semicolon-delimited '<label>|<iana>' World Clock entries
  bool weather_background = true;
  bool weather_app_background = true;
  bool home_sun = true;
  bool home_moon = true;
  bool wake_greeting = true;
  uint8_t album_art_style = 0;             // 0..3 ("Full Screen", "Letterbox", "Standard", "None")
  uint8_t music_overlay_style = 1;         // 0..2 ("Art Only", "Art + Metadata", "Always Visible")
  uint8_t album_sort_order = 0;            // 0..2 ("Alphabetical", "Newest on Top", "Oldest on Top")
  uint8_t artist_album_sort_order = 0;     // 0..2 ("Oldest on Top", "Newest on Top", "Alphabetical")
  uint8_t podcast_sort_order = 0;          // 0..1 ("Newest on Top", "Oldest on Top")
  uint8_t ww_sens_okay_nabu = 1;           // 0..2 ("Slightly sensitive", "Moderately sensitive", "Very sensitive")
  uint8_t ww_sens_hey_luna = 1;
  uint8_t ww_sens_hey_jarvis = 1;
  uint8_t ww_sens_hey_mycroft = 1;
  uint8_t ww_sens_alexa = 1;
  bool wake_screen_on_assist = true;
  uint8_t alert_volume = 80;               // 20..100 (%)
  uint8_t volume_step_size = 5;            // 1..10 (%)
  uint8_t speaker_volume_pct = 60;         // 10..100 (%) local speaker boot restore level
  bool game_sound_effects = true;
  bool ble_beacon_enabled = false;         // Off until Latter enables native_ble_tracking
  uint8_t ble_beacon_mode = 0;             // 0..2 ("Handheld Only", "Handheld + Screen Off", "Always")
  bool home_screen_enabled = true;
  uint8_t return_to_home_timeout = 3;      // 0..6 ("15s", "30s", "1 min", "2 min", "5 min", "10 min", "Never")

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
  bool bedtime_active = false;
  uint8_t bedtime_display = 0;             // 0..1 ("Screen Off", "Ambient Clock")
  uint8_t bedtime_brightness = 15;         // 1..100 (%)
  bool bedtime_schedule = false;
  uint8_t bedtime_start_hour = 22;         // 0..23
  uint8_t bedtime_end_hour = 7;            // 0..23
  uint8_t camera_default_view = 0;         // 0..2 ("Grid", "Single", "Carousel")
  uint8_t camera_carousel_interval = 1;    // 0..3 ("10s", "15s", "30s", "60s")
  uint8_t camera_framing = 0;              // 0..1 ("Uncropped", "Fill")
  uint8_t camera_stream_mode = 0;          // 0..2 ("Auto", "Low Bandwidth", "Snapshots")
  uint8_t camera_doorbell_popup = 0;       // 0..1 ("Snapshot", "Live Video"; P4-only for Live Video)
  bool do_not_disturb = false;             // Appended v3.0+ tail field: Do Not Disturb mode
  uint8_t music_auto_open = 0;             // Appended v3.0+ tail field: 0..2 ("Now Playing", "Lyrics", "Off")
  int32_t high_score_drop_four = 0;        // Appended v3.0+ tail field: Drop Four AI streak high score

  // Zero-size constexpr typed preference accessors (preserves exact NVS POD layout)
  constexpr MusicAutoOpenMode music_auto_open_enum() const noexcept {
    return static_cast<MusicAutoOpenMode>((music_auto_open < 3U) ? music_auto_open : 0U);
  }
  constexpr WakeWordMode wake_word_mode_enum() const noexcept {
    return static_cast<WakeWordMode>((wake_word_mode < 3U) ? wake_word_mode : 0U);
  }
  constexpr TapWakeMode tap_to_wake_enum() const noexcept {
    return static_cast<TapWakeMode>((tap_to_wake < 4U) ? tap_to_wake : 2U);
  }
  constexpr WakePolicyMode pickup_to_wake_enum() const noexcept {
    return static_cast<WakePolicyMode>((pickup_to_wake < 3U) ? pickup_to_wake : 1U);
  }
  constexpr ScreensaverMode screensaver_mode_enum() const noexcept {
    return static_cast<ScreensaverMode>((screensaver_mode < 3U) ? screensaver_mode : 0U);
  }
  constexpr ScreensaverStyle screensaver_style_enum() const noexcept {
    return static_cast<ScreensaverStyle>((screensaver_style < 6U) ? screensaver_style : 0U);
  }
  constexpr AutoBrightnessMode auto_brightness_enum() const noexcept {
    return static_cast<AutoBrightnessMode>((auto_brightness_mode < 4U) ? auto_brightness_mode : 1U);
  }
  constexpr ColorProfilePref color_profile_enum() const noexcept {
    return static_cast<ColorProfilePref>((color_profile < 5U) ? color_profile : 0U);
  }
  constexpr VaVisualStyle va_style() const noexcept {
    return static_cast<VaVisualStyle>((va_visual_profile < 5U) ? va_visual_profile : 0U);
  }
  constexpr ClockFaceStyle clock_face_style() const noexcept {
    return static_cast<ClockFaceStyle>((clock_face < 3U) ? clock_face : 0U);
  }
  constexpr uint32_t battery_screen_timeout_ms() const noexcept {
    constexpr uint32_t kMs[6] = {10000U, 15000U, 30000U, 60000U, 120000U, 0U};
    return kMs[(battery_screen_timeout < 6U) ? battery_screen_timeout : 2U];
  }
  constexpr uint32_t docked_idle_timeout_ms() const noexcept {
    constexpr uint32_t kMs[7] = {10000U, 15000U, 30000U, 60000U, 120000U, 300000U, 0U};
    return kMs[(docked_idle_timeout < 7U) ? docked_idle_timeout : 3U];
  }
  constexpr uint32_t screen_timeout_ms(bool docked_or_stationary) const noexcept {
    return docked_or_stationary ? docked_idle_timeout_ms() : battery_screen_timeout_ms();
  }
  constexpr uint32_t hibernate_delay_s() const noexcept {
    constexpr uint32_t kSec[5] = {300U, 600U, 900U, 1800U, 0U};
    return kSec[(battery_hibernate_timeout < 5U) ? battery_hibernate_timeout : 2U];
  }
  constexpr uint8_t manual_brightness_pct() const noexcept {
    return (manual_brightness >= 1U && manual_brightness <= 100U) ? manual_brightness : 80U;
  }
  constexpr uint8_t day_brightness_pct() const noexcept {
    return (day_brightness >= 1U && day_brightness <= 100U) ? day_brightness : 90U;
  }
  constexpr uint8_t night_brightness_pct() const noexcept {
    return (night_brightness >= 1U && night_brightness <= 100U) ? night_brightness : 35U;
  }
  constexpr uint8_t bedtime_brightness_pct() const noexcept {
    return (bedtime_brightness >= 1U && bedtime_brightness <= 100U) ? bedtime_brightness : 15U;
  }
  constexpr uint32_t return_to_home_ms() const noexcept {
    constexpr uint32_t kMs[7] = {15000U, 30000U, 60000U, 120000U, 300000U, 600000U, 0U};
    return kMs[(return_to_home_timeout < 7U) ? return_to_home_timeout : 3U];
  }
  constexpr uint32_t camera_carousel_interval_ms() const noexcept {
    constexpr uint32_t kMs[4] = {10000U, 15000U, 30000U, 60000U};
    return kMs[(camera_carousel_interval < 4U) ? camera_carousel_interval : 1U];
  }
};

static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs> &&
                  std::is_standard_layout_v<PersistedDevicePrefs>,
              "PersistedDevicePrefs must remain standard-layout and trivially copyable for NVS.");
static constexpr size_t kPersistedPrefsMinSize = offsetof(PersistedDevicePrefs, do_not_disturb);
static_assert(kPersistedPrefsMinSize > offsetof(PersistedDevicePrefs, camera_doorbell_popup) &&
                  kPersistedPrefsMinSize < sizeof(PersistedDevicePrefs),
              "Append-only NVS tail fields must be placed strictly at the end of PersistedDevicePrefs.");
static_assert(std::char_traits<char>::length(kDefaultToolsItems) < sizeof(PersistedDevicePrefs::tools_items),
              "PersistedDevicePrefs::tools_items must fit kDefaultToolsItems.");
static_assert(std::char_traits<char>::length(kDefaultGamesItems) < sizeof(PersistedDevicePrefs::games_items),
              "PersistedDevicePrefs::games_items must fit kDefaultGamesItems.");

struct DevicePrefsState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  uint32_t last_effects = 0;
  uint8_t weather_sim_condition = 0;  // Volatile; resets to "Live (Home Assistant)" on boot
  bool loaded = false;
  bool nvs_save_pending = false;
  uint32_t nvs_dirty_since_ms = 0;
  std::string tools_items = kDefaultToolsItems;
  std::string games_items = kDefaultGamesItems;
  bool weather_app_background = true;
  bool do_not_disturb = false;
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

inline constexpr const char *const kWakeWordModeOpts[3] = {
    "Docked Only", "Always On", "Disabled"};
inline constexpr const char *const kTapWakeOpts[4] = {
    "Single Tap", "Double Tap", "Docked Only", "Disabled"};
inline constexpr const char *const kWakePolicyOpts[3] = {
    "Always On", "Docked Only", "Disabled"};
inline constexpr const char *const kBattScreenTimeoutOpts[6] = {
    "10s", "15s", "30s", "1 min", "2 min", "Never"};
inline constexpr const char *const kDockedIdleTimeoutOpts[7] = {
    "10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"};
inline constexpr const char *const kBattHibernateTimeoutOpts[5] = {
    "5 min", "10 min", "15 min", "30 min", "Disabled"};
inline constexpr const char *const kScreensaverModeOpts[3] = {
    "Docked Only", "Always", "Never"};
inline constexpr const char *const kScreensaverStyleOpts[6] = {
    "Ambient Clock", "Photo Frame", "Aurora", "Starfield", "Digital Rain", "Fireworks"};
inline constexpr const char *const kAutoBrightnessModeOpts[4] = {
    "Disabled", "Sun Synchronized", "Time Schedule", "Ambient Light"};
inline constexpr const char *const kColorProfileOpts[5] = {
    "Steel Blue", "Pink", "Blue", "Green", "Custom"};
inline constexpr const char *const kVaVisualProfileOpts[5] = {
    "Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"};
inline constexpr const char *const kStartScreenOpts[13] = {
    "Home", "Pinned", "Time App", "Calendar", "Clock Face", "Media", "Home Hub", "Area Controls",
    "Remote", "Weather", "Cameras", "Games", "Tools"};
inline constexpr uint8_t kStartScreenTimeAppIdx = 2U;
inline constexpr const char *const kHomeClockTapOpts[2] = {
    "Time App", "Clock Face"};
inline constexpr const char *const kHomeDateTapOpts[2] = {
    "Day View", "Overview"};
inline constexpr const char *const kCalendarWeekStartOpts[2] = {
    "Sunday", "Monday"};
inline constexpr const char *const kBedtimeDisplayOpts[2] = {
    "Screen Off", "Ambient Clock"};
inline constexpr const char *const kCameraDefaultViewOpts[3] = {
    "Grid", "Single", "Carousel"};
inline constexpr const char *const kCameraCarouselIntervalOpts[4] = {
    "10s", "15s", "30s", "60s"};
inline constexpr const char *const kCameraFramingOpts[2] = {
    "Uncropped", "Fill"};
inline constexpr const char *const kCameraStreamModeOpts[3] = {
    "Auto", "Low Bandwidth", "Snapshots"};
inline constexpr const char *const kCameraDoorbellPopupOpts[2] = {
    "Snapshot", "Live Video"};

inline constexpr uint8_t snap_calendar_lookahead_days(int v) noexcept {
  if (v <= 10) return 7U;
  if (v <= 21) return 14U;
  if (v <= 45) return 30U;
  return 60U;
}
inline constexpr const char *const kReturnToHomeTimeoutOpts[7] = {
    "15s", "30s", "1 min", "2 min", "5 min", "10 min", "Never"};
inline constexpr const char *const kClockFaceOpts[3] = {
    "Minimal Digital", "Modern Analog", "Hybrid Digital"};
inline constexpr const char *const kTimeFormatOpts[2] = {"12-Hour (AM/PM)", "24-Hour"};
inline constexpr const char *const kAlbumArtStyleOpts[4] = {
    "Full Screen", "Letterbox", "Standard", "None"};
inline constexpr const char *const kMusicOverlayStyleOpts[3] = {
    "Art Only", "Art + Metadata", "Always Visible"};
inline constexpr const char *const kMusicAutoOpenOpts[3] = {
    "Now Playing", "Lyrics", "Off"};
inline constexpr const char *const kAlbumSortOpts[3] = {
    "Alphabetical", "Newest on Top", "Oldest on Top"};
inline constexpr const char *const kArtistAlbumSortOpts[3] = {
    "Oldest on Top", "Newest on Top", "Alphabetical"};
inline constexpr const char *const kPodcastSortOpts[2] = {
    "Newest on Top", "Oldest on Top"};
inline constexpr const char *const kWwSensitivityOpts[3] = {
    "Slightly sensitive", "Moderately sensitive", "Very sensitive"};
inline constexpr const char *const kBleBeaconModeOpts[3] = {
    "Handheld Only", "Handheld + Screen Off", "Always"};
inline constexpr const char *const kWeatherSimOpts[17] = {
    "Live (Home Assistant)", "Clear Night", "Sunny / Clear Day",
    "Partly Cloudy (Day)", "Partly Cloudy (Night)",
    "Cloudy / Overcast (Day)", "Cloudy / Overcast (Night)",
    "Fog / Mist (Day)", "Fog / Mist (Night)",
    "Rainy (Day)", "Rainy (Night)", "Pouring / Heavy Rain",
    "Snow (Day)", "Snow (Night)",
    "Thunderstorm (Day)", "Thunderstorm (Night)", "Windy"};

template <size_t N>
inline int find_opt_index(const char *const (&opts)[N], std::string_view sv) {
  for (size_t i = 0; i < N; ++i) {
    if (sv == opts[i]) return static_cast<int>(i);
  }
  return -1;
}

template <size_t N>
inline const char *opt_at_clamped(const char *const (&opts)[N], uint8_t idx, uint8_t fallback = 0) {
  return opts[(idx < N) ? idx : fallback];
}

inline const char *pref_wake_word_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kWakeWordModeOpts, p.wake_word_mode); }
inline const char *pref_tap_to_wake_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kTapWakeOpts, p.tap_to_wake, 2); }
inline const char *pref_pickup_to_wake_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kWakePolicyOpts, p.pickup_to_wake, 1); }
inline const char *pref_battery_screen_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBattScreenTimeoutOpts, p.battery_screen_timeout, 2); }
inline const char *pref_docked_idle_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kDockedIdleTimeoutOpts, p.docked_idle_timeout, 3); }
inline const char *pref_battery_hibernate_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBattHibernateTimeoutOpts, p.battery_hibernate_timeout, 2); }
inline const char *pref_screensaver_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kScreensaverModeOpts, p.screensaver_mode); }
inline const char *pref_screensaver_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kScreensaverStyleOpts, p.screensaver_style); }
inline const char *pref_bedtime_display_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBedtimeDisplayOpts, p.bedtime_display); }
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

inline const char *pref_auto_brightness_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kAutoBrightnessModeOpts, p.auto_brightness_mode, 1); }

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
      // Missing/unavailable Area lux sensor: graceful Sun Synchronized fallback
      const bool is_night = (sun_state == "below_horizon");
      if (is_night) {
        return {ActiveBrightnessTargetKind::NIGHT, PrefKey::NIGHT_BRIGHTNESS,
                lo_bri, "Ambient · Night"};
      }
      return {ActiveBrightnessTargetKind::DAY, PrefKey::DAY_BRIGHTNESS,
              hi_bri, "Ambient · Day"};
    }
    case AutoBrightnessMode::SunSynchronized: {
      const bool is_night = (sun_state == "below_horizon");
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
inline const char *pref_color_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kColorProfileOpts, p.color_profile); }
inline const char *pref_va_visual_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kVaVisualProfileOpts, p.va_visual_profile); }
inline const char *pref_start_screen_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kStartScreenOpts, p.start_screen); }
inline const char *pref_return_to_home_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kReturnToHomeTimeoutOpts, p.return_to_home_timeout, 3); }
inline constexpr uint32_t return_to_home_timeout_ms(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return p.return_to_home_ms();
}
inline const char *pref_clock_face_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kClockFaceOpts, p.clock_face); }
inline const char *pref_time_format_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kTimeFormatOpts, p.time_format); }
inline const char *pref_home_clock_tap_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kHomeClockTapOpts, p.home_clock_tap_action); }
inline const char *pref_home_date_tap_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kHomeDateTapOpts, p.home_date_tap_action, 1); }
inline const char *pref_calendar_week_start_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kCalendarWeekStartOpts, p.calendar_week_start); }
inline const char *pref_album_art_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kAlbumArtStyleOpts, p.album_art_style); }
inline const char *pref_music_overlay_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kMusicOverlayStyleOpts, p.music_overlay_style, 1); }
inline const char *pref_music_auto_open_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kMusicAutoOpenOpts, p.music_auto_open); }
inline const char *pref_album_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kAlbumSortOpts, p.album_sort_order); }
inline const char *pref_artist_album_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kArtistAlbumSortOpts, p.artist_album_sort_order); }
inline const char *pref_podcast_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kPodcastSortOpts, p.podcast_sort_order); }
inline const char *pref_ww_sens_cstr(uint8_t idx) { return opt_at_clamped(kWwSensitivityOpts, idx, 1); }
inline const char *pref_ble_beacon_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBleBeaconModeOpts, p.ble_beacon_mode); }
inline const char *pref_weather_sim_cstr(const DevicePrefsState &st = device_prefs_state()) { return opt_at_clamped(kWeatherSimOpts, st.weather_sim_condition); }
inline const char *pref_camera_default_view_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kCameraDefaultViewOpts, p.camera_default_view); }
inline const char *pref_camera_carousel_interval_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kCameraCarouselIntervalOpts, p.camera_carousel_interval, 1); }
inline const char *pref_camera_framing_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kCameraFramingOpts, p.camera_framing); }
inline const char *pref_camera_stream_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kCameraStreamModeOpts, p.camera_stream_mode); }
inline const char *pref_camera_doorbell_popup_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kCameraDoorbellPopupOpts, p.camera_doorbell_popup); }


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
  if (p.wake_word_mode >= 3) p.wake_word_mode = 0;
  if (p.tap_to_wake >= 4) p.tap_to_wake = 2;
  if (p.pickup_to_wake >= 3) p.pickup_to_wake = 1;
  if (p.battery_screen_timeout >= 6) p.battery_screen_timeout = 2;
  if (p.docked_idle_timeout >= 7) p.docked_idle_timeout = 3;
  if (p.battery_hibernate_timeout >= 5) p.battery_hibernate_timeout = 2;
  if (p.screensaver_mode >= 3) p.screensaver_mode = 0;
  if (p.screensaver_style >= 6) p.screensaver_style = 0;
  if (p.auto_brightness_mode >= 4) p.auto_brightness_mode = 1;
  p.manual_brightness = static_cast<uint8_t>(std::clamp<int>(p.manual_brightness, 1, 100));
  p.day_brightness = static_cast<uint8_t>(std::clamp<int>(p.day_brightness, 1, 100));
  p.night_brightness = static_cast<uint8_t>(std::clamp<int>(p.night_brightness, 1, 100));
  p.bedtime_brightness = static_cast<uint8_t>(std::clamp<int>(p.bedtime_brightness, 1, 100));
  if (p.day_start_hour > 23) p.day_start_hour = 7;
  if (p.night_start_hour > 23) p.night_start_hour = 20;
  if (p.color_profile >= 5) p.color_profile = 0;
  if (p.va_visual_profile >= 5) p.va_visual_profile = 0;
  if (p.start_screen >= 13) p.start_screen = 0;
  if (!p.home_screen_enabled && p.start_screen == 0) p.start_screen = kStartScreenTimeAppIdx;  // Single-app mode cannot start on Home -> Time App
  if (p.return_to_home_timeout >= 7) p.return_to_home_timeout = 3;
  if (p.clock_face >= 3) p.clock_face = 0;
  if (p.time_format >= 2) p.time_format = 0;
  if (p.home_clock_tap_action >= 2) p.home_clock_tap_action = 0;
  if (p.home_date_tap_action >= 2) p.home_date_tap_action = 1;
  p.calendar_lookahead_days = snap_calendar_lookahead_days(p.calendar_lookahead_days);
  if (p.calendar_week_start >= 2) p.calendar_week_start = 0;
  if (p.bedtime_display >= 2) p.bedtime_display = 0;
  if (p.bedtime_start_hour > 23) p.bedtime_start_hour = 22;
  if (p.bedtime_end_hour > 23) p.bedtime_end_hour = 7;
  p.local_clock_label[sizeof(p.local_clock_label) - 1] = '\0';
  p.world_clocks_spec[sizeof(p.world_clocks_spec) - 1] = '\0';
  p.tools_items[sizeof(p.tools_items) - 1] = '\0';
  p.games_items[sizeof(p.games_items) - 1] = '\0';
  copy_bounded_cstr(p.tools_items, normalize_tools_items_csv(p.tools_items));
  copy_bounded_cstr(p.games_items, normalize_games_items_csv(p.games_items));
  if (&p == &persisted_prefs()) {
    device_prefs_state().tools_items = p.tools_items;
    device_prefs_state().games_items = p.games_items;
    device_prefs_state().weather_app_background = p.weather_app_background;
    device_prefs_state().do_not_disturb = p.do_not_disturb;
  }
  if (p.album_art_style >= 4) p.album_art_style = 0;
  if (p.music_overlay_style >= 3) p.music_overlay_style = 1;
  if (p.music_auto_open >= 3) p.music_auto_open = 0;
  if (p.album_sort_order >= 3) p.album_sort_order = 0;
  if (p.artist_album_sort_order >= 3) p.artist_album_sort_order = 0;
  if (p.podcast_sort_order >= 2) p.podcast_sort_order = 0;
  if (p.ww_sens_okay_nabu >= 3) p.ww_sens_okay_nabu = 1;
  if (p.ww_sens_hey_luna >= 3) p.ww_sens_hey_luna = 1;
  if (p.ww_sens_hey_jarvis >= 3) p.ww_sens_hey_jarvis = 1;
  if (p.ww_sens_hey_mycroft >= 3) p.ww_sens_hey_mycroft = 1;
  if (p.ww_sens_alexa >= 3) p.ww_sens_alexa = 1;
  p.alert_volume = static_cast<uint8_t>(std::clamp<int>(p.alert_volume, 20, 100));
  p.volume_step_size = static_cast<uint8_t>(std::clamp<int>(p.volume_step_size, 1, 10));
  p.speaker_volume_pct = static_cast<uint8_t>(std::clamp<int>(p.speaker_volume_pct, 10, 100));
  if (p.ble_beacon_mode >= 3) p.ble_beacon_mode = 0;
  if (p.camera_default_view >= 3) p.camera_default_view = 0;
  if (p.camera_carousel_interval >= 4) p.camera_carousel_interval = 1;
  if (p.camera_framing >= 2) p.camera_framing = 0;
  if (p.camera_stream_mode >= 3) p.camera_stream_mode = 0;
  if (p.camera_doorbell_popup >= 2) p.camera_doorbell_popup = 0;
#if !LATTER_CHIP_P4
  p.camera_doorbell_popup = 0;  // Live Video doorbell popup is P4-only
#endif
#if !LATTER_HAS_IMU || LATTER_IS_STATIONARY
  p.pickup_to_wake = 2;  // Disabled
#endif
#if !LATTER_HAS_BATTERY || LATTER_IS_STATIONARY
  p.battery_hibernate_timeout = 4;  // Disabled
#endif
#if !LATTER_ROUND_DISPLAY
  if (p.clock_face == 1) p.clock_face = 0;  // Rectangular panels clamp Modern Analog -> Minimal Digital
#endif
  p.clock_face_digital = (p.clock_face != 1);
  p.is_24h_format = (p.time_format == 1);
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


// Append-only prefix loader: accepts any NVS blob in [kPersistedPrefsMinSize, sizeof(PersistedDevicePrefs)]
// and zero-initializes newly appended tail fields without wiping NVS across post-v3.0 firmware upgrades.
inline bool load_persisted_prefs_from_buffer(const void *buf, size_t len,
                                             PersistedDevicePrefs &out = persisted_prefs()) {
  if (buf == nullptr || len < kPersistedPrefsMinSize || len > sizeof(PersistedDevicePrefs)) {
    return false;
  }
  PersistedDevicePrefs loaded{};
  std::memcpy(&loaded, buf, len);
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

const char *pref_key_name(PrefKey key);
std::string get_pref_value_str(PrefKey key, const PersistedDevicePrefs &p = persisted_prefs());
bool set_pref_by_key_str(PersistedDevicePrefs &p, PrefKey key, std::string_view sv,
                         bool unused_cascade, PrefMask &changed_mask, uint32_t &effects_mask);
std::string encode_prefs_mask(PrefMask mask, const PersistedDevicePrefs &p = persisted_prefs());
std::string format_dirty_prefs(const PersistedDevicePrefs &p = persisted_prefs());
std::string build_hello_dirty_prefs(const PersistedDevicePrefs &p = persisted_prefs());
bool parse_v3_device_prefs_payload(std::string_view payload,
                                   uint32_t &out_effects,
                                   uint32_t now_ms = 0,
                                   PersistedDevicePrefs &p = persisted_prefs(),
                                   DevicePrefsState &st = device_prefs_state());

}  // namespace latter_ui
