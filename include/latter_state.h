#pragma once

// ==============================================================================
// Latter Core SDK: Protocol v3 Domain State, Runtime State & Persisted POD Prefs
// (include/latter_state.h)
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <type_traits>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if defined(USE_ESP32) && __has_include("esphome/core/preferences.h")
#include "esphome/core/preferences.h"
#define LATTER_HAS_ESP_PREFS 1
#else
#define LATTER_HAS_ESP_PREFS 0
#endif

#include "hal_manifest.h"
#include "latter_protocol.h"
#include "latter_rpc.h"

namespace latter_ui {

// Em-dash placeholder rendered when Home Assistant / Protocol v3 state is not yet populated.
inline constexpr const char *kUnknownGlyph = "—";

enum class LockState : uint8_t {
  UNKNOWN  = 0,
  UNLOCKED = 1,
  LOCKED   = 2,
};

enum class GarageState : uint8_t {
  UNKNOWN = 0,
  CLOSED  = 1,
  OPEN    = 2,
};

struct LinkState {
  bool ha_connected = false;
  bool wifi_connected = false;
  uint32_t last_ha_connect_ms = 0;
  uint32_t last_ha_disconnect_ms = 0;
  uint32_t last_wifi_connect_ms = 0;
};

struct WholeHomeState {
  int tab = 0;
  int thermostat_target_f = -1;
  float thermostat_current_f = NAN;
  std::string thermostat_mode = "";
  GarageState garage_state = GarageState::UNKNOWN;
  LockState lock_state = LockState::UNKNOWN;
  std::string vacuum_state = "";
};

// Protocol v3 area_context state (unread wire field covers_open is parsed on the wire without heap storage).
struct AreaContextState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string area_id = "";
  std::string area_name = "";
  std::string site_id = "";
  std::string ble_closest_proxy = "";
  std::string dock_apps = "";
  std::string wallpaper_url = "";
  std::string theme_color_hex = "";
  std::string available_domains = "";
  float area_temp = NAN;
  float area_humidity = NAN;
  int lights_on = 0;
  std::string alarm_entity = "";
  std::string alarm_state = "";
  std::string thermostat_entity = "";
  std::string lock_entity = "";
  std::string garage_entity = "";
  std::string vacuum_entity = "";
  std::string camera_snapshot_url = "";
  std::string cameras_summary = "";
  std::string doorbell_talk_state = "";
  std::string intercom_targets_summary = "";
  int alert_id = -1;
  std::string alert_title = "";
  std::string alert_message = "";
  std::string alert_priority = "normal";
  int alert_duration = 10;
  bool alert_chime = true;
  std::string alert_camera_url = "";
  std::string activity_card = "";        // "1" / "0"; empty = not set -> kDefaultActivityCard
  std::string activity_sources = "";     // csv of source keys; empty = not set -> kDefaultActivitySources
  std::string security_pin_policy = "";  // "allow" | "require_pin" | "block"
  std::string home_categories = "";      // ordered available Home Hub category tokens ("alarm,thermostat,entry,vacuum")
};

// Protocol v3 active_media state (unread wire fields position_updated_at, supported_features,
// volume_entity are dropped; last_speaker is retained and wired to music_browse).
struct MediaState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string state = "";
  std::string title = "";
  std::string artist = "";
  std::string album = "";
  float duration = 0.0f;
  float position = 0.0f;
  float volume = 0.5f;
  bool has_volume = false;
  std::string media_type = "";
  std::string art_url = "";
  std::string active_speaker = "";
  std::string speaker_name = "";
  std::string last_speaker = "";
  std::string local_speaker = "";
  bool is_local = false;
};

struct RemoteState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  int active_slot = 0;
  int configured_count = 0;
  std::string slots_summary = "";
};

struct WeatherState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string condition = "";
  float temperature = NAN;
  float humidity = NAN;
  float wind_speed = NAN;
  float wind_bearing = NAN;
  float high = NAN;
  float low = NAN;
  std::string forecast_5d = "";
  std::string sun_state = "";
  float sun_elevation = NAN;
  float sun_azimuth = NAN;
  std::string sun_next_rising = "";
  std::string sun_next_setting = "";
  std::string moon_phase = "";
};

// Protocol v3 calendar_agenda state (unread next_event wire field is not heap-stored).
struct CalendarState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string summary = "";
  int count = 0;
};

struct BrowseSlotItem {
  std::string name = "";
  std::string id = "";
  std::string type = "";
  bool active = false;
  int level = -1;
};

// ==============================================================================
// Versioned Persisted Device Preferences (Single ESPPreferenceObject POD Blob)
// ==============================================================================
static constexpr uint32_t kPersistedPrefsMagic = 0x4C505246U;  // "LPRF"
static constexpr uint16_t kPersistedPrefsVersion = 4U;
static constexpr uint32_t kPersistedPrefsHash = 0x4C415450U;   // "LATP"

enum class PrefKey : uint8_t {
  POWER_PROFILE = 0,
  WAKE_WORD_MODE = 1,
  TAP_TO_WAKE = 2,
  PICKUP_TO_WAKE = 3,
  BATTERY_SCREEN_TIMEOUT = 4,
  DOCKED_IDLE_TIMEOUT = 5,
  BATTERY_HIBERNATE_TIMEOUT = 6,
  DOCKED_SCREENSAVER = 7,
  SCREENSAVER_STYLE = 8,
  HOLD_CROWN_POWER_OFF = 9,
  AUTO_BRIGHTNESS_MODE = 10,
  MANUAL_BRIGHTNESS = 11,
  DAY_BRIGHTNESS = 12,
  NIGHT_BRIGHTNESS = 13,
  DAY_START_HOUR = 14,
  NIGHT_START_HOUR = 15,
  COLOR_PROFILE = 16,
  VA_VISUAL_PROFILE = 17,
  START_SCREEN = 18,
  CLOCK_FACE = 19,
  TIME_FORMAT = 20,
  WEATHER_BACKGROUND = 21,
  HOME_SUN = 22,
  HOME_MOON = 23,
  WAKE_GREETING = 24,
  ALBUM_ART_STYLE = 25,
  MUSIC_OVERLAY_STYLE = 26,
  SHOW_SPEAKER_SHORTCUT = 27,
  SHOW_LIBRARY_SHORTCUT = 28,
  ALBUM_SORT_ORDER = 29,
  ARTIST_ALBUM_SORT_ORDER = 30,
  PODCAST_SORT_ORDER = 31,
  WW_SENS_OKAY_NABU = 32,
  WW_SENS_HEY_LUNA = 33,
  WW_SENS_HEY_JARVIS = 34,
  WW_SENS_HEY_MYCROFT = 35,
  WW_SENS_ALEXA = 36,
  WAKE_SCREEN_ON_ASSIST = 37,
  ALERT_VOLUME = 38,
  VOLUME_STEP_SIZE = 39,
  GAME_SOUND_EFFECTS = 40,
  BLE_BEACON_ENABLED = 41,
  BLE_BEACON_MODE = 42,
  HOME_SCREEN_ENABLED = 43,
  RETURN_TO_HOME_TIMEOUT = 44,
  COUNT = 45,
};

inline constexpr uint64_t pref_key_bit(PrefKey k) {
  return 1ULL << static_cast<uint8_t>(k);
}

// Side-effect bitmask returned when device_prefs or local preferences mutate.
static constexpr uint32_t kPrefEffectWakeWordSync   = 1U << 0;
static constexpr uint32_t kPrefEffectWakeWordSens   = 1U << 1;
static constexpr uint32_t kPrefEffectBrightness     = 1U << 2;
static constexpr uint32_t kPrefEffectSleepTimer     = 1U << 3;
static constexpr uint32_t kPrefEffectDockedSaverOff = 1U << 4;
static constexpr uint32_t kPrefEffectThemePreset    = 1U << 5;
static constexpr uint32_t kPrefEffectPowerPolicy    = 1U << 6;
static constexpr uint32_t kPrefEffectRedraw         = 1U << 7;
static constexpr uint32_t kPrefEffectNavPolicy      = 1U << 8;

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
  uint8_t power_profile = 0;               // 0..5 ("Portable Carry" .. "Custom")
  uint8_t wake_word_mode = 0;              // 0..2 ("Docked Only", "Always On", "Disabled")
  uint8_t tap_to_wake = 1;                 // 0..2 ("Always On", "Docked Only", "Disabled")
  uint8_t pickup_to_wake = 1;              // 0..2 ("Always On", "Docked Only", "Disabled")
  uint8_t battery_screen_timeout = 2;      // 0..5 ("10s", "15s", "30s", "1 min", "2 min", "Never")
  uint8_t docked_idle_timeout = 3;         // 0..6 ("10s", "15s", "30s", "1 min", "2 min", "5 min", "Never")
  uint8_t battery_hibernate_timeout = 2;   // 0..4 ("5 min", "10 min", "15 min", "30 min", "Disabled")
  bool docked_screensaver = true;
  uint8_t screensaver_style = 0;           // 0..4 ("Ambient Clock" .. "Fireworks")
  bool hold_crown_power_off = false;
  uint8_t auto_brightness_mode = 1;        // 0..2 ("Disabled", "Sun Synchronized", "Time Schedule")
  uint8_t manual_brightness = 3;           // 0..4 ("60%" .. "100%")
  uint8_t day_brightness = 3;              // 0..4 ("60%" .. "100%")
  uint8_t night_brightness = 1;            // 0..4 ("60%" .. "100%")
  uint8_t day_start_hour = 7;              // 0..23
  uint8_t night_start_hour = 20;           // 0..23
  uint8_t color_profile = 0;               // 0..4 ("Steel Blue", "Pink", "Blue", "Green", "Custom")
  uint8_t va_visual_profile = 0;           // 0..4 ("Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave")
  uint8_t start_screen = 0;                // 0..9 ("Home", "Clock", "Media", "Home Hub", "Area Controls", "Remote", "Weather", "Cameras", "Games", "Tools")
  uint8_t clock_face = 0;                  // 0..2 ("Minimal Digital", "Modern Roman (Analog)", "Modern Digital (Bold)")
  uint8_t time_format = 0;                 // 0..1 ("12-Hour (AM/PM)", "24-Hour")
  bool weather_background = true;
  bool home_sun = true;
  bool home_moon = true;
  bool wake_greeting = true;
  uint8_t album_art_style = 0;             // 0..3 ("Full Screen", "Letterbox", "Standard", "None")
  uint8_t music_overlay_style = 1;         // 0..2 ("Art Only", "Art + Metadata", "Always Visible")
  bool show_speaker_shortcut = true;
  bool show_library_shortcut = true;
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
  uint64_t dirty_mask = 0;

  uint32_t custom_theme_rgb = 0x3898FFU;
  int32_t high_score_marbles = 0;
  int32_t high_score_archery = 0;
  int32_t high_score_bricks = 0;
  int32_t high_score_chomp = 0;
  int32_t high_score_2048 = 0;
  int32_t high_score_memory = 0;
  PersistedAlarmPref alarms[3] = {
      {7, 0, false, 1},
      {8, 30, false, 2},
      {9, 0, false, 2},
  };
};

static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs>,
              "PersistedDevicePrefs must remain a POD blob for ESPPreferenceObject.");

struct DevicePrefsState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  uint32_t last_effects = 0;
  uint8_t weather_sim_condition = 0;  // Volatile; resets to "Live (Home Assistant)" on boot
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

inline constexpr const char *const kPowerProfileOpts[6] = {
    "Portable Carry", "Handheld Roaming", "Wearable Watch",
    "Tabletop Dock", "Always-On Wall / Desk", "Custom"};
inline constexpr const char *const kWakeWordModeOpts[3] = {
    "Docked Only", "Always On", "Disabled"};
inline constexpr const char *const kWakePolicyOpts[3] = {
    "Always On", "Docked Only", "Disabled"};
inline constexpr const char *const kBattScreenTimeoutOpts[6] = {
    "10s", "15s", "30s", "1 min", "2 min", "Never"};
inline constexpr const char *const kDockedIdleTimeoutOpts[7] = {
    "10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"};
inline constexpr const char *const kBattHibernateTimeoutOpts[5] = {
    "5 min", "10 min", "15 min", "30 min", "Disabled"};
inline constexpr const char *const kScreensaverStyleOpts[5] = {
    "Ambient Clock", "Aurora", "Starfield", "Digital Rain", "Fireworks"};
inline constexpr const char *const kAutoBrightnessModeOpts[3] = {
    "Disabled", "Sun Synchronized", "Time Schedule"};
inline constexpr const char *const kBrightnessPresetOpts[5] = {
    "60%", "70%", "80%", "90%", "100%"};
inline constexpr const char *const kColorProfileOpts[5] = {
    "Steel Blue", "Pink", "Blue", "Green", "Custom"};
inline constexpr const char *const kVaVisualProfileOpts[5] = {
    "Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"};
inline constexpr const char *const kStartScreenOpts[10] = {
    "Home", "Clock", "Media", "Home Hub", "Area Controls",
    "Remote", "Weather", "Cameras", "Games", "Tools"};
inline constexpr const char *const kReturnToHomeTimeoutOpts[7] = {
    "15s", "30s", "1 min", "2 min", "5 min", "10 min", "Never"};
inline constexpr const char *const kClockFaceOpts[3] = {
    "Minimal Digital", "Modern Roman (Analog)", "Modern Digital (Bold)"};
inline constexpr const char *const kTimeFormatOpts[2] = {"12-Hour (AM/PM)", "24-Hour"};
inline constexpr const char *const kAlbumArtStyleOpts[4] = {
    "Full Screen", "Letterbox", "Standard", "None"};
inline constexpr const char *const kMusicOverlayStyleOpts[3] = {
    "Art Only", "Art + Metadata", "Always Visible"};
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

