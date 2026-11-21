#pragma once

// ==============================================================================
// Latter Core SDK: Protocol v3 Domain State, Runtime State & Persisted POD Prefs
// (include/latter_state.h)
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#if defined(USE_ESP32) && __has_include("esphome/core/preferences.h")
#include "esphome/core/preferences.h"
#define LATTER_HAS_ESP_PREFS 1
#else
#define LATTER_HAS_ESP_PREFS 0
#endif
#if defined(USE_ESP32) && !defined(LATTER_HOST_TEST) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define LATTER_HAS_SPIRAM_ALLOC 1
#else
#define LATTER_HAS_SPIRAM_ALLOC 0
#endif

#include "hal_manifest.h"
#include "hal_video_stream.h"
#include "audio_router.h"
#include "latter_protocol.h"
#include "latter_rpc.h"

namespace latter_ui {

// Canonical top-level page IDs.
namespace PageId {
  static constexpr int CLOCK      = 0;
  static constexpr int HOME       = 1;
  static constexpr int MUSIC      = 2;
  static constexpr int GAMES      = 3;
  static constexpr int SYSTEM     = 4;
  static constexpr int WEATHER    = 5;
  static constexpr int AREA       = 6;
  static constexpr int REMOTE     = 7;
  static constexpr int TOOLS      = 8;
  static constexpr int HOME_HUB   = 9;
  static constexpr int TIME       = 10;
  static constexpr int PINNED     = 11;
  static constexpr int CALENDAR   = 12;
  static constexpr int CAMERAS    = 13;
}  // namespace PageId

namespace CamerasTab {
  static constexpr int GRID     = 0;
  static constexpr int SINGLE   = 1;
  static constexpr int CAROUSEL = 2;
  static constexpr int SETTINGS = 3;
}  // namespace CamerasTab

inline constexpr uint8_t kCamCapAudio    = 0x01U;
inline constexpr uint8_t kCamCapTalkback = 0x02U;
inline constexpr uint8_t kCamCapDoorbell = 0x04U;
inline constexpr uint8_t kCamCapCarousel = 0x08U;
inline constexpr uint8_t kCamCapReply    = 0x10U;
inline constexpr uint8_t kCamCapUnlock   = 0x20U;

namespace CalendarSubpage {
  static constexpr int OVERVIEW = 0;
  static constexpr int DAY      = 1;
  static constexpr int WEEK     = 2;
  static constexpr int MONTH    = 3;
}  // namespace CalendarSubpage

namespace CalDrilldown {
  static constexpr int NONE          = 0;
  static constexpr int DETAIL        = 1;
  static constexpr int MAP           = 2;
  static constexpr int ADD_STEP1     = 3;
  static constexpr int ADD_STEP2     = 4;
  static constexpr int PICK_TITLE    = 5;
  static constexpr int PICK_LOCATION = 6;
  static constexpr int PICK_CALENDAR = 7;
  static constexpr int PICK_REPEAT   = 8;
}  // namespace CalDrilldown

enum class MusicView : uint8_t {
  NOW_PLAYING  = 0,
  LIBRARY      = 1,
  SPEAKERS     = 2,
  VOLUME       = 3,
  QUEUE        = 4,
  LYRICS       = 5,
  ALBUM_TRACKS = 6,
  INFO         = 7,
  SETTINGS     = 8,
};

enum class MusicDrilldown : uint8_t {
  NONE            = 0,
  LIB_LIST        = 1,
  SPEAKER_ACTIONS = 2,
};

enum class MusicTakeover : uint8_t {
  NONE         = 0,
  SCRUB        = 1,
  ACTION_SHEET = 2,
};

inline constexpr int kMusicPeerTabCount = 9;

inline constexpr bool is_music_ambient_tab(MusicView v) noexcept {
  return v == MusicView::NOW_PLAYING || v == MusicView::LYRICS || v == MusicView::INFO;
}

inline constexpr uint32_t music_peer_tab_timeout_ms(MusicView v) noexcept {
  if (is_music_ambient_tab(v)) return 6000U;
  return (v == MusicView::VOLUME) ? 5000U : 10000U;
}

inline constexpr bool resolve_music_route_target(
    RouteId route_id, MusicView &out_view, MusicDrilldown &out_drilldown) noexcept {
  out_drilldown = MusicDrilldown::NONE;
  switch (route_id) {
    case RouteId::APP_MUSIC:
      out_view = MusicView::NOW_PLAYING;
      return true;
    case RouteId::MUSIC_LIBRARY:
      out_view = MusicView::LIBRARY;
      return true;
    case RouteId::MUSIC_SPEAKERS:
      out_view = MusicView::SPEAKERS;
      return true;
    case RouteId::MUSIC_QUEUE:
      out_view = MusicView::QUEUE;
      return true;
    case RouteId::MUSIC_LYRICS:
      out_view = MusicView::LYRICS;
      return true;
    case RouteId::MUSIC_ALBUM_TRACKS:
      out_view = MusicView::ALBUM_TRACKS;
      return true;
    case RouteId::MUSIC_INFO:
      out_view = MusicView::INFO;
      return true;
    case RouteId::MUSIC_FAVORITES:
    case RouteId::MUSIC_PLAYLISTS:
      out_view = MusicView::LIBRARY;
      out_drilldown = MusicDrilldown::LIB_LIST;
      return true;
    default:
      return false;
  }
}

enum class MusicRepeatMode : uint8_t {
  OFF = 0,
  ALL = 1,
  ONE = 2,
};

inline constexpr uint8_t parse_music_repeat_mode(std::string_view sv) noexcept {
  if (sv == "one" || sv == "ONE" || sv == "repeat_one" || sv == "2") {
    return static_cast<uint8_t>(MusicRepeatMode::ONE);
  }
  if (sv == "all" || sv == "ALL" || sv == "repeat_all" || sv == "1") {
    return static_cast<uint8_t>(MusicRepeatMode::ALL);
  }
  return static_cast<uint8_t>(MusicRepeatMode::OFF);
}

inline constexpr const char *music_repeat_mode_cstr(uint8_t mode) noexcept {
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ONE)) return "one";
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ALL)) return "all";
  return "off";
}

inline constexpr const char *music_repeat_mode_label(uint8_t mode) noexcept {
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ONE)) return "One";
  if (mode == static_cast<uint8_t>(MusicRepeatMode::ALL)) return "All";
  return "Off";
}

inline std::string build_music_track_key(std::string_view title, std::string_view artist) {
  if (title.empty()) return "";
  std::string out;
  out.reserve(title.size() + 1U + artist.size());
  out.append(title);
  out.push_back('|');
  out.append(artist);
  return out;
}

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

enum class WakeWordMode : uint8_t {
  DockedOnly = 0,
  AlwaysOn   = 1,
  Disabled   = 2,
};

enum class WakePolicyMode : uint8_t {
  AlwaysOn   = 0,
  DockedOnly = 1,
  Disabled   = 2,
};

enum class TapWakeMode : uint8_t {
  SingleTap  = 0,
  DoubleTap  = 1,
  DockedOnly = 2,
  Disabled   = 3,
};

enum class ScreensaverMode : uint8_t {
  DockedOnly = 0,
  Always     = 1,
  Never      = 2,
};

enum class ScreensaverStyle : uint8_t {
  AmbientClock = 0,
  PhotoFrame   = 1,
  Aurora       = 2,
  Starfield    = 3,
  DigitalRain  = 4,
  Fireworks    = 5,
};

inline constexpr std::string_view kDayNightSkyWallpaperUrl = "latter://day_night_sky";

inline constexpr bool is_day_night_sky_wallpaper_url(std::string_view url) noexcept {
  return url == kDayNightSkyWallpaperUrl;
}

inline constexpr bool resolve_day_night_sky_is_night(
    std::string_view sun_state, bool time_valid, int hour) noexcept {
  if (sun_state == "below_horizon") return true;
  if (sun_state == "above_horizon") return false;
  if (time_valid && hour >= 0 && hour <= 23) {
    return (hour < 6 || hour >= 20);
  }
  return false;
}

enum class AutoBrightnessMode : uint8_t {
  Disabled        = 0,
  SunSynchronized = 1,
  TimeSchedule    = 2,
};

enum class ColorProfilePref : uint8_t {
  SteelBlue = 0,
  Pink      = 1,
  Blue      = 2,
  Green     = 3,
  Custom    = 4,
};

enum class VaVisualStyle : uint8_t {
  Nebula   = 0,
  Solaris  = 1,
  Rings    = 2,
  LightBar = 3,
  DotWave  = 4,
};

enum class ClockFaceStyle : uint8_t {
  MinimalDigital = 0,
  ModernAnalog   = 1,
  HybridDigital  = 2,
};

enum class MediaPlaybackState : uint8_t {
  Idle       = 0,
  Playing    = 1,
  Paused     = 2,
  Buffering  = 3,
  Announcing = 4,
};

enum class AlertPriority : uint8_t {
  Normal   = 0,
  Critical = 1,
};

enum class UpdateCheckState : uint8_t {
  Idle     = 0,
  Checking = 1,
  Checked  = 2,
};

inline constexpr MediaPlaybackState parse_media_playback_state(std::string_view sv) noexcept {
  if (sv == "playing") return MediaPlaybackState::Playing;
  if (sv == "paused") return MediaPlaybackState::Paused;
  if (sv == "buffering") return MediaPlaybackState::Buffering;
  if (sv == "announcing") return MediaPlaybackState::Announcing;
  return MediaPlaybackState::Idle;
}

inline constexpr AlertPriority parse_alert_priority(std::string_view sv) noexcept {
  return (sv == "critical") ? AlertPriority::Critical : AlertPriority::Normal;
}

struct LinkState {
  bool ha_connected = false;
  bool wifi_connected = false;
  uint32_t last_ha_connect_ms = 0;
  uint32_t last_ha_disconnect_ms = 0;
  uint32_t last_wifi_connect_ms = 0;
};

namespace HomeHubTab {
  static constexpr int ALARM      = 0;
  static constexpr int INTERCOM   = 1;
  static constexpr int THERMOSTAT = 2;
  static constexpr int ENTRY      = 3;
  static constexpr int VACUUM     = 4;
}  // namespace HomeHubTab

namespace ToolsSubpage {
  static constexpr int LAUNCHER   = 0;
  static constexpr int STOPWATCH  = 3;
  static constexpr int DICE       = 4;
  static constexpr int FLASHLIGHT = 5;
  static constexpr int CALCULATOR = 6;
  static constexpr int LEVEL      = 7;
  static constexpr int COIN_FLIP  = 8;
}  // namespace ToolsSubpage

inline constexpr int kMaxHomeHubTabs = 5;

inline constexpr uint16_t kAlarmCapCodeDisarm  = 0x01U;
inline constexpr uint16_t kAlarmCapCodeArm     = 0x02U;
inline constexpr uint16_t kAlarmCapArmHome     = 0x04U;
inline constexpr uint16_t kAlarmCapArmAway     = 0x08U;
inline constexpr uint16_t kAlarmCapArmNight    = 0x10U;
inline constexpr uint16_t kAlarmCapArmVacation = 0x20U;
inline constexpr uint16_t kAlarmCapDefault     = kAlarmCapArmHome | kAlarmCapArmAway;

inline constexpr uint16_t kClimateCapOff        = 0x01U;
inline constexpr uint16_t kClimateCapHeat       = 0x02U;
inline constexpr uint16_t kClimateCapCool       = 0x04U;
inline constexpr uint16_t kClimateCapAuto       = 0x08U;
inline constexpr uint16_t kClimateCapFan        = 0x10U;
inline constexpr uint16_t kClimateCapDry        = 0x20U;
inline constexpr uint16_t kClimateCapTargetTemp = 0x40U;
inline constexpr uint16_t kClimateCapCelsius    = 0x80U;
inline constexpr uint16_t kClimateCapDefault    =
    kClimateCapOff | kClimateCapHeat | kClimateCapCool | kClimateCapAuto | kClimateCapTargetTemp;

inline constexpr uint16_t kVacuumCapStart      = 0x01U;
inline constexpr uint16_t kVacuumCapPause      = 0x02U;
inline constexpr uint16_t kVacuumCapStop       = 0x04U;
inline constexpr uint16_t kVacuumCapReturnHome = 0x08U;
inline constexpr uint16_t kVacuumCapCleanSpot  = 0x10U;
inline constexpr uint16_t kVacuumCapLocate     = 0x20U;
inline constexpr uint16_t kVacuumCapDefault    =
    kVacuumCapStart | kVacuumCapPause | kVacuumCapReturnHome;

inline constexpr uint16_t kMediaCapPause          = 0x0001U;
inline constexpr uint16_t kMediaCapSeek           = 0x0002U;
inline constexpr uint16_t kMediaCapVolumeSet      = 0x0004U;
inline constexpr uint16_t kMediaCapVolumeMute     = 0x0008U;
inline constexpr uint16_t kMediaCapPreviousTrack  = 0x0010U;
inline constexpr uint16_t kMediaCapNextTrack      = 0x0020U;
inline constexpr uint16_t kMediaCapShuffleSet     = 0x0040U;
inline constexpr uint16_t kMediaCapRepeatSet      = 0x0080U;
inline constexpr uint16_t kMediaCapGrouping       = 0x0100U;
inline constexpr uint16_t kMediaCapMusicAssistant = 0x0200U;
inline constexpr uint16_t kMediaCapFavorite       = 0x0400U;
inline constexpr uint16_t kMediaCapDefault        =
    kMediaCapPause | kMediaCapSeek | kMediaCapVolumeSet | kMediaCapVolumeMute |
    kMediaCapPreviousTrack | kMediaCapNextTrack;

inline constexpr int kMaxThermostatZones  = 8;
inline constexpr int kMaxEntryLockItems   = 8;
inline constexpr int kMaxEntryGarageItems = 6;
inline constexpr int kMaxEntryItems       = kMaxEntryLockItems + kMaxEntryGarageItems;
inline constexpr int kMaxVacuumUnits      = 6;
inline constexpr int kMaxAreaClimates     = 6;

struct ThermostatZoneItem {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  std::string mode = "";
  int target_temp = -1;
  float current_temp = NAN;
  uint16_t caps = 0;
};

struct EntryControlItem {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  bool is_lock = true;
  LockState lock_state = LockState::UNKNOWN;
  GarageState garage_state = GarageState::UNKNOWN;
};

struct VacuumUnitItem {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  std::string state = "";
  uint16_t caps = 0;
};

struct HomeHubState {
  int tab = 0;
  int mode_bar_page = 0;
  int thermostat_idx = 0;
  int thermostat_count = 0;
  ThermostatZoneItem thermostats[kMaxThermostatZones]{};
  int entry_page = 0;
  int entry_count = 0;
  int broadcast_page = 0;
  EntryControlItem entries[kMaxEntryItems]{};
  int vacuum_idx = 0;
  int vacuum_count = 0;
  VacuumUnitItem vacuums[kMaxVacuumUnits]{};
  int area_climate_count = 0;
  ThermostatZoneItem area_climates[kMaxAreaClimates]{};
  uint32_t local_mutation_seq = 0;
};

// Protocol v3 area_context state.
struct AreaContextState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string area_id = "";
  std::string area_name = "";
  std::string site_id = "";
  std::string ble_closest_proxy = "";
  std::string wallpaper_url = "";
  std::string available_domains = "";
  float area_temp = NAN;
  float area_humidity = NAN;
  int lights_on = 0;
  int covers_open = 0;
  std::string alarm_entity = "";
  std::string alarm_state = "";
  uint16_t alarm_caps = 0;
  std::string thermostats_summary = "";
  std::string locks_summary = "";
  std::string garages_summary = "";
  std::string vacuums_summary = "";
  std::string area_climates_summary = "";
  std::string camera_snapshot_url = "";
  std::string camera_stream_url = "";
  std::string cameras_summary = "";
  std::string doorbell_talk_state = "";
  std::string broadcast_phrases = "";
  int alert_id = -1;
  std::string alert_title = "";
  std::string alert_message = "";
  std::string alert_priority = "normal";
  AlertPriority alert_priority_level = AlertPriority::Normal;
  int alert_duration = 10;
  bool alert_chime = true;
  std::string alert_camera_url = "";
  std::string alert_camera_entity = "";
  std::string activity_card = "";        // "1" / "0"; empty = not set -> kDefaultActivityCard
  std::string activity_sources = "";     // csv of source keys; empty = not set -> kDefaultActivitySources
  std::string security_pin_policy = "";  // "allow" | "require_pin" | "block"
  std::string home_categories = "";      // ordered available Home Hub category tokens ("alarm,intercom,thermostat,entry,vacuum")
  std::string area_icon = "";            // Home Assistant AreaEntry icon (e.g. "mdi:sofa") or inferred archetype icon
  std::string local_tz_info = "";        // "<iana>|<city>|<abbr>|<utc_offset_min>|<is_dst_0_or_1>"
  std::string world_clocks_summary = ""; // ";"-delimited "<iana>|<city>|<abbr>|<utc_offset_min>|<is_dst_0_or_1>"
  std::string screensaver_photo_url = "";
};

// Protocol v3 active_media state.
struct MediaState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string state = "";
  MediaPlaybackState playback_state = MediaPlaybackState::Idle;
  bool playing = false;
  std::string title = "";
  std::string artist = "";
  std::string album = "";
  float duration = 0.0f;
  float position = 0.0f;
  uint32_t position_updated_at = 0;
  uint32_t position_updated_ms = 0;
  float volume = 0.5f;
  bool has_volume = false;
  std::string media_type = "";
  std::string art_url = "";
  std::string active_speaker = "";
  std::string speaker_name = "";
  std::string last_speaker = "";
  std::string local_speaker = "";
  bool is_local = false;
  bool shuffle = false;
  uint8_t repeat_mode = 0;  // 0 = off, 1 = all, 2 = one
  std::string repeat = "off";
  bool muted = false;
  bool favorite = false;
  int queue_count = 0;
  uint16_t media_caps = 0;

  uint32_t interpolated_position_ms(uint32_t now_ms) const noexcept {
    const float base_s = (position > 0.0f && !std::isnan(position)) ? position : 0.0f;
    double pos_ms = static_cast<double>(base_s) * 1000.0;
    const bool is_playing =
        playing || playback_state == MediaPlaybackState::Playing || state == "playing";
    if (is_playing && position_updated_ms > 0U && now_ms > position_updated_ms) {
      pos_ms += static_cast<double>(now_ms - position_updated_ms);
    }
    if (duration > 0.0f && !std::isnan(duration)) {
      const double max_ms = static_cast<double>(duration) * 1000.0;
      if (pos_ms > max_ms) pos_ms = max_ms;
    }
    return (pos_ms > 0.0) ? static_cast<uint32_t>(pos_ms + 0.5) : 0U;
  }
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

// Protocol v3 calendar_agenda state.
struct CalendarState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string summary = "";
  int count = 0;
  std::string calendars_meta = "";
  std::string locations_catalog = "";
};

struct BrowseSlotItem {
  std::string name = "";
  std::string id = "";
  std::string type = "";
  bool active = false;
  int level = -1;
  std::string queue_item_id = "";
  bool is_current = false;
};

using MusicBrowseSlot = BrowseSlotItem;

// Protocol v3 shortcuts state (App Dock + Pinned App universal shortcut engine).
enum class ShortcutKind : uint8_t {
  ROUTE = 0,
  ENTITY = 1,
};

enum class ShortcutOrigin : uint8_t {
  NONE = 0,
  HOME_DOCK = 1,
  PINNED_APP = 2,
};

struct ShortcutItem {
  ShortcutKind kind = ShortcutKind::ROUTE;
  RouteId route_id = RouteId::NONE;
  std::string target = "";
  std::string action = "";
  std::string label = "";
  std::string icon = "";   // empty = native bespoke glyph (for ROUTE), or "mdi:<slug>"
  bool confirm = false;
  int8_t state = -1;       // -1 = stateless/route, 0 = off/closed/locked, 1 = on/open/unlocked, 2 = unavailable
};

struct ShortcutsState {
  static constexpr size_t kMaxDockItems = 16;
  static constexpr size_t kMaxPinnedItems = 256;
  uint32_t epoch = 0;
  uint32_t rev = 0;
  bool dock_configured = false;
  bool live_received = false;
  uint16_t cold_pinned_total = 0;
  std::vector<ShortcutItem> dock_items{};
  std::vector<ShortcutItem> pinned_items{};
  // Multi-chunk assembly buffer (commits atomically when chunk_index + 1 == chunk_count)
  uint32_t staging_epoch = 0;
  uint32_t staging_rev = 0;
  uint16_t staging_chunk_count = 0;
  uint16_t staging_next_chunk = 0;
  uint16_t staging_pinned_total = 0;
  std::vector<ShortcutItem> staging_dock_items{};
  std::vector<ShortcutItem> staging_pinned_items{};
};

struct ShortcutConfirmState {
  static constexpr uint32_t kAutoDismissMs = 10000U;
  bool open = false;
  uint32_t opened_ms = 0;
  bool is_danger = false;
  std::string title = "";
  std::string line1 = "";
  std::string line2 = "";
  std::string verb_btn = "";
  std::string entity_id = "";
  std::string action = "";

  void clear() { *this = ShortcutConfirmState{}; }
};

// ==============================================================================
// Versioned Persisted Device Preferences (Single ESPPreferenceObject POD Blob)
// ==============================================================================
static constexpr uint32_t kPersistedPrefsMagic = 0x4C505246U;  // "LPRF"
static constexpr uint16_t kPersistedPrefsVersion = 17U;
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
  COUNT = 64,
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
  int32_t high_score_2048 = 0;
  int32_t high_score_memory = 0;
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

  // Zero-size constexpr typed preference accessors (preserves exact NVS POD layout)
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
    return static_cast<AutoBrightnessMode>((auto_brightness_mode < 3U) ? auto_brightness_mode : 1U);
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

static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs>,
              "PersistedDevicePrefs must remain a POD blob for ESPPreferenceObject.");
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
inline constexpr const char *const kAutoBrightnessModeOpts[3] = {
    "Disabled", "Sun Synchronized", "Time Schedule"};
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
};

struct ActiveBrightnessTarget {
  ActiveBrightnessTargetKind kind{ActiveBrightnessTargetKind::MANUAL};
  PrefKey pref_key{PrefKey::MANUAL_BRIGHTNESS};
  uint8_t pct{80};
  const char *context_label{"Manual"};  // "Bedtime", "Manual", "Auto · Day", "Auto · Night"
};

inline ActiveBrightnessTarget resolve_active_brightness_target(
    const PersistedDevicePrefs &prefs,
    std::string_view sun_state,
    bool time_valid,
    int hour) noexcept {
  if (prefs.bedtime_active) {
    return {ActiveBrightnessTargetKind::BEDTIME, PrefKey::BEDTIME_BRIGHTNESS,
            prefs.bedtime_brightness_pct(), "Bedtime"};
  }
  const std::string_view mode = pref_auto_brightness_mode_cstr(prefs);
  if (mode == "Sun Synchronized") {
    const bool is_night = (sun_state == "below_horizon");
    if (is_night) {
      return {ActiveBrightnessTargetKind::NIGHT, PrefKey::NIGHT_BRIGHTNESS,
              prefs.night_brightness_pct(), "Auto · Night"};
    }
    return {ActiveBrightnessTargetKind::DAY, PrefKey::DAY_BRIGHTNESS,
            prefs.day_brightness_pct(), "Auto · Day"};
  }
  if (mode == "Time Schedule") {
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
              prefs.day_brightness_pct(), "Auto · Day"};
    }
    return {ActiveBrightnessTargetKind::NIGHT, PrefKey::NIGHT_BRIGHTNESS,
            prefs.night_brightness_pct(), "Auto · Night"};
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

template <size_t N>
inline void copy_bounded_cstr(char (&dst)[N], std::string_view src) {
  static_assert(N > 0, "Destination buffer must be non-empty");
  const size_t n = (src.size() < (N - 1)) ? src.size() : (N - 1);
  if (n > 0) std::memcpy(dst, src.data(), n);
  dst[n] = '\0';
}

inline std::string_view trim_ascii_ws(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
    sv.remove_suffix(1);
  }
  return sv;
}

inline constexpr const char *const kCanonicalToolsItemTokens[6] = {
    "stopwatch", "level", "flashlight", "calculator", "dice", "coin_flip"};
inline constexpr const char *const kCanonicalGamesItemTokens[6] = {
    "marbles", "archery", "bricks", "chomp", "2048", "memory"};

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

// 1-based game IDs matching NavRuntimeState::active_game (1=Marbles .. 6=Memory Match).
inline constexpr int game_id_for_token(std::string_view tok) noexcept {
  if (tok == "marbles") return 1;
  if (tok == "archery") return 2;
  if (tok == "bricks") return 3;
  if (tok == "chomp") return 4;
  if (tok == "2048") return 5;
  if (tok == "memory") return 6;
  return 0;
}

inline std::string normalize_tools_items_csv(std::string_view sv) {
  std::string_view seen[6]{};
  int count = 0;
  size_t start = 0;
  while (start <= sv.size() && count < 6) {
    const size_t pos = sv.find(',', start);
    const std::string_view tok = trim_ascii_ws(
        (pos == std::string_view::npos) ? sv.substr(start) : sv.substr(start, pos - start));
    if (tools_subpage_for_token(tok) > 0) {
      bool dup = false;
      for (int i = 0; i < count; ++i) {
        if (seen[i] == tok) {
          dup = true;
          break;
        }
      }
      if (!dup) seen[count++] = tok;
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  if (count <= 0) return std::string(kDefaultToolsItems);
  std::string out;
  out.reserve(56);
  for (int i = 0; i < count; ++i) {
    if (i > 0) out.push_back(',');
    out.append(seen[i]);
  }
  return out;
}

inline std::string normalize_games_items_csv(std::string_view sv) {
  std::string_view seen[6]{};
  int count = 0;
  size_t start = 0;
  while (start <= sv.size() && count < 6) {
    const size_t pos = sv.find(',', start);
    const std::string_view tok = trim_ascii_ws(
        (pos == std::string_view::npos) ? sv.substr(start) : sv.substr(start, pos - start));
    if (game_id_for_token(tok) > 0) {
      bool dup = false;
      for (int i = 0; i < count; ++i) {
        if (seen[i] == tok) {
          dup = true;
          break;
        }
      }
      if (!dup) seen[count++] = tok;
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  if (count <= 0) return std::string(kDefaultGamesItems);
  std::string out;
  out.reserve(48);
  for (int i = 0; i < count; ++i) {
    if (i > 0) out.push_back(',');
    out.append(seen[i]);
  }
  return out;
}

inline std::string_view effective_tools_items_sv(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return (p.tools_items[0] != '\0') ? std::string_view(p.tools_items) : std::string_view(kDefaultToolsItems);
}

inline std::string_view effective_games_items_sv(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return (p.games_items[0] != '\0') ? std::string_view(p.games_items) : std::string_view(kDefaultGamesItems);
}

struct ToolsLauncherItemSpec {
  int id = 0;
  const char *label = "";
};

inline constexpr int kMaxToolsLauncherItems = 6;
inline constexpr int kMaxGamesLauncherItems = 6;

inline int collect_tools_launcher_items_for(std::string_view csv,
                                            bool round_display,
                                            bool has_imu,
                                            ToolsLauncherItemSpec *out_items,
                                            int max_items = kMaxToolsLauncherItems) noexcept {
  if (out_items == nullptr || max_items <= 0) return 0;
  int count = 0;
  size_t start = 0;
  const int utility_cap = std::min(max_items, kMaxToolsLauncherItems);
  while (!csv.empty() && start <= csv.size() && count < utility_cap) {
    const size_t pos = csv.find(',', start);
    const std::string_view tok = trim_ascii_ws(
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start));
    const int sub = tools_subpage_for_token(tok);
    if (sub > 0) {
      const bool cap_ok =
          (sub != ToolsSubpage::STOPWATCH || round_display) &&
          (sub != ToolsSubpage::LEVEL || has_imu);
      if (cap_ok) {
        bool dup = false;
        for (int i = 0; i < count; ++i) {
          if (out_items[i].id == sub) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          out_items[count++] = ToolsLauncherItemSpec{sub, tools_launcher_label_for_subpage(sub)};
        }
      }
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return count;
}

inline int collect_tools_launcher_items(ToolsLauncherItemSpec *out_items,
                                        const PersistedDevicePrefs &p = persisted_prefs(),
                                        int max_items = kMaxToolsLauncherItems) noexcept {
  return collect_tools_launcher_items_for(
      effective_tools_items_sv(p), LATTER_ROUND_DISPLAY != 0, LATTER_HAS_IMU != 0,
      out_items, max_items);
}

inline int active_tools_launcher_item_count(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  ToolsLauncherItemSpec items[kMaxToolsLauncherItems]{};
  return collect_tools_launcher_items(items, p, kMaxToolsLauncherItems);
}

inline int active_tools_launcher_page_count(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  const int cnt = active_tools_launcher_item_count(p);
  return (cnt <= 6) ? 1 : ((cnt + 5) / 6);
}

inline int collect_enabled_games_for(std::string_view csv,
                                     int *out_game_ids,
                                     int max_games = kMaxGamesLauncherItems) noexcept {
  if (out_game_ids == nullptr || max_games <= 0 || csv.empty()) return 0;
  int count = 0;
  size_t start = 0;
  const int cap = std::min(max_games, kMaxGamesLauncherItems);
  while (start <= csv.size() && count < cap) {
    const size_t pos = csv.find(',', start);
    const std::string_view tok = trim_ascii_ws(
        (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start));
    const int gid = game_id_for_token(tok);
    if (gid > 0) {
      bool dup = false;
      for (int i = 0; i < count; ++i) {
        if (out_game_ids[i] == gid) {
          dup = true;
          break;
        }
      }
      if (!dup) out_game_ids[count++] = gid;
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return count;
}

inline int collect_enabled_games(int *out_game_ids,
                                 const PersistedDevicePrefs &p = persisted_prefs(),
                                 int max_games = kMaxGamesLauncherItems) noexcept {
  return collect_enabled_games_for(effective_games_items_sv(p), out_game_ids, max_games);
}

inline int active_games_launcher_count(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  int ids[kMaxGamesLauncherItems]{};
  return collect_enabled_games(ids, p, kMaxGamesLauncherItems);
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
  if (p.auto_brightness_mode >= 3) p.auto_brightness_mode = 1;
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
  }
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
    seed_hardware_sleep_defaults(p);
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
  AlertPriority priority_level = AlertPriority::Normal;
  std::string camera_url = "";
  std::string camera_entity = "";
  std::string loaded_camera_url = "";
  uint32_t start_ms = 0;
  uint32_t duration_ms = 10000U;
  int last_seen_alert_id = -1;

  bool is_critical() const noexcept {
    return priority_level == AlertPriority::Critical || priority == "critical";
  }
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
  static constexpr uint8_t kMaxLapSplits = 8;
  bool running = false;
  bool started_in_analog = false;
  uint32_t start_time_ms = 0;
  uint32_t elapsed_ms = 0;
  uint32_t lap_ms = 0;
  uint32_t lap_splits_ms[kMaxLapSplits]{};
  uint8_t lap_count = 0;

  void record_lap_elapsed(uint32_t cur_elapsed_ms) {
    const uint32_t split_ms = (cur_elapsed_ms >= lap_ms) ? (cur_elapsed_ms - lap_ms) : cur_elapsed_ms;
    lap_splits_ms[lap_count % kMaxLapSplits] = split_ms;
    if (lap_count < 255U) {
      ++lap_count;
    } else {
      lap_count = static_cast<uint8_t>((lap_count % kMaxLapSplits) + kMaxLapSplits);
    }
    lap_ms = cur_elapsed_ms;
  }

  void reset() {
    running = false;
    started_in_analog = false;
    start_time_ms = 0;
    elapsed_ms = 0;
    lap_ms = 0;
    for (uint32_t &sp : lap_splits_ms) sp = 0;
    lap_count = 0;
  }
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
  int home_settings_page = 1;
  int home_activity_idx = 0;
  int home_dock_page = 0;
  int time_subpage = 0;
  int world_clock_page = 0;
  uint8_t alarm_edit_field = 1;  // 0 = HH focused, 1 = MM focused
  int sky_weather_subpage = 0;
  int area_subpage = 0;
  int remote_subpage = 0;
  int tools_subpage = 0;
  int tools_menu_page = 0;
  int pinned_menu_page = 0;
  ShortcutOrigin shortcut_origin = ShortcutOrigin::NONE;
  int shortcut_origin_pinned_page = 0;
  std::string pinned_feedback = "";
  uint32_t pinned_feedback_ms = 0;
  ShortcutConfirmState shortcut_confirm{};
  bool quick_actions_open = false;
  std::string pending_shortcut_entity_id = "";
  std::string pending_shortcut_action = "";
  std::string pending_shortcut_label = "";
  int display_menu_page = 1;
  int system_dashboard_page = 1;
  int system_menu_mode = 0;
  int sleep_menu_page = 1;
  int audio_menu_parent_page = 4;
  bool menu_scroll_active = false;
  int menu_scroll_dir = 1;
  int menu_scroll_target_page = 1;
  uint32_t menu_scroll_start_ms = 0;
  int display_power_state = 0;  // 0 = ACTIVE, 1 = SCREENSAVER, 2 = SCREEN_OFF / STANDBY
  uint32_t sleep_start_time = 0;
  uint32_t standby_entered_ms = 0;
  bool wake_greeting_pending = true;
  bool is_hibernating = false;
  int hibernate_countdown_val = 0;
  bool hud_wake_finger_released = true;
  bool pwr_key_wake_consumed = false;
  bool ui_anim_poller_running = true;
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

inline constexpr size_t kMaxMusicLyricLines = 128;

struct MusicLyricLine {
  int32_t t_ms = -1;  // >= 0 for LRC [mm:ss.xx] synced line; -1 for plain unsynced line
  std::string text = "";
};

struct MusicLyricsState {
  uint32_t rev = 0;
  int latest_req_id = 0;
  std::string track_key = "";
  bool synced = false;
  bool loading = false;
  std::vector<MusicLyricLine> lines{};
  int active_line_idx = -1;
  int scroll_px = 0;
  int target_scroll_px = 0;
  int from_scroll_px = 0;
  uint32_t scroll_anim_start_ms = 0;
  uint32_t manual_override_until_ms = 0;

  // Multi-chunk assembly buffer for latter_set_music_lyrics
  int staging_req_id = -1;
  int staging_expected_chunks = 0;
  int staging_next_chunk = 0;
  std::string staging_track_key = "";
  bool staging_synced = false;
  std::vector<MusicLyricLine> staging_lines{};

  void clear() {
    track_key.clear();
    synced = false;
    loading = false;
    lines.clear();
    active_line_idx = -1;
    scroll_px = 0;
    target_scroll_px = 0;
    from_scroll_px = 0;
    scroll_anim_start_ms = 0;
    manual_override_until_ms = 0;
    staging_req_id = -1;
    staging_expected_chunks = 0;
    staging_next_chunk = 0;
    staging_track_key.clear();
    staging_synced = false;
    staging_lines.clear();
  }

  int find_active_line(uint32_t pos_ms) const noexcept {
    if (!synced || lines.empty()) return -1;
    int best = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].t_ms < 0) continue;
      if (static_cast<uint32_t>(lines[i].t_ms) <= pos_ms) {
        best = static_cast<int>(i);
      } else {
        break;
      }
    }
    return best;
  }
};

struct MusicRuntimeState {
  MusicView view{MusicView::NOW_PLAYING};
  MusicDrilldown drilldown{MusicDrilldown::NONE};
  MusicTakeover takeover{MusicTakeover::NONE};
  uint32_t takeover_interacted_ms = 0;
  uint8_t carousel_page = 0;     // 0..5 sliding 4-slot window start across 9 peer tabs (0..8)
  int mode_bar_win_start = 0;    // 0..5 window start for render_in_app_mode_bar_track
  int8_t action_sheet_slot = -1; // 0..4 when opened from a slot row; -1 for header container
  bool action_sheet_is_queue = false;
  bool action_sheet_is_container = false;
  std::string action_sheet_id = "";
  std::string action_sheet_name = "";
  std::string action_sheet_type = "";
  std::string action_sheet_queue_item_id = "";
  int info_scroll_px = 0;
  int info_scroll_page = 1;
  int settings_page = 1;
  MusicLyricsState lyrics{};

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
  int remote_vol_pct = 70;
  uint32_t last_local_vol_tap_ms = 0;
  uint32_t remote_play_override_ms = 0;
  bool remote_play_override_state = false;
  bool queue_cleared = false;
  uint32_t scroll_tap_lockout_until_ms = 0;
  std::string last_sendspin_artist = "";
  std::string last_sendspin_title = "";
  std::string last_sendspin_album = "";
  std::string loaded_art_url = "";
  std::string loaded_wallpaper_url = "";
  std::string loaded_screensaver_photo_url = "";
  bool screensaver_photo_failed = false;

  void sync_carousel_for_view() noexcept {
    const int idx = std::clamp(static_cast<int>(view), 0, kMusicPeerTabCount - 1);
    if (mode_bar_win_start < 0 || mode_bar_win_start > kMusicPeerTabCount - 4) {
      mode_bar_win_start = 0;
    }
    if (idx < mode_bar_win_start) {
      mode_bar_win_start = idx;
    } else if (idx >= mode_bar_win_start + 4) {
      mode_bar_win_start = idx - 3;
    }
    mode_bar_win_start = std::clamp(mode_bar_win_start, 0, kMusicPeerTabCount - 4);
    carousel_page = static_cast<uint8_t>(mode_bar_win_start);
  }

  void reset_overlays() noexcept {
    drilldown = MusicDrilldown::NONE;
    takeover = MusicTakeover::NONE;
    takeover_interacted_ms = 0U;
    overlay_mode = 0;
    target_speaker_id.clear();
    target_speaker_name.clear();
    action_sheet_slot = -1;
    action_sheet_is_queue = false;
    action_sheet_is_container = false;
    action_sheet_id.clear();
    action_sheet_name.clear();
    action_sheet_type.clear();
    action_sheet_queue_item_id.clear();
  }

  bool apply_route(RouteId route_id) {
    MusicView next_view = MusicView::NOW_PLAYING;
    MusicDrilldown next_drill = MusicDrilldown::NONE;
    if (!resolve_music_route_target(route_id, next_view, next_drill)) {
      return false;
    }
    reset_overlays();
    view = next_view;
    drilldown = next_drill;
    sync_carousel_for_view();
    if (route_id == RouteId::MUSIC_LIBRARY) {
      selected_category_mode = "categories";
      selected_category_title = "LIBRARY";
      category_page = 1;
      drilldown_parent_mode.clear();
    } else if (route_id == RouteId::MUSIC_FAVORITES) {
      selected_category_mode = "favorites";
      selected_category_title = "FAVORITES";
      drilldown_parent_mode.clear();
    } else if (route_id == RouteId::MUSIC_PLAYLISTS) {
      selected_category_mode = "playlist";
      selected_category_title = "PLAYLISTS";
      drilldown_parent_mode.clear();
    }
    return true;
  }
};

struct AreaRuntimeState {
  std::string browse_mode = "entities";
  std::string domain_filter = "scene";
  std::string active_area_id = "";
  std::string active_area_name = "Area Control";
  std::string area_label = "Home";
  std::string site_id = "primary";
  std::string ble_proxy = "";
  std::string available_domains_csv = "";
  int cat_dock_page = 0;
  std::string alarm_entity = "";
  std::string alarm_state = "";
  std::string alarm_service = "alarm_control_panel.alarm_disarm";
  std::string target_entity_id = "";
  std::string target_action = "toggle";
  int target_level = -1;
  std::string target_code = "";
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
  std::string light_insp_entity_id = "";
  std::string light_insp_name = "Light";
  std::string light_insp_domain = "light";
  int light_insp_mode = 0;
  int light_insp_slot = -1;
  bool light_insp_active = true;
  float light_insp_hue_deg = 35.0f;
  float light_insp_sat = 0.85f;
  int light_insp_kelvin = 3500;
  int light_insp_bri_pct = 80;
  std::string color_hex_param = "#FFAA55";
};

struct RemoteRuntimeState {
  static constexpr int kMaxSlots = 10;
  int active_slot = 0;
  int control_page = 0;
  std::string slot_ents[kMaxSlots]{};
  std::string slot_names[kMaxSlots]{};
  bool slot_states_on[kMaxSlots] = {false};
  std::string action_feedback = "";
  uint32_t feedback_timer = 0;
};

struct CalendarUiState {
  int subpage = CalendarSubpage::OVERVIEW;
  int drilldown = CalDrilldown::NONE;
  int return_subpage = CalendarSubpage::OVERVIEW;
  int picker_return_drilldown = CalDrilldown::ADD_STEP1;
  int32_t selected_ymd = 0;  // YYYYMMDD (0 = seed from current local date)
  int overview_page = 0;
  int day_page = 0;
  int week_page = 0;
  int picker_page = 0;
  int selected_event_idx = -1;
  bool delete_confirm_open = false;
  uint8_t dictation_field = 0;  // 0 = none, 1 = Title, 2 = Location
  uint32_t dictation_started_ms = 0;
  uint32_t next_req_id = 1;
  std::string detail_map_url = "";
  std::string detail_map_location = "";
  int map_zoom = 15;
  int map_pan_x = 0;
  int map_pan_y = 0;
  std::string feedback_text = "";
  uint32_t feedback_ms = 0;

  // + Add Event composer draft state
  std::string draft_title = "Reminder";
  std::string draft_location = "";
  uint8_t draft_cal_idx = 0;
  uint8_t draft_rrule_idx = 0;   // 0=Once, 1=Daily, 2=Weekdays, 3=Weekly, 4=Biweekly, 5=Monthly, 6=Yearly
  int32_t draft_ymd = 0;         // YYYYMMDD
  int16_t draft_start_min = 540; // 09:00
  int16_t draft_end_min = 600;   // 10:00
  bool draft_all_day = false;
  uint8_t draft_time_field = 0;  // 0 = Start focused, 1 = End focused
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
  bool broadcast_dictation_active = false;
  uint32_t broadcast_dictation_started_ms = 0;
  int cam_carousel_idx = 0;
  bool cam_call_active = false;
  bool cam_listen_active = false;
  bool cam_intercom_ptt_active = false;
  bool cam_quick_reply_open = false;
  std::string cam_intercom_status = "";
  int cam_noise_idx = 0;
  bool cam_landscape_mode = false;
  bool cam_expanded_fullscreen = false;
  uint32_t last_cam_live_fetch_ms = 0;
  uint32_t cam_error_backoff_until_ms = 0;
  bool cam_download_in_progress = false;
};

struct CamerasRuntimeState {
  int tab = CamerasTab::GRID;
  int selected_idx = 0;
  int grid_page = 0;
  int settings_page = 0;
  int carousel_idx = 0;
  uint32_t carousel_last_advance_ms = 0;
  bool carousel_paused = false;
  bool hud_active = true;
  uint32_t hud_timer = 0;
  bool quick_reply_open = false;
  bool landscape_mode = false;
  bool doorbell_live_popup_active = false;
  int video_y0 = 0;
  int video_y1 = LATTER_SCREEN_H - 1;
  uint32_t last_stream_sync_ms = 0;

  bool effective_fill(const PersistedDevicePrefs &p = persisted_prefs()) const noexcept {
    return p.camera_framing == 1;
  }
};

struct HelloSyncState {
  bool in_flight = false;
  uint32_t started_ms = 0;
  uint32_t last_sent_ms = 0;
  PrefMask last_sent_dirty_mask{};
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
  CamerasRuntimeState cameras{};
  HelloSyncState hello_sync{};

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
  void (*on_v3_domain_pushed)(std::string_view domain) = nullptr;
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

inline std::string format_agenda_time_label(std::string_view raw_lbl, bool is_24h) {
  if (is_24h || raw_lbl.size() < 6) return std::string(raw_lbl);
  const size_t t_pos = raw_lbl.size() - 5;
  if (raw_lbl[t_pos - 1] != ' ' || raw_lbl[t_pos + 2] != ':') return std::string(raw_lbl);
  auto is_dig = [](char c) { return c >= '0' && c <= '9'; };
  if (!is_dig(raw_lbl[t_pos]) || !is_dig(raw_lbl[t_pos + 1]) ||
      !is_dig(raw_lbl[t_pos + 3]) || !is_dig(raw_lbl[t_pos + 4])) {
    return std::string(raw_lbl);
  }
  const int hh = (raw_lbl[t_pos] - '0') * 10 + (raw_lbl[t_pos + 1] - '0');
  const int mm = (raw_lbl[t_pos + 3] - '0') * 10 + (raw_lbl[t_pos + 4] - '0');
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return std::string(raw_lbl);
  const int h12 = (hh % 12 == 0) ? 12 : (hh % 12);
  const char *ampm = (hh >= 12) ? "PM" : "AM";
  char buf[24];
  std::snprintf(buf, sizeof(buf), " %d:%02d %s", h12, mm, ampm);
  std::string out(raw_lbl.substr(0, t_pos - 1));
  out += buf;
  return out;
}

inline void format_schedule_range_label(int start_hr, int end_hr, bool is_24h, char *out, size_t cap) {
  if (!out || cap == 0) return;
  const int sh = ((start_hr % 24) + 24) % 24;
  const int eh = ((end_hr % 24) + 24) % 24;
  if (is_24h) {
    std::snprintf(out, cap, "Schedule: %02d:00-%02d:00", sh, eh);
  } else {
    const int sh12 = (sh % 12 == 0) ? 12 : (sh % 12);
    const int eh12 = (eh % 12 == 0) ? 12 : (eh % 12);
    std::snprintf(out, cap, "Schedule: %d%s-%d%s", sh12, (sh >= 12) ? "PM" : "AM",
                  eh12, (eh >= 12) ? "PM" : "AM");
  }
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

inline HomeHubState &home_hub_state() {
  static HomeHubState st;
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

inline ShortcutsState &shortcuts_state() {
  static ShortcutsState st;
  return st;
}

inline ShortcutConfirmState &shortcut_confirm_state() {
  return runtime_state().nav.shortcut_confirm;
}

inline const char *resolve_area_dock_icon(const AreaContextState &ac) noexcept {
  const bool assigned =
      !ac.area_id.empty() ||
      (!ac.area_name.empty() && ac.area_name != "Unassigned" && ac.area_name != kUnknownGlyph);
  if (assigned && !ac.area_icon.empty()) {
    return ac.area_icon.c_str();
  }
  return "mdi:sofa";
}

inline constexpr uint32_t kStandbyGreetingIdleMs = 1800000U;

inline void note_wake_from_standby(uint32_t now_ms) {
  auto &nav = runtime_state().nav;
  const uint32_t start_ms = (nav.sleep_start_time != 0) ? nav.sleep_start_time : nav.standby_entered_ms;
  nav.standby_entered_ms = 0;
  if (persisted_prefs().wake_greeting &&
      start_ms != 0 &&
      static_cast<uint32_t>(now_ms - start_ms) >= kStandbyGreetingIdleMs) {
    nav.wake_greeting_pending = true;
  }
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

inline std::string_view trim_v3_num_sv(std::string_view sv) noexcept {
  while (!sv.empty() &&
         (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' ||
          sv.front() == '\n' || sv.front() == '\v' || sv.front() == '\f')) {
    sv.remove_prefix(1);
  }
  return sv;
}

inline int v3_parse_int(std::string_view sv, int fallback = 0) noexcept {
  sv = trim_v3_num_sv(sv);
  if (sv.size() >= 2 && sv.front() == '+' && sv[1] != '-' && sv[1] != '+') {
    sv.remove_prefix(1);
  }
  if (sv.empty()) return fallback;
  int val = 0;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
  if (ec != std::errc{} || ptr == sv.data()) return fallback;
  return val;
}

inline uint32_t v3_parse_u32(std::string_view sv, uint32_t fallback = 0) noexcept {
  sv = trim_v3_num_sv(sv);
  if (sv.empty() || sv.front() == '-') return fallback;
  if (sv.size() >= 2 && sv.front() == '+' && sv[1] != '-' && sv[1] != '+') {
    sv.remove_prefix(1);
  }
  uint32_t val = 0;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
  if (ec != std::errc{} || ptr == sv.data()) return fallback;
  return val;
}

inline float v3_parse_float(std::string_view sv, float fallback = NAN) noexcept {
  sv = trim_v3_num_sv(sv);
  if (sv.size() >= 2 && sv.front() == '+' && sv[1] != '-' && sv[1] != '+') {
    sv.remove_prefix(1);
  }
  if (sv.empty()) return fallback;
  float val = 0.0f;
  const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
  if (ec != std::errc{} || ptr == sv.data()) return fallback;
  return val;
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

inline int home_category_tab_for_token(std::string_view sv) noexcept {
  if (sv == "alarm") return HomeHubTab::ALARM;
  if (sv == "intercom") return HomeHubTab::INTERCOM;
  if (sv == "thermostat") return HomeHubTab::THERMOSTAT;
  if (sv == "entry") return HomeHubTab::ENTRY;
  if (sv == "vacuum") return HomeHubTab::VACUUM;
  return -1;
}

inline int parse_home_categories_csv(std::string_view csv, int *out_tabs, int max_tabs = kMaxHomeHubTabs) {
  if (csv.empty() || out_tabs == nullptr || max_tabs <= 0) return 0;
  int n = 0;
  size_t start = 0;
  while (start <= csv.size() && n < max_tabs) {
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
  shortcuts_state().epoch = 0;
  shortcuts_state().rev = 0;
  shortcuts_state().staging_epoch = 0;
  shortcuts_state().staging_rev = 0;
  shortcuts_state().staging_chunk_count = 0;
  shortcuts_state().staging_next_chunk = 0;
  shortcuts_state().staging_dock_items.clear();
  shortcuts_state().staging_pinned_items.clear();
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
  if (sv == "idle" || sv == "Idle") return "Idle";
  if (sv == "returning" || sv == "Returning") return "Returning";
  if (sv == "docked" || sv == "Docked") return "Docked";
  if (sv == "error" || sv == "Error") return "Error";
  return std::string(sv);
}

inline std::string fallback_entity_label_sv(std::string_view ent_id) {
  const size_t dot = ent_id.find('.');
  std::string s(dot == std::string_view::npos ? ent_id : ent_id.substr(dot + 1U));
  bool cap = true;
  for (char &ch : s) {
    if (ch == '_' || ch == '-') {
      ch = ' ';
      cap = true;
    } else if (cap) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      cap = false;
    }
  }
  return s;
}

inline int parse_thermostats_summary_records(std::string_view raw,
                                             ThermostatZoneItem *out_items,
                                             int max_items,
                                             bool has_area_col = true) {
  if (out_items == nullptr || max_items <= 0) return 0;
  for (int i = 0; i < max_items; ++i) out_items[i] = ThermostatZoneItem{};
  if (raw.empty()) return 0;
  std::string_view recs[kMaxThermostatZones];
  const int cap = std::min(max_items, kMaxThermostatZones);
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int cnt = 0;
  const size_t expected_cols = has_area_col ? 7U : 6U;
  for (size_t r = 0; r < n && cnt < cap; ++r) {
    std::string_view cols[8]{};
    const size_t got = v3_split_fields(recs[r], cols, 8U, '|');
    if (got < expected_cols || cols[0].empty()) continue;
    auto &dst = out_items[cnt++];
    dst.entity_id = std::string(cols[0]);
    dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : std::string(cols[1]);
    const size_t off = has_area_col ? 1U : 0U;
    dst.area_id = has_area_col ? std::string(cols[2]) : std::string{};
    dst.mode = std::string(cols[2U + off]);
    const uint32_t raw_caps = v3_parse_u32(cols[5U + off], 0U);
    dst.caps = (raw_caps != 0U) ? static_cast<uint16_t>(raw_caps & 0xFFFFU) : kClimateCapDefault;
    const bool is_celsius = (dst.caps & kClimateCapCelsius) != 0U;
    const float min_valid_t = is_celsius ? 4.0f : 35.0f;
    const float max_valid_t = is_celsius ? 45.0f : 110.0f;
    const float th_temp = v3_parse_float(cols[3U + off], NAN);
    dst.target_temp = (!std::isnan(th_temp) && th_temp >= min_valid_t && th_temp <= max_valid_t)
                          ? static_cast<int>(std::round(th_temp))
                          : -1;
    dst.current_temp = v3_parse_float(cols[4U + off], NAN);
  }
  return cnt;
}

inline int parse_entry_controls_records(std::string_view locks_raw,
                                        std::string_view garages_raw,
                                        EntryControlItem out_items[kMaxEntryItems]) {
  for (int i = 0; i < kMaxEntryItems; ++i) out_items[i] = EntryControlItem{};
  int cnt = 0;
  if (!locks_raw.empty()) {
    std::string_view recs[kMaxEntryLockItems];
    const size_t n = v3_split_fields(locks_raw, recs, kMaxEntryLockItems, kProtocolRecordSep);
    for (size_t r = 0; r < n && cnt < kMaxEntryItems; ++r) {
      std::string_view cols[5]{};
      if (v3_split_fields(recs[r], cols, 5U, '|') < 4U || cols[0].empty()) continue;
      auto &dst = out_items[cnt++];
      dst.entity_id = std::string(cols[0]);
      dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : std::string(cols[1]);
      dst.area_id = std::string(cols[2]);
      dst.is_lock = true;
      dst.lock_state = parse_lock_state_str(cols[3]);
    }
  }
  if (!garages_raw.empty()) {
    std::string_view recs[kMaxEntryGarageItems];
    const size_t n = v3_split_fields(garages_raw, recs, kMaxEntryGarageItems, kProtocolRecordSep);
    for (size_t r = 0; r < n && cnt < kMaxEntryItems; ++r) {
      std::string_view cols[5]{};
      if (v3_split_fields(recs[r], cols, 5U, '|') < 4U || cols[0].empty()) continue;
      auto &dst = out_items[cnt++];
      dst.entity_id = std::string(cols[0]);
      dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : std::string(cols[1]);
      dst.area_id = std::string(cols[2]);
      dst.is_lock = false;
      dst.garage_state = parse_garage_state_str(cols[3]);
    }
  }
  return cnt;
}

inline int parse_vacuums_summary_records(std::string_view raw,
                                         VacuumUnitItem out_items[kMaxVacuumUnits]) {
  for (int i = 0; i < kMaxVacuumUnits; ++i) out_items[i] = VacuumUnitItem{};
  if (raw.empty()) return 0;
  std::string_view recs[kMaxVacuumUnits];
  const size_t n = v3_split_fields(raw, recs, kMaxVacuumUnits, kProtocolRecordSep);
  int cnt = 0;
  for (size_t r = 0; r < n && cnt < kMaxVacuumUnits; ++r) {
    std::string_view cols[6]{};
    if (v3_split_fields(recs[r], cols, 6U, '|') < 5U || cols[0].empty()) continue;
    auto &dst = out_items[cnt++];
    dst.entity_id = std::string(cols[0]);
    dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : std::string(cols[1]);
    dst.area_id = std::string(cols[2]);
    dst.state = normalize_vacuum_state_str(cols[3]);
    const uint32_t raw_caps = v3_parse_u32(cols[4], 0U);
    dst.caps = (raw_caps != 0U) ? static_cast<uint16_t>(raw_caps & 0xFFFFU) : kVacuumCapDefault;
  }
  return cnt;
}

inline void sync_home_hub_for_active_area(std::string_view active_area_id,
                                          HomeHubState &hub = home_hub_state()) noexcept {
  if (active_area_id.empty()) return;
  for (int i = 0; i < hub.thermostat_count; ++i) {
    if (hub.thermostats[i].area_id == active_area_id) {
      hub.thermostat_idx = i;
      break;
    }
  }
  for (int i = 0; i < hub.vacuum_count; ++i) {
    if (hub.vacuums[i].area_id == active_area_id) {
      hub.vacuum_idx = i;
      break;
    }
  }
}

inline ThermostatZoneItem *find_climate_item_by_id(std::string_view entity_id,
                                                   HomeHubState &hub = home_hub_state()) noexcept {
  if (entity_id.empty()) return nullptr;
  for (int i = 0; i < hub.area_climate_count; ++i) {
    if (hub.area_climates[i].entity_id == entity_id) return &hub.area_climates[i];
  }
  for (int i = 0; i < hub.thermostat_count; ++i) {
    if (hub.thermostats[i].entity_id == entity_id) return &hub.thermostats[i];
  }
  return nullptr;
}

inline const ThermostatZoneItem *find_climate_item_by_id_const(
    std::string_view entity_id,
    const HomeHubState &hub = home_hub_state()) noexcept {
  if (entity_id.empty()) return nullptr;
  for (int i = 0; i < hub.area_climate_count; ++i) {
    if (hub.area_climates[i].entity_id == entity_id) return &hub.area_climates[i];
  }
  for (int i = 0; i < hub.thermostat_count; ++i) {
    if (hub.thermostats[i].entity_id == entity_id) return &hub.thermostats[i];
  }
  return nullptr;
}

// Parse Protocol v3 area_context payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
inline bool parse_v3_area_ctx_payload(std::string_view payload,
                                      AreaContextState &st = area_ctx_state(),
                                      HomeHubState &hub = home_hub_state()) {
  constexpr size_t kCount = static_cast<size_t>(AreaCtxField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(AreaCtxField::HEADER_VERSION)] != "v3") return false;
  const uint32_t prev_epoch = st.epoch;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("area_context", payload, kCount);

  const std::string prev_area_id = st.area_id;
  const std::string prev_therm_ent =
      (hub.thermostat_idx >= 0 && hub.thermostat_idx < hub.thermostat_count)
          ? hub.thermostats[hub.thermostat_idx].entity_id
          : std::string{};
  const std::string prev_vac_ent =
      (hub.vacuum_idx >= 0 && hub.vacuum_idx < hub.vacuum_count)
          ? hub.vacuums[hub.vacuum_idx].entity_id
          : std::string{};

  st.area_id           = std::string(f[static_cast<size_t>(AreaCtxField::AREA_ID)]);
  st.area_name         = std::string(f[static_cast<size_t>(AreaCtxField::AREA_NAME)]);
  st.site_id           = std::string(f[static_cast<size_t>(AreaCtxField::SITE_ID)]);
  st.ble_closest_proxy = std::string(f[static_cast<size_t>(AreaCtxField::BLE_CLOSEST_PROXY)]);
  st.wallpaper_url     = std::string(f[static_cast<size_t>(AreaCtxField::WALLPAPER_URL)]);
  st.available_domains = std::string(f[static_cast<size_t>(AreaCtxField::AVAILABLE_DOMAINS)]);
  st.area_temp         = v3_parse_float(f[static_cast<size_t>(AreaCtxField::AREA_TEMP)], NAN);
  st.area_humidity     = v3_parse_float(f[static_cast<size_t>(AreaCtxField::AREA_HUMIDITY)], NAN);
  st.lights_on         = v3_parse_int(f[static_cast<size_t>(AreaCtxField::LIGHTS_ON)], 0);
  st.covers_open       = v3_parse_int(f[static_cast<size_t>(AreaCtxField::COVERS_OPEN)], 0);
  st.alarm_entity      = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_ENTITY)]);
  st.alarm_state       = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_STATE)]);

  st.thermostats_summary   = std::string(f[static_cast<size_t>(AreaCtxField::THERMOSTATS_SUMMARY)]);
  st.locks_summary         = std::string(f[static_cast<size_t>(AreaCtxField::LOCKS_SUMMARY)]);
  st.garages_summary       = std::string(f[static_cast<size_t>(AreaCtxField::GARAGES_SUMMARY)]);
  st.vacuums_summary       = std::string(f[static_cast<size_t>(AreaCtxField::VACUUMS_SUMMARY)]);
  st.area_climates_summary = std::string(f[static_cast<size_t>(AreaCtxField::AREA_CLIMATES_SUMMARY)]);

  hub.thermostat_count = parse_thermostats_summary_records(
      st.thermostats_summary, hub.thermostats, kMaxThermostatZones, /*has_area_col=*/true);
  hub.entry_count = parse_entry_controls_records(st.locks_summary, st.garages_summary, hub.entries);
  hub.vacuum_count = parse_vacuums_summary_records(st.vacuums_summary, hub.vacuums);
  hub.area_climate_count = parse_thermostats_summary_records(
      st.area_climates_summary, hub.area_climates, kMaxAreaClimates, /*has_area_col=*/false);

  if (hub.thermostat_count <= 0) {
    hub.thermostat_idx = 0;
  } else {
    int preserved = -1;
    if (!prev_therm_ent.empty()) {
      for (int i = 0; i < hub.thermostat_count; ++i) {
        if (hub.thermostats[i].entity_id == prev_therm_ent) {
          preserved = i;
          break;
        }
      }
    }
    hub.thermostat_idx = (preserved >= 0)
                             ? preserved
                             : std::clamp(hub.thermostat_idx, 0, hub.thermostat_count - 1);
  }

  const int max_entry_pg = std::max(0, (hub.entry_count - 1) / 2);
  hub.entry_page = std::clamp(hub.entry_page, 0, max_entry_pg);

  if (hub.vacuum_count <= 0) {
    hub.vacuum_idx = 0;
  } else {
    int preserved = -1;
    if (!prev_vac_ent.empty()) {
      for (int i = 0; i < hub.vacuum_count; ++i) {
        if (hub.vacuums[i].entity_id == prev_vac_ent) {
          preserved = i;
          break;
        }
      }
    }
    hub.vacuum_idx = (preserved >= 0)
                         ? preserved
                         : std::clamp(hub.vacuum_idx, 0, hub.vacuum_count - 1);
  }

  if (!st.area_id.empty() && (prev_epoch == 0U || st.area_id != prev_area_id)) {
    sync_home_hub_for_active_area(st.area_id, hub);
  }

  st.camera_snapshot_url      = std::string(f[static_cast<size_t>(AreaCtxField::CAMERA_SNAPSHOT_URL)]);
  st.camera_stream_url        = std::string(f[static_cast<size_t>(AreaCtxField::CAMERA_STREAM_URL)]);
  st.cameras_summary          = std::string(f[static_cast<size_t>(AreaCtxField::CAMERAS_SUMMARY)]);
  st.doorbell_talk_state      = std::string(f[static_cast<size_t>(AreaCtxField::DOORBELL_TALK_STATE)]);
  st.broadcast_phrases        = std::string(f[static_cast<size_t>(AreaCtxField::BROADCAST_PHRASES)]);
  st.alert_id                 = v3_parse_int(f[static_cast<size_t>(AreaCtxField::ALERT_ID)], 0);
  st.alert_title              = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_TITLE)]);
  st.alert_message            = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_MESSAGE)]);
  st.alert_priority           = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_PRIORITY)]);
  st.alert_priority_level     = parse_alert_priority(st.alert_priority);
  st.alert_duration           = v3_parse_int(f[static_cast<size_t>(AreaCtxField::ALERT_DURATION)], 10);
  st.alert_chime              = v3_parse_bool(f[static_cast<size_t>(AreaCtxField::ALERT_CHIME)]);
  st.alert_camera_url         = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_CAMERA_URL)]);
  st.alert_camera_entity      = std::string(f[static_cast<size_t>(AreaCtxField::ALERT_CAMERA_ENTITY)]);
  st.activity_card            = std::string(f[static_cast<size_t>(AreaCtxField::ACTIVITY_CARD)]);
  st.activity_sources         = std::string(f[static_cast<size_t>(AreaCtxField::ACTIVITY_SOURCES)]);
  st.security_pin_policy      = std::string(f[static_cast<size_t>(AreaCtxField::SECURITY_PIN_POLICY)]);
  st.home_categories          = std::string(f[static_cast<size_t>(AreaCtxField::HOME_CATEGORIES)]);
  st.area_icon                = std::string(f[static_cast<size_t>(AreaCtxField::AREA_ICON)]);
  st.local_tz_info            = std::string(f[static_cast<size_t>(AreaCtxField::LOCAL_TZ_INFO)]);
  st.world_clocks_summary     = std::string(f[static_cast<size_t>(AreaCtxField::WORLD_CLOCKS_SUMMARY)]);
  st.screensaver_photo_url    = std::string(f[static_cast<size_t>(AreaCtxField::SCREENSAVER_PHOTO_URL)]);
  const uint32_t raw_alarm_caps = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::ALARM_CAPS)], 0U);
  st.alarm_caps = st.alarm_entity.empty()
                      ? 0U
                      : ((raw_alarm_caps != 0U)
                             ? static_cast<uint16_t>(raw_alarm_caps & 0xFFFFU)
                             : kAlarmCapDefault);
  return true;
}