inline const char *pref_power_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kPowerProfileOpts, p.power_profile); }
inline const char *pref_wake_word_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kWakeWordModeOpts, p.wake_word_mode); }
inline const char *pref_tap_to_wake_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kWakePolicyOpts, p.tap_to_wake, 1); }
inline const char *pref_pickup_to_wake_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kWakePolicyOpts, p.pickup_to_wake, 1); }
inline const char *pref_battery_screen_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBattScreenTimeoutOpts, p.battery_screen_timeout, 2); }
inline const char *pref_docked_idle_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kDockedIdleTimeoutOpts, p.docked_idle_timeout, 3); }
inline const char *pref_battery_hibernate_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBattHibernateTimeoutOpts, p.battery_hibernate_timeout, 2); }
inline const char *pref_screensaver_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kScreensaverStyleOpts, p.screensaver_style); }
inline const char *pref_auto_brightness_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kAutoBrightnessModeOpts, p.auto_brightness_mode, 1); }
inline const char *pref_manual_brightness_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBrightnessPresetOpts, p.manual_brightness, 3); }
inline const char *pref_day_brightness_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBrightnessPresetOpts, p.day_brightness, 3); }
inline const char *pref_night_brightness_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBrightnessPresetOpts, p.night_brightness, 1); }
inline const char *pref_color_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kColorProfileOpts, p.color_profile); }
inline const char *pref_va_visual_profile_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kVaVisualProfileOpts, p.va_visual_profile); }
inline const char *pref_start_screen_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kStartScreenOpts, p.start_screen); }
inline const char *pref_return_to_home_timeout_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kReturnToHomeTimeoutOpts, p.return_to_home_timeout, 3); }
inline uint32_t return_to_home_timeout_ms(const PersistedDevicePrefs &p = persisted_prefs()) {
  static constexpr uint32_t kMs[7] = {15000U, 30000U, 60000U, 120000U, 300000U, 600000U, 0U};
  return kMs[(p.return_to_home_timeout < 7) ? p.return_to_home_timeout : 3];
}
inline const char *pref_clock_face_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kClockFaceOpts, p.clock_face); }
inline const char *pref_time_format_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kTimeFormatOpts, p.time_format); }
inline const char *pref_album_art_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kAlbumArtStyleOpts, p.album_art_style); }
inline const char *pref_music_overlay_style_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kMusicOverlayStyleOpts, p.music_overlay_style, 1); }
inline const char *pref_album_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kAlbumSortOpts, p.album_sort_order); }
inline const char *pref_artist_album_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kArtistAlbumSortOpts, p.artist_album_sort_order); }
inline const char *pref_podcast_sort_order_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kPodcastSortOpts, p.podcast_sort_order); }
inline const char *pref_ww_sens_cstr(uint8_t idx) { return opt_at_clamped(kWwSensitivityOpts, idx, 1); }
inline const char *pref_ble_beacon_mode_cstr(const PersistedDevicePrefs &p = persisted_prefs()) { return opt_at_clamped(kBleBeaconModeOpts, p.ble_beacon_mode); }
inline const char *pref_weather_sim_cstr(const DevicePrefsState &st = device_prefs_state()) { return opt_at_clamped(kWeatherSimOpts, st.weather_sim_condition); }

inline void clamp_prefs_to_capabilities(PersistedDevicePrefs &p = persisted_prefs()) {
  if (p.power_profile >= 6) p.power_profile = 0;
  if (p.wake_word_mode >= 3) p.wake_word_mode = 0;
  if (p.tap_to_wake >= 3) p.tap_to_wake = 1;
  if (p.pickup_to_wake >= 3) p.pickup_to_wake = 1;
  if (p.battery_screen_timeout >= 6) p.battery_screen_timeout = 2;
  if (p.docked_idle_timeout >= 7) p.docked_idle_timeout = 3;
  if (p.battery_hibernate_timeout >= 5) p.battery_hibernate_timeout = 2;
  if (p.screensaver_style >= 5) p.screensaver_style = 0;
  if (p.auto_brightness_mode >= 3) p.auto_brightness_mode = 1;
  if (p.manual_brightness >= 5) p.manual_brightness = 3;
  if (p.day_brightness >= 5) p.day_brightness = 3;
  if (p.night_brightness >= 5) p.night_brightness = 1;
  if (p.day_start_hour > 23) p.day_start_hour = 7;
  if (p.night_start_hour > 23) p.night_start_hour = 20;
  if (p.color_profile >= 5) p.color_profile = 0;
  if (p.va_visual_profile >= 5) p.va_visual_profile = 0;
  if (p.start_screen >= 10) p.start_screen = 0;
  if (!p.home_screen_enabled && p.start_screen == 0) p.start_screen = 1;  // Single-app mode cannot start on Home
  if (p.return_to_home_timeout >= 7) p.return_to_home_timeout = 3;
  if (p.clock_face >= 3) p.clock_face = 0;
  if (p.time_format >= 2) p.time_format = 0;
  if (p.album_art_style >= 4) p.album_art_style = 0;
  if (p.music_overlay_style >= 3) p.music_overlay_style = 1;
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
#if !LATTER_HAS_IMU || LATTER_IS_STATIONARY
  p.pickup_to_wake = 2;  // Disabled
#endif
#if !LATTER_HAS_BATTERY || LATTER_IS_STATIONARY
  p.battery_hibernate_timeout = 4;  // Disabled
#endif
#if !LATTER_ROUND_DISPLAY
  if (p.clock_face == 1) p.clock_face = 0;  // Rectangular panels clamp Modern Roman (Analog) -> Minimal Digital
#endif
  p.clock_face_digital = (p.clock_face != 1);
  p.is_24h_format = (p.time_format == 1);
}

inline bool apply_power_profile_to_prefs(PersistedDevicePrefs &p, std::string_view prof_name,
                                         uint64_t *out_changed = nullptr, uint32_t *out_effects = nullptr) {
  int idx = find_opt_index(kPowerProfileOpts, prof_name);
  if (idx < 0) return false;
  auto set_u8 = [&](uint8_t &slot, uint8_t val, PrefKey key, uint32_t eff) {
    if (slot != val) {
      slot = val;
      if (out_changed != nullptr) *out_changed |= pref_key_bit(key);
      if (out_effects != nullptr) *out_effects |= (eff | kPrefEffectRedraw);
    }
  };
  set_u8(p.power_profile, static_cast<uint8_t>(idx), PrefKey::POWER_PROFILE, kPrefEffectSleepTimer);
  if (idx == 5) return true;  // Custom leaves individual sub-keys untouched

  struct SpecRow { uint8_t ww; uint8_t tap; uint8_t pick; uint8_t batt_scr; uint8_t dock_idle; uint8_t hib; bool saver; };
  static constexpr SpecRow kRows[5] = {
      {0, 1, 1, 2, 3, 2, true},  // Portable Carry: Docked Only, Docked Only, Docked Only, 30s, 1 min, 15 min, true
      {0, 0, 0, 2, 3, 3, true},  // Handheld Roaming: Docked Only, Always On, Always On, 30s, 1 min, 30 min, true
      {0, 0, 0, 1, 3, 2, true},  // Wearable Watch: Docked Only, Always On, Always On, 15s, 1 min, 15 min, true
      {0, 1, 2, 2, 5, 2, true},  // Tabletop Dock: Docked Only, Docked Only, Disabled, 30s, 5 min, 15 min, true
      {1, 0, 2, 5, 5, 4, true},  // Always-On Wall / Desk: Always On, Always On, Disabled, Never, 5 min, Disabled, true
  };
  const SpecRow &r = kRows[idx];
  const uint8_t eff_pick = (LATTER_HAS_IMU && !LATTER_IS_STATIONARY) ? r.pick : 2;
  const uint8_t eff_hib = (LATTER_HAS_BATTERY && !LATTER_IS_STATIONARY) ? r.hib : 4;
  const uint8_t eff_ble_mode = (idx == 1) ? 1U : 0U;  // Handheld Roaming -> "Handheld + Screen Off", others -> "Handheld Only"
  set_u8(p.wake_word_mode, r.ww, PrefKey::WAKE_WORD_MODE, kPrefEffectWakeWordSync);
  set_u8(p.tap_to_wake, r.tap, PrefKey::TAP_TO_WAKE, kPrefEffectPowerPolicy);
  set_u8(p.pickup_to_wake, eff_pick, PrefKey::PICKUP_TO_WAKE, kPrefEffectPowerPolicy);
  set_u8(p.battery_screen_timeout, r.batt_scr, PrefKey::BATTERY_SCREEN_TIMEOUT, kPrefEffectSleepTimer);
  set_u8(p.docked_idle_timeout, r.dock_idle, PrefKey::DOCKED_IDLE_TIMEOUT, kPrefEffectSleepTimer);
  set_u8(p.battery_hibernate_timeout, eff_hib, PrefKey::BATTERY_HIBERNATE_TIMEOUT, kPrefEffectPowerPolicy);
  set_u8(p.ble_beacon_mode, eff_ble_mode, PrefKey::BLE_BEACON_MODE, kPrefEffectPowerPolicy);
  if (p.docked_screensaver != r.saver) {
    p.docked_screensaver = r.saver;
    if (out_changed != nullptr) *out_changed |= pref_key_bit(PrefKey::DOCKED_SCREENSAVER);
    if (out_effects != nullptr) *out_effects |= (kPrefEffectSleepTimer | kPrefEffectRedraw);
  }
  return true;
}

#if LATTER_HAS_ESP_PREFS
inline esphome::ESPPreferenceObject &persisted_prefs_handle() {
  static esphome::ESPPreferenceObject pref;
  static bool inited = false;
  if (!inited && esphome::global_preferences != nullptr) {
    pref = esphome::global_preferences->make_preference<PersistedDevicePrefs>(kPersistedPrefsHash);
    inited = true;
  }
  return pref;
}
#endif

inline bool load_persisted_prefs() {
  auto &st = device_prefs_state();
  st.loaded = true;
  st.nvs_save_pending = false;
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  PersistedDevicePrefs loaded{};
  if (persisted_prefs_handle().load(&loaded) &&
      loaded.magic == kPersistedPrefsMagic &&
      loaded.version == kPersistedPrefsVersion) {
    for (auto &a : loaded.alarms) {
      if (a.hour > 23) a.hour = 7;
      if (a.minute > 59) a.minute = 0;
      if (a.recurrence > 3) a.recurrence = 0;
    }
    clamp_prefs_to_capabilities(loaded);
    persisted_prefs() = loaded;
    return true;
  }
#endif
  persisted_prefs() = PersistedDevicePrefs{};
  clamp_prefs_to_capabilities(persisted_prefs());
  return false;
}

inline void ensure_persisted_prefs_loaded() {
  if (!device_prefs_state().loaded) {
    load_persisted_prefs();
  }
}

inline bool save_persisted_prefs() {
  auto &p = persisted_prefs();
  p.magic = kPersistedPrefsMagic;
  p.version = kPersistedPrefsVersion;
  clamp_prefs_to_capabilities(p);
  device_prefs_state().nvs_save_pending = false;
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  return persisted_prefs_handle().save(&p);
#else
  return true;
#endif
}

inline void schedule_persisted_prefs_save(uint32_t now_ms) {
  auto &st = device_prefs_state();
  if (!st.nvs_save_pending) {
    st.nvs_save_pending = true;
    st.nvs_dirty_since_ms = now_ms;
  }
}

inline bool flush_persisted_prefs_if_dirty() {
  if (!device_prefs_state().nvs_save_pending) return false;
  return save_persisted_prefs();
}

inline bool tick_persisted_prefs_flush(uint32_t now_ms, uint32_t debounce_ms = 1500U) {
  auto &st = device_prefs_state();
  if (!st.nvs_save_pending) return false;
  if (static_cast<uint32_t>(now_ms - st.nvs_dirty_since_ms) < debounce_ms) return false;
  return save_persisted_prefs();
}

inline void seed_default_prefs_if_needed(
    std::string_view def_power_profile,
    std::string_view def_clock_face,
    std::string_view def_start_app,
    std::string_view def_screensaver_style,
    std::string_view def_art_mode,
    std::string_view def_album_sort,
    std::string_view def_artist_album_sort,
    std::string_view def_podcast_sort,
    PersistedDevicePrefs &p = persisted_prefs()) {
  if (!p.defaults_seeded) {
    p.defaults_seeded = true;
    int v = -1;
    if ((v = find_opt_index(kClockFaceOpts, def_clock_face)) >= 0) p.clock_face = static_cast<uint8_t>(v);
    if ((v = find_opt_index(kStartScreenOpts, def_start_app)) >= 0) p.start_screen = static_cast<uint8_t>(v);
    if ((v = find_opt_index(kScreensaverStyleOpts, def_screensaver_style)) >= 0) p.screensaver_style = static_cast<uint8_t>(v);
    if ((v = find_opt_index(kAlbumArtStyleOpts, def_art_mode)) >= 0) p.album_art_style = static_cast<uint8_t>(v);
    if ((v = find_opt_index(kAlbumSortOpts, def_album_sort)) >= 0) p.album_sort_order = static_cast<uint8_t>(v);
    if ((v = find_opt_index(kArtistAlbumSortOpts, def_artist_album_sort)) >= 0) p.artist_album_sort_order = static_cast<uint8_t>(v);
    if ((v = find_opt_index(kPodcastSortOpts, def_podcast_sort)) >= 0) p.podcast_sort_order = static_cast<uint8_t>(v);
    apply_power_profile_to_prefs(p, def_power_profile, nullptr, nullptr);
    clamp_prefs_to_capabilities(p);
    save_persisted_prefs();
  } else {
    clamp_prefs_to_capabilities(p);
  }
}