inline uint16_t effective_alarm_caps(const AreaContextState &ac = area_ctx_state()) noexcept {
  if (ac.alarm_entity.empty()) return 0U;
  return (ac.alarm_caps != 0U) ? ac.alarm_caps : kAlarmCapDefault;
}

inline uint16_t effective_climate_caps(const HomeHubState &hub = home_hub_state()) noexcept {
  if (hub.thermostat_count <= 0) return 0U;
  const int idx = std::clamp(hub.thermostat_idx, 0, hub.thermostat_count - 1);
  const uint16_t caps = hub.thermostats[idx].caps;
  return (caps != 0U) ? caps : kClimateCapDefault;
}

inline uint16_t effective_vacuum_caps(const HomeHubState &hub = home_hub_state()) noexcept {
  if (hub.vacuum_count <= 0) return 0U;
  const int idx = std::clamp(hub.vacuum_idx, 0, hub.vacuum_count - 1);
  const uint16_t caps = hub.vacuums[idx].caps;
  return (caps != 0U) ? caps : kVacuumCapDefault;
}

inline constexpr bool eq_ascii_ci_sv(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
    if (ca != cb) return false;
  }
  return true;
}

struct HomeHubActionSpec {
  int code{0};
  const char *label{""};
  const char *short_label{""};
};

struct ThermostatModeSpec {
  int code{0};
  const char *mode_id{""};
  const char *label{""};
  bool active{false};
};

inline constexpr int resolve_alarm_actions(std::string_view alarm_state, uint16_t eff_alarm_caps,
                                           HomeHubActionSpec out_specs[4]) noexcept {
  if (out_specs == nullptr || eff_alarm_caps == 0U) return 0;
  if (!alarm_state.empty() && !eq_ascii_ci_sv(alarm_state, "disarmed")) {
    out_specs[0] = HomeHubActionSpec{0, "Disarm", "Disarm"};
    return 1;
  }
  uint16_t caps = eff_alarm_caps;
  if ((caps & 0x3CU) == 0U) caps |= kAlarmCapDefault;
  int cnt = 0;
  if ((caps & kAlarmCapArmHome) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{1, "Arm Home", "Home"};
  }
  if ((caps & kAlarmCapArmAway) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{2, "Arm Away", "Away"};
  }
  if ((caps & kAlarmCapArmNight) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{3, "Night", "Night"};
  }
  if ((caps & kAlarmCapArmVacation) != 0U) {
    out_specs[cnt++] = HomeHubActionSpec{4, "Vacation", "Vacay"};
  }
  return cnt;
}

inline constexpr int resolve_vacuum_actions(std::string_view vacuum_state, uint16_t eff_vacuum_caps,
                                            HomeHubActionSpec out_specs[4]) noexcept {
  if (out_specs == nullptr || eff_vacuum_caps == 0U) return 0;
  const uint16_t caps = eff_vacuum_caps;
  int cnt = 0;

  if (vacuum_state.empty() || eq_ascii_ci_sv(vacuum_state, "docked")) {
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Clean", "Clean"};
    if ((caps & kVacuumCapCleanSpot) != 0U) out_specs[cnt++] = HomeHubActionSpec{4, "Spot", "Spot"};
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
  } else if (eq_ascii_ci_sv(vacuum_state, "cleaning")) {
    if ((caps & kVacuumCapPause) != 0U) out_specs[cnt++] = HomeHubActionSpec{1, "Pause", "Pause"};
    else if ((caps & kVacuumCapStop) != 0U) out_specs[cnt++] = HomeHubActionSpec{3, "Stop", "Stop"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
  } else if (eq_ascii_ci_sv(vacuum_state, "paused") || eq_ascii_ci_sv(vacuum_state, "idle")) {
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Resume", "Resume"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
    else if ((caps & kVacuumCapStop) != 0U) out_specs[cnt++] = HomeHubActionSpec{3, "Stop", "Stop"};
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
  } else if (eq_ascii_ci_sv(vacuum_state, "returning")) {
    if ((caps & kVacuumCapPause) != 0U) out_specs[cnt++] = HomeHubActionSpec{1, "Pause", "Pause"};
    else if ((caps & kVacuumCapStop) != 0U) out_specs[cnt++] = HomeHubActionSpec{3, "Stop", "Stop"};
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Clean", "Clean"};
  } else if (eq_ascii_ci_sv(vacuum_state, "error")) {
    if ((caps & kVacuumCapLocate) != 0U) out_specs[cnt++] = HomeHubActionSpec{5, "Locate", "Locate"};
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Resume", "Resume"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
  } else {
    if ((caps & kVacuumCapStart) != 0U) out_specs[cnt++] = HomeHubActionSpec{0, "Clean", "Clean"};
    if ((caps & kVacuumCapReturnHome) != 0U) out_specs[cnt++] = HomeHubActionSpec{2, "Dock", "Dock"};
  }
  return cnt;
}

inline constexpr bool thermostat_shows_setpoint_buttons(std::string_view cur_mode,
                                                        uint16_t eff_climate_caps) noexcept {
  if (eff_climate_caps == 0U) return false;
  return ((eff_climate_caps & kClimateCapTargetTemp) != 0U) && !eq_ascii_ci_sv(cur_mode, "off");
}

inline constexpr int resolve_thermostat_modes(std::string_view cur_mode, uint16_t eff_climate_caps,
                                              ThermostatModeSpec out_specs[6]) noexcept {
  if (out_specs == nullptr || eff_climate_caps == 0U) return 0;
  uint16_t caps = eff_climate_caps;
  if ((caps & 0x3FU) == 0U) caps |= (kClimateCapDefault & 0x3FU);
  int cnt = 0;
  if ((caps & kClimateCapHeat) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{0, "heat", "Heat", eq_ascii_ci_sv(cur_mode, "heat")};
  }
  if ((caps & kClimateCapCool) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{1, "cool", "Cool", eq_ascii_ci_sv(cur_mode, "cool")};
  }
  if ((caps & kClimateCapAuto) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{
        2, "heat_cool", "Auto", eq_ascii_ci_sv(cur_mode, "heat_cool") || eq_ascii_ci_sv(cur_mode, "auto")};
  }
  if ((caps & kClimateCapFan) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{4, "fan_only", "Fan", eq_ascii_ci_sv(cur_mode, "fan_only")};
  }
  if ((caps & kClimateCapDry) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{5, "dry", "Dry", eq_ascii_ci_sv(cur_mode, "dry")};
  }
  if ((caps & kClimateCapOff) != 0U) {
    out_specs[cnt++] = ThermostatModeSpec{3, "off", "Off", eq_ascii_ci_sv(cur_mode, "off")};
  }
  return cnt;
}

inline constexpr int step_thermostat_setpoint(int cur_target, int delta,
                                              uint16_t eff_climate_caps) noexcept {
  const bool is_celsius = (eff_climate_caps & kClimateCapCelsius) != 0U;
  const int lo = is_celsius ? 10 : 50;
  const int hi = is_celsius ? 35 : 90;
  const int fallback = is_celsius ? 21 : 70;
  const int min_valid = is_celsius ? 4 : 35;
  const int max_valid = is_celsius ? 45 : 110;
  const int base = (cur_target < min_valid || cur_target > max_valid) ? fallback : cur_target;
  return std::clamp(base + delta, lo, hi);
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
inline bool parse_v3_media_payload(std::string_view payload,
                                   MediaState &st = media_state(),
                                   uint32_t now_ms = 0) {
  constexpr size_t kCount = static_cast<size_t>(MediaField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(MediaField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(MediaField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(MediaField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("active_media", payload, kCount);

  st.state          = std::string(f[static_cast<size_t>(MediaField::STATE)]);
  st.playback_state = parse_media_playback_state(st.state);
  st.playing        = (st.playback_state == MediaPlaybackState::Playing);
  st.title          = std::string(f[static_cast<size_t>(MediaField::TITLE)]);
  st.artist         = std::string(f[static_cast<size_t>(MediaField::ARTIST)]);
  st.album          = std::string(f[static_cast<size_t>(MediaField::ALBUM)]);
  st.duration            = v3_parse_float(f[static_cast<size_t>(MediaField::DURATION)], 0.0f);
  st.position            = v3_parse_float(f[static_cast<size_t>(MediaField::POSITION)], 0.0f);
  st.position_updated_at = v3_parse_u32(f[static_cast<size_t>(MediaField::POSITION_UPDATED_AT)], 0);
  if (now_ms != 0U) {
    st.position_updated_ms = now_ms;
  }
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
  st.shuffle        = v3_parse_bool(f[static_cast<size_t>(MediaField::SHUFFLE)]);
  const std::string_view rep_sv = f[static_cast<size_t>(MediaField::REPEAT)];
  st.repeat_mode    = parse_music_repeat_mode(rep_sv);
  st.repeat         = music_repeat_mode_cstr(st.repeat_mode);
  st.muted          = v3_parse_bool(f[static_cast<size_t>(MediaField::MUTED)]);
  st.favorite       = v3_parse_bool(f[static_cast<size_t>(MediaField::FAVORITE)]);
  st.queue_count    = std::max(0, v3_parse_int(f[static_cast<size_t>(MediaField::QUEUE_COUNT)], 0));
  const uint32_t raw_mcaps = v3_parse_u32(f[static_cast<size_t>(MediaField::MEDIA_CAPS)], 0U);
  st.media_caps     = (raw_mcaps != 0U)
                          ? static_cast<uint16_t>(raw_mcaps & 0xFFFFU)
                          : (st.active_speaker.empty() ? 0U : kMediaCapDefault);
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

// Parse Protocol v3 calendar payload
// ("v3\x1f<epoch>\x1f<rev>\x1f<summary>\x1f<count>\x1f<calendars_meta>\x1f<locations_catalog>")
inline bool parse_v3_calendar_payload(std::string_view payload, CalendarState &st = calendar_state()) {
  constexpr size_t kCount = static_cast<size_t>(CalendarField::COUNT);
  std::string_view f[kCount]{};
  size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(CalendarField::HEADER_VERSION)] != "v3") return false;
  uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(CalendarField::HEADER_EPOCH)], 0);
  uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(CalendarField::HEADER_REV)], 0);
  if (!apply_if_newer(st.epoch, st.rev, epoch, rev)) return false;
  v3_check_field_count("calendar_agenda", payload, kCount);

  st.summary           = std::string(f[static_cast<size_t>(CalendarField::SUMMARY)]);
  st.count             = v3_parse_int(f[static_cast<size_t>(CalendarField::EVENT_COUNT)], 0);
  st.calendars_meta    = std::string(f[static_cast<size_t>(CalendarField::CALENDARS_META)]);
  st.locations_catalog = std::string(f[static_cast<size_t>(CalendarField::LOCATIONS_CATALOG)]);
  return true;
}

// ==============================================================================
// Universal Shortcut Engine: Route Catalog Lookup, Protocol v3 Chunk Parser & Confirm Copy
// ==============================================================================
inline constexpr const ShortcutRouteSpec *find_shortcut_route(std::string_view token) noexcept {
  if (token == "app:settings") return nullptr;
  for (const auto &spec : kShortcutRoutes) {
    if (token == spec.token) return &spec;
  }
  return nullptr;
}

inline constexpr const ShortcutRouteSpec *shortcut_route_spec(RouteId id) noexcept {
  for (const auto &spec : kShortcutRoutes) {
    if (spec.id == id) return &spec;
  }
  return nullptr;
}

inline constexpr bool is_route_supported_on_board(RouteId id) noexcept {
  if (id == RouteId::NONE) return false;
  if (id == RouteId::SETTINGS_ROOT) return true;
  const auto *spec = shortcut_route_spec(id);
  if (spec == nullptr) return false;
  if (spec->cap == ShortcutRouteCap::ROUND && !LATTER_ROUND_DISPLAY) return false;
  if (spec->cap == ShortcutRouteCap::IMU && !LATTER_HAS_IMU) return false;
  return true;
}

inline bool parse_shortcut_record(std::string_view rec, ShortcutItem &out_item) {
  if (rec.empty()) return false;
  std::string_view cols[7]{};
  const size_t n = v3_split_fields(rec, cols, 7, '|');
  if (n < 4) return false;
  out_item = ShortcutItem{};
  const std::string_view kind_sv = cols[0];
  const std::string_view target_sv = cols[1];
  const std::string_view action_sv = cols[2];
  const std::string_view label_sv = cols[3];
  const std::string_view icon_sv = (n >= 5) ? cols[4] : std::string_view{};
  const std::string_view confirm_sv = (n >= 6) ? cols[5] : std::string_view{};
  const std::string_view state_sv = (n >= 7) ? cols[6] : std::string_view{};

  if (kind_sv == "route") {
    out_item.kind = ShortcutKind::ROUTE;
    if (target_sv == "app:settings") {
      out_item.route_id = RouteId::SETTINGS_ROOT;
      out_item.target = "app:settings";
      out_item.label = label_sv.empty() ? "Settings" : std::string(label_sv);
    } else {
      const auto *spec = find_shortcut_route(target_sv);
      if (spec == nullptr) return false;
      out_item.route_id = spec->id;
      out_item.target = spec->token;
      out_item.label = label_sv.empty() ? std::string(spec->label) : std::string(label_sv);
    }
    out_item.icon = std::string(icon_sv);
    out_item.confirm = false;
    out_item.state = -1;
    return true;
  }

  if (kind_sv == "entity") {
    if (target_sv.empty() || target_sv.find('.') == std::string_view::npos || action_sv.empty()) {
      return false;
    }
    out_item.kind = ShortcutKind::ENTITY;
    out_item.route_id = RouteId::NONE;
    out_item.target = std::string(target_sv);
    out_item.action = std::string(action_sv);
    out_item.label = label_sv.empty() ? std::string(target_sv) : std::string(label_sv);
    out_item.icon = std::string(icon_sv);
    out_item.confirm = v3_parse_bool(confirm_sv);
    const int parsed_state = v3_parse_int(state_sv, -1);
    out_item.state = static_cast<int8_t>(std::clamp(parsed_state, -1, 2));
    return true;
  }

  return false;
}

inline void append_shortcut_records_blob(std::string_view blob,
                                         std::vector<ShortcutItem> &dst,
                                         size_t max_total,
                                         bool is_pinned_list) {
  if (blob.empty()) return;
  size_t start = 0;
  while (start <= blob.size() && dst.size() < max_total) {
    size_t pos = blob.find(kProtocolRecordSep, start);
    std::string_view rec = (pos == std::string_view::npos) ? blob.substr(start) : blob.substr(start, pos - start);
    ShortcutItem item{};
    if (parse_shortcut_record(rec, item)) {
      if (!(is_pinned_list && item.kind == ShortcutKind::ROUTE &&
            (item.route_id == RouteId::APP_PINNED ||
             (shortcut_route_spec(item.route_id) != nullptr && shortcut_route_spec(item.route_id)->dock_only)))) {
        dst.push_back(std::move(item));
      }
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
}

// Parse Protocol v3 shortcuts payload ("v3\x1f<epoch>\x1f<rev>\x1f<chunk_index>\x1f<chunk_count>\x1f<dock_items>\x1f<pinned_total>\x1f<pinned_items>")
// Returns true ONLY when the final chunk of a newer snapshot commits atomically.
inline bool parse_v3_shortcuts_payload(std::string_view payload, ShortcutsState &st = shortcuts_state()) {
  constexpr size_t kCount = static_cast<size_t>(ShortcutsField::COUNT);
  std::string_view f[kCount]{};
  const size_t n = v3_split_fields(payload, f, kCount, kProtocolFieldSep);
  if (n < kProtocolMinRequiredFields || f[static_cast<size_t>(ShortcutsField::HEADER_VERSION)] != "v3") {
    return false;
  }
  const uint32_t epoch = v3_parse_u32(f[static_cast<size_t>(ShortcutsField::HEADER_EPOCH)], 0);
  const uint32_t rev   = v3_parse_u32(f[static_cast<size_t>(ShortcutsField::HEADER_REV)], 0);
  if (epoch == 0 || rev == 0) return false;
  if (epoch == st.epoch && rev <= st.rev) return false;
  v3_check_field_count("shortcuts", payload, kCount);

  const int raw_chunk_idx = v3_parse_int(f[static_cast<size_t>(ShortcutsField::CHUNK_INDEX)], -1);
  const int raw_chunk_cnt = v3_parse_int(f[static_cast<size_t>(ShortcutsField::CHUNK_COUNT)], 0);
  if (raw_chunk_idx < 0 || raw_chunk_cnt <= 0 || raw_chunk_idx >= raw_chunk_cnt || raw_chunk_cnt > 32) {
    return false;
  }
  const uint16_t chunk_idx = static_cast<uint16_t>(raw_chunk_idx);
  const uint16_t chunk_cnt = static_cast<uint16_t>(raw_chunk_cnt);
  const int raw_pinned_total = std::max(0, v3_parse_int(f[static_cast<size_t>(ShortcutsField::PINNED_TOTAL)], 0));
  const uint16_t pinned_total = static_cast<uint16_t>(
      std::min<size_t>(static_cast<size_t>(raw_pinned_total), ShortcutsState::kMaxPinnedItems));

  if (chunk_idx == 0U) {
    st.staging_epoch = epoch;
    st.staging_rev = rev;
    st.staging_chunk_count = chunk_cnt;
    st.staging_next_chunk = 1U;
    st.staging_pinned_total = pinned_total;
    st.staging_dock_items.clear();
    st.staging_pinned_items.clear();
    if (pinned_total > 0U) {
      st.staging_pinned_items.reserve(pinned_total);
    }
    append_shortcut_records_blob(f[static_cast<size_t>(ShortcutsField::DOCK_ITEMS)],
                                 st.staging_dock_items, ShortcutsState::kMaxDockItems, false);
    append_shortcut_records_blob(f[static_cast<size_t>(ShortcutsField::PINNED_ITEMS)],
                                 st.staging_pinned_items, ShortcutsState::kMaxPinnedItems, true);
  } else {
    if (st.staging_epoch != epoch ||
        st.staging_rev != rev ||
        st.staging_chunk_count != chunk_cnt ||
        chunk_idx != st.staging_next_chunk) {
      st.staging_epoch = 0;
      st.staging_rev = 0;
      st.staging_chunk_count = 0;
      st.staging_next_chunk = 0;
      st.staging_dock_items.clear();
      st.staging_pinned_items.clear();
      return false;
    }
    st.staging_next_chunk = static_cast<uint16_t>(chunk_idx + 1U);
    append_shortcut_records_blob(f[static_cast<size_t>(ShortcutsField::PINNED_ITEMS)],
                                 st.staging_pinned_items, ShortcutsState::kMaxPinnedItems, true);
  }

  if (st.staging_next_chunk < chunk_cnt) {
    return false;
  }

  st.epoch = epoch;
  st.rev = rev;
  st.dock_configured = true;
  st.live_received = true;
  st.cold_pinned_total = static_cast<uint16_t>(st.staging_pinned_items.size());
  st.dock_items = std::move(st.staging_dock_items);
  st.pinned_items = std::move(st.staging_pinned_items);
  st.staging_epoch = 0;
  st.staging_rev = 0;
  st.staging_chunk_count = 0;
  st.staging_next_chunk = 0;
  st.staging_pinned_total = 0;
  st.staging_dock_items.clear();
  st.staging_pinned_items.clear();
  return true;
}

inline void build_shortcut_confirm_copy(const ShortcutItem &item,
                                        std::string &out_title,
                                        std::string &out_line1,
                                        std::string &out_line2,
                                        std::string &out_verb_btn,
                                        bool &out_is_danger) {
  out_title = "CONFIRM ACTION";
  out_line1 = item.label.empty() ? item.target : item.label;
  out_is_danger = false;

  const size_t dot = item.target.find('.');
  const std::string_view dom =
      (dot != std::string::npos) ? std::string_view(item.target).substr(0, dot) : std::string_view{};
  const std::string_view act(item.action);

  if (act == "toggle") {
    if (dom == "cover" || dom == "valve") {
      out_verb_btn = (item.state == 1) ? "Close" : ((item.state == 0) ? "Open" : "Toggle");
    } else if (dom == "lock") {
      out_verb_btn = (item.state == 1) ? "Lock" : ((item.state == 0) ? "Unlock" : "Toggle");
      out_is_danger = (item.state != 1);
    } else {
      out_verb_btn = (item.state == 1) ? "Turn Off" : ((item.state == 0) ? "Turn On" : "Toggle");
    }
  } else if (act == "turn_on") {
    if (dom == "scene") out_verb_btn = "Activate";
    else if (dom == "script") out_verb_btn = "Run";
    else out_verb_btn = "Turn On";
    if (dom == "siren") out_is_danger = true;
  } else if (act == "turn_off") {
    out_verb_btn = (dom == "script") ? "Stop" : "Turn Off";
  } else if (act == "open_cover" || act == "open_valve" || act == "open") {
    out_verb_btn = "Open";
    if (dom == "lock") out_is_danger = true;
  } else if (act == "close_cover" || act == "close_valve") {
    out_verb_btn = "Close";
  } else if (act == "stop_cover" || act == "stop_valve" || act == "stop") {
    out_verb_btn = "Stop";
  } else if (act == "trigger") {
    out_verb_btn = "Trigger";
  } else if (act == "press") {
    out_verb_btn = "Press";
  } else if (act == "lock") {
    out_verb_btn = "Lock";
  } else if (act == "unlock") {
    out_verb_btn = "Unlock";
    out_is_danger = true;
  } else if (act == "disarm" || act == "alarm_disarm") {
    out_verb_btn = "Disarm";
    out_is_danger = true;
  } else if (act == "arm_home" || act == "alarm_arm_home") {
    out_verb_btn = "Arm Home";
  } else if (act == "arm_away" || act == "alarm_arm_away") {
    out_verb_btn = "Arm Away";
  } else if (act == "arm_night" || act == "alarm_arm_night") {
    out_verb_btn = "Arm Night";
  } else if (act == "arm_vacation" || act == "alarm_arm_vacation") {
    out_verb_btn = "Arm Vacation";
  } else if (act == "start" || act == "start_mowing") {
    out_verb_btn = "Start";
  } else if (act == "pause") {
    out_verb_btn = "Pause";
  } else if (act == "return_to_base" || act == "dock") {
    out_verb_btn = "Dock";
  } else if (act == "clean_spot") {
    out_verb_btn = "Spot Clean";
  } else if (act == "locate") {
    out_verb_btn = "Locate";
  } else if (act == "media_play_pause") {
    out_verb_btn = (item.state == 1) ? "Pause" : "Play";
  } else if (act == "media_next_track") {
    out_verb_btn = "Next Track";
  } else if (act == "select_next") {
    out_verb_btn = "Next Option";
  } else if (act == "select_previous") {
    out_verb_btn = "Prev Option";
  } else {
    out_verb_btn = "Confirm";
  }

  out_line2 = out_verb_btn + " now?";
}

inline uint32_t compute_shortcuts_ui_sig(const ShortcutsState &st = shortcuts_state()) noexcept {
  uint32_t h = 2166136261U;
  h = (h ^ st.epoch) * 16777619U;
  h = (h ^ st.rev) * 16777619U;
  h = (h ^ static_cast<uint32_t>(st.dock_items.size())) * 16777619U;
  h = (h ^ static_cast<uint32_t>(st.pinned_items.size())) * 16777619U;
  for (const auto &it : st.dock_items) {
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.state + 2))) * 16777619U;
  }
  for (const auto &it : st.pinned_items) {
    h = (h ^ static_cast<uint32_t>(static_cast<uint8_t>(it.state + 2))) * 16777619U;
  }
  return h;
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

// ==============================================================================
// Time App: World Clock Catalog, TZ Metadata Parsers & Alarm Summary Codec
// ==============================================================================
static constexpr int kMaxWorldClocks = 4;

struct WorldClockCatalogItem {
  const char *iana;
  const char *city;
  int16_t std_offset_min;
};

inline constexpr WorldClockCatalogItem kWorldClockCatalog[24] = {
    {"America/Los_Angeles", "Los Angeles", -480},
    {"America/Denver", "Denver", -420},
    {"America/Chicago", "Chicago", -360},
    {"America/New_York", "New York", -300},
    {"America/Toronto", "Toronto", -300},
    {"America/Sao_Paulo", "Sao Paulo", -180},
    {"America/Argentina/Buenos_Aires", "Buenos Aires", -180},
    {"Pacific/Honolulu", "Honolulu", -600},
    {"America/Anchorage", "Anchorage", -540},
    {"Europe/London", "London", 0},
    {"Europe/Paris", "Paris", 60},
    {"Europe/Berlin", "Berlin", 60},
    {"Europe/Zurich", "Zurich", 60},
    {"Africa/Cairo", "Cairo", 120},
    {"Africa/Johannesburg", "Johannesburg", 120},
    {"Asia/Dubai", "Dubai", 240},
    {"Asia/Kolkata", "Kolkata", 330},
    {"Asia/Bangkok", "Bangkok", 420},
    {"Asia/Singapore", "Singapore", 480},
    {"Asia/Hong_Kong", "Hong Kong", 480},
    {"Asia/Shanghai", "Shanghai", 480},
    {"Asia/Tokyo", "Tokyo", 540},
    {"Asia/Seoul", "Seoul", 540},
    {"Australia/Sydney", "Sydney", 600},
};
inline constexpr size_t kWorldClockCatalogCount =
    sizeof(kWorldClockCatalog) / sizeof(kWorldClockCatalog[0]);

struct LocalTzInfo {
  std::string iana = "";
  std::string city = "";
  std::string abbr = "";
  int utc_offset_min = 0;
  bool is_dst = false;
  bool valid = false;
};

struct WorldClockEntry {
  std::string iana = "";
  std::string city = "";
  std::string abbr = "";
  int utc_offset_min = 0;
  bool is_dst = false;
  bool is_pinned_utc = false;
};

inline std::string format_utc_offset_str(int offset_min) {
  if (offset_min == 0) return "UTC+0";
  const char sign = (offset_min >= 0) ? '+' : '-';
  const int abs_m = std::abs(offset_min);
  const int h = abs_m / 60;
  const int m = abs_m % 60;
  char buf[16];
  if (m == 0) {
    std::snprintf(buf, sizeof(buf), "UTC%c%d", sign, h);
  } else {
    std::snprintf(buf, sizeof(buf), "UTC%c%d:%02d", sign, h, m);
  }
  return std::string(buf);
}

inline std::string city_from_iana_fallback(std::string_view iana) {
  for (const auto &item : kWorldClockCatalog) {
    if (iana == item.iana) return item.city;
  }
  const size_t slash = iana.rfind('/');
  std::string city(slash == std::string_view::npos ? iana : iana.substr(slash + 1));
  for (char &c : city) {
    if (c == '_') c = ' ';
  }
  return city;
}

inline int catalog_std_offset_min_for_iana(std::string_view iana, bool *found = nullptr) {
  for (const auto &item : kWorldClockCatalog) {
    if (iana == item.iana) {
      if (found != nullptr) *found = true;
      return item.std_offset_min;
    }
  }
  if (found != nullptr) *found = false;
  return 0;
}

inline bool is_valid_iana_zone_token(std::string_view sv) noexcept {
  if (sv.empty() || sv.size() > 48) return false;
  for (char c : sv) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+';
    if (!ok) return false;
  }
  return true;
}

inline std::string sanitize_clock_label(std::string_view sv, size_t max_bytes = 32) {
  std::string cleaned;
  cleaned.reserve(sv.size() < max_bytes ? sv.size() : max_bytes);
  bool prev_space = true;
  for (unsigned char uc : sv) {
    if (uc < 0x20U || uc == 0x7FU || uc == '|' || uc == ';' || uc == 0x1EU || uc == 0x1FU ||
        uc == ' ' || uc == '\t' || uc == '\r' || uc == '\n') {
      if (!prev_space) {
        cleaned.push_back(' ');
        prev_space = true;
      }
      continue;
    }
    cleaned.push_back(static_cast<char>(uc));
    prev_space = false;
  }
  while (!cleaned.empty() && cleaned.back() == ' ') {
    cleaned.pop_back();
  }
  if (cleaned.size() > max_bytes) {
    size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(cleaned[cut]) & 0xC0U) == 0x80U) {
      --cut;
    }
    cleaned.resize(cut);
    while (!cleaned.empty() && cleaned.back() == ' ') {
      cleaned.pop_back();
    }
  }
  return cleaned;
}

struct WorldClockSpecItem {
  std::string label = "";
  std::string iana = "";
};

inline int parse_world_clocks_spec(std::string_view spec, WorldClockSpecItem out_items[kMaxWorldClocks]) {
  auto eq_ascii_ci = [](std::string_view a, std::string_view b) noexcept -> bool {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      unsigned char ca = static_cast<unsigned char>(a[i]);
      unsigned char cb = static_cast<unsigned char>(b[i]);
      if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
      if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
      if (ca != cb) return false;
    }
    return true;
  };
  int count = 0;
  size_t start = 0;
  while (start <= spec.size() && count < kMaxWorldClocks) {
    size_t pos = spec.find(';', start);
    if (pos == std::string_view::npos) pos = spec.size();
    std::string_view rec = trim_ascii_ws(spec.substr(start, pos - start));
    start = pos + 1;
    if (rec.empty()) continue;
    const size_t pipe = rec.find('|');
    if (pipe == std::string_view::npos) continue;
    const std::string_view raw_lbl = rec.substr(0, pipe);
    const std::string_view iana = trim_ascii_ws(rec.substr(pipe + 1));
    if (!is_valid_iana_zone_token(iana)) continue;
    if (iana == "UTC" || iana == "Etc/UTC" || iana == "Etc/GMT" || iana == "GMT") continue;
    std::string lbl = sanitize_clock_label(raw_lbl);
    if (lbl.empty()) lbl = city_from_iana_fallback(iana);
    bool dup = false;
    for (int i = 0; i < count; ++i) {
      if (out_items[i].iana == iana && eq_ascii_ci(out_items[i].label, lbl)) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out_items[count++] = WorldClockSpecItem{std::move(lbl), std::string(iana)};
    }
  }
  return count;
}

inline std::string normalize_world_clocks_spec(std::string_view spec) {
  WorldClockSpecItem items[kMaxWorldClocks];
  const int count = parse_world_clocks_spec(spec, items);
  std::string out;
  for (int i = 0; i < count; ++i) {
    if (i > 0) out.push_back(';');
    out.append(items[i].label);
    out.push_back('|');
    out.append(items[i].iana);
  }
  return out;
}

inline LocalTzInfo parse_local_tz_info(std::string_view raw) {
  LocalTzInfo info{};
  if (raw.empty()) return info;
  std::string_view cols[5];
  if (v3_split_fields(raw, cols, 5, '|') != 5 || cols[0].empty()) return info;
  info.iana = std::string(cols[0]);
  info.city = cols[1].empty() ? city_from_iana_fallback(cols[0]) : std::string(cols[1]);
  info.utc_offset_min = std::clamp(v3_parse_int(cols[3], 0), -840, 840);
  info.abbr = cols[2].empty() ? format_utc_offset_str(info.utc_offset_min) : std::string(cols[2]);
  info.is_dst = v3_parse_bool(cols[4]);
  info.valid = true;
  return info;
}

inline int collect_active_world_clocks(const PersistedDevicePrefs &p,
                                       const AreaContextState &ac,
                                       WorldClockEntry out_entries[kMaxWorldClocks + 1]) {
  int total = 0;
  if (p.show_utc_clock) {
    out_entries[total++] = WorldClockEntry{"UTC", "UTC", "UTC", 0, false, true};
  }
  WorldClockSpecItem items[kMaxWorldClocks];
  const int z_cnt = parse_world_clocks_spec(p.world_clocks_spec, items);
  if (z_cnt == 0) return total;

  std::string_view summary_recs[8];
  const size_t s_cnt = ac.world_clocks_summary.empty()
                           ? 0
                           : v3_split_fields(ac.world_clocks_summary, summary_recs, 8, ';');
  bool used_rec[8]{};

  for (int i = 0; i < z_cnt && total < (kMaxWorldClocks + 1); ++i) {
    const std::string &iana = items[i].iana;
    const std::string &custom_lbl = items[i].label;
    int match_r = -1;
    std::string_view matched_cols[5];
    for (size_t r = 0; r < s_cnt; ++r) {
      if (used_rec[r]) continue;
      std::string_view cols[5];
      if (v3_split_fields(summary_recs[r], cols, 5, '|') == 5 && cols[0] == iana) {
        if (cols[1] == custom_lbl) {
          for (int c = 0; c < 5; ++c) matched_cols[c] = cols[c];
          match_r = static_cast<int>(r);
          break;
        }
        if (match_r < 0) {
          for (int c = 0; c < 5; ++c) matched_cols[c] = cols[c];
          match_r = static_cast<int>(r);
        }
      }
    }
    if (match_r >= 0) {
      used_rec[match_r] = true;
      const int off = std::clamp(v3_parse_int(matched_cols[3], 0), -840, 840);
      out_entries[total++] = WorldClockEntry{
          iana,
          custom_lbl.empty() ? city_from_iana_fallback(iana) : custom_lbl,
          matched_cols[2].empty() ? format_utc_offset_str(off) : std::string(matched_cols[2]),
          off,
          v3_parse_bool(matched_cols[4]),
          false,
      };
    } else {
      const int off = catalog_std_offset_min_for_iana(iana);
      out_entries[total++] = WorldClockEntry{
          iana,
          custom_lbl.empty() ? city_from_iana_fallback(iana) : custom_lbl,
          format_utc_offset_str(off),
          off,
          false,
          false,
      };
    }
  }
  return total;
}

inline int infer_local_utc_offset_min(uint32_t utc_epoch, int local_hour, int local_min, int local_sec) noexcept {
  const int utc_day_sec = static_cast<int>(utc_epoch % 86400U);
  const int loc_day_sec = std::clamp(local_hour, 0, 23) * 3600 +
                          std::clamp(local_min, 0, 59) * 60 +
                          std::clamp(local_sec, 0, 59);
  int diff_sec = loc_day_sec - utc_day_sec;
  if (diff_sec <= -43200) diff_sec += 86400;
  if (diff_sec > 50400) diff_sec -= 86400;
  const int rounded_min = (diff_sec >= 0) ? ((diff_sec + 30) / 60) : ((diff_sec - 30) / 60);
  return std::clamp(rounded_min, -720, 840);
}

inline void compute_world_clock_wall_time(uint32_t utc_epoch,
                                          int local_utc_offset_min,
                                          int target_utc_offset_min,
                                          int &out_hour24,
                                          int &out_min,
                                          int &out_day_delta) noexcept {
  auto floor_day = [](int64_t sec) -> int64_t {
    return (sec >= 0) ? (sec / 86400LL) : ((sec - 86399LL) / 86400LL);
  };
  const int64_t local_adj = static_cast<int64_t>(utc_epoch) + static_cast<int64_t>(local_utc_offset_min) * 60LL;
  const int64_t target_adj = static_cast<int64_t>(utc_epoch) + static_cast<int64_t>(target_utc_offset_min) * 60LL;
  const int64_t local_day = floor_day(local_adj);
  const int64_t target_day = floor_day(target_adj);
  const int rem = static_cast<int>(target_adj - target_day * 86400LL);
  out_hour24 = std::clamp(rem / 3600, 0, 23);
  out_min = std::clamp((rem % 3600) / 60, 0, 59);
  out_day_delta = std::clamp(static_cast<int>(target_day - local_day), -2, 2);
}

inline std::string format_alarms_summary(const PersistedDevicePrefs &p = persisted_prefs()) {
  char buf[64];
  std::snprintf(buf, sizeof(buf),
                "%02u:%02u|%u|%u;%02u:%02u|%u|%u;%02u:%02u|%u|%u",
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[0].hour, 23)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[0].minute, 59)),
                p.alarms[0].enabled ? 1U : 0U,
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[0].recurrence, 3)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[1].hour, 23)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[1].minute, 59)),
                p.alarms[1].enabled ? 1U : 0U,
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[1].recurrence, 3)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[2].hour, 23)),
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[2].minute, 59)),
                p.alarms[2].enabled ? 1U : 0U,
                static_cast<unsigned>(std::min<uint8_t>(p.alarms[2].recurrence, 3)));
  return std::string(buf);
}

inline bool parse_alarms_summary(std::string_view sv, PersistedAlarmPref (&out)[3]) noexcept {
  std::string_view recs[4];
  if (v3_split_fields(sv, recs, 4, ';') != 3) return false;
  for (int i = 0; i < 3; ++i) {
    std::string_view cols[4];
    if (v3_split_fields(recs[i], cols, 4, '|') != 3) return false;
    const size_t colon = cols[0].find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= cols[0].size()) return false;
    const int hh = v3_parse_int(cols[0].substr(0, colon), -1);
    const int mm = v3_parse_int(cols[0].substr(colon + 1), -1);
    const int en = v3_parse_int(cols[1], -1);
    const int rec = v3_parse_int(cols[2], -1);
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || (en != 0 && en != 1) || rec < 0 || rec > 3) {
      return false;
    }
    out[i].hour = static_cast<uint8_t>(hh);
    out[i].minute = static_cast<uint8_t>(mm);
    out[i].enabled = (en == 1);
    out[i].recurrence = static_cast<uint8_t>(rec);
  }
  return true;
}

inline const char *pref_key_name(PrefKey key) {
  switch (key) {
    case PrefKey::WAKE_WORD_MODE: return "wake_word_mode";
    case PrefKey::TAP_TO_WAKE: return "tap_to_wake";
    case PrefKey::PICKUP_TO_WAKE: return "pickup_to_wake";
    case PrefKey::BATTERY_SCREEN_TIMEOUT: return "battery_screen_timeout";
    case PrefKey::DOCKED_IDLE_TIMEOUT: return "docked_idle_timeout";
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT: return "battery_hibernate_timeout";
    case PrefKey::SCREENSAVER_MODE: return "screensaver_mode";
    case PrefKey::SCREENSAVER_STYLE: return "screensaver_style";
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
    case PrefKey::HOME_CLOCK_TAP_ACTION: return "home_clock_tap_action";
    case PrefKey::SHOW_UTC_CLOCK: return "show_utc_clock";
    case PrefKey::WORLD_CLOCKS: return "world_clocks";
    case PrefKey::ALARMS_SUMMARY: return "alarms_summary";
    case PrefKey::LOCAL_CLOCK_LABEL: return "local_clock_label";
    case PrefKey::TOOLS_ITEMS: return "tools_items";
    case PrefKey::GAMES_ITEMS: return "games_items";
    case PrefKey::HOME_DATE_TAP_ACTION: return "home_date_tap_action";
    case PrefKey::CALENDAR_LOOKAHEAD_DAYS: return "calendar_lookahead_days";
    case PrefKey::CALENDAR_WEEK_START: return "calendar_week_start";
    case PrefKey::BEDTIME_ACTIVE: return "bedtime_active";
    case PrefKey::BEDTIME_DISPLAY: return "bedtime_display";
    case PrefKey::BEDTIME_SCHEDULE: return "bedtime_schedule";
    case PrefKey::BEDTIME_START_HOUR: return "bedtime_start_hour";
    case PrefKey::BEDTIME_END_HOUR: return "bedtime_end_hour";
    case PrefKey::BEDTIME_BRIGHTNESS: return "bedtime_brightness";
    case PrefKey::CAMERA_DEFAULT_VIEW: return "camera_default_view";
    case PrefKey::CAMERA_CAROUSEL_INTERVAL: return "camera_carousel_interval";
    case PrefKey::CAMERA_FRAMING: return "camera_framing";
    case PrefKey::CAMERA_STREAM_MODE: return "camera_stream_mode";
    case PrefKey::CAMERA_DOORBELL_POPUP: return "camera_doorbell_popup";
    case PrefKey::SCREENSAVER_PHOTO_CLOCK: return "screensaver_photo_clock";
    case PrefKey::WEATHER_APP_BACKGROUND: return "weather_app_background";
    case PrefKey::COUNT: break;
  }
  return "";
}

inline std::string get_pref_value_str(PrefKey key, const PersistedDevicePrefs &p = persisted_prefs()) {
  switch (key) {
    case PrefKey::WAKE_WORD_MODE: return pref_wake_word_mode_cstr(p);
    case PrefKey::TAP_TO_WAKE: return pref_tap_to_wake_cstr(p);
    case PrefKey::PICKUP_TO_WAKE: return pref_pickup_to_wake_cstr(p);
    case PrefKey::BATTERY_SCREEN_TIMEOUT: return pref_battery_screen_timeout_cstr(p);
    case PrefKey::DOCKED_IDLE_TIMEOUT: return pref_docked_idle_timeout_cstr(p);
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT: return pref_battery_hibernate_timeout_cstr(p);
    case PrefKey::SCREENSAVER_MODE: return pref_screensaver_mode_cstr(p);
    case PrefKey::SCREENSAVER_STYLE: return pref_screensaver_style_cstr(p);
    case PrefKey::AUTO_BRIGHTNESS_MODE: return pref_auto_brightness_mode_cstr(p);
    case PrefKey::MANUAL_BRIGHTNESS: return std::to_string(p.manual_brightness_pct());
    case PrefKey::DAY_BRIGHTNESS: return std::to_string(p.day_brightness_pct());
    case PrefKey::NIGHT_BRIGHTNESS: return std::to_string(p.night_brightness_pct());
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
    case PrefKey::HOME_CLOCK_TAP_ACTION: return pref_home_clock_tap_cstr(p);
    case PrefKey::SHOW_UTC_CLOCK: return p.show_utc_clock ? "1" : "0";
    case PrefKey::WORLD_CLOCKS: return std::string(p.world_clocks_spec);
    case PrefKey::ALARMS_SUMMARY: return format_alarms_summary(p);
    case PrefKey::LOCAL_CLOCK_LABEL: return std::string(p.local_clock_label);
    case PrefKey::TOOLS_ITEMS: return std::string(effective_tools_items_sv(p));
    case PrefKey::GAMES_ITEMS: return std::string(effective_games_items_sv(p));
    case PrefKey::HOME_DATE_TAP_ACTION: return pref_home_date_tap_cstr(p);
    case PrefKey::CALENDAR_LOOKAHEAD_DAYS: return std::to_string(snap_calendar_lookahead_days(p.calendar_lookahead_days));
    case PrefKey::CALENDAR_WEEK_START: return pref_calendar_week_start_cstr(p);
    case PrefKey::BEDTIME_ACTIVE: return p.bedtime_active ? "1" : "0";
    case PrefKey::BEDTIME_DISPLAY: return pref_bedtime_display_cstr(p);
    case PrefKey::BEDTIME_SCHEDULE: return p.bedtime_schedule ? "1" : "0";
    case PrefKey::BEDTIME_START_HOUR: return std::to_string(p.bedtime_start_hour);
    case PrefKey::BEDTIME_END_HOUR: return std::to_string(p.bedtime_end_hour);
    case PrefKey::BEDTIME_BRIGHTNESS: return std::to_string(p.bedtime_brightness_pct());
    case PrefKey::CAMERA_DEFAULT_VIEW: return pref_camera_default_view_cstr(p);
    case PrefKey::CAMERA_CAROUSEL_INTERVAL: return pref_camera_carousel_interval_cstr(p);
    case PrefKey::CAMERA_FRAMING: return pref_camera_framing_cstr(p);
    case PrefKey::CAMERA_STREAM_MODE: return pref_camera_stream_mode_cstr(p);
    case PrefKey::CAMERA_DOORBELL_POPUP: return pref_camera_doorbell_popup_cstr(p);
    case PrefKey::SCREENSAVER_PHOTO_CLOCK: return p.screensaver_photo_clock ? "1" : "0";
    case PrefKey::WEATHER_APP_BACKGROUND: return p.weather_app_background ? "1" : "0";
    case PrefKey::COUNT: break;
  }
  return "";
}