// Unified 6-game high-score accessors (1=Marbles, 2=Archery, 3=Bricks, 4=Chomp, 5=2048, 6=Memory Match)
inline int get_game_high_score(int game_id) {
  const auto &p = persisted_prefs();
  switch (game_id) {
    case 1: return p.high_score_marbles;
    case 2: return p.high_score_archery;
    case 3: return p.high_score_bricks;
    case 4: return p.high_score_chomp;
    case 5: return p.high_score_2048;
    case 6: return p.high_score_memory;
    default: return 0;
  }
}

inline bool update_game_high_score(int game_id, int score) {
  if (score <= 0) return false;
  auto &p = persisted_prefs();
  int32_t *slot = nullptr;
  switch (game_id) {
    case 1: slot = &p.high_score_marbles; break;
    case 2: slot = &p.high_score_archery; break;
    case 3: slot = &p.high_score_bricks; break;
    case 4: slot = &p.high_score_chomp; break;
    case 5: slot = &p.high_score_2048; break;
    case 6: slot = &p.high_score_memory; break;
    default: return false;
  }
  if (score <= *slot) return false;
  *slot = score;
  save_persisted_prefs();
  return true;
}

// ==============================================================================
// Typed C++ Volatile Runtime State (replaces ~199 YAML globals)
// ==============================================================================
struct AlertBannerState {
  bool active = false;
  std::string title = "";
  std::string message = "";
  std::string priority = "normal";
  std::string camera_url = "";
  std::string loaded_camera_url = "";
  uint32_t start_ms = 0;
  uint32_t duration_ms = 10000U;
  int last_seen_alert_id = -1;
};

struct MultiTimerSlot {
  std::string id = "";
  std::string name = "";
  int duration_sec = 300;
  int remaining_sec = 300;
  uint32_t target_ms = 0;
  bool running = false;
  bool paused = false;
};

struct TimerRuntimeState {
  MultiTimerSlot slots[3] = {
      {"", "", 300, 300, 0, false, false},
      {"", "", 600, 600, 0, false, false},
      {"", "", 900, 900, 0, false, false},
  };
  int view_slot = 0;  // 0 = 3-Timer Landing Page, 1..3 = Single-Timer Detail/Editor
  int duration_sec = 300;
  int remaining_sec = 300;
  uint32_t target_ms = 0;
  bool running = false;
  bool paused = false;
  bool ringing = false;
  std::string va_timer_name = "";
};

struct StopwatchRuntimeState {
  bool running = false;
  uint32_t start_time_ms = 0;
  uint32_t elapsed_ms = 0;
  uint32_t lap_ms = 0;
};

struct AlarmRuntimeState {
  int edit_idx = 0;
  bool ringing = false;
  int ringing_idx = 0;
  uint32_t snooze_until = 0;
  uint32_t last_fired_epoch_min = 0;
  bool find_my_active = false;
  uint32_t find_my_start_ms = 0;
};

struct VoiceSessionState {
  bool active = false;
  bool vad_active = false;
  bool tts_received = false;
  bool resume_music_after = false;
  bool was_cancelled = false;
  bool intercom_turn = false;
  int return_page = 0;
  std::string status_text = "";
  std::string last_detected_wake_word = "";
};

struct NavRuntimeState {
  int gui_page = 1;
  int home_subpage = 0;
  int home_activity_idx = 0;
  int sky_weather_subpage = 0;
  int area_subpage = 0;
  int remote_subpage = 0;
  int tools_subpage = 0;
  int tools_menu_page = 0;
  int display_menu_page = 1;
  int system_dashboard_page = 1;
  int system_menu_mode = 0;
  int sleep_menu_page = 1;
  int audio_menu_parent_page = 4;
  bool menu_scroll_active = false;
  int menu_scroll_dir = 1;
  int menu_scroll_target_page = 1;
  int display_power_state = 0;  // 0 = ACTIVE, 1 = SCREENSAVER, 2 = SCREEN_OFF / STANDBY
  uint32_t sleep_start_time = 0;
  bool is_hibernating = false;
  int hibernate_countdown_val = 0;
  bool hud_wake_finger_released = true;
  bool ota_in_progress = false;
  int ota_progress_pct = 0;
  int update_check_state = 0;   // 0 = Idle, 1 = Checking..., 2 = Checked
  uint32_t update_check_timer = 0;
  int game_carousel_idx = 0;
  int active_game = 0;
  float marble_touch_x = -1.0f;
  float marble_touch_y = -1.0f;
  bool game_btn_pressed = false;
};

struct MusicRuntimeState {
  int overlay_mode = 0;
  bool direct_nav = false;
  bool hud_active = false;
  uint32_t hud_timer = 0;
  int category_page = 1;
  std::string selected_category_mode = "speakers";
  std::string selected_category_title = "SPEAKERS";
  std::string drilldown_artist_name = "";
  std::string drilldown_album_id = "";
  std::string drilldown_parent_mode = "";
  int drilldown_artist_page = 1;
  int drilldown_album_page = 1;
  std::string active_playback_media_type = "music";
  std::string active_speaker_entity = "";
  std::string active_speaker_name = "";
  std::string target_speaker_id = "";
  std::string target_speaker_name = "";
  std::string selected_slot_name = "";
  std::string selected_slot_id = "";
  std::string selected_slot_type = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
  float pos_base = 0.0f;
  uint32_t pos_sync_time = 0;
  int skip_burst_count = 0;
  uint32_t skip_last_tap_ms = 0;
  std::string loaded_art_url = "";
  std::string loaded_wallpaper_url = "";
};

struct AreaRuntimeState {
  std::string browse_mode = "entities";
  std::string domain_filter = "scene";
  std::string active_area_id = "";
  std::string active_area_name = "Area Control";
  std::string alarm_entity = "";
  std::string alarm_state = "disarmed";
  std::string alarm_service = "alarm_control_panel.alarm_disarm";
  std::string target_entity_id = "";
  std::string target_action = "toggle";
  int target_level = -1;
  std::string target_code = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
};

struct RemoteRuntimeState {
  int active_slot = 0;
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
};

struct CalendarUiState {
  std::string quick_summary = "Reminder";
  int quick_in_minutes = 30;
  int quick_duration_minutes = 30;
};

struct IntercomRuntimeState {
  std::string broadcast_message = "Dinner is ready";
  std::string broadcast_feedback = "";
  uint32_t broadcast_feedback_ms = 0;
  std::string cam_action = "listen_start";
  std::string cam_message = "Please leave it at the door.";
  std::string cam_noise_reduction = "strong";
  std::string cam_code = "";
  uint32_t cam_status_ms = 0;
};

struct DeviceRuntimeState {
  static constexpr size_t kSlotChannelCount = static_cast<size_t>(latter_rpc::SlotChannel::COUNT);
  static constexpr size_t kListSlotsPerPage = static_cast<size_t>(LATTER_SLOT_COUNT);
  static constexpr size_t kCardSlotsPerPage = static_cast<size_t>(LATTER_CARD_SLOT_COUNT);
  static constexpr size_t kSlotsPerPage =
      (kListSlotsPerPage > kCardSlotsPerPage) ? kListSlotsPerPage : kCardSlotsPerPage;

  BrowseSlotItem slots[kSlotChannelCount][kSlotsPerPage]{};
  int slot_page[kSlotChannelCount] = {1, 1, 1, 1};
  int slot_total_pages[kSlotChannelCount] = {1, 1, 1, 1};
  uint32_t last_music_page_req_ms = 0;

  AlertBannerState alert{};
  TimerRuntimeState timers{};
  StopwatchRuntimeState stopwatch{};
  AlarmRuntimeState alarms{};
  VoiceSessionState voice{};
  NavRuntimeState nav{};
  MusicRuntimeState music{};
  AreaRuntimeState area{};
  RemoteRuntimeState remote{};
  CalendarUiState cal{};
  IntercomRuntimeState intercom{};

  // Live hardware telemetry & optional peripheral hooks (populated by modules/hal/pmu_axp2101.yaml and
  // modules/hal/imu_qmi8658.yaml when included; safe mains-powered / no-IMU defaults otherwise so
  // pmu_none.yaml & imu_none.yaml are retired without ESPHome Python lambda id(...) lookup errors).
  bool hw_usb_connected = true;
  bool hw_batt_charging = false;
  float hw_batt_level = NAN;
  float hw_batt_voltage = NAN;
  float hw_accel_x = 0.0f;
  float hw_accel_y = 0.0f;
  float hw_accel_z = 1.0f;
  float hw_imu_roll = 0.0f;
  float hw_imu_pitch = 0.0f;
  float hw_imu_temp = 30.0f;
  float hw_motion_mag = 0.0f;
  void (*hw_boot_battery_check)() = nullptr;
  void (*hw_init_pmu)() = nullptr;
  void (*hw_poll_pmu_1s)() = nullptr;
  void (*hw_wait_pwr_key_release)() = nullptr;
  bool (*hw_pwr_key_pressed)() = nullptr;
  void (*hw_update_batt_level)() = nullptr;
  void (*hw_set_imu_power_mode)(int ctrl7_val) = nullptr;

  // Display/session lifecycle hooks bound by modules/system_core.yaml on top of modules/base.yaml
  void (*on_ha_client_connected)(const std::string &client_info, const std::string &client_address) = nullptr;
  void (*on_ha_client_disconnected)(const std::string &client_info) = nullptr;
  void (*on_v3_domain_pushed)(std::string_view domain, const std::string &prev_dock) = nullptr;
  void (*on_device_command)(std::string_view command) = nullptr;

  DeviceRuntimeState() {
    for (size_t i = 0; i < kSlotsPerPage; ++i) {
      slots[static_cast<size_t>(latter_rpc::SlotChannel::MUSIC)][i].type = "player";
    }
    slots[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)][0].name = "Tap Header for Areas";
    slots[static_cast<size_t>(latter_rpc::SlotChannel::AREA_ENTITIES)][0].type = "text";
  }

  BrowseSlotItem *slots_for(latter_rpc::SlotChannel ch) {
    return slots[static_cast<size_t>(ch)];
  }
  const BrowseSlotItem *slots_for(latter_rpc::SlotChannel ch) const {
    return slots[static_cast<size_t>(ch)];
  }
  BrowseSlotItem *music_slots() { return slots_for(latter_rpc::SlotChannel::MUSIC); }
  const BrowseSlotItem *music_slots() const { return slots_for(latter_rpc::SlotChannel::MUSIC); }
  BrowseSlotItem *area_list_slots() { return slots_for(latter_rpc::SlotChannel::AREA_LIST); }
  const BrowseSlotItem *area_list_slots() const { return slots_for(latter_rpc::SlotChannel::AREA_LIST); }
  BrowseSlotItem *area_entity_slots() { return slots_for(latter_rpc::SlotChannel::AREA_ENTITIES); }
  const BrowseSlotItem *area_entity_slots() const { return slots_for(latter_rpc::SlotChannel::AREA_ENTITIES); }
  BrowseSlotItem *calendar_slots() { return slots_for(latter_rpc::SlotChannel::CALENDAR); }
  const BrowseSlotItem *calendar_slots() const { return slots_for(latter_rpc::SlotChannel::CALENDAR); }

  int &slot_page_for(latter_rpc::SlotChannel ch) { return slot_page[static_cast<size_t>(ch)]; }
  int slot_page_for(latter_rpc::SlotChannel ch) const { return slot_page[static_cast<size_t>(ch)]; }
  int &slot_total_pages_for(latter_rpc::SlotChannel ch) { return slot_total_pages[static_cast<size_t>(ch)]; }
  int slot_total_pages_for(latter_rpc::SlotChannel ch) const { return slot_total_pages[static_cast<size_t>(ch)]; }
};

inline DeviceRuntimeState &runtime_state() {
  static DeviceRuntimeState st;
  return st;
}

inline MultiTimerSlot *timer_slots() {
  return runtime_state().timers.slots;
}

inline std::string format_timer_slot_label(int slot_idx) {
  if (slot_idx < 0 || slot_idx >= 3) return "Timer";
  const auto &s = timer_slots()[slot_idx];
  if (!s.name.empty()) return s.name;
  char buf[24];
  int mins = s.duration_sec / 60;
  int secs = s.duration_sec % 60;
  if (secs == 0) std::snprintf(buf, sizeof(buf), "%d Min Timer", mins);
  else std::snprintf(buf, sizeof(buf), "%dm %ds Timer", mins, secs);
  return std::string(buf);
}

inline std::string format_timer_header_upper(int slot_idx) {
  if (slot_idx < 0 || slot_idx >= 3) return "TIMER";
  const auto &s = timer_slots()[slot_idx];
  if (!s.name.empty()) {
    std::string up = s.name;
    for (char &c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up;
  }
  char buf[20];
  std::snprintf(buf, sizeof(buf), "TIMER %d", slot_idx + 1);
  return std::string(buf);
}

inline void sync_multi_timer_globals(bool &out_any_running, bool &out_any_paused,
                                     int &out_min_rem_sec, int &out_active_dur_sec,
                                     uint32_t &out_min_target_ms, int view_slot = -1) {
  auto *slots = timer_slots();
  const int eff_view = (view_slot >= 0) ? view_slot : runtime_state().timers.view_slot;
  bool any_run = false;
  bool any_pau = false;
  int min_rem = 0;
  int act_dur = slots[0].duration_sec;
  uint32_t min_t = 0;

  for (int i = 0; i < 3; i++) {
    if (slots[i].running) {
      if (!any_run || slots[i].remaining_sec < min_rem) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
        min_t   = slots[i].target_ms;
      }
      any_run = true;
    } else if (slots[i].paused && !any_run) {
      if (!any_pau) {
        min_rem = slots[i].remaining_sec;
        act_dur = slots[i].duration_sec;
      }
      any_pau = true;
    }
  }
  if (!any_run && !any_pau) {
    int v_idx = (eff_view >= 1 && eff_view <= 3) ? (eff_view - 1) : 0;
    min_rem = slots[v_idx].duration_sec;
    act_dur = slots[v_idx].duration_sec;
  }
  out_any_running    = any_run;
  out_any_paused     = any_pau;
  out_min_rem_sec    = min_rem;
  out_active_dur_sec = act_dur;
  out_min_target_ms  = min_t;
}