inline bool set_pref_by_key_str(PersistedDevicePrefs &p, PrefKey key, std::string_view sv,
                                bool /*unused_cascade*/, PrefMask &changed_mask, uint32_t &effects_mask) {
  if (sv.empty() &&
      key != PrefKey::WORLD_CLOCKS &&
      key != PrefKey::LOCAL_CLOCK_LABEL &&
      key != PrefKey::TOOLS_ITEMS &&
      key != PrefKey::GAMES_ITEMS) {
    return false;
  }
  auto apply_enum = [&]<size_t N>(uint8_t &slot, const char *const (&opts)[N], uint32_t eff) -> bool {
    int idx = find_opt_index(opts, sv);
    if (idx < 0) return false;
    uint8_t u = static_cast<uint8_t>(idx);
    if (key == PrefKey::PICKUP_TO_WAKE && (!LATTER_HAS_IMU || LATTER_IS_STATIONARY)) u = 2;
    if (key == PrefKey::BATTERY_HIBERNATE_TIMEOUT && (!LATTER_HAS_BATTERY || LATTER_IS_STATIONARY)) u = 4;
    if (key == PrefKey::CLOCK_FACE && !LATTER_ROUND_DISPLAY && u == 1) u = 0;
    if (key == PrefKey::START_SCREEN && !p.home_screen_enabled && u == 0) u = kStartScreenTimeAppIdx;
    if (key == PrefKey::CAMERA_DOORBELL_POPUP && !LATTER_CHIP_P4) u = 0;
    if (slot == u) return false;
    slot = u;
    changed_mask |= pref_key_bit(key);
    effects_mask |= (eff | kPrefEffectRedraw);
    if (key == PrefKey::CLOCK_FACE) p.clock_face_digital = (p.clock_face != 1);
    if (key == PrefKey::TIME_FORMAT) p.is_24h_format = (p.time_format == 1);
    return true;
  };
  auto apply_bool = [&](bool &slot, uint32_t eff) -> bool {
    bool b = v3_parse_bool(sv);
    if (slot == b) return false;
    slot = b;
    changed_mask |= pref_key_bit(key);
    effects_mask |= (eff | kPrefEffectRedraw);
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
    case PrefKey::WAKE_WORD_MODE:
      return apply_enum(p.wake_word_mode, kWakeWordModeOpts, kPrefEffectWakeWordSync);
    case PrefKey::TAP_TO_WAKE:
      return apply_enum(p.tap_to_wake, kTapWakeOpts, kPrefEffectPowerPolicy);
    case PrefKey::PICKUP_TO_WAKE:
      return apply_enum(p.pickup_to_wake, kWakePolicyOpts, kPrefEffectPowerPolicy);
    case PrefKey::BATTERY_SCREEN_TIMEOUT:
      return apply_enum(p.battery_screen_timeout, kBattScreenTimeoutOpts, kPrefEffectSleepTimer);
    case PrefKey::DOCKED_IDLE_TIMEOUT:
      return apply_enum(p.docked_idle_timeout, kDockedIdleTimeoutOpts, kPrefEffectSleepTimer);
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT:
      return apply_enum(p.battery_hibernate_timeout, kBattHibernateTimeoutOpts, kPrefEffectPowerPolicy);
    case PrefKey::SCREENSAVER_MODE:
      return apply_enum(p.screensaver_mode, kScreensaverModeOpts,
                        kPrefEffectSleepTimer | kPrefEffectScreensaverChange);
    case PrefKey::SCREENSAVER_STYLE:
      return apply_enum(p.screensaver_style, kScreensaverStyleOpts, kPrefEffectScreensaverChange);
    case PrefKey::AUTO_BRIGHTNESS_MODE:
      return apply_enum(p.auto_brightness_mode, kAutoBrightnessModeOpts, kPrefEffectBrightness);
    case PrefKey::MANUAL_BRIGHTNESS:
      return apply_num(p.manual_brightness, 1, 100, kPrefEffectBrightness);
    case PrefKey::DAY_BRIGHTNESS:
      return apply_num(p.day_brightness, 1, 100, kPrefEffectBrightness);
    case PrefKey::NIGHT_BRIGHTNESS:
      return apply_num(p.night_brightness, 1, 100, kPrefEffectBrightness);
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
        p.start_screen = kStartScreenTimeAppIdx;  // Coerce Home -> Time App when Home Screen is disabled
        changed_mask |= pref_key_bit(PrefKey::START_SCREEN);
        effects_mask |= (kPrefEffectNavPolicy | kPrefEffectRedraw);
      }
      return chg;
    }
    case PrefKey::RETURN_TO_HOME_TIMEOUT:
      return apply_enum(p.return_to_home_timeout, kReturnToHomeTimeoutOpts, 0);
    case PrefKey::HOME_CLOCK_TAP_ACTION:
      return apply_enum(p.home_clock_tap_action, kHomeClockTapOpts, 0);
    case PrefKey::SHOW_UTC_CLOCK:
      return apply_bool(p.show_utc_clock, 0);
    case PrefKey::WORLD_CLOCKS: {
      const std::string norm = normalize_world_clocks_spec(sv);
      if (std::string_view(p.world_clocks_spec) == norm) return false;
      copy_bounded_cstr(p.world_clocks_spec, norm);
      changed_mask |= pref_key_bit(key);
      effects_mask |= kPrefEffectRedraw;
      return true;
    }
    case PrefKey::LOCAL_CLOCK_LABEL: {
      const std::string norm = sanitize_clock_label(sv);
      if (std::string_view(p.local_clock_label) == norm) return false;
      copy_bounded_cstr(p.local_clock_label, norm);
      changed_mask |= pref_key_bit(key);
      effects_mask |= kPrefEffectRedraw;
      return true;
    }
    case PrefKey::ALARMS_SUMMARY: {
      PersistedAlarmPref parsed[3]{};
      if (!parse_alarms_summary(sv, parsed)) return false;
      bool diff = false;
      for (int i = 0; i < 3; ++i) {
        if (p.alarms[i].hour != parsed[i].hour ||
            p.alarms[i].minute != parsed[i].minute ||
            p.alarms[i].enabled != parsed[i].enabled ||
            p.alarms[i].recurrence != parsed[i].recurrence) {
          p.alarms[i] = parsed[i];
          diff = true;
        }
      }
      if (!diff) return false;
      changed_mask |= pref_key_bit(key);
      effects_mask |= kPrefEffectRedraw;
      return true;
    }
    case PrefKey::TOOLS_ITEMS: {
      const std::string norm = normalize_tools_items_csv(sv);
      if (effective_tools_items_sv(p) == norm && p.tools_items[0] != '\0') return false;
      copy_bounded_cstr(p.tools_items, norm);
      if (&p == &persisted_prefs()) {
        device_prefs_state().tools_items = norm;
      }
      changed_mask |= pref_key_bit(key);
      effects_mask |= (kPrefEffectNavPolicy | kPrefEffectRedraw);
      return true;
    }
    case PrefKey::GAMES_ITEMS: {
      const std::string norm = normalize_games_items_csv(sv);
      if (effective_games_items_sv(p) == norm && p.games_items[0] != '\0') return false;
      copy_bounded_cstr(p.games_items, norm);
      if (&p == &persisted_prefs()) {
        device_prefs_state().games_items = norm;
      }
      changed_mask |= pref_key_bit(key);
      effects_mask |= (kPrefEffectNavPolicy | kPrefEffectRedraw);
      return true;
    }
    case PrefKey::HOME_DATE_TAP_ACTION:
      return apply_enum(p.home_date_tap_action, kHomeDateTapOpts, 0);
    case PrefKey::CALENDAR_LOOKAHEAD_DAYS: {
      const int parsed = v3_parse_int(sv, -9999);
      if (parsed == -9999) return false;
      const uint8_t snapped = snap_calendar_lookahead_days(parsed);
      if (p.calendar_lookahead_days == snapped) return false;
      p.calendar_lookahead_days = snapped;
      changed_mask |= pref_key_bit(key);
      effects_mask |= kPrefEffectRedraw;
      return true;
    }
    case PrefKey::CALENDAR_WEEK_START:
      return apply_enum(p.calendar_week_start, kCalendarWeekStartOpts, 0);
    case PrefKey::BEDTIME_ACTIVE:
      return apply_bool(p.bedtime_active,
                        kPrefEffectBedtime | kPrefEffectBrightness | kPrefEffectSleepTimer);
    case PrefKey::BEDTIME_DISPLAY:
      return apply_enum(p.bedtime_display, kBedtimeDisplayOpts,
                        kPrefEffectBedtime | kPrefEffectBrightness | kPrefEffectSleepTimer);
    case PrefKey::BEDTIME_SCHEDULE:
      return apply_bool(p.bedtime_schedule, kPrefEffectBedtime);
    case PrefKey::BEDTIME_START_HOUR:
      return apply_num(p.bedtime_start_hour, 0, 23, kPrefEffectBedtime);
    case PrefKey::BEDTIME_END_HOUR:
      return apply_num(p.bedtime_end_hour, 0, 23, kPrefEffectBedtime);
    case PrefKey::BEDTIME_BRIGHTNESS:
      return apply_num(p.bedtime_brightness, 1, 100, kPrefEffectBrightness);
    case PrefKey::CAMERA_DEFAULT_VIEW:
      return apply_enum(p.camera_default_view, kCameraDefaultViewOpts, 0);
    case PrefKey::CAMERA_CAROUSEL_INTERVAL:
      return apply_enum(p.camera_carousel_interval, kCameraCarouselIntervalOpts, 0);
    case PrefKey::CAMERA_FRAMING:
      return apply_enum(p.camera_framing, kCameraFramingOpts, 0);
    case PrefKey::CAMERA_STREAM_MODE:
      return apply_enum(p.camera_stream_mode, kCameraStreamModeOpts, 0);
    case PrefKey::CAMERA_DOORBELL_POPUP:
      return apply_enum(p.camera_doorbell_popup, kCameraDoorbellPopupOpts, 0);
    case PrefKey::SCREENSAVER_PHOTO_CLOCK:
      return apply_bool(p.screensaver_photo_clock, kPrefEffectScreensaverChange);
    case PrefKey::WEATHER_APP_BACKGROUND: {
      const bool chg = apply_bool(p.weather_app_background, 0);
      if (chg && &p == &persisted_prefs()) {
        device_prefs_state().weather_app_background = p.weather_app_background;
      }
      return chg;
    }
    case PrefKey::COUNT:
      break;
  }
  return false;
}

inline std::string encode_prefs_mask(PrefMask mask, const PersistedDevicePrefs &p = persisted_prefs()) {
  if (mask.empty()) return "";
  std::string out;
  out.reserve(128);
  for (uint8_t i = 0; i < static_cast<uint8_t>(PrefKey::COUNT); ++i) {
    const auto key = static_cast<PrefKey>(i);
    if ((mask & pref_key_bit(key)).empty()) continue;
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
// ble_beacon_enabled, world_clocks, alarms_summary, local_clock_label, tools_items, and
// games_items unless explicitly edited locally) so Latter seeds board-appropriate defaults
// without clobbering Studio-configured lists.
inline std::string build_hello_dirty_prefs(const PersistedDevicePrefs &p = persisted_prefs()) {
  if (p.prefs_seq == 0) {
    constexpr PrefMask kAllMask = pref_mask_all(static_cast<uint8_t>(PrefKey::COUNT));
    constexpr PrefMask kExcludeUnlessDirty =
        pref_key_bit(PrefKey::BLE_BEACON_ENABLED) |
        pref_key_bit(PrefKey::WORLD_CLOCKS) |
        pref_key_bit(PrefKey::ALARMS_SUMMARY) |
        pref_key_bit(PrefKey::LOCAL_CLOCK_LABEL) |
        pref_key_bit(PrefKey::TOOLS_ITEMS) |
        pref_key_bit(PrefKey::GAMES_ITEMS) |
        pref_key_bit(PrefKey::BEDTIME_ACTIVE);
    const PrefMask seed_mask = (kAllMask & ~kExcludeUnlessDirty) | p.dirty_mask;
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

  PrefMask changed_mask{};
  auto apply_f = [&](DevicePrefsField df, PrefKey pk) {
    set_pref_by_key_str(p, pk, f[static_cast<size_t>(df)], false, changed_mask, out_effects);
  };

  apply_f(DevicePrefsField::WAKE_WORD_MODE, PrefKey::WAKE_WORD_MODE);
  apply_f(DevicePrefsField::TAP_TO_WAKE, PrefKey::TAP_TO_WAKE);
  apply_f(DevicePrefsField::PICKUP_TO_WAKE, PrefKey::PICKUP_TO_WAKE);
  apply_f(DevicePrefsField::BATTERY_SCREEN_TIMEOUT, PrefKey::BATTERY_SCREEN_TIMEOUT);
  apply_f(DevicePrefsField::DOCKED_IDLE_TIMEOUT, PrefKey::DOCKED_IDLE_TIMEOUT);
  apply_f(DevicePrefsField::BATTERY_HIBERNATE_TIMEOUT, PrefKey::BATTERY_HIBERNATE_TIMEOUT);
  apply_f(DevicePrefsField::SCREENSAVER_MODE, PrefKey::SCREENSAVER_MODE);
  apply_f(DevicePrefsField::SCREENSAVER_STYLE, PrefKey::SCREENSAVER_STYLE);
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
  apply_f(DevicePrefsField::HOME_CLOCK_TAP_ACTION, PrefKey::HOME_CLOCK_TAP_ACTION);
  apply_f(DevicePrefsField::SHOW_UTC_CLOCK, PrefKey::SHOW_UTC_CLOCK);
  apply_f(DevicePrefsField::WORLD_CLOCKS, PrefKey::WORLD_CLOCKS);
  apply_f(DevicePrefsField::ALARMS_SUMMARY, PrefKey::ALARMS_SUMMARY);
  apply_f(DevicePrefsField::LOCAL_CLOCK_LABEL, PrefKey::LOCAL_CLOCK_LABEL);
  apply_f(DevicePrefsField::TOOLS_ITEMS, PrefKey::TOOLS_ITEMS);
  apply_f(DevicePrefsField::GAMES_ITEMS, PrefKey::GAMES_ITEMS);
  apply_f(DevicePrefsField::HOME_DATE_TAP_ACTION, PrefKey::HOME_DATE_TAP_ACTION);
  apply_f(DevicePrefsField::CALENDAR_LOOKAHEAD_DAYS, PrefKey::CALENDAR_LOOKAHEAD_DAYS);
  apply_f(DevicePrefsField::CALENDAR_WEEK_START, PrefKey::CALENDAR_WEEK_START);
  apply_f(DevicePrefsField::BEDTIME_ACTIVE, PrefKey::BEDTIME_ACTIVE);
  apply_f(DevicePrefsField::BEDTIME_DISPLAY, PrefKey::BEDTIME_DISPLAY);
  apply_f(DevicePrefsField::BEDTIME_SCHEDULE, PrefKey::BEDTIME_SCHEDULE);
  apply_f(DevicePrefsField::BEDTIME_START_HOUR, PrefKey::BEDTIME_START_HOUR);
  apply_f(DevicePrefsField::BEDTIME_END_HOUR, PrefKey::BEDTIME_END_HOUR);
  apply_f(DevicePrefsField::BEDTIME_BRIGHTNESS, PrefKey::BEDTIME_BRIGHTNESS);
  apply_f(DevicePrefsField::CAMERA_DEFAULT_VIEW, PrefKey::CAMERA_DEFAULT_VIEW);
  apply_f(DevicePrefsField::CAMERA_CAROUSEL_INTERVAL, PrefKey::CAMERA_CAROUSEL_INTERVAL);
  apply_f(DevicePrefsField::CAMERA_FRAMING, PrefKey::CAMERA_FRAMING);
  apply_f(DevicePrefsField::CAMERA_STREAM_MODE, PrefKey::CAMERA_STREAM_MODE);
  apply_f(DevicePrefsField::CAMERA_DOORBELL_POPUP, PrefKey::CAMERA_DOORBELL_POPUP);
  apply_f(DevicePrefsField::SCREENSAVER_PHOTO_CLOCK, PrefKey::SCREENSAVER_PHOTO_CLOCK);
  apply_f(DevicePrefsField::WEATHER_APP_BACKGROUND, PrefKey::WEATHER_APP_BACKGROUND);
  st.tools_items = std::string(effective_tools_items_sv(p));
  st.games_items = std::string(effective_games_items_sv(p));
  st.weather_app_background = p.weather_app_background;

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
  const bool meta_changed = (in_seq > 0 && p.prefs_seq != in_seq) || p.dirty_mask.any();
  if (in_seq > 0) p.prefs_seq = in_seq;
  p.dirty_mask.clear();

  if (changed_mask.any() || meta_changed) {
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

// ==============================================================================
// Calendar App: Civil Date Math, PSRAM Window Store, Metadata & STT Dictation
// ==============================================================================
inline constexpr bool is_leap_year(int y) noexcept {
  return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

inline constexpr int days_in_month(int y, int m) noexcept {
  switch (m) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12: return 31;
    case 4: case 6: case 9: case 11: return 30;
    case 2: return is_leap_year(y) ? 29 : 28;
    default: return 30;
  }
}

inline constexpr int32_t make_ymd(int y, int m, int d) noexcept {
  return static_cast<int32_t>(y) * 10000 + static_cast<int32_t>(m) * 100 + static_cast<int32_t>(d);
}

inline constexpr bool split_ymd(int32_t ymd, int &y, int &m, int &d) noexcept {
  if (ymd < 19700101 || ymd > 21991231) return false;
  y = static_cast<int>(ymd / 10000);
  m = static_cast<int>((ymd / 100) % 100);
  d = static_cast<int>(ymd % 100);
  if (m < 1 || m > 12) return false;
  if (d < 1 || d > days_in_month(y, m)) return false;
  return true;
}

// Howard Hinnant civil-from-days / days-from-civil (1970-01-01 == 0, TZ-free).
inline constexpr int32_t days_from_civil(int y, int m, int d) noexcept {
  y -= (m <= 2) ? 1 : 0;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153U * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2U) / 5U +
                       static_cast<unsigned>(d) - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

inline constexpr void civil_from_days(int32_t z, int &y, int &m, int &d) noexcept {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
  y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
  const unsigned mp = (5U * doy + 2U) / 153U;
  d = static_cast<int>(doy - (153U * mp + 2U) / 5U + 1U);
  m = static_cast<int>(mp < 10U ? mp + 3U : mp - 9U);
  y += (m <= 2) ? 1 : 0;
}

// Returns 0=Sunday .. 6=Saturday.
inline constexpr int weekday_sun0_from_ymd(int32_t ymd) noexcept {
  int y = 0, m = 0, d = 0;
  if (!split_ymd(ymd, y, m, d)) return 0;
  const int32_t z = days_from_civil(y, m, d);
  return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

inline constexpr int32_t add_days_ymd(int32_t ymd, int delta_days) noexcept {
  int y = 0, m = 0, d = 0;
  if (!split_ymd(ymd, y, m, d)) return ymd;
  const int32_t z = days_from_civil(y, m, d) + static_cast<int32_t>(delta_days);
  int ny = 0, nm = 0, nd = 0;
  civil_from_days(z, ny, nm, nd);
  return make_ymd(ny, nm, nd);
}

inline constexpr int32_t add_months_ymd(int32_t ymd, int delta_months) noexcept {
  int y = 0, m = 0, d = 0;
  if (!split_ymd(ymd, y, m, d)) return ymd;
  const int total_m = (y * 12 + (m - 1)) + delta_months;
  int ny = total_m / 12;
  int nm = (total_m % 12) + 1;
  if (nm <= 0) {
    nm += 12;
    ny -= 1;
  }
  ny = std::clamp(ny, 1970, 2199);
  const int nd = std::min(d, days_in_month(ny, nm));
  return make_ymd(ny, nm, nd);
}

inline constexpr int32_t start_of_week_ymd(int32_t ymd, bool monday_first) noexcept {
  const int wd = weekday_sun0_from_ymd(ymd);
  const int back = monday_first ? ((wd + 6) % 7) : wd;
  return add_days_ymd(ymd, -back);
}

inline constexpr int days_between_ymd(int32_t start_ymd, int32_t end_ymd) noexcept {
  int sy = 0, sm = 0, sd = 0;
  int ey = 0, em = 0, ed = 0;
  if (!split_ymd(start_ymd, sy, sm, sd) || !split_ymd(end_ymd, ey, em, ed)) return 0;
  return static_cast<int>(days_from_civil(ey, em, ed) - days_from_civil(sy, sm, sd));
}

inline void format_ymd_iso(int32_t ymd, char (&out)[12]) noexcept {
  int y = 2026, m = 1, d = 1;
  (void)split_ymd(ymd, y, m, d);
  std::snprintf(out, sizeof(out), "%04d-%02d-%02d", y, m, d);
}

inline int32_t parse_ymd_iso(std::string_view sv) noexcept {
  sv = trim_ascii_ws(sv);
  if (sv.size() == 8) {
    const int parsed = v3_parse_int(sv, 0);
    int y = 0, m = 0, d = 0;
    return split_ymd(parsed, y, m, d) ? parsed : 0;
  }
  if (sv.size() >= 10 && sv[4] == '-' && sv[7] == '-') {
    const int y = v3_parse_int(sv.substr(0, 4), 0);
    const int m = v3_parse_int(sv.substr(5, 2), 0);
    const int d = v3_parse_int(sv.substr(8, 2), 0);
    const int32_t ymd = make_ymd(y, m, d);
    int cy = 0, cm = 0, cd = 0;
    return split_ymd(ymd, cy, cm, cd) ? ymd : 0;
  }
  return 0;
}

inline constexpr int kMaxCalendarSources = 8;
inline constexpr int kMaxCalendarLocations = 12;
inline constexpr int kMaxCalendarWindowEvents = 64;
inline constexpr int kMaxCalendarSummaryPreviewEvents = 5;

inline constexpr const char *const kCalTitlePresets[10] = {
    "Reminder", "Meeting", "Appointment", "Errand", "Dinner",
    "Workout", "Call", "Travel", "Family", "Service"};

inline constexpr const char *const kCalRepeatLabels[7] = {
    "Once", "Daily", "Weekdays", "Weekly", "Biweekly", "Monthly", "Yearly"};

inline constexpr const char *const kCalRepeatRrules[7] = {
    "",
    "FREQ=DAILY",
    "FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR",
    "FREQ=WEEKLY",
    "FREQ=WEEKLY;INTERVAL=2",
    "FREQ=MONTHLY",
    "FREQ=YEARLY"};

struct CalendarSourceMeta {
  char entity_id[64]{};
  char label[32]{};
  uint8_t color_idx = 0;
  bool can_create = false;
  bool can_delete = false;
};

inline int parse_calendar_sources_meta(std::string_view raw,
                                       CalendarSourceMeta *out,
                                       int max_out = kMaxCalendarSources) noexcept {
  if (raw.empty() || out == nullptr || max_out <= 0) return 0;
  const int cap = std::min(max_out, kMaxCalendarSources);
  std::string_view recs[kMaxCalendarSources]{};
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int count = 0;
  for (size_t i = 0; i < n; ++i) {
    std::string_view cols[5]{};
    const size_t cn = v3_split_fields(recs[i], cols, 5, '|');
    if (cn < 2 || cols[0].empty()) continue;
    CalendarSourceMeta item{};
    copy_bounded_cstr(item.entity_id, cols[0]);
    copy_bounded_cstr(item.label, cols[1].empty() ? cols[0] : cols[1]);
    item.color_idx = static_cast<uint8_t>(std::clamp(cn >= 3 ? v3_parse_int(cols[2], count) : count, 0, 5));
    item.can_create = (cn >= 4) && v3_parse_bool(cols[3]);
    item.can_delete = (cn >= 5) && v3_parse_bool(cols[4]);
    out[count++] = item;
  }
  return count;
}

inline int collect_writable_calendar_sources(std::string_view raw,
                                             CalendarSourceMeta *out,
                                             int *out_source_indices,
                                             int max_out = kMaxCalendarSources) noexcept {
  if (raw.empty() || max_out <= 0) return 0;
  std::string_view recs[kMaxCalendarSources]{};
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(kMaxCalendarSources), kProtocolRecordSep);
  int src_idx = 0;
  int wcnt = 0;
  for (size_t i = 0; i < n; ++i) {
    std::string_view cols[5]{};
    const size_t cn = v3_split_fields(recs[i], cols, 5, '|');
    if (cn < 2 || cols[0].empty()) continue;
    const bool can_create = (cn >= 4) && (cols[3] == "1" || cols[3] == "true");
    if (can_create && wcnt < max_out) {
      if (out != nullptr) {
        CalendarSourceMeta item{};
        copy_bounded_cstr(item.entity_id, cols[0]);
        copy_bounded_cstr(item.label, cols[1].empty() ? cols[0] : cols[1]);
        item.color_idx = static_cast<uint8_t>(std::clamp(cn >= 3 ? v3_parse_int(cols[2], src_idx) : src_idx, 0, 5));
        item.can_create = true;
        item.can_delete = (cn >= 5) && (cols[4] == "1" || cols[4] == "true");
        out[wcnt] = item;
      }
      if (out_source_indices != nullptr) out_source_indices[wcnt] = src_idx;
      ++wcnt;
    }
    ++src_idx;
  }
  return wcnt;
}

inline int parse_calendar_locations_catalog(std::string_view raw,
                                            std::string_view *out,
                                            int max_out = kMaxCalendarLocations) noexcept {
  if (raw.empty() || out == nullptr || max_out <= 0) return 0;
  const int cap = std::min(max_out, kMaxCalendarLocations);
  std::string_view recs[kMaxCalendarLocations]{};
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int count = 0;
  for (size_t i = 0; i < n; ++i) {
    const std::string_view sv = trim_ascii_ws(recs[i]);
    if (!sv.empty()) out[count++] = sv;
  }
  return count;
}

struct CalendarWindowEvent {
  int32_t start_ymd = 0;    // YYYYMMDD local start date
  int32_t end_ymd = 0;      // YYYYMMDD inclusive local end date
  int16_t start_min = -1;   // 0..1439 local minutes from midnight (-1 when all_day)
  int16_t end_min = -1;     // 0..1440 local minutes from midnight (-1 when all_day)
  uint8_t all_day = 0;
  uint8_t cal_idx = 0;
  char uid[72]{};
  char recurrence_id[40]{};
  char title[60]{};
  char location[68]{};
  char description[144]{};

  void reset_in_place() noexcept {
    start_ymd = 0;
    end_ymd = 0;
    start_min = -1;
    end_min = -1;
    all_day = 0;
    cal_idx = 0;
    std::memset(uid, 0, sizeof(uid));
    std::memset(recurrence_id, 0, sizeof(recurrence_id));
    std::memset(title, 0, sizeof(title));
    std::memset(location, 0, sizeof(location));
    std::memset(description, 0, sizeof(description));
  }
};

static_assert(std::is_trivially_copyable_v<CalendarWindowEvent> &&
                  std::is_standard_layout_v<CalendarWindowEvent>,
              "CalendarWindowEvent must remain standard-layout and trivially copyable for PSRAM storage.");

struct CalendarWindowStore {
  uint32_t rev = 0;
  int32_t window_start_ymd = 0;
  int32_t window_end_ymd = 0;
  int count = 0;
  bool loading = false;
  CalendarWindowEvent events[kMaxCalendarWindowEvents]{};

  int staging_req_id = -1;
  int staging_expected_chunks = 0;
  int staging_next_chunk = 0;
  int32_t staging_start_ymd = 0;
  int32_t staging_end_ymd = 0;
  int staging_count = 0;
  CalendarWindowEvent staging_events[kMaxCalendarWindowEvents]{};

  CalendarWindowStore(const CalendarWindowStore &) = delete;
  CalendarWindowStore &operator=(const CalendarWindowStore &) = delete;
  CalendarWindowStore(CalendarWindowStore &&) = delete;
  CalendarWindowStore &operator=(CalendarWindowStore &&) = delete;

  void reset_in_place() noexcept {
    rev = 0;
    window_start_ymd = 0;
    window_end_ymd = 0;
    count = 0;
    loading = false;
    for (auto &ev : events) {
      ev.reset_in_place();
    }
    staging_req_id = -1;
    staging_expected_chunks = 0;
    staging_next_chunk = 0;
    staging_start_ymd = 0;
    staging_end_ymd = 0;
    staging_count = 0;
    for (auto &ev : staging_events) {
      ev.reset_in_place();
    }
  }

 private:
  CalendarWindowStore() noexcept { reset_in_place(); }
  friend CalendarWindowStore &calendar_window_store() noexcept;
};

static_assert(std::is_standard_layout_v<CalendarWindowStore>,
              "CalendarWindowStore must remain standard-layout for PSRAM calloc.");
static_assert(!std::is_default_constructible_v<CalendarWindowStore> &&
                  !std::is_copy_constructible_v<CalendarWindowStore> &&
                  !std::is_copy_assignable_v<CalendarWindowStore> &&
                  !std::is_move_constructible_v<CalendarWindowStore> &&
                  !std::is_move_assignable_v<CalendarWindowStore>,
              "CalendarWindowStore (~51 KB) must never be constructed on the stack or copied by value.");

inline CalendarWindowStore &calendar_window_store() noexcept {
#if LATTER_HAS_SPIRAM_ALLOC
  static CalendarWindowStore *ptr = nullptr;
  if (ptr == nullptr) {
    void *raw = heap_caps_calloc(1, sizeof(CalendarWindowStore), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw == nullptr) {
      raw = heap_caps_calloc(1, sizeof(CalendarWindowStore), MALLOC_CAP_8BIT);
    }
    if (raw != nullptr) {
      ptr = reinterpret_cast<CalendarWindowStore *>(raw);
      ptr->reset_in_place();
    } else {
      ptr = new CalendarWindowStore();
    }
  }
  return *ptr;
#else
  static CalendarWindowStore store;
  return store;
#endif
}

inline void reset_calendar_window_store() noexcept {
  calendar_window_store().reset_in_place();
}

inline CalendarWindowEvent *calendar_summary_preview_scratch() noexcept {
#if LATTER_HAS_SPIRAM_ALLOC
  static CalendarWindowEvent *ptr = nullptr;
  if (ptr == nullptr) {
    ptr = static_cast<CalendarWindowEvent *>(
        heap_caps_calloc(kMaxCalendarSummaryPreviewEvents, sizeof(CalendarWindowEvent),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ptr == nullptr) {
      ptr = static_cast<CalendarWindowEvent *>(
          heap_caps_calloc(kMaxCalendarSummaryPreviewEvents, sizeof(CalendarWindowEvent), MALLOC_CAP_8BIT));
    }
    if (ptr != nullptr) {
      for (int i = 0; i < kMaxCalendarSummaryPreviewEvents; ++i) {
        ptr[i].reset_in_place();
      }
    } else {
      ptr = new CalendarWindowEvent[kMaxCalendarSummaryPreviewEvents]{};
    }
  }
  return ptr;
#else
  static CalendarWindowEvent buf[kMaxCalendarSummaryPreviewEvents]{};
  return buf;
#endif
}

// Populates up to `kMaxCalendarSummaryPreviewEvents` preview cards from `calendar_state().summary`
// into PSRAM/heap scratch (`< 256 B` stack, zero mutation of `calendar_window_store()`).
inline int populate_calendar_summary_preview(
    std::string_view summary,
    int32_t today_ymd,
    CalendarWindowEvent *out_events = calendar_summary_preview_scratch(),
    int max_out = kMaxCalendarSummaryPreviewEvents) noexcept {
  if (summary.empty() || out_events == nullptr || max_out <= 0) return 0;
  const int cap = std::min(max_out, kMaxCalendarSummaryPreviewEvents);
  std::string_view recs[kMaxCalendarSummaryPreviewEvents]{};
  const size_t n = v3_split_fields(summary, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int count = 0;
  for (size_t i = 0; i < n && count < cap; ++i) {
    std::string_view f[4]{};
    if (v3_split_fields(recs[i], f, 4, '|') != 4) continue;
    auto &dst = out_events[count];
    dst.reset_in_place();
    dst.start_ymd = today_ymd;
    dst.end_ymd = today_ymd;
    dst.all_day = (f[3] == "1") ? 1U : 0U;
    copy_bounded_cstr(dst.title, f[1].empty() ? std::string_view("Event") : f[1]);
    copy_bounded_cstr(dst.location, f[2]);
    ++count;
  }
  return count;
}

inline bool parse_calendar_window_event_record(std::string_view rec, CalendarWindowEvent &out) noexcept {
  if (rec.empty()) return false;
  std::string_view f[11]{};
  const size_t n = v3_split_fields(rec, f, 11, kProtocolFieldSep);
  if (n < 9) return false;
  const int32_t s_ymd = parse_ymd_iso(f[0]);
  if (s_ymd == 0) return false;
  int32_t e_ymd = parse_ymd_iso(f[1]);
  if (e_ymd < s_ymd) e_ymd = s_ymd;

  out.reset_in_place();
  out.start_ymd = s_ymd;
  out.end_ymd = e_ymd;
  out.start_min = static_cast<int16_t>(std::clamp(v3_parse_int(f[2], -1), -1, 1439));
  out.end_min = static_cast<int16_t>(std::clamp(v3_parse_int(f[3], -1), -1, 1440));
  out.all_day = v3_parse_bool(f[4]) ? 1U : 0U;
  out.cal_idx = static_cast<uint8_t>(std::clamp(v3_parse_int(f[5], 0), 0, 7));
  copy_bounded_cstr(out.uid, f[6]);
  copy_bounded_cstr(out.recurrence_id, f[7]);
  copy_bounded_cstr(out.title, f[8].empty() ? std::string_view("Event") : f[8]);
  if (n >= 10) copy_bounded_cstr(out.location, f[9]);
  if (n >= 11) copy_bounded_cstr(out.description, f[10]);
  return true;
}

inline bool apply_calendar_view_action(int req_id,
                                       int chunk_index,
                                       int chunk_count,
                                       std::string_view window_start,
                                       std::string_view window_end,
                                       std::string_view map_url,
                                       std::string_view events,
                                       CalendarWindowStore &store = calendar_window_store(),
                                       DeviceRuntimeState &rs = runtime_state()) {
  if (!map_url.empty() && chunk_count <= 0) {
    rs.cal.detail_map_url = std::string(map_url);
    store.rev++;
    return true;
  }
  if (chunk_index < 0 || chunk_count <= 0 || chunk_index >= chunk_count || chunk_count > 16) {
    return false;
  }
  if (chunk_index == 0) {
    store.staging_req_id = req_id;
    store.staging_expected_chunks = chunk_count;
    store.staging_next_chunk = 0;
    store.staging_start_ymd = parse_ymd_iso(window_start);
    store.staging_end_ymd = parse_ymd_iso(window_end);
    store.staging_count = 0;
  } else if (req_id != store.staging_req_id ||
             chunk_index != store.staging_next_chunk ||
             chunk_count != store.staging_expected_chunks) {
    store.staging_expected_chunks = 0;
    store.staging_next_chunk = 0;
    store.staging_count = 0;
    return false;
  }

  if (!events.empty()) {
    size_t pos = 0;
    while (pos <= events.size() && store.staging_count < kMaxCalendarWindowEvents) {
      const size_t sep = events.find(kProtocolRecordSep, pos);
      const std::string_view rec =
          (sep == std::string_view::npos) ? events.substr(pos) : events.substr(pos, sep - pos);
      if (parse_calendar_window_event_record(rec, store.staging_events[store.staging_count])) {
        store.staging_count++;
      }
      if (sep == std::string_view::npos) break;
      pos = sep + 1;
    }
  }

  store.staging_next_chunk++;
  if (store.staging_next_chunk < store.staging_expected_chunks) {
    return false;
  }

  store.count = store.staging_count;
  for (int i = 0; i < store.count; ++i) {
    store.events[i] = store.staging_events[i];
  }
  if (store.staging_start_ymd > 0) store.window_start_ymd = store.staging_start_ymd;
  if (store.staging_end_ymd > 0) store.window_end_ymd = store.staging_end_ymd;
  store.loading = false;
  store.staging_expected_chunks = 0;
  store.staging_next_chunk = 0;
  if (!map_url.empty()) {
    rs.cal.detail_map_url = std::string(map_url);
  }
  if (rs.cal.selected_event_idx >= store.count) {
    rs.cal.selected_event_idx = -1;
    if (rs.cal.drilldown == CalDrilldown::DETAIL || rs.cal.drilldown == CalDrilldown::MAP) {
      rs.cal.drilldown = CalDrilldown::NONE;
      rs.cal.delete_confirm_open = false;
    }
  }
  store.rev++;
  return true;
}

inline constexpr bool calendar_event_overlaps_ymd(const CalendarWindowEvent &ev, int32_t ymd) noexcept {
  return ymd >= ev.start_ymd && ymd <= ev.end_ymd;
}

inline constexpr bool calendar_event_is_upcoming(const CalendarWindowEvent &ev,
                                                 int32_t today_ymd,
                                                 int now_min_of_day,
                                                 int32_t horizon_ymd) noexcept {
  if (ev.start_ymd > horizon_ymd || ev.end_ymd < today_ymd) return false;
  if (ev.end_ymd > today_ymd || ev.all_day != 0U) return true;
  if (ev.end_min >= 0) return ev.end_min > now_min_of_day;
  if (ev.start_min >= 0) return (ev.start_min + 60) > now_min_of_day;
  return true;
}

inline int collect_calendar_day_events(int32_t ymd,
                                       int *out_indices,
                                       int max_out,
                                       const CalendarWindowStore &store = calendar_window_store()) noexcept {
  if (out_indices == nullptr || max_out <= 0) return 0;
  int n = 0;
  // All-day events first, then chronological timed events.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < store.count && n < max_out; ++i) {
      const auto &ev = store.events[i];
      if (!calendar_event_overlaps_ymd(ev, ymd)) continue;
      const bool is_all_day = (ev.all_day != 0U);
      if ((pass == 0 && !is_all_day) || (pass == 1 && is_all_day)) continue;
      out_indices[n++] = i;
    }
  }
  return n;
}

inline int collect_calendar_overview_events(int32_t today_ymd,
                                            int now_min_of_day,
                                            int lookahead_days,
                                            int *out_indices,
                                            int max_out,
                                            const CalendarWindowStore &store = calendar_window_store()) noexcept {
  if (out_indices == nullptr || max_out <= 0) return 0;
  const int clamped_days = std::max(1, lookahead_days);
  const int32_t horizon_ymd = add_days_ymd(today_ymd, clamped_days);
  int n = 0;
  for (int i = 0; i < store.count && n < max_out; ++i) {
    if (calendar_event_is_upcoming(store.events[i], today_ymd, now_min_of_day, horizon_ymd)) {
      out_indices[n++] = i;
    }
  }
  return n;
}

// Collect up to 3 distinct source calendar indices (`cal_idx`) active on `ymd` for Month/Week dots.
inline int collect_calendar_day_dots(int32_t ymd,
                                     uint8_t (&out_cal_indices)[3],
                                     const CalendarWindowStore &store = calendar_window_store()) noexcept {
  int cnt = 0;
  for (int i = 0; i < store.count && cnt < 3; ++i) {
    const auto &ev = store.events[i];
    if (!calendar_event_overlaps_ymd(ev, ymd)) continue;
    bool seen = false;
    for (int j = 0; j < cnt; ++j) {
      if (out_cal_indices[j] == ev.cal_idx) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      out_cal_indices[cnt++] = ev.cal_idx;
    }
  }
  return cnt;
}

inline bool is_calendar_map_view_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.nav.gui_page == PageId::CALENDAR && rs.cal.drilldown == CalDrilldown::MAP;
}

inline bool cal_dictation_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.cal.dictation_field != 0U;
}

inline void start_calendar_dictation(uint8_t field,
                                     uint32_t now_ms,
                                     DeviceRuntimeState &rs = runtime_state()) {
  rs.cal.dictation_field = field;
  rs.cal.dictation_started_ms = now_ms;
  rs.cal.feedback_text = "Listening — speak now...";
  rs.cal.feedback_ms = now_ms;
}

inline bool finish_calendar_dictation(std::string_view stt_text,
                                      uint32_t now_ms,
                                      DeviceRuntimeState &rs = runtime_state()) {
  const uint8_t field = rs.cal.dictation_field;
  rs.cal.dictation_field = 0U;
  rs.cal.dictation_started_ms = 0U;
  std::string_view sv = trim_ascii_ws(stt_text);
  while (sv.size() > 1U && (sv.back() == '.' || sv.back() == '!' || sv.back() == '?')) {
    sv.remove_suffix(1);
    sv = trim_ascii_ws(sv);
  }
  if (sv.empty() || field == 0U) {
    rs.cal.feedback_text = "Didn't catch that";
    rs.cal.feedback_ms = now_ms;
    return false;
  }
  std::string cleaned(sv.substr(0, (field == 1U) ? 56U : 64U));
  if (!cleaned.empty() && std::islower(static_cast<unsigned char>(cleaned[0]))) {
    cleaned[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cleaned[0])));
  }
  if (field == 1U) {
    rs.cal.draft_title = std::move(cleaned);
  } else if (field == 2U) {
    rs.cal.draft_location = std::move(cleaned);
  }
  rs.cal.feedback_text = "Captured";
  rs.cal.feedback_ms = now_ms;
  return true;
}

inline void cancel_calendar_dictation(const char *msg,
                                      uint32_t now_ms,
                                      DeviceRuntimeState &rs = runtime_state()) {
  rs.cal.dictation_field = 0U;
  rs.cal.dictation_started_ms = 0U;
  rs.cal.feedback_text = (msg != nullptr) ? msg : "";
  rs.cal.feedback_ms = now_ms;
}

inline bool broadcast_dictation_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.intercom.broadcast_dictation_active;
}

inline void start_broadcast_dictation(uint32_t now_ms,
                                      DeviceRuntimeState &rs = runtime_state()) {
  rs.intercom.broadcast_dictation_active = true;
  rs.intercom.broadcast_dictation_started_ms = now_ms;
  rs.intercom.broadcast_feedback = "Listening...";
  rs.intercom.broadcast_feedback_ms = now_ms;
}

inline bool finish_broadcast_dictation(std::string_view stt_text,
                                       uint32_t now_ms,
                                       DeviceRuntimeState &rs = runtime_state()) {
  rs.intercom.broadcast_dictation_active = false;
  rs.intercom.broadcast_dictation_started_ms = 0U;
  std::string_view sv = trim_ascii_ws(stt_text);
  if (sv.empty()) {
    rs.intercom.broadcast_feedback = "Didn't catch that";
    rs.intercom.broadcast_feedback_ms = now_ms;
    return false;
  }
  std::string cleaned(sv.substr(0, 160U));
  if (!cleaned.empty() && std::islower(static_cast<unsigned char>(cleaned[0]))) {
    cleaned[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cleaned[0])));
  }
  rs.intercom.broadcast_message = std::move(cleaned);
  rs.intercom.broadcast_feedback = "Broadcast sent";
  rs.intercom.broadcast_feedback_ms = now_ms;
  return true;
}

inline void cancel_broadcast_dictation(const char *msg,
                                       uint32_t now_ms,
                                       DeviceRuntimeState &rs = runtime_state()) {
  rs.intercom.broadcast_dictation_active = false;
  rs.intercom.broadcast_dictation_started_ms = 0U;
  if (msg != nullptr && msg[0] != '\0') {
    rs.intercom.broadcast_feedback = msg;
    rs.intercom.broadcast_feedback_ms = now_ms;
  }
}

// Parse Contract v3 packed browse/area slots ("name\x1fid\x1ftype\x1factive\x1flevel[\x1fqueue_item_id\x1fis_current]\x1e...")
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
    std::string_view cols[8];
    size_t col_cnt = v3_split_fields(recs[i], cols, 8, '\x1f');
    if (col_cnt >= 2) {
      out_slots[valid_cnt].name          = std::string(cols[0]);
      out_slots[valid_cnt].id            = std::string(cols[1]);
      out_slots[valid_cnt].type          = (col_cnt >= 3) ? std::string(cols[2]) : "";
      out_slots[valid_cnt].active        = (col_cnt >= 4) ? v3_parse_bool(cols[3]) : false;
      out_slots[valid_cnt].level         = (col_cnt >= 5) ? v3_parse_int(cols[4], -1) : -1;
      out_slots[valid_cnt].queue_item_id = (col_cnt >= 6) ? std::string(cols[5]) : "";
      out_slots[valid_cnt].is_current    = (col_cnt >= 7) ? v3_parse_bool(cols[6]) : false;
      valid_cnt++;
    }
  }
  return valid_cnt;
}

inline void append_music_lyrics_records_blob(std::string_view blob,
                                             std::vector<MusicLyricLine> &dst,
                                             size_t max_total = kMaxMusicLyricLines) {
  const std::string_view trimmed = trim_ascii_ws(blob);
  if (trimmed.empty() || dst.size() >= max_total) return;

  // Canonical Protocol v3 record format: 0x1E-joined "<t_ms>\x1f<text>" lines.
  if (trimmed.front() != '[') {
    size_t pos = 0;
    while (pos <= blob.size() && dst.size() < max_total) {
      const size_t sep = blob.find(kProtocolRecordSep, pos);
      const std::string_view rec =
          (sep == std::string_view::npos) ? blob.substr(pos) : blob.substr(pos, sep - pos);
      if (!rec.empty()) {
        const size_t fsep = rec.find(kProtocolFieldSep);
        MusicLyricLine line{};
        if (fsep != std::string_view::npos) {
          line.t_ms = static_cast<int32_t>(v3_parse_int(rec.substr(0, fsep), -1));
          line.text = std::string(rec.substr(fsep + 1U));
        } else {
          line.t_ms = -1;
          line.text = std::string(rec);
        }
        dst.push_back(std::move(line));
      }
      if (sep == std::string_view::npos) break;
      pos = sep + 1U;
    }
    return;
  }

  // Fallback compact JSON array parser: [{"t_ms":1200,"text":"..."}, ...]
  auto extract_json_str = [](std::string_view obj, std::string_view key, std::string &out) -> bool {
    const size_t kpos = obj.find(key);
    if (kpos == std::string_view::npos) return false;
    const size_t colon = obj.find(':', kpos + key.size());
    if (colon == std::string_view::npos) return false;
    const size_t q0 = obj.find('"', colon + 1U);
    if (q0 == std::string_view::npos) return false;
    out.clear();
    bool esc = false;
    for (size_t i = q0 + 1U; i < obj.size(); ++i) {
      const char ch = obj[i];
      if (esc) {
        if (ch == 'n') out.push_back('\n');
        else if (ch == 'r') out.push_back(' ');
        else if (ch == 't') out.push_back(' ');
        else out.push_back(ch);
        esc = false;
        continue;
      }
      if (ch == '\\') {
        esc = true;
        continue;
      }
      if (ch == '"') return true;
      out.push_back(ch);
    }
    return false;
  };

  size_t cur = 0;
  while (cur < trimmed.size() && dst.size() < max_total) {
    const size_t lbrace = trimmed.find('{', cur);
    if (lbrace == std::string_view::npos) break;
    bool in_str = false;
    bool esc = false;
    size_t rbrace = std::string_view::npos;
    for (size_t i = lbrace + 1U; i < trimmed.size(); ++i) {
      const char ch = trimmed[i];
      if (in_str) {
        if (esc) esc = false;
        else if (ch == '\\') esc = true;
        else if (ch == '"') in_str = false;
        continue;
      }
      if (ch == '"') in_str = true;
      else if (ch == '}') {
        rbrace = i;
        break;
      }
    }
    if (rbrace == std::string_view::npos) break;
    const std::string_view obj = trimmed.substr(lbrace, rbrace - lbrace + 1U);
    MusicLyricLine line{};
    const size_t tpos = obj.find("\"t_ms\"");
    if (tpos != std::string_view::npos) {
      const size_t colon = obj.find(':', tpos + 6U);
      if (colon != std::string_view::npos) {
        size_t ns = colon + 1U;
        while (ns < obj.size() && (obj[ns] == ' ' || obj[ns] == '\t')) ++ns;
        size_t ne = ns;
        if (ne < obj.size() && (obj[ne] == '-' || obj[ne] == '+')) ++ne;
        while (ne < obj.size() && obj[ne] >= '0' && obj[ne] <= '9') ++ne;
        line.t_ms = static_cast<int32_t>(v3_parse_int(obj.substr(ns, ne - ns), -1));
      }
    }
    if (extract_json_str(obj, "\"text\"", line.text)) {
      dst.push_back(std::move(line));
    }
    cur = rbrace + 1U;
  }
}

inline bool apply_music_lyrics_action(int req_id,
                                      int chunk_index,
                                      int chunk_count,
                                      std::string_view track_key,
                                      bool synced,
                                      std::string_view lines_blob,
                                      MusicLyricsState &st = runtime_state().music.lyrics) {
  if (req_id > 0 && st.latest_req_id > 0 && req_id < st.latest_req_id) {
    return false;
  }
  const int eff_chunk_count = (chunk_count <= 0) ? 1 : chunk_count;
  const int eff_chunk_index = (chunk_count <= 0) ? 0 : chunk_index;
  if (eff_chunk_index < 0 || eff_chunk_index >= eff_chunk_count || eff_chunk_count > 16) {
    return false;
  }

  if (eff_chunk_index == 0) {
    st.staging_req_id = req_id;
    st.staging_expected_chunks = eff_chunk_count;
    st.staging_next_chunk = 0;
    st.staging_track_key = std::string(track_key);
    st.staging_synced = synced;
    st.staging_lines.clear();
    st.staging_lines.reserve(std::min<size_t>(kMaxMusicLyricLines, 64U));
  } else if (req_id != st.staging_req_id ||
             eff_chunk_index != st.staging_next_chunk ||
             eff_chunk_count != st.staging_expected_chunks) {
    st.staging_expected_chunks = 0;
    st.staging_next_chunk = 0;
    st.staging_lines.clear();
    return false;
  }

  append_music_lyrics_records_blob(lines_blob, st.staging_lines, kMaxMusicLyricLines);
  st.staging_next_chunk = eff_chunk_index + 1;
  if (st.staging_next_chunk < st.staging_expected_chunks) {
    return false;
  }

  st.track_key = std::move(st.staging_track_key);
  st.synced = st.staging_synced;
  st.lines = std::move(st.staging_lines);
  if (st.synced && st.lines.size() > 1U) {
    std::stable_sort(st.lines.begin(), st.lines.end(),
                     [](const MusicLyricLine &a, const MusicLyricLine &b) {
                       return a.t_ms < b.t_ms;
                     });
  }
  st.loading = false;
  st.active_line_idx = -1;
  st.scroll_px = 0;
  st.target_scroll_px = 0;
  st.from_scroll_px = 0;
  st.scroll_anim_start_ms = 0;
  st.manual_override_until_ms = 0;
  st.staging_expected_chunks = 0;
  st.staging_next_chunk = 0;
  st.staging_lines.clear();
  st.rev++;
  return true;
}

template <typename StrT, typename = decltype(std::declval<const StrT &>().c_str())>
inline size_t parse_v3_slots_payload(const StrT &slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  return parse_v3_slots_payload(std::string_view(slots_blob.c_str(), slots_blob.size()), out_slots, max_slots);
}

// Shared slot-response applier for both set_browse_slots and set_area_slots.
inline bool apply_slots_action(int req_id, std::string_view mode,
                               int page, int total_pages, std::string_view slots_blob,
                               uint32_t now_ms = 0, bool force_area_entities = false,
                               DeviceRuntimeState &rt = runtime_state()) {
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
                                 AlertBannerState &ab = runtime_state().alert,
                                 std::string_view camera_entity = "") {
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
  ab.priority_level = parse_alert_priority(ab.priority);
  int dur = (duration_s > 0) ? duration_s : 10;
  if (dur < 3) dur = 3;
  if (dur > 60) dur = 60;
  ab.duration_ms = static_cast<uint32_t>(dur) * 1000U;
  ab.start_ms = now_ms;
  ab.camera_url = std::string(camera_url);
  ab.camera_entity = std::string(camera_entity);
  if (ab.camera_entity.empty() && !ab.camera_url.empty()) {
    const size_t epos = ab.camera_url.find("entity_id=");
    if (epos != std::string::npos) {
      const size_t start = epos + 10U;
      const size_t amp = ab.camera_url.find('&', start);
      ab.camera_entity = (amp == std::string::npos)
                             ? ab.camera_url.substr(start)
                             : ab.camera_url.substr(start, amp - start);
    }
  }
  ab.active = true;
  return true;
}

// ==============================================================================
// Canonical Music Playback Override & Volume Feedback Helpers
// ==============================================================================

inline int &remote_vol_pct() { return runtime_state().music.remote_vol_pct; }
inline uint32_t &last_local_vol_tap_ms() { return runtime_state().music.last_local_vol_tap_ms; }
inline uint32_t &remote_play_override_ms() { return runtime_state().music.remote_play_override_ms; }
inline bool &remote_play_override_state() { return runtime_state().music.remote_play_override_state; }

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

inline bool &music_queue_cleared() noexcept { return runtime_state().music.queue_cleared; }

inline bool is_music_paused_effective(bool is_effectively_playing,
                                      bool is_local_speaker,
                                      std::string_view ms_state,
                                      bool local_mp_paused = false) noexcept {
  return !is_effectively_playing && !music_queue_cleared() &&
         (local_mp_paused || ms_state == "paused" ||
          (is_local_speaker && (ms_state == "playing" || ms_state == "buffering")));
}