inline void sync_multi_timer_state(int view_slot = -1, TimerRuntimeState &ts = runtime_state().timers) {
  sync_multi_timer_globals(ts.running, ts.paused, ts.remaining_sec, ts.duration_sec, ts.target_ms,
                           view_slot >= 0 ? view_slot : ts.view_slot);
}

inline int upsert_voice_timer_slot(const std::string &id_str, const std::string &name_str,
                                   int total_sec, int left_sec, bool is_act, uint32_t now_ms) {
  auto *slots = timer_slots();
  int target = -1;

  for (int i = 0; i < 3; i++) {
    if (!id_str.empty() && slots[i].id == id_str) { target = i; break; }
    if (!name_str.empty() && !slots[i].name.empty()) {
      std::string a = slots[i].name, b = name_str;
      for (char &c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      for (char &c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (a == b) { target = i; break; }
    }
  }

  if (target < 0) {
    if (!slots[0].running && !slots[0].paused) {
      if (!slots[1].running && !slots[1].paused && !slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
        slots[1] = slots[0];
      }
      target = 0;
    } else if (!slots[1].running && !slots[1].paused) {
      if (!slots[2].running && !slots[2].paused) {
        slots[2] = slots[1];
      }
      target = 1;
    } else {
      target = 2;
    }
  }

  if (total_sec <= 0) total_sec = std::max(60, left_sec);
  bool same_id = (!id_str.empty() && slots[target].id == id_str);
  slots[target].id = id_str;
  if (!name_str.empty() || !same_id) {
    slots[target].name = name_str;
  }
  slots[target].duration_sec  = total_sec;
  slots[target].remaining_sec = left_sec;
  slots[target].target_ms     = now_ms + static_cast<uint32_t>(left_sec) * 1000U;
  slots[target].running       = is_act;
  slots[target].paused        = !is_act;
  return target;
}

inline LinkState &link_state() {
  static LinkState st;
  return st;
}

inline WholeHomeState &whole_home_state() {
  static WholeHomeState st;
  return st;
}

inline AreaContextState &area_ctx_state() {
  static AreaContextState st;
  return st;
}

inline MediaState &media_state() {
  static MediaState st;
  return st;
}

inline RemoteState &remote_state() {
  static RemoteState st;
  return st;
}

inline WeatherState &weather_state() {
  static WeatherState st;
  return st;
}

inline CalendarState &calendar_state() {
  static CalendarState st;
  return st;
}

// Zero-allocation tokenizer for \x1f-delimited Protocol v3 payloads
inline size_t v3_split_fields(std::string_view payload, std::string_view *out, size_t max_fields, char sep = '\x1f') {
  if (payload.empty() || max_fields == 0) return 0;
  size_t count = 0;
  size_t start = 0;
  while (count < max_fields) {
    size_t pos = payload.find(sep, start);
    if (pos == std::string_view::npos) {
      out[count++] = payload.substr(start);
      break;
    }
    out[count++] = payload.substr(start, pos - start);
    start = pos + 1;
  }
  return count;
}

inline int v3_parse_int(std::string_view sv, int fallback = 0) {
  if (sv.empty()) return fallback;
  char buf[24];
  size_t len = (sv.size() < sizeof(buf) - 1) ? sv.size() : (sizeof(buf) - 1);
  std::memcpy(buf, sv.data(), len);
  buf[len] = '\0';
  char *endp = nullptr;
  long v = std::strtol(buf, &endp, 10);
  if (endp == buf) return fallback;
  return static_cast<int>(v);
}

inline uint32_t v3_parse_u32(std::string_view sv, uint32_t fallback = 0) {
  if (sv.empty()) return fallback;
  char buf[24];
  size_t len = (sv.size() < sizeof(buf) - 1) ? sv.size() : (sizeof(buf) - 1);
  std::memcpy(buf, sv.data(), len);
  buf[len] = '\0';
  char *endp = nullptr;
  unsigned long v = std::strtoul(buf, &endp, 10);
  if (endp == buf) return fallback;
  return static_cast<uint32_t>(v);
}

inline float v3_parse_float(std::string_view sv, float fallback = NAN) {
  if (sv.empty()) return fallback;
  char buf[32];
  size_t len = (sv.size() < sizeof(buf) - 1) ? sv.size() : (sizeof(buf) - 1);
  std::memcpy(buf, sv.data(), len);
  buf[len] = '\0';
  char *endp = nullptr;
  float v = std::strtof(buf, &endp);
  if (endp == buf) return fallback;
  return v;
}

inline bool v3_parse_bool(std::string_view sv) {
  return (sv == "1" || sv == "true" || sv == "True" || sv == "on");
}

inline constexpr const char *const kCanonicalAreaCategoryTokens[8] = {
    "scene", "light", "cover", "fan", "switch", "climate", "media", "all"};

inline bool is_canonical_area_category_token(std::string_view sv) {
  for (const char *tok : kCanonicalAreaCategoryTokens) {
    if (sv == tok) return true;
  }
  return false;
}

inline int parse_area_categories_csv(std::string_view csv, std::string *out_cats, int max_cats = 8) {
  if (csv.empty() || out_cats == nullptr || max_cats <= 0) return 0;
  int n = 0;
  size_t start = 0;
  while (start <= csv.size() && n < max_cats) {
    size_t pos = csv.find(',', start);
    std::string_view part =
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1);
    if (!part.empty() && is_canonical_area_category_token(part)) {
      bool dup = false;
      for (int i = 0; i < n; ++i) {
        if (out_cats[i] == part) {
          dup = true;
          break;
        }
      }
      if (!dup) out_cats[n++] = std::string(part);
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return n;
}

inline int home_category_tab_for_token(std::string_view sv) {
  if (sv == "alarm") return 0;
  if (sv == "thermostat") return 1;
  if (sv == "entry") return 2;
  if (sv == "vacuum") return 3;
  return -1;
}

inline int parse_home_categories_csv(std::string_view csv, int out_tabs[4]) {
  if (csv.empty() || out_tabs == nullptr) return 0;
  int n = 0;
  size_t start = 0;
  while (start <= csv.size() && n < 4) {
    size_t pos = csv.find(',', start);
    std::string_view part =
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1);
    const int tab_id = home_category_tab_for_token(part);
    if (tab_id >= 0) {
      bool dup = false;
      for (int i = 0; i < n; ++i) {
        if (out_tabs[i] == tab_id) {
          dup = true;
          break;
        }
      }
      if (!dup) out_tabs[n++] = tab_id;
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return n;
}

inline bool apply_if_newer(uint32_t &cur_epoch, uint32_t &cur_rev, uint32_t in_epoch, uint32_t in_rev) {
  if (in_epoch == 0 || in_rev == 0) return false;
  if (in_epoch != cur_epoch) {
    cur_epoch = in_epoch;
    cur_rev = in_rev;
    return true;
  }
  if (in_rev > cur_rev) {
    cur_rev = in_rev;
    return true;
  }
  return false;
}

inline uint32_t &v3_mismatch_warn_count() {
  static uint32_t cnt = 0;
  return cnt;
}

inline void v3_check_field_count(const char *domain, std::string_view payload, size_t expected) {
  const size_t got = payload.empty() ? 0 : (1U + static_cast<size_t>(
      std::count(payload.begin(), payload.end(), kProtocolFieldSep)));
  if (got != expected) {
    v3_mismatch_warn_count()++;
#ifdef ESP_LOGW
    ESP_LOGW("latter_v3", "Domain %s field count %u != expected %u (tolerated)",
             domain, static_cast<unsigned>(got), static_cast<unsigned>(expected));
#else
    (void)domain;
#endif
  }
}

inline void reset_all_v3_rev_guards() {
  area_ctx_state().epoch = 0;
  area_ctx_state().rev = 0;
  area_ctx_state().alert_id = -1;
  runtime_state().alert.last_seen_alert_id = -1;
  media_state().epoch = 0;
  media_state().rev = 0;
  remote_state().epoch = 0;
  remote_state().rev = 0;
  weather_state().epoch = 0;
  weather_state().rev = 0;
  calendar_state().epoch = 0;
  calendar_state().rev = 0;
  device_prefs_state().epoch = 0;
  device_prefs_state().rev = 0;
  v3_mismatch_warn_count() = 0;
}

inline LockState parse_lock_state_str(std::string_view sv) {
  if (sv == "locked") return LockState::LOCKED;
  if (sv == "unlocked" || sv == "unlocking" || sv == "open") return LockState::UNLOCKED;
  return LockState::UNKNOWN;
}

inline GarageState parse_garage_state_str(std::string_view sv) {
  if (sv == "open" || sv == "opening") return GarageState::OPEN;
  if (sv == "closed" || sv == "closing") return GarageState::CLOSED;
  return GarageState::UNKNOWN;
}

inline std::string normalize_vacuum_state_str(std::string_view sv) {
  if (sv.empty()) return "";
  if (sv == "cleaning" || sv == "Cleaning") return "Cleaning";
  if (sv == "paused" || sv == "Paused") return "Paused";
  if (sv == "returning" || sv == "Returning") return "Returning";
  if (sv == "docked" || sv == "Docked" || sv == "idle") return "Docked";
  return std::string(sv);
}

// Parse Protocol v3 area_context payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_area_ctx_payload(std::string_view payload,
                                      AreaContextState &st = area_ctx_state(),
                                      WholeHomeState &wh = whole_home_state()) {
  constexpr size_t kCount = static_cast<size_t>(AreaCtxField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(AreaCtxField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("area_context", payload, kCount);

  st.area_id           = std::string(f[static_cast<size_t>(AreaCtxField::AREA_ID)]);
  st.area_name         = std::string(f[static_cast<size_t>(AreaCtxField::AREA_NAME)]);
  st.site_id           = std::string(f[static_cast<size_t>(AreaCtxField::SITE_ID)]);
  st.ble_closest_proxy = std::string(f[static_cast<size_t>(AreaCtxField::BLE_CLOSEST_PROXY)]);
  st.dock_apps         = std::string(f[static_cast<size_t>(AreaCtxField::DOCK_APPS)]);
  st.wallpaper_url     = std::string(f[static_cast<size_t>(AreaCtxField::WALLPAPER_URL)]);
  st.theme_color_hex   = std::string(f[static_cast<size_t>(AreaCtxField::THEME_COLOR_HEX)]);
  st.available_domains = std::string(f[static_cast<size_t>(AreaCtxField::AVAILABLE_DOMAINS)]);
  st.area_temp         = v3_parse_float(f[static_cast<size_t>(AreaCtxField::AREA_TEMP)], NAN);
  st.area_humidity     = v3_parse_float(f[static_cast<size_t>(AreaCtxField::AREA_HUMIDITY)], NAN);
  st.lights_on         = v3_parse_int(f[static_cast<size_t>(AreaCtxField::LIGHTS_ON)], 0);
  st.alarm_entity      = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_ENTITY)]);
  st.alarm_state       = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_STATE)]);
  st.thermostat_entity = std::string(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_ENTITY)]);
  st.lock_entity       = std::string(f[static_cast<size_t>(AreaCtxField::LOCK_ENTITY)]);
  st.garage_entity     = std::string(f[static_cast<size_t>(AreaCtxField::GARAGE_ENTITY)]);
  st.vacuum_entity     = std::string(f[static_cast<size_t>(AreaCtxField::VACUUM_ENTITY)]);

  wh.thermostat_mode = std::string(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_STATE)]);
  float th_temp = v3_parse_float(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_TEMP)], NAN);
  wh.thermostat_target_f = (!std::isnan(th_temp) && th_temp >= 40.0f && th_temp <= 100.0f)
                               ? static_cast<int>(std::round(th_temp))
                               : -1;  // no setpoint: unknown
  wh.thermostat_current_f = v3_parse_float(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_CURRENT_TEMP)], NAN);
  wh.lock_state           = parse_lock_state_str(f[static_cast<size_t>(AreaCtxField::LOCK_STATE)]);
  wh.garage_state         = parse_garage_state_str(f[static_cast<size_t>(AreaCtxField::GARAGE_STATE)]);
  wh.vacuum_state         = normalize_vacuum_state_str(f[static_cast<size_t>(AreaCtxField::VACUUM_STATE)]);

  st.camera_snapshot_url      = std::string(f[static_cast<size_t>(AreaCtxField::CAMERA_SNAPSHOT_URL)]);
  st.cameras_summary          = std::string(f[static_cast<size_t>(AreaCtxField::CAMERAS_SUMMARY)]);
  st.doorbell_talk_state      = std::string(f[static_cast<size_t>(AreaCtxField::DOORBELL_TALK_STATE)]);
  st.intercom_targets_summary = std::string(f[static_cast<size_t>(AreaCtxField::INTERCOM_TARGETS_SUMMARY)]);
  st.alert_id                 = v3_parse_int(f[static_cast<size_t>(AreaCtxField::ALERT_ID)], 0);
  st.alert_title              = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_TITLE)]);
  st.alert_message            = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_MESSAGE)]);
  st.alert_priority           = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_PRIORITY)]);
  st.alert_duration           = v3_parse_int(f[static_cast<size_t>(AreaCtxField::ALERT_DURATION)], 10);
  st.alert_chime              = v3_parse_bool(f[static_cast<size_t>(AreaCtxField::ALERT_CHIME)]);
  st.alert_camera_url         = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_CAMERA_URL)]);
  st.activity_card            = std::string(f[static_cast<size_t>(AreaCtxField::ACTIVITY_CARD)]);
  st.activity_sources         = std::string(f[static_cast<size_t>(AreaCtxField::ACTIVITY_SOURCES)]);
  st.security_pin_policy      = std::string(f[static_cast<size_t>(AreaCtxField::SECURITY_PIN_POLICY)]);
  st.home_categories          = std::string(f[static_cast<size_t>(AreaCtxField::HOME_CATEGORIES)]);
  return true;
}