inline void sync_audio_session_media_observation(bool local_mp_playing,
                                                 bool local_mp_paused,
                                                 uint32_t now_ms,
                                                 const MediaState &ms = media_state(),
                                                 const AreaContextState &ac = area_ctx_state()) {
  const bool is_remote = !ms.is_local;
  const bool eff_playing = is_music_playing_effective(is_remote, local_mp_playing, ms.state, now_ms);
  const bool remote_paused_override =
      is_remote && remote_play_override_ms() > 0U &&
      (now_ms - remote_play_override_ms() < 2500U) && !remote_play_override_state();
  const bool eff_paused = is_music_paused_effective(
      eff_playing, ms.is_local, ms.state,
      local_mp_paused || (remote_paused_override && (ms.state == "playing" || ms.state == "buffering")));
  if (!eff_playing && ms.state == "paused") {
    audio_session().note_local_playback_stopped();
  }
  const std::string_view spk = !ms.active_speaker.empty()
                                   ? std::string_view(ms.active_speaker)
                                   : std::string_view(runtime_state().music.active_speaker_entity);
  audio_session().note_media_state_observation(eff_playing, eff_paused, spk, ac.area_id, now_ms);
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

inline int collect_home_hub_tabs(int *out_tabs, const AreaContextState &ac = area_ctx_state(),
                                 int max_tabs = kMaxHomeHubTabs) {
  if (out_tabs == nullptr || max_tabs <= 0 || ac.epoch == 0U) return 0;
  return parse_home_categories_csv(ac.home_categories, out_tabs, max_tabs);
}

inline bool is_cameras_start_screen(const PersistedDevicePrefs &p = persisted_prefs()) noexcept {
  return std::string_view(pref_start_screen_cstr(p)) == "Cameras";
}

inline int resolve_anchor_dock_app(const PersistedDevicePrefs &p = persisted_prefs()) {
  const std::string_view opt(pref_start_screen_cstr(p));
  if (opt == "Home") return p.home_screen_enabled ? PageId::HOME : PageId::TIME;
  if (opt == "Pinned") return PageId::PINNED;
  if (opt == "Time App") return PageId::TIME;
  if (opt == "Calendar") return PageId::CALENDAR;
  if (opt == "Clock Face") return PageId::CLOCK;
  if (opt == "Media") return PageId::MUSIC;
  if (opt == "Home Hub") return PageId::HOME_HUB;
  if (opt == "Cameras") return PageId::CAMERAS;
  if (opt == "Area Controls") return PageId::AREA;
  if (opt == "Remote") return PageId::REMOTE;
  if (opt == "Weather") return PageId::WEATHER;
  if (opt == "Games") return PageId::GAMES;
  if (opt == "Tools") return PageId::TOOLS;
  return p.home_screen_enabled ? PageId::HOME : PageId::TIME;
}

inline int resolve_start_screen_page(const PersistedDevicePrefs &p = persisted_prefs()) {
  return resolve_anchor_dock_app(p);
}

inline int resolve_start_screen_page(int app) {
  return app;
}

inline constexpr bool is_alarm_entity_target(std::string_view domain_or_entity) noexcept {
  return domain_or_entity == "home_hub.alarm" ||
         domain_or_entity == "alarm_control_panel" ||
         domain_or_entity.rfind("alarm_control_panel.", 0) == 0;
}

inline constexpr bool is_alarm_disarm_action(std::string_view action) noexcept {
  return action == "disarm" ||
         action == "alarm_disarm" ||
         action == "alarm_control_panel.alarm_disarm";
}

inline constexpr bool is_alarm_arm_action(std::string_view action) noexcept {
  return action == "arm_home" ||
         action == "arm_away" ||
         action == "arm_night" ||
         action == "arm_vacation" ||
         action.rfind("alarm_arm_", 0) == 0 ||
         action.rfind("alarm_control_panel.alarm_arm_", 0) == 0;
}

inline bool is_sensitive_perimeter_action_impl(const std::string &domain_or_entity,
                                               const std::string &action, int level = 100) {
  if (is_alarm_disarm_action(action)) {
    return true;
  }
  if (is_alarm_entity_target(domain_or_entity) && is_alarm_arm_action(action)) {
    return (effective_alarm_caps() & kAlarmCapCodeArm) != 0U;
  }
  if (action == "unlock" || action == "open_cover") {
    return true;
  }
  if (domain_or_entity == "lock" && action == "open") {
    return true;
  }
  if (domain_or_entity == "home_hub.garage" && action == "turn_on") {
    return true;
  }
  if (domain_or_entity.rfind("cover.", 0) == 0 &&
      (domain_or_entity.find("garage") != std::string::npos ||
       domain_or_entity.find("gate") != std::string::npos) &&
      (action == "turn_on" || action == "open" || action == "toggle" ||
       (action == "set_level" && level > 0))) {
    return true;
  }
  return false;
}

inline bool evaluate_perimeter_action_blocked(std::string_view policy,
                                              const std::string &domain_or_entity,
                                              const std::string &action,
                                              int level = 100) {
  if (policy != "block") return false;
  if (is_alarm_arm_action(action)) return false;
  return is_sensitive_perimeter_action_impl(domain_or_entity, action, level);
}

inline bool evaluate_perimeter_pin_prompt(std::string_view policy,
                                          const std::string &domain_or_entity,
                                          const std::string &action,
                                          int level,
                                          uint16_t alarm_caps,
                                          bool is_stationary) {
  if (is_alarm_entity_target(domain_or_entity) ||
      is_alarm_disarm_action(action) ||
      is_alarm_arm_action(action)) {
    if (is_alarm_disarm_action(action)) {
      if (policy == "block") return false;
      return ((alarm_caps & kAlarmCapCodeDisarm) != 0U) || (policy == "require_pin");
    }
    if (is_alarm_arm_action(action)) {
      return (alarm_caps & kAlarmCapCodeArm) != 0U;
    }
    return false;
  }
  if (!is_sensitive_perimeter_action_impl(domain_or_entity, action, level)) return false;
  if (policy == "allow" || policy == "block") return false;
  if (policy == "require_pin") return true;
  return is_stationary;
}

inline bool should_return_to_home_on_idle(const DeviceRuntimeState &rt,
                                          const PersistedDevicePrefs &p,
                                          bool media_playing,
                                          bool cam_session_active = false,
                                          bool is_stationary = (LATTER_IS_STATIONARY != 0)) {
  if (!p.home_screen_enabled) return false;
  if (return_to_home_timeout_ms(p) == 0U) return false;
  if (rt.nav.gui_page == 1 && rt.nav.home_subpage == 0) return false;
  if (rt.nav.is_hibernating || rt.nav.ota_in_progress) return false;
  if (rt.alarms.ringing || rt.timers.ringing || rt.alarms.find_my_active || rt.alert.active) {
    return false;
  }
  if (rt.voice.active || rt.voice.tts_received || !rt.voice.status_text.empty() ||
      cam_session_active || rt.nav.shortcut_confirm.open || rt.nav.quick_actions_open) {
    return false;
  }
  if (rt.nav.gui_page == 3 && rt.nav.active_game > 0) return false;
  if (rt.nav.gui_page == PageId::MUSIC &&
      is_music_ambient_tab(rt.music.view) &&
      rt.music.drilldown == MusicDrilldown::NONE &&
      rt.music.takeover == MusicTakeover::NONE &&
      rt.music.overlay_mode == 0 && media_playing) {
    return false;
  }
  if (rt.nav.gui_page == PageId::CAMERAS) {
    if (is_stationary &&
        (rt.cameras.tab == CamerasTab::CAROUSEL || rt.cameras.tab == CamerasTab::GRID)) {
      return false;
    }
  }
  if (rt.nav.gui_page == 10) {
    if (rt.nav.time_subpage == 1 && rt.timers.running) return false;
    if (rt.nav.time_subpage == 3 && rt.stopwatch.running) return false;
  }
  if (rt.nav.gui_page == 8) {
    if (rt.nav.tools_subpage == 3 && rt.stopwatch.running) return false;
    if (rt.nav.tools_subpage == 5 || rt.nav.tools_subpage == 7) {
      return false;
    }
  }
  if (rt.nav.gui_page == PageId::CALENDAR) {
    if (rt.cal.drilldown >= CalDrilldown::ADD_STEP1 || rt.cal.dictation_field != 0U ||
        rt.cal.delete_confirm_open) {
      return false;
    }
  }
  return true;
}

// ==============================================================================
// Hibernate Snapshot POD & Cold-Boot Hydration (NVS-backed Frame #1 Seed)
// ==============================================================================
static constexpr uint32_t kHibernateSnapshotMagic   = 0x4C485350U;  // "LHSP"
static constexpr uint16_t kHibernateSnapshotVersion = 3U;
static constexpr uint32_t kHibernateSnapshotHash    = 0x4C484942U;  // "LHIB"
static constexpr size_t kHibernateDockSlots         = 10U;
static constexpr size_t kHibernatePinnedPage1Slots  = 6U;

struct DockSnapshotSlot {
  uint8_t kind = 0;         // 0 = empty, 1 = ROUTE, 2 = ENTITY
  uint8_t confirm = 0;      // 0 = direct, 1 = confirm
  uint8_t route_id = 0;     // RouteId when kind == 1
  int8_t state = -1;        // -1 = stateless/route, 0 = off, 1 = on, 2 = unavailable
  char icon_slug[24]{};     // MDI slug without "mdi:" prefix (or empty for default route glyph)
  char label[22]{};         // Display label
  char target[38]{};        // Entity ID when kind == 2 (route token resolved via route_id)
  char action[20]{};        // Explicit HA service action when kind == 2
};

struct HibernateSnapshot {
  uint32_t magic = kHibernateSnapshotMagic;
  uint16_t version = kHibernateSnapshotVersion;
  bool sun_below_horizon = false;
  bool dock_configured = false;
  uint16_t dock_count = 0;
  uint16_t pinned_total = 0;
  DockSnapshotSlot dock_slots[kHibernateDockSlots]{};
  DockSnapshotSlot pinned_page1[kHibernatePinnedPage1Slots]{};
  char area_name[48]{};
  char area_icon[32]{};
  char weather_condition[24]{};
  float weather_temp = NAN;
  char posix_tz[48]{};
};

static_assert(std::is_trivially_copyable_v<HibernateSnapshot>,
              "HibernateSnapshot must remain a POD blob for ESPPreferenceObject.");

inline void pack_snapshot_shortcut_slot(const ShortcutItem &src, DockSnapshotSlot &dst) {
  dst = DockSnapshotSlot{};
  dst.kind = (src.kind == ShortcutKind::ENTITY) ? 2U : 1U;
  dst.confirm = src.confirm ? 1U : 0U;
  dst.route_id = static_cast<uint8_t>(src.route_id);
  dst.state = src.state;
  std::string_view slug(src.icon);
  if (slug.rfind("mdi:", 0) == 0) slug.remove_prefix(4);
  copy_bounded_cstr(dst.icon_slug, slug);
  copy_bounded_cstr(dst.label, src.label);
  if (src.kind == ShortcutKind::ENTITY) {
    copy_bounded_cstr(dst.target, src.target);
    copy_bounded_cstr(dst.action, src.action);
  }
}

inline bool unpack_snapshot_shortcut_slot(const DockSnapshotSlot &src, ShortcutItem &dst) {
  if (src.kind != 1U && src.kind != 2U) return false;
  dst = ShortcutItem{};
  const char *icon_end = static_cast<const char *>(std::memchr(src.icon_slug, '\0', sizeof(src.icon_slug)));
  const size_t icon_len = (icon_end != nullptr) ? static_cast<size_t>(icon_end - src.icon_slug) : (sizeof(src.icon_slug) - 1U);
  const std::string_view slug_sv(src.icon_slug, icon_len);

  const char *lbl_end = static_cast<const char *>(std::memchr(src.label, '\0', sizeof(src.label)));
  const size_t lbl_len = (lbl_end != nullptr) ? static_cast<size_t>(lbl_end - src.label) : (sizeof(src.label) - 1U);
  const std::string_view lbl_sv(src.label, lbl_len);

  if (src.kind == 1U) {
    const auto rid = static_cast<RouteId>(src.route_id);
    if (rid == RouteId::SETTINGS_ROOT) {
      dst.kind = ShortcutKind::ROUTE;
      dst.route_id = RouteId::SETTINGS_ROOT;
      dst.target = "app:settings";
      dst.label = lbl_sv.empty() ? "Settings" : std::string(lbl_sv);
    } else {
      const auto *spec = shortcut_route_spec(rid);
      if (spec == nullptr) return false;
      dst.kind = ShortcutKind::ROUTE;
      dst.route_id = rid;
      dst.target = spec->token;
      dst.label = lbl_sv.empty() ? std::string(spec->label) : std::string(lbl_sv);
    }
    dst.icon = slug_sv.empty() ? "" : ("mdi:" + std::string(slug_sv));
    dst.confirm = false;
    dst.state = -1;
    return true;
  }

  const char *tgt_end = static_cast<const char *>(std::memchr(src.target, '\0', sizeof(src.target)));
  const size_t tgt_len = (tgt_end != nullptr) ? static_cast<size_t>(tgt_end - src.target) : (sizeof(src.target) - 1U);
  const std::string_view tgt_sv(src.target, tgt_len);

  const char *act_end = static_cast<const char *>(std::memchr(src.action, '\0', sizeof(src.action)));
  const size_t act_len = (act_end != nullptr) ? static_cast<size_t>(act_end - src.action) : (sizeof(src.action) - 1U);
  const std::string_view act_sv(src.action, act_len);

  if (tgt_sv.empty() || act_sv.empty()) return false;
  dst.kind = ShortcutKind::ENTITY;
  dst.route_id = RouteId::NONE;
  dst.target = std::string(tgt_sv);
  dst.action = std::string(act_sv);
  dst.label = lbl_sv.empty() ? std::string(tgt_sv) : std::string(lbl_sv);
  dst.icon = slug_sv.empty() ? "" : ("mdi:" + std::string(slug_sv));
  dst.confirm = (src.confirm != 0U);
  dst.state = static_cast<int8_t>(std::clamp<int>(src.state, -1, 2));
  return true;
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
  auto snap_ptr = std::make_unique<HibernateSnapshot>();
  HibernateSnapshot &snap = *snap_ptr;
  snap.magic = kHibernateSnapshotMagic;
  snap.version = kHibernateSnapshotVersion;
  const auto &ac = area_ctx_state();
  const auto &ws = weather_state();
  const auto &sc = shortcuts_state();

  snap.dock_configured = sc.dock_configured;
  const size_t dcnt = std::min<size_t>(sc.dock_items.size(), kHibernateDockSlots);
  snap.dock_count = static_cast<uint16_t>(dcnt);
  for (size_t i = 0; i < dcnt; ++i) {
    pack_snapshot_shortcut_slot(sc.dock_items[i], snap.dock_slots[i]);
  }
  snap.pinned_total = sc.live_received
                          ? static_cast<uint16_t>(std::min<size_t>(sc.pinned_items.size(), ShortcutsState::kMaxPinnedItems))
                          : sc.cold_pinned_total;
  const size_t pcnt = std::min<size_t>(sc.pinned_items.size(), kHibernatePinnedPage1Slots);
  for (size_t i = 0; i < pcnt; ++i) {
    pack_snapshot_shortcut_slot(sc.pinned_items[i], snap.pinned_page1[i]);
  }

  copy_bounded_cstr(snap.area_name, ac.area_name);
  copy_bounded_cstr(snap.area_icon, ac.area_icon);
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
  auto snap_ptr = std::make_unique<HibernateSnapshot>();
  HibernateSnapshot &snap = *snap_ptr;
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
  snap.area_name[sizeof(snap.area_name) - 1] = '\0';
  snap.area_icon[sizeof(snap.area_icon) - 1] = '\0';
  snap.weather_condition[sizeof(snap.weather_condition) - 1] = '\0';
  snap.posix_tz[sizeof(snap.posix_tz) - 1] = '\0';

  if (snap.posix_tz[0] != '\0') {
    hydrated_posix_tz() = snap.posix_tz;
#if defined(ESP_PLATFORM) && !defined(LATTER_HOST_TEST)
    setenv("TZ", snap.posix_tz, 1);
    tzset();
#endif
  }

  auto &sc = shortcuts_state();
  if (!sc.live_received && sc.epoch == 0U) {
    sc.cold_pinned_total = snap.pinned_total;
    if (snap.dock_configured && sc.dock_items.empty()) {
      sc.dock_configured = true;
      const size_t dcnt = std::min<size_t>(snap.dock_count, kHibernateDockSlots);
      for (size_t i = 0; i < dcnt; ++i) {
        ShortcutItem item{};
        if (unpack_snapshot_shortcut_slot(snap.dock_slots[i], item)) {
          sc.dock_items.push_back(std::move(item));
        }
      }
    }
    if (sc.pinned_items.empty() && snap.pinned_total > 0U) {
      const size_t pcnt = std::min<size_t>(snap.pinned_total, kHibernatePinnedPage1Slots);
      for (size_t i = 0; i < pcnt; ++i) {
        ShortcutItem item{};
        if (unpack_snapshot_shortcut_slot(snap.pinned_page1[i], item)) {
          sc.pinned_items.push_back(std::move(item));
        }
      }
    }
  }

  auto &ac = area_ctx_state();
  if (ac.area_name.empty() && snap.area_name[0] != '\0') {
    ac.area_name = snap.area_name;
    if (runtime_state().area.active_area_name.empty() ||
        runtime_state().area.active_area_name == "Area Control") {
      runtime_state().area.active_area_name = snap.area_name;
    }
  }
  if (ac.area_icon.empty() && snap.area_icon[0] != '\0') {
    ac.area_icon = snap.area_icon;
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
inline HelloSyncState &hello_sync_state() { return runtime_state().hello_sync; }

inline void reset_hello_sync_on_connect() {
  auto &st = hello_sync_state();
  st.in_flight = false;
  st.started_ms = 0;
  st.last_sent_ms = 0;
  st.last_sent_dirty_mask.clear();
}

inline void mark_hello_in_flight(bool active, uint32_t now_ms = 0) {
  auto &st = hello_sync_state();
  st.in_flight = active;
  st.started_ms = active ? ((now_ms == 0U) ? 1U : now_ms) : 0U;
}

inline void mark_hello_sent(uint32_t now_ms, PrefMask dirty_mask) {
  auto &st = hello_sync_state();
  st.in_flight = false;
  st.started_ms = 0;
  st.last_sent_ms = (now_ms == 0U) ? 1U : now_ms;
  st.last_sent_dirty_mask = dirty_mask;
}

inline bool should_trigger_hello_on_ping(uint32_t now_ms, PrefMask dirty_mask) {
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

inline bool &pwr_key_wake_consumed() { return runtime_state().nav.pwr_key_wake_consumed; }

inline bool &ui_anim_poller_running() { return runtime_state().nav.ui_anim_poller_running; }

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

// ==============================================================================
// Consolidated UI / Domain State Accessors & Pure Helpers
// ==============================================================================
inline uint32_t &menu_scroll_start_ms() { return runtime_state().nav.menu_scroll_start_ms; }
inline int &pinned_menu_page() { return runtime_state().nav.pinned_menu_page; }
inline int &timer_view_slot() { return runtime_state().timers.view_slot; }
inline bool &cam_quick_reply_open() { return runtime_state().cameras.quick_reply_open; }
inline int &home_dock_page() { return runtime_state().nav.home_dock_page; }
inline uint32_t &last_music_page_req_ms() { return runtime_state().last_music_page_req_ms; }
inline uint32_t &music_scroll_tap_lockout_until_ms() { return runtime_state().music.scroll_tap_lockout_until_ms; }

inline constexpr int MAX_REMOTE_SLOTS = RemoteRuntimeState::kMaxSlots;

inline std::string *remote_slots_ent() { return runtime_state().remote.slot_ents; }
inline std::string *remote_slots_name() { return runtime_state().remote.slot_names; }
inline bool *remote_slot_states_on() { return runtime_state().remote.slot_states_on; }
inline int &remote_control_page() { return runtime_state().remote.control_page; }

inline bool is_configured_remote_slot(const std::string &ent) {
  return !ent.empty();
}

inline bool has_any_configured_remote() {
  const std::string *t_ents = remote_slots_ent();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return true;
  }
  return false;
}

inline std::string format_remote_entity_fallback(const std::string &ent) {
  if (!is_configured_remote_slot(ent)) return "";
  std::string s = ent;
  const size_t dot = s.find('.');
  if (dot != std::string::npos) s = s.substr(dot + 1);
  bool cap = true;
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '_') {
      s[i] = ' ';
      cap = true;
    } else if (cap) {
      s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
      cap = false;
    }
  }
  return s;
}

inline void parse_latter_remote_slots_summary(const std::string &summary) {
  std::string *t_ents = remote_slots_ent();
  std::string *t_names = remote_slots_name();
  bool *st_on = remote_slot_states_on();
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    t_ents[i].clear();
    t_names[i].clear();
    st_on[i] = false;
  }
  if (!summary.empty()) {
    std::string_view recs[MAX_REMOTE_SLOTS];
    const size_t n = v3_split_fields(summary, recs, MAX_REMOTE_SLOTS, kProtocolRecordSep);
    for (size_t r = 0; r < n; r++) {
      std::string_view f[6];
      if (v3_split_fields(recs[r], f, 6, '|') != 6 || f[2].empty()) continue;
      const int slot_num = v3_parse_int(f[0], 0);
      if (slot_num < 1 || slot_num > MAX_REMOTE_SLOTS) continue;
      const int idx = slot_num - 1;
      const std::string_view st = f[5];
      st_on[idx] = (st == "on" || st == "playing" || st == "paused" || st == "buffering" || st == "idle");
      t_ents[idx] = std::string(f[2]);
      t_names[idx] = f[1].empty() ? format_remote_entity_fallback(t_ents[idx]) : std::string(f[1]);
    }
  }
  remote_control_page() = 0;
}

inline int collect_configured_remote_slots(int out_slots[MAX_REMOTE_SLOTS]) {
  const std::string *t_ents = remote_slots_ent();
  int count = 0;
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) {
      out_slots[count++] = i;
    }
  }
  return count;
}

inline int resolve_active_remote_slot(int active_slot) {
  const std::string *t_ents = remote_slots_ent();
  if (active_slot >= 0 && active_slot < MAX_REMOTE_SLOTS && is_configured_remote_slot(t_ents[active_slot])) {
    return active_slot;
  }
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return i;
  }
  return 0;
}

inline std::string &area_available_domains_csv() { return runtime_state().area.available_domains_csv; }
inline int &area_cat_dock_page() { return runtime_state().area.cat_dock_page; }
inline std::string &light_insp_entity_id() { return runtime_state().area.light_insp_entity_id; }
inline std::string &light_insp_name() { return runtime_state().area.light_insp_name; }
inline std::string &light_insp_domain() { return runtime_state().area.light_insp_domain; }
inline int &light_insp_mode() { return runtime_state().area.light_insp_mode; }
inline int &light_insp_slot() { return runtime_state().area.light_insp_slot; }
inline bool &light_insp_active() { return runtime_state().area.light_insp_active; }
inline float &light_insp_hue_deg() { return runtime_state().area.light_insp_hue_deg; }
inline float &light_insp_sat() { return runtime_state().area.light_insp_sat; }
inline int &light_insp_kelvin() { return runtime_state().area.light_insp_kelvin; }
inline int &light_insp_bri_pct() { return runtime_state().area.light_insp_bri_pct; }
inline std::string &area_color_hex_param() { return runtime_state().area.color_hex_param; }

inline int &home_hub_tab() { return home_hub_state().tab; }
inline int &home_hub_mode_bar_page() { return home_hub_state().mode_bar_page; }
inline int &home_hub_thermostat_idx() { return home_hub_state().thermostat_idx; }
inline int &home_hub_entry_page() { return home_hub_state().entry_page; }
inline int &home_hub_broadcast_page() { return home_hub_state().broadcast_page; }
inline int &home_hub_vacuum_idx() { return home_hub_state().vacuum_idx; }

inline int collect_area_categories(std::string *out_cats) {
  const std::string_view src =
      (area_ctx_state().epoch == 0U) ? std::string_view(kDefaultAreaCategories)
                                     : std::string_view(area_available_domains_csv());
  return parse_area_categories_csv(src, out_cats, 8);
}

inline std::string resolve_preferred_area_category() {
  std::string cats[8];
  const int cnt = collect_area_categories(cats);
  return (cnt > 0) ? cats[0] : std::string{};
}

inline std::string &latter_site_id() { return runtime_state().area.site_id; }
inline std::string &latter_ble_proxy() { return runtime_state().area.ble_proxy; }
inline std::string &latter_area_label() { return runtime_state().area.area_label; }

inline constexpr int MAX_CAMERA_SLOTS = 10;

struct CameraRecord {
  std::string entity_id = "";
  std::string name = "";
  std::string area_id = "";
  uint8_t caps = 0;
  std::string unlock_entity = "";

  constexpr bool has_audio() const noexcept { return (caps & kCamCapAudio) != 0U; }
  constexpr bool has_talkback() const noexcept { return (caps & kCamCapTalkback) != 0U; }
  constexpr bool is_doorbell() const noexcept { return (caps & kCamCapDoorbell) != 0U; }
  constexpr bool in_carousel() const noexcept { return (caps & kCamCapCarousel) != 0U; }
  constexpr bool show_reply() const noexcept { return (caps & kCamCapReply) != 0U; }
  bool has_unlock() const noexcept { return ((caps & kCamCapUnlock) != 0U) && !unlock_entity.empty(); }
  bool unlock_is_lock() const noexcept { return unlock_entity.rfind("lock.", 0) == 0; }
};

inline int parse_cameras_records(std::string_view raw, CameraRecord *out, int max_slots = MAX_CAMERA_SLOTS) {
  if (raw.empty() || out == nullptr || max_slots <= 0) return 0;
  std::string_view recs[MAX_CAMERA_SLOTS];
  const int cap = (max_slots < MAX_CAMERA_SLOTS) ? max_slots : MAX_CAMERA_SLOTS;
  const size_t n = v3_split_fields(raw, recs, static_cast<size_t>(cap), kProtocolRecordSep);
  int idx = 0;
  for (size_t r = 0; r < n && idx < cap; ++r) {
    std::string_view f[5];
    const size_t fc = v3_split_fields(recs[r], f, 5, '|');
    if (fc < 2 || f[0].empty()) continue;
    out[idx].entity_id = std::string(f[0]);
    out[idx].name = f[1].empty() ? std::string(f[0]) : std::string(f[1]);
    out[idx].area_id = (fc >= 3) ? std::string(f[2]) : "";
    uint8_t parsed_caps = 0;
    if (fc >= 4 && !f[3].empty()) {
      const std::string_view cv = f[3];
      if (cv.rfind("0x", 0) == 0 || cv.rfind("0X", 0) == 0) {
        uint32_t acc = 0;
        for (size_t i = 2; i < cv.size(); ++i) {
          const char ch = cv[i];
          if (ch >= '0' && ch <= '9') acc = (acc << 4) | static_cast<uint32_t>(ch - '0');
          else if (ch >= 'a' && ch <= 'f') acc = (acc << 4) | static_cast<uint32_t>(10 + ch - 'a');
          else if (ch >= 'A' && ch <= 'F') acc = (acc << 4) | static_cast<uint32_t>(10 + ch - 'A');
          else break;
        }
        parsed_caps = static_cast<uint8_t>(acc & 0xFFU);
      } else if (cv[0] >= '0' && cv[0] <= '9') {
        parsed_caps = static_cast<uint8_t>(v3_parse_u32(cv, 0U) & 0xFFU);
      }
    }
    out[idx].caps = parsed_caps;
    out[idx].unlock_entity = (fc >= 5) ? std::string(f[4]) : "";
    ++idx;
  }
  return idx;
}

inline CameraRecord *camera_records_scratch() {
  static std::vector<CameraRecord> s_buf(static_cast<size_t>(MAX_CAMERA_SLOTS));
  return s_buf.data();
}

inline int count_configured_cameras(const AreaContextState &ac = area_ctx_state()) noexcept {
  if (!ac.cameras_summary.empty()) {
    std::string_view recs[MAX_CAMERA_SLOTS];
    const size_t n = v3_split_fields(ac.cameras_summary, recs, static_cast<size_t>(MAX_CAMERA_SLOTS), kProtocolRecordSep);
    int valid = 0;
    for (size_t r = 0; r < n; ++r) {
      std::string_view f[2];
      const size_t fc = v3_split_fields(recs[r], f, 2, '|');
      if (fc >= 2 && !f[0].empty()) ++valid;
    }
    if (valid > 0) return valid;
  }
  return (!ac.camera_stream_url.empty() || !ac.camera_snapshot_url.empty()) ? 1 : 0;
}

inline constexpr int kMaxEffectiveDockItems = 16;

inline ShortcutItem *dock_items_scratch() {
  static std::vector<ShortcutItem> s_buf(static_cast<size_t>(kMaxEffectiveDockItems));
  return s_buf.data();
}

inline int collect_effective_dock_items(ShortcutItem *out_items,
                                        int max_items = kMaxEffectiveDockItems,
                                        const ShortcutsState &st = shortcuts_state()) {
  if (out_items == nullptr || max_items <= 0) return 0;
  const bool has_pinned = !st.pinned_items.empty() || (!st.live_received && st.cold_pinned_total > 0U);
  const bool has_remote = has_any_configured_remote();
  const bool has_cameras = count_configured_cameras() > 0;
  int count = 0;

  auto push_item = [&](const ShortcutItem &src) {
    if (count >= max_items - 1) return;  // Reserve final slot for Settings
    if (src.kind == ShortcutKind::ROUTE) {
      if (src.route_id == RouteId::SETTINGS_ROOT) return;
      if (!is_route_supported_on_board(src.route_id)) return;
      if (src.route_id == RouteId::APP_PINNED && !has_pinned) return;
      if ((src.route_id == RouteId::APP_REMOTE || src.route_id == RouteId::REMOTE_DEVICES) && !has_remote) {
        return;
      }
      if ((src.route_id == RouteId::APP_CAMERAS || src.route_id == RouteId::CAMERAS_GRID ||
           src.route_id == RouteId::CAMERAS_SINGLE || src.route_id == RouteId::CAMERAS_CAROUSEL ||
           src.route_id == RouteId::CAMERAS_SETTINGS) && !has_cameras) {
        return;
      }
      for (int i = 0; i < count; ++i) {
        if (out_items[i].kind == ShortcutKind::ROUTE && out_items[i].route_id == src.route_id) {
          return;
        }
      }
    } else {
      if (src.target.empty() || src.action.empty()) return;
    }
    out_items[count++] = src;
  };

  if (st.dock_configured) {
    for (const auto &it : st.dock_items) {
      push_item(it);
    }
  } else {
    std::string_view csv(kDefaultDockRoutes);
    size_t start = 0;
    while (start <= csv.size()) {
      size_t pos = csv.find(',', start);
      std::string_view tok =
          (pos == std::string_view::npos) ? csv.substr(start) : csv.substr(start, pos - start);
      tok = trim_ascii_ws(tok);
      if (const auto *spec = find_shortcut_route(tok)) {
        ShortcutItem item{};
        item.kind = ShortcutKind::ROUTE;
        item.route_id = spec->id;
        item.target = spec->token;
        item.label = spec->label;
        push_item(item);
      }
      if (pos == std::string_view::npos) break;
      start = pos + 1;
    }
  }

  if (count < max_items) {
    ShortcutItem settings_item{};
    settings_item.kind = ShortcutKind::ROUTE;
    settings_item.route_id = RouteId::SETTINGS_ROOT;
    settings_item.target = "app:settings";
    settings_item.label = "Settings";
    out_items[count++] = std::move(settings_item);
  }
  return count;
}

inline int pinned_total_pages(const ShortcutsState &st = shortcuts_state()) noexcept {
  const size_t n = st.pinned_items.size();
  return (n == 0U) ? 1 : static_cast<int>((n + 5U) / 6U);
}

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

// ==============================================================================
// Compiled Protocol v3 Domain Reducer & Timer/Alarm Controller Declarations
// (Implemented in components/latter_core/runtime_controllers.cpp)
// ==============================================================================
struct V3ReduceContext {
  uint32_t now_ms{0U};
  const char *friendly_name{""};
  bool local_mp_playing{false};
  bool local_mp_paused{false};
  int local_album_art_w{0};
  bool remote_art_fetch_running{false};
  uint32_t last_touch_ms{0U};
};

struct V3DomainEffects {
  bool area_changed{false};
  bool mark_dirty_full{false};
  bool fetch_area_browse{false};
  bool fetch_on_demand_camera{false};
  bool apply_theme_preset{false};
  bool alert_triggered{false};
  int alert_sfx_id{0};
  bool fetch_alert_camera{false};
  bool release_alert_camera_art{false};
  bool stop_wallpaper_fetch{false};
  bool release_wallpaper_art{false};
  bool fetch_wallpaper{false};
  bool stop_screensaver_photo_fetch{false};
  bool release_screensaver_photo_art{false};
  bool fetch_screensaver_photo{false};
  bool weather_sim_dirty{false};
  bool reset_ambient_frame_ms{false};
  bool ensure_anim_poller{false};
  bool schedule_disp_update{false};
  bool throttled_disp_update{false};
  bool sync_local_volume{false};
  float local_volume_0_to_1{0.0f};
  bool stop_remote_art_fetch{false};
  bool release_remote_art{false};
  bool fetch_remote_art{false};
  bool fetch_music_lyrics{false};
  bool navigate_home_from_remote{false};
  bool navigate_start_screen_remote{false};
  bool sync_display_brightness{false};
  bool apply_device_prefs_effects{false};
};

class V3DomainReducer {
 public:
  void reset();
  V3DomainEffects reduce(std::string_view domain, const V3ReduceContext &ctx);

 private:
  std::string prev_area_icon_{};
  std::string prev_title_{};
  std::string prev_artist_{};
  std::string prev_album_{};
  std::string prev_state_{};
  std::string prev_spk_{};
  std::string prev_spk_name_{};
  std::string prev_art_{};
  bool prev_is_local_{false};
  int prev_vol_pct_{-1};
  bool prev_shuffle_{false};
  uint8_t prev_repeat_mode_{0};
  bool prev_muted_{false};
  bool prev_favorite_{false};
  int prev_queue_count_{0};
  uint16_t prev_media_caps_{0};
  std::string prev_sun_{};
  std::string prev_wallpaper_url_{};
};

V3DomainReducer &v3_domain_reducer() noexcept;

struct TimerTickEffects {
  bool any_expired{false};
  bool sec_changed{false};
  bool auto_silenced{false};
};

struct AlarmTickEffects {
  bool snooze_fired{false};
  int triggered_idx{0};
  bool prefs_saved{false};
  bool auto_silenced{false};
};

struct VaTimerEffects {
  bool fire_alert{false};
  bool stop_sound{false};
  bool schedule_disp{false};
};

class TimerAlarmController {
 public:
  static constexpr uint32_t kAutoSilenceMs = 300000U;
  static constexpr uint32_t kSnoozeSeconds = 300U;

  void reset() noexcept { *this = TimerAlarmController{}; }

  TimerTickEffects tick_timers_250ms(uint32_t now_ms);
  AlarmTickEffects tick_alarms_1s(bool time_valid, uint32_t now_epoch,
                                  int hour, int minute, int second, int day_of_week,
                                  uint32_t now_ms);

  VaTimerEffects on_va_timer_started(const std::string &id, const std::string &name,
                                     int total_sec, int left_sec, bool is_active,
                                     uint32_t now_ms);
  VaTimerEffects on_va_timer_updated(const std::string &id, const std::string &name,
                                     int total_sec, int left_sec, bool is_active,
                                     uint32_t now_ms);
  VaTimerEffects on_va_timer_cancelled(const std::string &id, const std::string &name,
                                       uint32_t now_ms);
  VaTimerEffects on_va_timer_finished(const std::string &id, const std::string &name,
                                      uint32_t now_ms);

  bool snooze_active_alarm(bool time_valid, uint32_t now_epoch, uint32_t now_ms) noexcept;
  bool dismiss_ringing() noexcept;

  uint32_t ringing_started_ms() const noexcept { return ringing_started_ms_; }

 private:
  void stamp_ringing_start_(uint32_t now_ms) noexcept {
    ringing_started_ms_ = (now_ms == 0U) ? 1U : now_ms;
  }
  bool check_auto_silence_(uint32_t now_ms) noexcept;
  static bool should_redraw_va_timer_(const DeviceRuntimeState &rs) noexcept;

  uint32_t ringing_started_ms_{0U};
};

TimerAlarmController &timer_alarm_controller() noexcept;

// Resets volatile runtime state, domain state, RPC state, and all core C++20 controllers
// deterministically in a single call (preserving bound hardware/lifecycle function pointers
// unless preserve_hooks is false).
void reset_runtime_state(bool preserve_hooks = false);

// ==============================================================================
// Intercom & Camera State / Protocol v3 Summary Parsers & Stream URL Builders
// ==============================================================================
inline int &cam_carousel_idx() { return runtime_state().cameras.selected_idx; }
inline bool &cam_call_active() { return runtime_state().intercom.cam_call_active; }
inline bool &cam_listen_active() { return runtime_state().intercom.cam_listen_active; }
inline bool &cam_intercom_ptt_active() { return runtime_state().intercom.cam_intercom_ptt_active; }
inline std::string &cam_intercom_status() { return runtime_state().intercom.cam_intercom_status; }
inline int &cam_noise_idx() { return runtime_state().intercom.cam_noise_idx; }
inline bool &cam_landscape_mode() { return runtime_state().cameras.landscape_mode; }
inline bool &cam_expanded_fullscreen() { return runtime_state().intercom.cam_expanded_fullscreen; }
inline uint32_t &last_cam_live_fetch_ms() { return runtime_state().intercom.last_cam_live_fetch_ms; }
inline uint32_t &cam_error_backoff_until_ms() { return runtime_state().intercom.cam_error_backoff_until_ms; }
inline bool &cam_download_in_progress() { return runtime_state().intercom.cam_download_in_progress; }

inline bool can_start_online_art_fetch(bool has_existing_buffer = false) noexcept {
#if defined(ESP_PLATFORM)
  const size_t int_blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (int_blk > 0U && int_blk < 8192U) {
    return false;
  }
  const size_t need_psram = has_existing_buffer
                                ? 65536U
                                : (static_cast<size_t>(LATTER_SCREEN_W) *
                                       static_cast<size_t>(LATTER_SCREEN_H) * sizeof(uint16_t) +
                                   49152U);
  const size_t psram_blk = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (psram_blk < need_psram) {
    return false;
  }
#else
  (void)has_existing_buffer;
#endif
  return true;
}

inline bool is_camera_view_active(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  return rs.nav.gui_page == PageId::CAMERAS;
}

inline bool is_p4_live_doorbell_popup_enabled(
    const PersistedDevicePrefs &p = persisted_prefs(),
    const AreaContextState &ac = area_ctx_state(),
    const AlertBannerState &ab = runtime_state().alert,
    bool is_p4 = (LATTER_CHIP_P4 != 0)) noexcept {
  if (!is_p4 || p.camera_doorbell_popup != 1U) return false;
  if (ac.camera_stream_url.empty()) return false;
  return !ab.camera_entity.empty() || !ab.camera_url.empty();
}

inline int parse_broadcast_phrases(const std::string &raw, std::vector<std::string> &out) {
  out.clear();
  size_t start = 0;
  while (start < raw.size() && out.size() < 12U) {
    const size_t sep = raw.find(kProtocolRecordSep, start);
    const std::string_view rec =
        (sep == std::string::npos) ? std::string_view(raw).substr(start)
                                   : std::string_view(raw).substr(start, sep - start);
    const std::string_view trimmed = trim_ascii_ws(rec);
    if (!trimmed.empty()) {
      out.emplace_back(trimmed.substr(0, 64U));
    }
    if (sep == std::string::npos) break;
    start = sep + 1;
  }
  return static_cast<int>(out.size());
}

inline int parse_cameras_summary_list(const std::string &raw,
                                      std::string out_ids[MAX_CAMERA_SLOTS],
                                      std::string out_names[MAX_CAMERA_SLOTS],
                                      std::string out_urls[MAX_CAMERA_SLOTS] = nullptr,
                                      std::string out_areas[MAX_CAMERA_SLOTS] = nullptr,
                                      uint8_t out_caps[MAX_CAMERA_SLOTS] = nullptr) {
  CameraRecord *recs = camera_records_scratch();
  const int cnt = parse_cameras_records(raw, recs, MAX_CAMERA_SLOTS);
  for (int i = 0; i < cnt; ++i) {
    out_ids[i] = recs[i].entity_id;
    out_names[i] = recs[i].name;
    if (out_areas != nullptr) out_areas[i] = recs[i].area_id;
    if (out_caps != nullptr) out_caps[i] = recs[i].caps;
    if (out_urls != nullptr) out_urls[i].clear();
  }
  return cnt;
}

inline int find_camera_index_by_entity(const CameraRecord *cams, int cam_cnt,
                                       std::string_view entity_id) noexcept {
  if (cams == nullptr || cam_cnt <= 0 || entity_id.empty()) return -1;
  for (int i = 0; i < cam_cnt; ++i) {
    if (cams[i].entity_id == entity_id) return i;
  }
  return -1;
}

inline int collect_carousel_camera_indices(const CameraRecord *cams, int cam_cnt,
                                           int *out_indices, int max_out) noexcept {
  if (cams == nullptr || cam_cnt <= 0 || out_indices == nullptr || max_out <= 0) return 0;
  int n = 0;
  for (int i = 0; i < cam_cnt && n < max_out; ++i) {
    if (cams[i].in_carousel()) {
      out_indices[n++] = i;
    }
  }
  if (n == 0) {
    for (int i = 0; i < cam_cnt && n < max_out; ++i) {
      out_indices[n++] = i;
    }
  }
  return n;
}

inline constexpr int cam_align16_floor(int px) noexcept {
  return (px <= 16) ? 16 : (px & ~15);
}

inline void resolve_camera_stream_dims(int panel_w, int panel_h, bool is_round,
                                       bool is_grid, bool is_p4, bool low_bw,
                                       uint16_t &out_w, uint16_t &out_h,
                                       bool rot90 = false) noexcept {
  if (is_round) {
    if (is_grid) {
      out_w = low_bw ? 256U : 320U;
      out_h = low_bw ? 192U : 256U;
    } else {
      out_w = low_bw ? 320U : 384U;
      out_h = low_bw ? 176U : 224U;
    }
    return;
  }
  const int max_dim = is_p4 ? (low_bw ? 512 : 768) : (low_bw ? 320 : 480);
  if (rot90 && !is_grid && panel_h > panel_w) {
    const int h = std::min(panel_h, max_dim);
    int w = std::min(panel_w, (h * 9) / 16);
    if (w < 128) w = std::min(panel_w, 192);
    out_w = static_cast<uint16_t>(cam_align16_floor(w));
    out_h = static_cast<uint16_t>(cam_align16_floor(h));
    return;
  }
  int w = std::min(panel_w, max_dim);
  int h = is_grid ? std::min(panel_h - 96, (w * 3) / 4)
                  : std::min(panel_h, (w * 9) / 16);
  if (h < 128) h = std::min(panel_h, 192);
  out_w = static_cast<uint16_t>(cam_align16_floor(w));
  out_h = static_cast<uint16_t>(cam_align16_floor(h));
}

inline constexpr uint8_t resolve_camera_stream_fps(bool is_p4, bool is_grid,
                                                   bool low_bw, bool audio_or_ptt_active) noexcept {
  if (is_p4) {
    if (is_grid) return low_bw ? 4U : 6U;
    return low_bw ? 6U : 12U;
  }
  if (audio_or_ptt_active) return 2U;
  if (is_grid) return 2U;
  return low_bw ? 3U : 5U;
}

inline std::string strip_query_param(std::string_view url, std::string_view key) {
  std::string out;
  out.reserve(url.size());
  const size_t qpos = url.find('?');
  if (qpos == std::string_view::npos) return std::string(url);
  out.append(url.substr(0, qpos + 1));
  std::string_view qs = url.substr(qpos + 1);
  bool first = true;
  size_t pos = 0;
  while (pos <= qs.size()) {
    const size_t amp = qs.find('&', pos);
    const std::string_view seg =
        (amp == std::string_view::npos) ? qs.substr(pos) : qs.substr(pos, amp - pos);
    if (!seg.empty()) {
      const size_t eq = seg.find('=');
      const std::string_view k = (eq == std::string_view::npos) ? seg : seg.substr(0, eq);
      if (k != key) {
        if (!first) out.push_back('&');
        out.append(seg);
        first = false;
      }
    }
    if (amp == std::string_view::npos) break;
    pos = amp + 1;
  }
  return out;
}

inline std::string build_camera_single_stream_url(std::string_view base_stream_url,
                                                  std::string_view entity_id,
                                                  uint16_t w, uint16_t h, uint8_t fps,
                                                  bool fill, bool rot90 = false) {
  if (base_stream_url.empty()) return "";
  std::string out = strip_query_param(base_stream_url, "entity_id");
  const char sep = (out.find('?') == std::string::npos) ? '?' : '&';
  if (!entity_id.empty()) {
    out.push_back(sep);
    out.append("entity_id=");
    out.append(entity_id);
  }
  auto append_kv = [&](const char *k, const std::string &v) {
    out.push_back((out.find('?') == std::string::npos) ? '?' : '&');
    out.append(k);
    out.push_back('=');
    out.append(v);
  };
  append_kv("w", std::to_string(w));
  append_kv("h", std::to_string(h));
  append_kv("fps", std::to_string(fps));
  append_kv("fit", fill ? "fill" : "contain");
  if (rot90) append_kv("rot", "90");
  return out;
}

inline std::string build_camera_grid_stream_url(std::string_view base_stream_url,
                                                const std::string *entities, int count,
                                                uint16_t w, uint16_t h, uint8_t fps) {
  if (base_stream_url.empty() || entities == nullptr || count <= 0) return "";
  std::string base = strip_query_param(base_stream_url, "entity_id");
  const size_t pos = base.find("/camera_stream/");
  if (pos != std::string::npos) {
    base.replace(pos, 15U, "/camera_grid/");
  }
  std::string cams_csv;
  const int lim = std::min(count, 4);
  for (int i = 0; i < lim; ++i) {
    if (entities[i].empty()) continue;
    if (!cams_csv.empty()) cams_csv.push_back(',');
    cams_csv.append(entities[i]);
  }
  if (cams_csv.empty()) return "";
  auto append_kv = [&](const char *k, const std::string &v) {
    base.push_back((base.find('?') == std::string::npos) ? '?' : '&');
    base.append(k);
    base.push_back('=');
    base.append(v);
  };
  append_kv("cameras", cams_csv);
  append_kv("w", std::to_string(w));
  append_kv("h", std::to_string(h));
  append_kv("fps", std::to_string(fps));
  return base;
}

inline std::string build_camera_snapshot_url(std::string_view base_snap_url,
                                             std::string_view entity_id,
                                             bool fill, bool rot90, uint32_t ts_ms) {
  if (base_snap_url.empty()) return "";
  std::string out = strip_query_param(base_snap_url, "entity_id");
  out = strip_query_param(out, "fit");
  out = strip_query_param(out, "rot");
  out = strip_query_param(out, "ts");
  auto append_kv = [&](const char *k, std::string_view v) {
    out.push_back((out.find('?') == std::string::npos) ? '?' : '&');
    out.append(k);
    out.push_back('=');
    out.append(v);
  };
  if (!entity_id.empty()) append_kv("entity_id", entity_id);
  append_kv("fit", fill ? "fill" : "contain");
  if (rot90) append_kv("rot", "90");
  append_kv("ts", std::to_string(ts_ms));
  return out;
}

inline std::string resolve_active_carousel_camera_area_id(const std::string &cameras_summary) {
  const auto &rs = runtime_state();
  CameraRecord *cams = camera_records_scratch();
  const int cnt = parse_cameras_records(cameras_summary, cams, MAX_CAMERA_SLOTS);
  if (rs.alert.active && !rs.alert.camera_entity.empty()) {
    const int ai = find_camera_index_by_entity(cams, cnt, rs.alert.camera_entity);
    if (ai >= 0) return cams[ai].area_id;
  }
  if (cnt > 0) {
    int idx = cam_carousel_idx();
    if (rs.nav.gui_page == PageId::CAMERAS && rs.cameras.tab == CamerasTab::CAROUSEL) {
      int car[MAX_CAMERA_SLOTS]{};
      const int cc = collect_carousel_camera_indices(cams, cnt, car, MAX_CAMERA_SLOTS);
      if (cc > 0) idx = car[std::clamp(rs.cameras.carousel_idx, 0, cc - 1)];
    }
    if (idx < 0 || idx >= cnt) idx = 0;
    return cams[idx].area_id;
  }
  return "";
}

inline std::string resolve_active_carousel_camera_id(const std::string &cameras_summary,
                                                     const std::string &fallback_cam_url) {
  const auto &rs = runtime_state();
  if (rs.alert.active && !rs.alert.camera_entity.empty()) {
    return rs.alert.camera_entity;
  }
  CameraRecord *cams = camera_records_scratch();
  const int cnt = parse_cameras_records(cameras_summary, cams, MAX_CAMERA_SLOTS);
  if (cnt > 0) {
    int idx = cam_carousel_idx();
    if (rs.nav.gui_page == PageId::CAMERAS && rs.cameras.tab == CamerasTab::CAROUSEL) {
      int car[MAX_CAMERA_SLOTS]{};
      const int cc = collect_carousel_camera_indices(cams, cnt, car, MAX_CAMERA_SLOTS);
      if (cc > 0) idx = car[std::clamp(rs.cameras.carousel_idx, 0, cc - 1)];
    }
    if (idx < 0 || idx >= cnt) {
      cam_carousel_idx() = 0;
      idx = 0;
    }
    return cams[idx].entity_id;
  }
  const size_t epos = fallback_cam_url.find("entity_id=");
  if (epos != std::string::npos) {
    const size_t start = epos + 10;
    const size_t amp = fallback_cam_url.find('&', start);
    return (amp == std::string::npos)
               ? fallback_cam_url.substr(start)
               : fallback_cam_url.substr(start, amp - start);
  }
  return "";
}

inline std::string build_carousel_camera_url(const std::string &base_cam_url,
                                             const std::string &cameras_summary,
                                             uint32_t now_ms) {
  if (base_cam_url.empty()) return "";
  const auto &rs = runtime_state();
  const std::string ent = resolve_active_carousel_camera_id(cameras_summary, base_cam_url);
  return build_camera_snapshot_url(
      base_cam_url, ent, rs.cameras.effective_fill(persisted_prefs()), cam_landscape_mode(), now_ms);
}

inline constexpr bool alarm_editor_blink_dim(uint32_t now_ms, uint32_t last_input_ms) noexcept {
  const uint32_t elapsed = (now_ms >= last_input_ms) ? (now_ms - last_input_ms) : 0U;
  if (elapsed < 500U) return false;
  return (((elapsed - 500U) / 500U) % 2U) == 0U;
}

inline int compute_touch_context(const DeviceRuntimeState &rs = runtime_state()) noexcept {
  const int gp = rs.nav.gui_page;
  int sub = 0;
  int drill = 0;
  switch (gp) {
    case 1:
      sub = rs.nav.home_subpage;
      break;
    case 2:
      sub = ((static_cast<int>(rs.music.view) & 0x0F) << 4) |
            ((static_cast<int>(rs.music.drilldown) & 0x03) << 2) |
            (static_cast<int>(rs.music.takeover) & 0x03);
      if ((rs.music.view == MusicView::LIBRARY && rs.music.drilldown == MusicDrilldown::LIB_LIST) ||
          rs.music.view == MusicView::ALBUM_TRACKS) {
        for (char ch : rs.music.selected_category_mode) {
          drill = ((drill * 31) + static_cast<unsigned char>(ch)) & 0x7F;
        }
      } else if (rs.music.view == MusicView::SETTINGS) {
        drill = ((rs.music.mode_bar_win_start & 0x07) << 3) | (rs.music.settings_page & 0x07);
      } else {
        drill = rs.music.mode_bar_win_start & 0x7F;
      }
      break;
    case 3:
      sub = rs.nav.active_game;
      break;
    case 4:
      sub = rs.nav.system_menu_mode;
      break;
    case 5:
      sub = rs.nav.sky_weather_subpage;
      break;
    case 6:
      sub = rs.nav.area_subpage;
      drill = (rs.area.browse_mode == "areas" || rs.area.browse_mode == "area_areas") ? 1 : 0;
      break;
    case 7:
      sub = rs.nav.remote_subpage;
      break;
    case 8:
      sub = rs.nav.tools_subpage;
      break;
    case 9:
      sub = home_hub_tab();
      drill = (rs.intercom.broadcast_dictation_active ? 4 : 0) |
              (((sub == HomeHubTab::INTERCOM ? home_hub_state().broadcast_page
                                             : home_hub_state().entry_page) & 0x0F) << 3);
      break;
    case 10:
      sub = rs.nav.time_subpage;
      if (sub == 1 && rs.timers.view_slot > 0) {
        drill = rs.timers.view_slot;
      } else if (sub == 2 && rs.alarms.edit_idx > 0) {
        drill = rs.alarms.edit_idx;
      }
      break;
    case 11:
      sub = rs.nav.pinned_menu_page;
      break;
    case 12:
      sub = rs.cal.subpage;
      drill = (rs.cal.drilldown & 0x1F) | (rs.cal.delete_confirm_open ? 0x40 : 0);
      break;
    case 13:
      sub = rs.cameras.tab;
      drill = (rs.cameras.hud_active ? 1 : 0) |
              (rs.cameras.quick_reply_open ? 2 : 0) |
              (((sub == CamerasTab::SETTINGS ? rs.cameras.settings_page
                                             : rs.cameras.grid_page) & 0x07) << 2);
      break;
    default:
      break;
  }
  if (rs.nav.shortcut_confirm.open) {
    drill = (drill & 0x7F) | 0x80;
  }
  int ctx = ((gp & 0xFF) << 16) | ((sub & 0xFF) << 8) | (drill & 0xFF);
  if (rs.nav.quick_actions_open) {
    ctx |= (1 << 24);
  }
  return ctx;
}

}  // namespace latter_ui