static constexpr uint8_t kActivitySourceAll = 0x3E;

inline uint8_t activity_source_mask(const AreaContextState &st = area_ctx_state()) {
  const std::string_view card = st.activity_card.empty() ? std::string_view(kDefaultActivityCard)
                                                         : std::string_view(st.activity_card);
  if (!v3_parse_bool(card)) return 0;
  const std::string_view csv = st.activity_sources.empty() ? std::string_view(kDefaultActivitySources)
                                                           : std::string_view(st.activity_sources);
  uint8_t mask = 0;
  size_t start = 0;
  while (start <= csv.size()) {
    size_t end = csv.find(',', start);
    if (end == std::string_view::npos) end = csv.size();
    const std::string_view key = csv.substr(start, end - start);
    if (key == "timers") mask |= 1U << 1;
    else if (key == "stopwatch") mask |= 1U << 2;
    else if (key == "music_queue") mask |= 1U << 3;
    else if (key == "alarms") mask |= 1U << 4;
    else if (key == "calendar") mask |= 1U << 5;
    start = end + 1;
  }
  return mask;
}

// Parse Protocol v3 active_media payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_media_payload(std::string_view payload, MediaState &st = media_state()) {
  constexpr size_t kCount = static_cast<size_t>(MediaField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(MediaField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(MediaField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(MediaField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("active_media", payload, kCount);

  st.state          = std::string(f[static_cast<size_t>(MediaField::STATE)]);
  st.title          = std::string(f[static_cast<size_t>(MediaField::TITLE)]);
  st.artist         = std::string(f[static_cast<size_t>(MediaField::ARTIST)]);
  st.album          = std::string(f[static_cast<size_t>(MediaField::ALBUM)]);
  st.duration       = v3_parse_float(f[static_cast<size_t>(MediaField::DURATION)], 0.0f);
  st.position       = v3_parse_float(f[static_cast<size_t>(MediaField::POSITION)], 0.0f);
  float raw_vol     = v3_parse_float(f[static_cast<size_t>(MediaField::VOLUME)], NAN);
  if (!std::isnan(raw_vol)) {
    st.volume     = std::max(0.0f, std::min(1.0f, raw_vol * 0.01f));
    st.has_volume = true;
  } else {
    st.has_volume = false;
  }
  st.media_type     = std::string(f[static_cast<size_t>(MediaField::MEDIA_TYPE)]);
  st.art_url        = std::string(f[static_cast<size_t>(MediaField::ART_URL)]);
  st.active_speaker = std::string(f[static_cast<size_t>(MediaField::ACTIVE_SPEAKER)]);
  st.speaker_name   = std::string(f[static_cast<size_t>(MediaField::SPEAKER_NAME)]);
  st.last_speaker   = std::string(f[static_cast<size_t>(MediaField::LAST_SPEAKER)]);
  st.local_speaker  = std::string(f[static_cast<size_t>(MediaField::LOCAL_SPEAKER)]);
  st.is_local       = v3_parse_bool(f[static_cast<size_t>(MediaField::IS_LOCAL)]);
  return true;
}

// Parse Protocol v3 remote_config payload ("v3\x1f<epoch>\x1f<rev>\x1f<active_slot>\x1f<count>\x1f<slots_summary>")
inline bool parse_v3_remote_payload(std::string_view payload, RemoteState &st = remote_state()) {
  constexpr size_t kCount = static_cast<size_t>(RemoteField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(RemoteField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(RemoteField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(RemoteField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("remote_config", payload, kCount);

  int slot_1based     = v3_parse_int(f[static_cast<size_t>(RemoteField::ACTIVE_SLOT)], 1);
  st.active_slot      = (slot_1based > 0) ? (slot_1based - 1) : 0;
  st.configured_count = v3_parse_int(f[static_cast<size_t>(RemoteField::CONFIGURED_COUNT)], 0);
  st.slots_summary    = std::string(f[static_cast<size_t>(RemoteField::SLOTS_SUMMARY)]);
  return true;
}

// Parse Protocol v3 weather payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_weather_payload(std::string_view payload, WeatherState &st = weather_state()) {
  constexpr size_t kCount = static_cast<size_t>(WeatherField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(WeatherField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(WeatherField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(WeatherField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("weather_forecast", payload, kCount);

  st.condition        = std::string(f[static_cast<size_t>(WeatherField::CONDITION)]);
  st.temperature      = v3_parse_float(f[static_cast<size_t>(WeatherField::TEMPERATURE)], NAN);
  st.humidity         = v3_parse_float(f[static_cast<size_t>(WeatherField::HUMIDITY)], NAN);
  st.wind_speed       = v3_parse_float(f[static_cast<size_t>(WeatherField::WIND_SPEED)], NAN);
  st.wind_bearing     = v3_parse_float(f[static_cast<size_t>(WeatherField::WIND_BEARING)], NAN);
  st.high             = v3_parse_float(f[static_cast<size_t>(WeatherField::HIGH)], NAN);
  st.low              = v3_parse_float(f[static_cast<size_t>(WeatherField::LOW)], NAN);
  st.forecast_5d      = std::string(f[static_cast<size_t>(WeatherField::FORECAST_5D)]);
  st.sun_state        = std::string(f[static_cast<size_t>(WeatherField::SUN_STATE)]);
  st.sun_elevation    = v3_parse_float(f[static_cast<size_t>(WeatherField::SUN_ELEVATION)], NAN);
  st.sun_azimuth      = v3_parse_float(f[static_cast<size_t>(WeatherField::SUN_AZIMUTH)], NAN);
  st.sun_next_rising  = std::string(f[static_cast<size_t>(WeatherField::SUN_NEXT_RISING)]);
  st.sun_next_setting = std::string(f[static_cast<size_t>(WeatherField::SUN_NEXT_SETTING)]);
  st.moon_phase       = std::string(f[static_cast<size_t>(WeatherField::MOON_PHASE)]);
  return true;
}

// Parse Protocol v3 calendar payload ("v3\x1f<epoch>\x1f<rev>\x1f<summary>\x1f<count>\x1f<next_event>")
inline bool parse_v3_calendar_payload(std::string_view payload, CalendarState &st = calendar_state()) {
  constexpr size_t kCount = static_cast<size_t>(CalendarField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(CalendarField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(CalendarField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(CalendarField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("calendar_agenda", payload, kCount);

  st.summary = std::string(f[static_cast<size_t>(CalendarField::SUMMARY)]);
  st.count   = v3_parse_int(f[static_cast<size_t>(CalendarField::EVENT_COUNT)], 0);
  return true;
}

inline uint32_t parse_hex_rgb_u24(std::string_view sv, uint32_t fallback) {
  if (sv.size() != 7 || sv[0] != '#') return fallback;
  auto hex_nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  uint32_t rgb = 0;
  for (size_t i = 1; i < 7; ++i) {
    int n = hex_nib(sv[i]);
    if (n < 0) return fallback;
    rgb = (rgb << 4) | static_cast<uint32_t>(n);
  }
  return rgb;
}

inline const char *pref_key_name(PrefKey key) {
  switch (key) {
    case PrefKey::POWER_PROFILE: return "power_profile";
    case PrefKey::WAKE_WORD_MODE: return "wake_word_mode";
    case PrefKey::TAP_TO_WAKE: return "tap_to_wake";
    case PrefKey::PICKUP_TO_WAKE: return "pickup_to_wake";
    case PrefKey::BATTERY_SCREEN_TIMEOUT: return "battery_screen_timeout";
    case PrefKey::DOCKED_IDLE_TIMEOUT: return "docked_idle_timeout";
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT: return "battery_hibernate_timeout";
    case PrefKey::DOCKED_SCREENSAVER: return "docked_screensaver";
    case PrefKey::SCREENSAVER_STYLE: return "screensaver_style";
    case PrefKey::HOLD_CROWN_POWER_OFF: return "hold_crown_power_off";
    case PrefKey::AUTO_BRIGHTNESS_MODE: return "auto_brightness_mode";
    case PrefKey::MANUAL_BRIGHTNESS: return "manual_brightness";
    case PrefKey::DAY_BRIGHTNESS: return "day_brightness";
    case PrefKey::NIGHT_BRIGHTNESS: return "night_brightness";
    case PrefKey::DAY_START_HOUR: return "day_start_hour";
    case PrefKey::NIGHT_START_HOUR: return "night_start_hour";
    case PrefKey::COLOR_PROFILE: return "color_profile";
    case PrefKey::VA_VISUAL_PROFILE: return "va_visual_profile";
    case PrefKey::START_SCREEN: return "start_screen";
    case PrefKey::CLOCK_FACE: return "clock_face";
    case PrefKey::TIME_FORMAT: return "time_format";
    case PrefKey::WEATHER_BACKGROUND: return "weather_background";
    case PrefKey::HOME_SUN: return "home_sun";
    case PrefKey::HOME_MOON: return "home_moon";
    case PrefKey::WAKE_GREETING: return "wake_greeting";
    case PrefKey::ALBUM_ART_STYLE: return "album_art_style";
    case PrefKey::MUSIC_OVERLAY_STYLE: return "music_overlay_style";
    case PrefKey::SHOW_SPEAKER_SHORTCUT: return "show_speaker_shortcut";
    case PrefKey::SHOW_LIBRARY_SHORTCUT: return "show_library_shortcut";
    case PrefKey::ALBUM_SORT_ORDER: return "album_sort_order";
    case PrefKey::ARTIST_ALBUM_SORT_ORDER: return "artist_album_sort_order";
    case PrefKey::PODCAST_SORT_ORDER: return "podcast_sort_order";
    case PrefKey::WW_SENS_OKAY_NABU: return "ww_sens_okay_nabu";
    case PrefKey::WW_SENS_HEY_LUNA: return "ww_sens_hey_luna";
    case PrefKey::WW_SENS_HEY_JARVIS: return "ww_sens_hey_jarvis";
    case PrefKey::WW_SENS_HEY_MYCROFT: return "ww_sens_hey_mycroft";
    case PrefKey::WW_SENS_ALEXA: return "ww_sens_alexa";
    case PrefKey::WAKE_SCREEN_ON_ASSIST: return "wake_screen_on_assist";
    case PrefKey::ALERT_VOLUME: return "alert_volume";
    case PrefKey::VOLUME_STEP_SIZE: return "volume_step_size";
    case PrefKey::GAME_SOUND_EFFECTS: return "game_sound_effects";
    case PrefKey::BLE_BEACON_ENABLED: return "ble_beacon_enabled";
    case PrefKey::BLE_BEACON_MODE: return "ble_beacon_mode";
    case PrefKey::HOME_SCREEN_ENABLED: return "home_screen_enabled";
    case PrefKey::RETURN_TO_HOME_TIMEOUT: return "return_to_home_timeout";
    case PrefKey::COUNT: break;
  }
  return "";
}

inline std::string get_pref_value_str(PrefKey key, const PersistedDevicePrefs &p = persisted_prefs()) {
  switch (key) {
    case PrefKey::POWER_PROFILE: return pref_power_profile_cstr(p);
    case PrefKey::WAKE_WORD_MODE: return pref_wake_word_mode_cstr(p);
    case PrefKey::TAP_TO_WAKE: return pref_tap_to_wake_cstr(p);
    case PrefKey::PICKUP_TO_WAKE: return pref_pickup_to_wake_cstr(p);
    case PrefKey::BATTERY_SCREEN_TIMEOUT: return pref_battery_screen_timeout_cstr(p);
    case PrefKey::DOCKED_IDLE_TIMEOUT: return pref_docked_idle_timeout_cstr(p);
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT: return pref_battery_hibernate_timeout_cstr(p);
    case PrefKey::DOCKED_SCREENSAVER: return p.docked_screensaver ? "1" : "0";
    case PrefKey::SCREENSAVER_STYLE: return pref_screensaver_style_cstr(p);
    case PrefKey::HOLD_CROWN_POWER_OFF: return p.hold_crown_power_off ? "1" : "0";
    case PrefKey::AUTO_BRIGHTNESS_MODE: return pref_auto_brightness_mode_cstr(p);
    case PrefKey::MANUAL_BRIGHTNESS: return pref_manual_brightness_cstr(p);
    case PrefKey::DAY_BRIGHTNESS: return pref_day_brightness_cstr(p);
    case PrefKey::NIGHT_BRIGHTNESS: return pref_night_brightness_cstr(p);
    case PrefKey::DAY_START_HOUR: return std::to_string(p.day_start_hour);
    case PrefKey::NIGHT_START_HOUR: return std::to_string(p.night_start_hour);
    case PrefKey::COLOR_PROFILE: return pref_color_profile_cstr(p);
    case PrefKey::VA_VISUAL_PROFILE: return pref_va_visual_profile_cstr(p);
    case PrefKey::START_SCREEN: return pref_start_screen_cstr(p);
    case PrefKey::CLOCK_FACE: return pref_clock_face_cstr(p);
    case PrefKey::TIME_FORMAT: return pref_time_format_cstr(p);
    case PrefKey::WEATHER_BACKGROUND: return p.weather_background ? "1" : "0";
    case PrefKey::HOME_SUN: return p.home_sun ? "1" : "0";
    case PrefKey::HOME_MOON: return p.home_moon ? "1" : "0";
    case PrefKey::WAKE_GREETING: return p.wake_greeting ? "1" : "0";
    case PrefKey::ALBUM_ART_STYLE: return pref_album_art_style_cstr(p);
    case PrefKey::MUSIC_OVERLAY_STYLE: return pref_music_overlay_style_cstr(p);
    case PrefKey::SHOW_SPEAKER_SHORTCUT: return p.show_speaker_shortcut ? "1" : "0";
    case PrefKey::SHOW_LIBRARY_SHORTCUT: return p.show_library_shortcut ? "1" : "0";
    case PrefKey::ALBUM_SORT_ORDER: return pref_album_sort_order_cstr(p);
    case PrefKey::ARTIST_ALBUM_SORT_ORDER: return pref_artist_album_sort_order_cstr(p);
    case PrefKey::PODCAST_SORT_ORDER: return pref_podcast_sort_order_cstr(p);
    case PrefKey::WW_SENS_OKAY_NABU: return pref_ww_sens_cstr(p.ww_sens_okay_nabu);
    case PrefKey::WW_SENS_HEY_LUNA: return pref_ww_sens_cstr(p.ww_sens_hey_luna);
    case PrefKey::WW_SENS_HEY_JARVIS: return pref_ww_sens_cstr(p.ww_sens_hey_jarvis);
    case PrefKey::WW_SENS_HEY_MYCROFT: return pref_ww_sens_cstr(p.ww_sens_hey_mycroft);
    case PrefKey::WW_SENS_ALEXA: return pref_ww_sens_cstr(p.ww_sens_alexa);
    case PrefKey::WAKE_SCREEN_ON_ASSIST: return p.wake_screen_on_assist ? "1" : "0";
    case PrefKey::ALERT_VOLUME: return std::to_string(p.alert_volume);
    case PrefKey::VOLUME_STEP_SIZE: return std::to_string(p.volume_step_size);
    case PrefKey::GAME_SOUND_EFFECTS: return p.game_sound_effects ? "1" : "0";
    case PrefKey::BLE_BEACON_ENABLED: return p.ble_beacon_enabled ? "1" : "0";
    case PrefKey::BLE_BEACON_MODE: return pref_ble_beacon_mode_cstr(p);
    case PrefKey::HOME_SCREEN_ENABLED: return p.home_screen_enabled ? "1" : "0";
    case PrefKey::RETURN_TO_HOME_TIMEOUT: return pref_return_to_home_timeout_cstr(p);
    case PrefKey::COUNT: break;
  }
  return "";
}

inline bool set_pref_by_key_str(PersistedDevicePrefs &p, PrefKey key, std::string_view sv,
                                bool cascade_profile, uint64_t &changed_mask, uint32_t &effects_mask) {
  if (sv.empty()) return false;
  auto apply_enum = [&]<size_t N>(uint8_t &slot, const char *const (&opts)[N], uint32_t eff, bool is_prof_subkey = false) -> bool {
    int idx = find_opt_index(opts, sv);
    if (idx < 0) return false;
    uint8_t u = static_cast<uint8_t>(idx);
    if (key == PrefKey::PICKUP_TO_WAKE && (!LATTER_HAS_IMU || LATTER_IS_STATIONARY)) u = 2;
    if (key == PrefKey::BATTERY_HIBERNATE_TIMEOUT && (!LATTER_HAS_BATTERY || LATTER_IS_STATIONARY)) u = 4;
    if (key == PrefKey::CLOCK_FACE && !LATTER_ROUND_DISPLAY && u == 1) u = 0;
    if (key == PrefKey::START_SCREEN && !p.home_screen_enabled && u == 0) u = 1;
    if (slot == u) return false;
    slot = u;
    changed_mask |= pref_key_bit(key);
    effects_mask |= (eff | kPrefEffectRedraw);
    if (key == PrefKey::CLOCK_FACE) p.clock_face_digital = (p.clock_face != 1);
    if (key == PrefKey::TIME_FORMAT) p.is_24h_format = (p.time_format == 1);
    if (cascade_profile && is_prof_subkey && p.power_profile != 5) {
      p.power_profile = 5;  // Custom
      changed_mask |= pref_key_bit(PrefKey::POWER_PROFILE);
    }
    return true;
  };
  auto apply_bool = [&](bool &slot, uint32_t eff, bool is_prof_subkey = false) -> bool {
    bool b = v3_parse_bool(sv);
    if (slot == b) return false;
    bool was_true = slot;
    slot = b;
    changed_mask |= pref_key_bit(key);
    effects_mask |= (eff | kPrefEffectRedraw);
    if (key == PrefKey::DOCKED_SCREENSAVER && was_true && !b) {
      effects_mask |= kPrefEffectDockedSaverOff;
    }
    if (cascade_profile && is_prof_subkey && p.power_profile != 5) {
      p.power_profile = 5;  // Custom
      changed_mask |= pref_key_bit(PrefKey::POWER_PROFILE);
    }
    return true;
  };
  auto apply_num = [&](uint8_t &slot, int lo, int hi, uint32_t eff) -> bool {
    int parsed = v3_parse_int(sv, -9999);
    if (parsed == -9999) return false;
    uint8_t cl = static_cast<uint8_t>(std::clamp(parsed, lo, hi));
    if (slot == cl) return false;
    slot = cl;
    changed_mask |= pref_key_bit(key);
    effects_mask |= (eff | kPrefEffectRedraw);
    return true;
  };

  switch (key) {
    case PrefKey::POWER_PROFILE:
      if (cascade_profile) {
        uint64_t before = changed_mask;
        apply_power_profile_to_prefs(p, sv, &changed_mask, &effects_mask);
        return changed_mask != before;
      }
      return apply_enum(p.power_profile, kPowerProfileOpts, kPrefEffectSleepTimer);
    case PrefKey::WAKE_WORD_MODE:
      return apply_enum(p.wake_word_mode, kWakeWordModeOpts, kPrefEffectWakeWordSync, true);
    case PrefKey::TAP_TO_WAKE:
      return apply_enum(p.tap_to_wake, kWakePolicyOpts, kPrefEffectPowerPolicy, true);
    case PrefKey::PICKUP_TO_WAKE:
      return apply_enum(p.pickup_to_wake, kWakePolicyOpts, kPrefEffectPowerPolicy, true);
    case PrefKey::BATTERY_SCREEN_TIMEOUT:
      return apply_enum(p.battery_screen_timeout, kBattScreenTimeoutOpts, kPrefEffectSleepTimer, true);
    case PrefKey::DOCKED_IDLE_TIMEOUT:
      return apply_enum(p.docked_idle_timeout, kDockedIdleTimeoutOpts, kPrefEffectSleepTimer, true);
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT:
      return apply_enum(p.battery_hibernate_timeout, kBattHibernateTimeoutOpts, kPrefEffectPowerPolicy, true);
    case PrefKey::DOCKED_SCREENSAVER:
      return apply_bool(p.docked_screensaver, kPrefEffectSleepTimer, true);
    case PrefKey::SCREENSAVER_STYLE:
      return apply_enum(p.screensaver_style, kScreensaverStyleOpts, 0);
    case PrefKey::HOLD_CROWN_POWER_OFF:
      return apply_bool(p.hold_crown_power_off, 0);
    case PrefKey::AUTO_BRIGHTNESS_MODE:
      return apply_enum(p.auto_brightness_mode, kAutoBrightnessModeOpts, kPrefEffectBrightness);
    case PrefKey::MANUAL_BRIGHTNESS:
      return apply_enum(p.manual_brightness, kBrightnessPresetOpts, kPrefEffectBrightness);
    case PrefKey::DAY_BRIGHTNESS:
      return apply_enum(p.day_brightness, kBrightnessPresetOpts, kPrefEffectBrightness);
    case PrefKey::NIGHT_BRIGHTNESS:
      return apply_enum(p.night_brightness, kBrightnessPresetOpts, kPrefEffectBrightness);
    case PrefKey::DAY_START_HOUR:
      return apply_num(p.day_start_hour, 0, 23, kPrefEffectBrightness);
    case PrefKey::NIGHT_START_HOUR:
      return apply_num(p.night_start_hour, 0, 23, kPrefEffectBrightness);
    case PrefKey::COLOR_PROFILE:
      return apply_enum(p.color_profile, kColorProfileOpts, kPrefEffectThemePreset);
    case PrefKey::VA_VISUAL_PROFILE:
      return apply_enum(p.va_visual_profile, kVaVisualProfileOpts, 0);
    case PrefKey::START_SCREEN:
      return apply_enum(p.start_screen, kStartScreenOpts, kPrefEffectNavPolicy);
    case PrefKey::CLOCK_FACE:
      return apply_enum(p.clock_face, kClockFaceOpts, 0);
    case PrefKey::TIME_FORMAT:
      return apply_enum(p.time_format, kTimeFormatOpts, 0);
    case PrefKey::WEATHER_BACKGROUND:
      return apply_bool(p.weather_background, 0);
    case PrefKey::HOME_SUN:
      return apply_bool(p.home_sun, 0);
    case PrefKey::HOME_MOON:
      return apply_bool(p.home_moon, 0);
    case PrefKey::WAKE_GREETING:
      return apply_bool(p.wake_greeting, 0);
    case PrefKey::ALBUM_ART_STYLE:
      return apply_enum(p.album_art_style, kAlbumArtStyleOpts, 0);
    case PrefKey::MUSIC_OVERLAY_STYLE:
      return apply_enum(p.music_overlay_style, kMusicOverlayStyleOpts, 0);
    case PrefKey::SHOW_SPEAKER_SHORTCUT:
      return apply_bool(p.show_speaker_shortcut, 0);
    case PrefKey::SHOW_LIBRARY_SHORTCUT:
      return apply_bool(p.show_library_shortcut, 0);
    case PrefKey::ALBUM_SORT_ORDER:
      return apply_enum(p.album_sort_order, kAlbumSortOpts, 0);
    case PrefKey::ARTIST_ALBUM_SORT_ORDER:
      return apply_enum(p.artist_album_sort_order, kArtistAlbumSortOpts, 0);
    case PrefKey::PODCAST_SORT_ORDER:
      return apply_enum(p.podcast_sort_order, kPodcastSortOpts, 0);
    case PrefKey::WW_SENS_OKAY_NABU:
      return apply_enum(p.ww_sens_okay_nabu, kWwSensitivityOpts, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_HEY_LUNA:
      return apply_enum(p.ww_sens_hey_luna, kWwSensitivityOpts, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_HEY_JARVIS:
      return apply_enum(p.ww_sens_hey_jarvis, kWwSensitivityOpts, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_HEY_MYCROFT:
      return apply_enum(p.ww_sens_hey_mycroft, kWwSensitivityOpts, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_ALEXA:
      return apply_enum(p.ww_sens_alexa, kWwSensitivityOpts, kPrefEffectWakeWordSens);
    case PrefKey::WAKE_SCREEN_ON_ASSIST:
      return apply_bool(p.wake_screen_on_assist, 0);
    case PrefKey::ALERT_VOLUME:
      return apply_num(p.alert_volume, 20, 100, 0);
    case PrefKey::VOLUME_STEP_SIZE:
      return apply_num(p.volume_step_size, 1, 10, 0);
    case PrefKey::GAME_SOUND_EFFECTS:
      return apply_bool(p.game_sound_effects, 0);
    case PrefKey::BLE_BEACON_ENABLED:
      return apply_bool(p.ble_beacon_enabled, kPrefEffectPowerPolicy);
    case PrefKey::BLE_BEACON_MODE:
      return apply_enum(p.ble_beacon_mode, kBleBeaconModeOpts, kPrefEffectPowerPolicy);
    case PrefKey::HOME_SCREEN_ENABLED: {
      const bool chg = apply_bool(p.home_screen_enabled, kPrefEffectNavPolicy);
      if (!p.home_screen_enabled && p.start_screen == 0) {
        p.start_screen = 1;  // Coerce Home -> Clock when Home Screen is disabled
        changed_mask |= pref_key_bit(PrefKey::START_SCREEN);
        effects_mask |= (kPrefEffectNavPolicy | kPrefEffectRedraw);
      }
      return chg;
    }
    case PrefKey::RETURN_TO_HOME_TIMEOUT:
      return apply_enum(p.return_to_home_timeout, kReturnToHomeTimeoutOpts, 0);
    case PrefKey::COUNT:
      break;
  }
  return false;
}

inline std::string encode_prefs_mask(uint64_t mask, const PersistedDevicePrefs &p = persisted_prefs()) {
  if (mask == 0ULL) return "";
  std::string out;
  out.reserve(128);
  for (uint8_t i = 0; i < static_cast<uint8_t>(PrefKey::COUNT); ++i) {
    const auto key = static_cast<PrefKey>(i);
    if ((mask & pref_key_bit(key)) == 0ULL) continue;
    const char *name = pref_key_name(key);
    if (name[0] == '\0') continue;
    if (!out.empty()) out.push_back(kProtocolRecordSep);
    out.append(name);
    out.push_back('=');
    out.append(get_pref_value_str(key, p));
  }
  return out;
}

// On first-time adoption (prefs_seq == 0), advertise compiled board defaults (excluding
// ble_beacon_enabled unless explicitly toggled locally) so Latter seeds board-appropriate defaults.
inline std::string build_hello_dirty_prefs(const PersistedDevicePrefs &p = persisted_prefs()) {
  if (p.prefs_seq == 0) {
    constexpr uint64_t kAllMask = (1ULL << static_cast<uint8_t>(PrefKey::COUNT)) - 1ULL;
    const uint64_t seed_mask = (kAllMask & ~pref_key_bit(PrefKey::BLE_BEACON_ENABLED)) | p.dirty_mask;
    return encode_prefs_mask(seed_mask, p);
  }
  return encode_prefs_mask(p.dirty_mask, p);
}

// Parse Protocol v3 device_prefs payload ("v3\x1f<epoch>\x1f<rev>\x1f<prefs_seq>\x1f...")
inline bool parse_v3_device_prefs_payload(std::string_view payload,
                                          uint32_t &out_effects,
                                          uint32_t now_ms = 0,
                                          PersistedDevicePrefs &p = persisted_prefs(),
                                          DevicePrefsState &st = device_prefs_state()) {
  out_effects = 0;
  constexpr size_t kCount = static_cast<size_t>(DevicePrefsField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(DevicePrefsField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(DevicePrefsField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(DevicePrefsField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("device_prefs", payload, kCount);

  uint64_t changed_mask = 0;
  auto apply_f = [&](DevicePrefsField df, PrefKey pk) {
    set_pref_by_key_str(p, pk, f[static_cast<size_t>(df)], false, changed_mask, out_effects);
  };

  apply_f(DevicePrefsField::POWER_PROFILE, PrefKey::POWER_PROFILE);
  apply_f(DevicePrefsField::WAKE_WORD_MODE, PrefKey::WAKE_WORD_MODE);
  apply_f(DevicePrefsField::TAP_TO_WAKE, PrefKey::TAP_TO_WAKE);
  apply_f(DevicePrefsField::PICKUP_TO_WAKE, PrefKey::PICKUP_TO_WAKE);
  apply_f(DevicePrefsField::BATTERY_SCREEN_TIMEOUT, PrefKey::BATTERY_SCREEN_TIMEOUT);
  apply_f(DevicePrefsField::DOCKED_IDLE_TIMEOUT, PrefKey::DOCKED_IDLE_TIMEOUT);
  apply_f(DevicePrefsField::BATTERY_HIBERNATE_TIMEOUT, PrefKey::BATTERY_HIBERNATE_TIMEOUT);
  apply_f(DevicePrefsField::DOCKED_SCREENSAVER, PrefKey::DOCKED_SCREENSAVER);
  apply_f(DevicePrefsField::SCREENSAVER_STYLE, PrefKey::SCREENSAVER_STYLE);
  apply_f(DevicePrefsField::HOLD_CROWN_POWER_OFF, PrefKey::HOLD_CROWN_POWER_OFF);
  apply_f(DevicePrefsField::AUTO_BRIGHTNESS_MODE, PrefKey::AUTO_BRIGHTNESS_MODE);
  apply_f(DevicePrefsField::MANUAL_BRIGHTNESS, PrefKey::MANUAL_BRIGHTNESS);
  apply_f(DevicePrefsField::DAY_BRIGHTNESS, PrefKey::DAY_BRIGHTNESS);
  apply_f(DevicePrefsField::NIGHT_BRIGHTNESS, PrefKey::NIGHT_BRIGHTNESS);
  apply_f(DevicePrefsField::DAY_START_HOUR, PrefKey::DAY_START_HOUR);
  apply_f(DevicePrefsField::NIGHT_START_HOUR, PrefKey::NIGHT_START_HOUR);
  apply_f(DevicePrefsField::COLOR_PROFILE, PrefKey::COLOR_PROFILE);
  apply_f(DevicePrefsField::VA_VISUAL_PROFILE, PrefKey::VA_VISUAL_PROFILE);
  apply_f(DevicePrefsField::HOME_SCREEN_ENABLED, PrefKey::HOME_SCREEN_ENABLED);
  apply_f(DevicePrefsField::START_SCREEN, PrefKey::START_SCREEN);
  apply_f(DevicePrefsField::RETURN_TO_HOME_TIMEOUT, PrefKey::RETURN_TO_HOME_TIMEOUT);
  apply_f(DevicePrefsField::CLOCK_FACE, PrefKey::CLOCK_FACE);
  apply_f(DevicePrefsField::TIME_FORMAT, PrefKey::TIME_FORMAT);
  apply_f(DevicePrefsField::WEATHER_BACKGROUND, PrefKey::WEATHER_BACKGROUND);
  apply_f(DevicePrefsField::HOME_SUN, PrefKey::HOME_SUN);
  apply_f(DevicePrefsField::HOME_MOON, PrefKey::HOME_MOON);
  apply_f(DevicePrefsField::WAKE_GREETING, PrefKey::WAKE_GREETING);
  apply_f(DevicePrefsField::ALBUM_ART_STYLE, PrefKey::ALBUM_ART_STYLE);
  apply_f(DevicePrefsField::MUSIC_OVERLAY_STYLE, PrefKey::MUSIC_OVERLAY_STYLE);
  apply_f(DevicePrefsField::SHOW_SPEAKER_SHORTCUT, PrefKey::SHOW_SPEAKER_SHORTCUT);
  apply_f(DevicePrefsField::SHOW_LIBRARY_SHORTCUT, PrefKey::SHOW_LIBRARY_SHORTCUT);
  apply_f(DevicePrefsField::ALBUM_SORT_ORDER, PrefKey::ALBUM_SORT_ORDER);
  apply_f(DevicePrefsField::ARTIST_ALBUM_SORT_ORDER, PrefKey::ARTIST_ALBUM_SORT_ORDER);
  apply_f(DevicePrefsField::PODCAST_SORT_ORDER, PrefKey::PODCAST_SORT_ORDER);
  apply_f(DevicePrefsField::WW_SENS_OKAY_NABU, PrefKey::WW_SENS_OKAY_NABU);
  apply_f(DevicePrefsField::WW_SENS_HEY_LUNA, PrefKey::WW_SENS_HEY_LUNA);
  apply_f(DevicePrefsField::WW_SENS_HEY_JARVIS, PrefKey::WW_SENS_HEY_JARVIS);
  apply_f(DevicePrefsField::WW_SENS_HEY_MYCROFT, PrefKey::WW_SENS_HEY_MYCROFT);
  apply_f(DevicePrefsField::WW_SENS_ALEXA, PrefKey::WW_SENS_ALEXA);
  apply_f(DevicePrefsField::WAKE_SCREEN_ON_ASSIST, PrefKey::WAKE_SCREEN_ON_ASSIST);
  apply_f(DevicePrefsField::ALERT_VOLUME, PrefKey::ALERT_VOLUME);
  apply_f(DevicePrefsField::VOLUME_STEP_SIZE, PrefKey::VOLUME_STEP_SIZE);
  apply_f(DevicePrefsField::GAME_SOUND_EFFECTS, PrefKey::GAME_SOUND_EFFECTS);
  apply_f(DevicePrefsField::BLE_BEACON_ENABLED, PrefKey::BLE_BEACON_ENABLED);
  apply_f(DevicePrefsField::BLE_BEACON_MODE, PrefKey::BLE_BEACON_MODE);

  const std::string_view sim_sv = f[static_cast<size_t>(DevicePrefsField::WEATHER_SIM_CONDITION)];
  if (!sim_sv.empty()) {
    int sim_idx = find_opt_index(kWeatherSimOpts, sim_sv);
    if (sim_idx >= 0 && st.weather_sim_condition != static_cast<uint8_t>(sim_idx)) {
      st.weather_sim_condition = static_cast<uint8_t>(sim_idx);
      out_effects |= kPrefEffectRedraw;
    }
  }

  const std::string_view hex_sv = f[static_cast<size_t>(DevicePrefsField::THEME_COLOR_HEX)];
  if (hex_sv.size() == 7 && hex_sv[0] == '#') {
    uint32_t new_rgb = parse_hex_rgb_u24(hex_sv, p.custom_theme_rgb);
    if (new_rgb != p.custom_theme_rgb) {
      p.custom_theme_rgb = new_rgb;
      changed_mask |= 1ULL;  // Trigger NVS persist
      if (p.color_profile == 4) {
        out_effects |= (kPrefEffectThemePreset | kPrefEffectRedraw);
      }
    }
  }

  const uint32_t in_seq = v3_parse_u32(f[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)], 0);
  const bool meta_changed = (in_seq > 0 && p.prefs_seq != in_seq) || (p.dirty_mask != 0ULL);
  if (in_seq > 0) p.prefs_seq = in_seq;
  p.dirty_mask = 0ULL;

  if (changed_mask != 0ULL || meta_changed) {
    schedule_persisted_prefs_save(now_ms);
  }
  return true;
}

struct CalendarRecord {
  std::string_view time;
  std::string_view title;
  std::string_view location;
  bool all_day = false;
};

inline size_t parse_calendar_records(std::string_view summary, CalendarRecord *out, size_t max_records) {
  if (summary.empty() || out == nullptr || max_records == 0) return 0;
  constexpr size_t kMaxRecords = 8;
  std::string_view recs[kMaxRecords];
  const size_t n = v3_split_fields(summary, recs, max_records < kMaxRecords ? max_records : kMaxRecords,
                                   kProtocolRecordSep);
  size_t count = 0;
  for (size_t i = 0; i < n; i++) {
    std::string_view f[4];
    if (v3_split_fields(recs[i], f, 4, '|') != 4) continue;
    out[count].time = f[0];
    out[count].title = f[1];
    out[count].location = f[2];
    out[count].all_day = (f[3] == "1");
    count++;
  }
  return count;
}

// Parse Contract v3 packed browse/area slots ("name\x1fid\x1ftype\x1factive\x1flevel\x1e...")
inline size_t parse_v3_slots_payload(std::string_view slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  for (size_t i = 0; i < max_slots; i++) {
    out_slots[i] = BrowseSlotItem{};
  }
  if (slots_blob.empty() || max_slots == 0) return 0;

  std::string_view recs[16];
  size_t rec_cnt = v3_split_fields(slots_blob, recs, (max_slots < 16) ? max_slots : 16, '\x1e');
  size_t valid_cnt = 0;
  for (size_t i = 0; i < rec_cnt && valid_cnt < max_slots; i++) {
    if (recs[i].empty()) continue;
    std::string_view cols[6];
    size_t col_cnt = v3_split_fields(recs[i], cols, 6, '\x1f');
    if (col_cnt >= 2) {
      out_slots[valid_cnt].name   = std::string(cols[0]);
      out_slots[valid_cnt].id     = std::string(cols[1]);
      out_slots[valid_cnt].type   = (col_cnt >= 3) ? std::string(cols[2]) : "";
      out_slots[valid_cnt].active = (col_cnt >= 4) ? v3_parse_bool(cols[3]) : false;
      out_slots[valid_cnt].level  = (col_cnt >= 5) ? v3_parse_int(cols[4], -1) : -1;
      valid_cnt++;
    }
  }
  return valid_cnt;
}

template <typename StrT, typename = decltype(std::declval<const StrT &>().c_str())>
inline size_t parse_v3_slots_payload(const StrT &slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  return parse_v3_slots_payload(std::string_view(slots_blob.c_str(), slots_blob.size()), out_slots, max_slots);
}

// Shared slot-response applier for both set_browse_slots and set_area_slots.
inline bool apply_slots_action(int contract, int req_id, std::string_view mode,
                               int page, int total_pages, std::string_view slots_blob,
                               uint32_t now_ms = 0, bool force_area_entities = false,
                               DeviceRuntimeState &rt = runtime_state()) {
  (void)contract;
  const latter_rpc::SlotChannel ch =
      force_area_entities ? latter_rpc::SlotChannel::AREA_ENTITIES
                          : latter_rpc::classify_browse_mode(mode);
  if (!latter_rpc::accept_req_id(ch, req_id)) return false;

  const size_t max_items =
      (ch == latter_rpc::SlotChannel::AREA_ENTITIES) ? DeviceRuntimeState::kCardSlotsPerPage
                                                     : DeviceRuntimeState::kListSlotsPerPage;
  parse_v3_slots_payload(slots_blob, rt.slots_for(ch), max_items);

  if (ch == latter_rpc::SlotChannel::MUSIC) {
    if (now_ms == 0 || (now_ms - rt.last_music_page_req_ms) > 350U || page == rt.slot_page_for(ch)) {
      rt.slot_page_for(ch) = page;
    }
  } else {
    rt.slot_page_for(ch) = page;
  }
  rt.slot_total_pages_for(ch) = total_pages;

  if (ch == latter_rpc::SlotChannel::AREA_ENTITIES) {
    rt.area.browse_mode = force_area_entities ? std::string(mode) : std::string("entities");
  }
  return true;
}

// Shared notification/alert banner populator for area_context push and show_notification action.
inline bool trigger_alert_banner(std::string_view title, std::string_view message,
                                 std::string_view priority, int duration_s,
                                 std::string_view camera_url, int alert_id,
                                 uint32_t now_ms, bool seed_only_if_unseen = false,
                                 AlertBannerState &ab = runtime_state().alert) {
  if (seed_only_if_unseen) {
    if (alert_id < 0) return false;
    if (ab.last_seen_alert_id < 0) {
      ab.last_seen_alert_id = alert_id;
      return false;
    }
    if (alert_id <= ab.last_seen_alert_id) return false;
    ab.last_seen_alert_id = alert_id;
  } else {
    if (alert_id > 0 && alert_id <= ab.last_seen_alert_id) return false;
    if (alert_id > 0) ab.last_seen_alert_id = alert_id;
  }

  ab.title = title.empty() ? std::string("Notification") : std::string(title);
  ab.message = std::string(message);
  ab.priority = priority.empty() ? std::string("normal") : std::string(priority);
  int dur = (duration_s > 0) ? duration_s : 10;
  if (dur < 3) dur = 3;
  if (dur > 60) dur = 60;
  ab.duration_ms = static_cast<uint32_t>(dur) * 1000U;
  ab.start_ms = now_ms;
  ab.camera_url = std::string(camera_url);
  ab.active = true;
  return true;
}

// ==============================================================================
// Canonical Music Playback Override & Volume Feedback Helpers
// ==============================================================================

inline int &remote_vol_pct() {
  static int pct = 70;
  return pct;
}

inline uint32_t &last_local_vol_tap_ms() {
  static uint32_t ts = 0;
  return ts;
}

inline uint32_t &remote_play_override_ms() {
  static uint32_t ms = 0;
  return ms;
}

inline bool &remote_play_override_state() {
  static bool st = false;
  return st;
}

inline void set_remote_play_override(bool playing, uint32_t now_ms) {
  remote_play_override_state() = playing;
  remote_play_override_ms() = now_ms;
}

inline void clear_remote_play_override() {
  remote_play_override_ms() = 0;
  remote_play_override_state() = false;
}

inline bool is_music_playing_effective(bool is_remote, bool local_playing,
                                       std::string_view remote_state_str, uint32_t now_ms) {
  if (!is_remote) {
    return local_playing;
  }
  if (remote_play_override_ms() > 0U && (now_ms - remote_play_override_ms() < 2500U)) {
    return remote_play_override_state();
  }
  return (remote_state_str == "playing" || remote_state_str == "buffering");
}

inline int step_music_volume_feedback(int dir, float step_pct_cfg, bool is_local,
                                      float local_mp_vol, uint32_t now_ms,
                                      MusicRuntimeState &m = runtime_state().music) {
  const int v_step = (std::isnan(step_pct_cfg) || step_pct_cfg <= 0.0f)
                         ? 5
                         : std::max(1, static_cast<int>(std::lround(step_pct_cfg)));
  const int delta = (dir >= 0) ? v_step : -v_step;
  last_local_vol_tap_ms() = now_ms;
  int next_pct = -1;
  if (is_local) {
    const int cur_pct = (std::isnan(local_mp_vol) || local_mp_vol <= 0.0f)
                            ? 70
                            : static_cast<int>(std::lround(local_mp_vol * 100.0f));
    next_pct = std::clamp(cur_pct + delta, 10, 100);
    m.action_feedback = "Volume: " + std::to_string(next_pct) + "%";
  } else if (remote_vol_pct() >= 0) {
    next_pct = std::clamp(remote_vol_pct() + delta, 0, 100);
    remote_vol_pct() = next_pct;
    m.action_feedback = "Volume: " + std::to_string(next_pct) + "%";
  } else {
    m.action_feedback = (dir >= 0) ? "Volume +" : "Volume -";
  }
  m.feedback_timer = now_ms;
  return next_pct;
}

static constexpr int kMaxDockApps = 10;

inline int dock_app_id_for_token(std::string_view tok) {
  if (tok == "music") return 2;
  if (tok == "alarm") return 9;
  if (tok == "area") return 6;
  if (tok == "remote") return 7;
  if (tok == "weather") return 5;
  if (tok == "camera") return 12;
  if (tok == "games") return 3;
  if (tok == "tools") return 8;
  return -1;
}

inline int collect_home_hub_tabs(int *out_tabs, const AreaContextState &ac = area_ctx_state()) {
  if (ac.epoch == 0U) {
    out_tabs[0] = 0;
    out_tabs[1] = 1;
    out_tabs[2] = 2;
    out_tabs[3] = 3;
    return 4;
  }
  return parse_home_categories_csv(ac.home_categories, out_tabs);
}

inline int resolve_anchor_dock_app(const PersistedDevicePrefs &p = persisted_prefs()) {
  const std::string_view opt(pref_start_screen_cstr(p));
  if (opt == "Home") return p.home_screen_enabled ? 1 : 0;
  if (opt == "Clock") return 0;
  if (opt == "Media") return 2;
  if (opt == "Home Hub") return 9;
  if (opt == "Area Controls") return 6;
  if (opt == "Remote") return 7;
  if (opt == "Weather") return 5;
  if (opt == "Cameras") return 12;
  if (opt == "Games") return 3;
  if (opt == "Tools") return 8;
  return p.home_screen_enabled ? 1 : 0;
}

inline bool should_return_to_home_on_idle(const DeviceRuntimeState &rt,
                                          const PersistedDevicePrefs &p,
                                          bool media_playing,
                                          bool cam_session_active = false) {
  if (!p.home_screen_enabled) return false;
  if (return_to_home_timeout_ms(p) == 0U) return false;
  if (rt.nav.gui_page == 1 && rt.nav.home_subpage == 0) return false;
  if (rt.nav.is_hibernating || rt.nav.ota_in_progress) return false;
  if (rt.alarms.ringing || rt.timers.ringing || rt.alarms.find_my_active || rt.alert.active) {
    return false;
  }
  if (rt.voice.active || rt.voice.tts_received || !rt.voice.status_text.empty() ||
      cam_session_active) {
    return false;
  }
  if (rt.nav.gui_page == 3 && rt.nav.active_game > 0) return false;
  if (rt.nav.gui_page == 2 && rt.music.overlay_mode == 0 && media_playing) {
    return false;
  }
  if (rt.nav.gui_page == 8) {
    if (rt.nav.tools_subpage == 1 && rt.timers.running) return false;
    if (rt.nav.tools_subpage == 3 && rt.stopwatch.running) return false;
    if (rt.nav.tools_subpage == 5 || rt.nav.tools_subpage == 7) {
      return false;
    }
  }
  return true;
}

// ==============================================================================
// Hibernate Snapshot POD & Cold-Boot Hydration (NVS-backed Frame #1 Seed)
// ==============================================================================
static constexpr uint32_t kHibernateSnapshotMagic   = 0x4C485350U;  // "LHSP"
static constexpr uint16_t kHibernateSnapshotVersion = 1U;
static constexpr uint32_t kHibernateSnapshotHash    = 0x4C484942U;  // "LHIB"

struct HibernateSnapshot {
  uint32_t magic = kHibernateSnapshotMagic;
  uint16_t version = kHibernateSnapshotVersion;
  bool sun_below_horizon = false;
  char dock_apps[96]{};
  char theme_color_hex[8]{};
  char area_name[48]{};
  char weather_condition[24]{};
  float weather_temp = NAN;
  char posix_tz[48]{};
};

static_assert(std::is_trivially_copyable_v<HibernateSnapshot>,
              "HibernateSnapshot must remain a POD blob for ESPPreferenceObject.");

template <size_t N>
inline void copy_bounded_cstr(char (&dst)[N], std::string_view src) {
  static_assert(N > 0, "Destination buffer must be non-empty");
  const size_t n = (src.size() < (N - 1)) ? src.size() : (N - 1);
  if (n > 0) std::memcpy(dst, src.data(), n);
  dst[n] = '\0';
}

#if LATTER_HAS_ESP_PREFS
inline esphome::ESPPreferenceObject &hibernate_snapshot_handle() {
  static esphome::ESPPreferenceObject pref;
  static bool inited = false;
  if (!inited && esphome::global_preferences != nullptr) {
    pref = esphome::global_preferences->make_preference<HibernateSnapshot>(kHibernateSnapshotHash);
    inited = true;
  }
  return pref;
}
#else
struct HostHibernateSnapshotStore {
  bool has_saved = false;
  HibernateSnapshot blob{};
};

inline HostHibernateSnapshotStore &host_hibernate_snapshot_store() {
  static HostHibernateSnapshotStore st{};
  return st;
}
#endif

inline std::string &hydrated_posix_tz() {
  static std::string tz;
  return tz;
}

#if !LATTER_HAS_ESP_PREFS
inline void reset_hibernate_snapshot_for_test() {
  host_hibernate_snapshot_store() = HostHibernateSnapshotStore{};
  hydrated_posix_tz().clear();
}
#endif

inline bool save_hibernate_snapshot(const char *tz_override = nullptr) {
  HibernateSnapshot snap{};
  snap.magic = kHibernateSnapshotMagic;
  snap.version = kHibernateSnapshotVersion;
  const auto &ac = area_ctx_state();
  const auto &ws = weather_state();
  copy_bounded_cstr(snap.dock_apps, ac.dock_apps);
  copy_bounded_cstr(snap.theme_color_hex, ac.theme_color_hex);
  copy_bounded_cstr(snap.area_name, ac.area_name);
  copy_bounded_cstr(snap.weather_condition, ws.condition);
  snap.weather_temp = ws.temperature;
  snap.sun_below_horizon = (ws.sun_state == "below_horizon");
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char *tz_env = (tz_override != nullptr) ? tz_override : std::getenv("TZ");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  if (tz_env != nullptr && tz_env[0] != '\0') {
    copy_bounded_cstr(snap.posix_tz, tz_env);
  }
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  const bool ok = hibernate_snapshot_handle().save(&snap);
  esphome::global_preferences->sync();
  return ok;
#else
  auto &store = host_hibernate_snapshot_store();
  store.blob = snap;
  store.has_saved = true;
  return true;
#endif
}

inline bool load_hibernate_snapshot_if_cold() {
  HibernateSnapshot snap{};
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  if (!hibernate_snapshot_handle().load(&snap)) return false;
#else
  const auto &store = host_hibernate_snapshot_store();
  if (!store.has_saved) return false;
  snap = store.blob;
#endif
  if (snap.magic != kHibernateSnapshotMagic || snap.version != kHibernateSnapshotVersion) {
    return false;
  }
  snap.dock_apps[sizeof(snap.dock_apps) - 1] = '\0';
  snap.theme_color_hex[sizeof(snap.theme_color_hex) - 1] = '\0';
  snap.area_name[sizeof(snap.area_name) - 1] = '\0';
  snap.weather_condition[sizeof(snap.weather_condition) - 1] = '\0';
  snap.posix_tz[sizeof(snap.posix_tz) - 1] = '\0';

  if (snap.posix_tz[0] != '\0') {
    hydrated_posix_tz() = snap.posix_tz;
#if defined(ESP_PLATFORM) && !defined(LATTER_HOST_TEST)
    setenv("TZ", snap.posix_tz, 1);
    tzset();
#endif
  }

  auto &ac = area_ctx_state();
  if (ac.dock_apps.empty() && snap.dock_apps[0] != '\0') {
    ac.dock_apps = snap.dock_apps;
  }
  if (ac.area_name.empty() && snap.area_name[0] != '\0') {
    ac.area_name = snap.area_name;
    if (runtime_state().area.active_area_name.empty() ||
        runtime_state().area.active_area_name == "Area Control") {
      runtime_state().area.active_area_name = snap.area_name;
    }
  }
  if (ac.theme_color_hex.empty() && snap.theme_color_hex[0] != '\0') {
    ac.theme_color_hex = snap.theme_color_hex;
    if (ac.theme_color_hex.size() == 7 && ac.theme_color_hex[0] == '#') {
      auto &p = persisted_prefs();
      p.custom_theme_rgb = parse_hex_rgb_u24(ac.theme_color_hex, p.custom_theme_rgb);
    }
  }

  auto &ws = weather_state();
  if (ws.condition.empty()) {
    if (snap.weather_condition[0] != '\0') {
      ws.condition = snap.weather_condition;
      ws.temperature = snap.weather_temp;
      ws.sun_state = snap.sun_below_horizon ? "below_horizon" : "above_horizon";
    } else if (ws.sun_state.empty()) {
      ws.sun_state = snap.sun_below_horizon ? "below_horizon" : "above_horizon";
    }
  }
  return true;
}

// ==============================================================================
// Handshake Deduplication, Crown Wake Latch & Animation Poller Coordination
// ==============================================================================
struct HelloSyncState {
  bool in_flight = false;
  uint32_t started_ms = 0;
  uint32_t last_sent_ms = 0;
  uint64_t last_sent_dirty_mask = 0;
};

inline HelloSyncState &hello_sync_state() {
  static HelloSyncState st{};
  return st;
}

inline void reset_hello_sync_on_connect() {
  auto &st = hello_sync_state();
  st.in_flight = false;
  st.started_ms = 0;
  st.last_sent_ms = 0;
  st.last_sent_dirty_mask = 0;
}

inline void mark_hello_in_flight(bool active, uint32_t now_ms = 0) {
  auto &st = hello_sync_state();
  st.in_flight = active;
  st.started_ms = active ? ((now_ms == 0U) ? 1U : now_ms) : 0U;
}

inline void mark_hello_sent(uint32_t now_ms, uint64_t dirty_mask) {
  auto &st = hello_sync_state();
  st.in_flight = false;
  st.started_ms = 0;
  st.last_sent_ms = (now_ms == 0U) ? 1U : now_ms;
  st.last_sent_dirty_mask = dirty_mask;
}

inline bool should_trigger_hello_on_ping(uint32_t now_ms, uint64_t dirty_mask) {
  const auto &st = hello_sync_state();
  if (st.in_flight && (st.started_ms == 0U || static_cast<uint32_t>(now_ms - st.started_ms) < 31000U)) {
    return false;
  }
  if (st.last_sent_ms != 0U &&
      static_cast<uint32_t>(now_ms - st.last_sent_ms) < 1500U &&
      dirty_mask == st.last_sent_dirty_mask) {
    return false;
  }
  return true;
}

inline bool &pwr_key_wake_consumed() {
  static bool latched = false;
  return latched;
}

inline bool &hibernate_ui_prepared() {
  static bool prep = false;
  return prep;
}

inline bool &ui_anim_poller_running() {
  static bool running = true;  // ESPHome interval: 20ms starts active at boot
  return running;
}

template <typename PollerT>
inline void set_ui_anim_poller(PollerT *tick, bool want) {
  if (tick == nullptr) return;
  if (ui_anim_poller_running() == want) return;
  ui_anim_poller_running() = want;
  if (want) {
    tick->start_poller();
  } else {
    tick->stop_poller();
  }
}

template <typename PollerT>
inline void ensure_ui_anim_poller(PollerT *tick) {
  set_ui_anim_poller(tick, true);
}

}  // namespace latter_ui
