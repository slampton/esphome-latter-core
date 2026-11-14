#pragma once
// AUTO-GENERATED FROM protocol/latter_v3.json BY tools/gen_protocol.py — DO NOT EDIT MANUALLY.

#include <cstddef>
#include <cstdint>

namespace latter_ui {

static constexpr int kProtocolContractVersion = 3;
static constexpr size_t kProtocolMinRequiredFields = 3;
static constexpr char kProtocolFieldSep = '\x1f';
static constexpr char kProtocolRecordSep = '\x1e';

static constexpr const char *kDefaultDockApps = "music,alarm,area,remote,games,tools";  // defaults.dock_apps
static constexpr const char *kDefaultActivityCard = "1";  // defaults.activity_card
static constexpr const char *kDefaultActivitySources = "calendar,music_queue,stopwatch,alarms,timers";  // defaults.activity_sources
static constexpr const char *kDefaultAreaCategories = "scene,light,cover,fan,switch,climate,media";  // defaults.area_categories
static constexpr const char *kDefaultHomeCategories = "alarm,thermostat,entry,vacuum";  // defaults.home_categories

enum class ProtocolHeaderField : size_t {
  HEADER_VERSION = 0,  // version (string)
  HEADER_EPOCH = 1,  // epoch (u32)
  HEADER_REV = 2,  // rev (u32)
  COUNT = 3
};

// Domain: area_context (sensor.<device>_area_context)
enum class AreaCtxField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  AREA_ID = 3,  // area_id (string)
  AREA_NAME = 4,  // area_name (string)
  SITE_ID = 5,  // site_id (string)
  BLE_CLOSEST_PROXY = 6,  // ble_closest_proxy (string)
  DOCK_APPS = 7,  // dock_apps (csv)
  WALLPAPER_URL = 8,  // wallpaper_url (url)
  THEME_COLOR_HEX = 9,  // theme_color_hex (string)
  AVAILABLE_DOMAINS = 10,  // available_domains (csv)
  AREA_TEMP = 11,  // area_temp (string)
  AREA_HUMIDITY = 12,  // area_humidity (string)
  LIGHTS_ON = 13,  // lights_on (u16)
  COVERS_OPEN = 14,  // covers_open (u16)
  ALARM_ENTITY = 15,  // alarm_entity (entity_id)
  ALARM_STATE = 16,  // alarm_state (string)
  THERMOSTAT_ENTITY = 17,  // thermostat_entity (entity_id)
  THERMOSTAT_STATE = 18,  // thermostat_state (string)
  THERMOSTAT_TEMP = 19,  // thermostat_temp (f32)
  THERMOSTAT_CURRENT_TEMP = 20,  // thermostat_current_temp (f32)
  LOCK_ENTITY = 21,  // lock_entity (entity_id)
  LOCK_STATE = 22,  // lock_state (string)
  GARAGE_ENTITY = 23,  // garage_entity (entity_id)
  GARAGE_STATE = 24,  // garage_state (string)
  VACUUM_ENTITY = 25,  // vacuum_entity (entity_id)
  VACUUM_STATE = 26,  // vacuum_state (string)
  CAMERA_SNAPSHOT_URL = 27,  // camera_snapshot_url (url)
  CAMERAS_SUMMARY = 28,  // cameras_summary (records)
  DOORBELL_TALK_STATE = 29,  // doorbell_talk_state (string)
  INTERCOM_TARGETS_SUMMARY = 30,  // intercom_targets_summary (records)
  ALERT_ID = 31,  // alert_id (u32)
  ALERT_TITLE = 32,  // alert_title (string)
  ALERT_MESSAGE = 33,  // alert_message (string)
  ALERT_PRIORITY = 34,  // alert_priority (string)
  ALERT_DURATION = 35,  // alert_duration (u16)
  ALERT_CHIME = 36,  // alert_chime (bool)
  ALERT_CAMERA_URL = 37,  // alert_camera_url (url)
  ACTIVITY_CARD = 38,  // activity_card (bool)
  ACTIVITY_SOURCES = 39,  // activity_sources (csv)
  SECURITY_PIN_POLICY = 40,  // security_pin_policy (string)
  HOME_CATEGORIES = 41,  // home_categories (csv)
  AREA_ICON = 42,  // area_icon (string)
  COUNT = 43
};

// Domain: active_media (sensor.<device>_active_media)
enum class MediaField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  STATE = 3,  // state (string)
  TITLE = 4,  // title (string)
  ARTIST = 5,  // artist (string)
  ALBUM = 6,  // album (string)
  DURATION = 7,  // duration (f32)
  POSITION = 8,  // position (f32)
  POSITION_UPDATED_AT = 9,  // position_updated_at (u32)
  VOLUME = 10,  // volume (u16)
  MEDIA_TYPE = 11,  // media_type (string)
  SUPPORTED_FEATURES = 12,  // supported_features (u32)
  ART_URL = 13,  // art_url (url)
  ACTIVE_SPEAKER = 14,  // active_speaker (entity_id)
  SPEAKER_NAME = 15,  // speaker_name (string)
  LAST_SPEAKER = 16,  // last_speaker (entity_id)
  LOCAL_SPEAKER = 17,  // local_speaker (entity_id)
  VOLUME_ENTITY = 18,  // volume_entity (entity_id)
  IS_LOCAL = 19,  // is_local (bool)
  COUNT = 20
};

// Domain: remote_config (sensor.<device>_remote_config)
enum class RemoteField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  ACTIVE_SLOT = 3,  // active_slot (u16)
  CONFIGURED_COUNT = 4,  // configured_count (u16)
  SLOTS_SUMMARY = 5,  // slots_summary (records)
  COUNT = 6
};

// Domain: weather_forecast (sensor.<device>_weather_forecast)
enum class WeatherField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  CONDITION = 3,  // condition (string)
  TEMPERATURE = 4,  // temperature (f32)
  HUMIDITY = 5,  // humidity (f32)
  WIND_SPEED = 6,  // wind_speed (f32)
  WIND_BEARING = 7,  // wind_bearing (string)
  HIGH = 8,  // high (f32)
  LOW = 9,  // low (f32)
  FORECAST_5D = 10,  // forecast_5d (records)
  SUN_STATE = 11,  // sun_state (string)
  SUN_ELEVATION = 12,  // sun_elevation (f32)
  SUN_AZIMUTH = 13,  // sun_azimuth (f32)
  SUN_NEXT_RISING = 14,  // sun_next_rising (string)
  SUN_NEXT_SETTING = 15,  // sun_next_setting (string)
  MOON_PHASE = 16,  // moon_phase (string)
  COUNT = 17
};

// Domain: calendar_agenda (sensor.<device>_calendar_agenda)
enum class CalendarField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  SUMMARY = 3,  // summary (records)
  EVENT_COUNT = 4,  // count (u16)
  NEXT_EVENT = 5,  // next_event (string)
  COUNT = 6
};

// Domain: device_prefs (sensor.<device>_device_prefs)
enum class DevicePrefsField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  PREFS_SEQ = 3,  // prefs_seq (u32)
  POWER_PROFILE = 4,  // power_profile (string)
  WAKE_WORD_MODE = 5,  // wake_word_mode (string)
  TAP_TO_WAKE = 6,  // tap_to_wake (string)
  PICKUP_TO_WAKE = 7,  // pickup_to_wake (string)
  BATTERY_SCREEN_TIMEOUT = 8,  // battery_screen_timeout (string)
  DOCKED_IDLE_TIMEOUT = 9,  // docked_idle_timeout (string)
  BATTERY_HIBERNATE_TIMEOUT = 10,  // battery_hibernate_timeout (string)
  DOCKED_SCREENSAVER = 11,  // docked_screensaver (bool)
  SCREENSAVER_STYLE = 12,  // screensaver_style (string)
  HOLD_CROWN_POWER_OFF = 13,  // hold_crown_power_off (bool)
  AUTO_BRIGHTNESS_MODE = 14,  // auto_brightness_mode (string)
  MANUAL_BRIGHTNESS = 15,  // manual_brightness (string)
  DAY_BRIGHTNESS = 16,  // day_brightness (string)
  NIGHT_BRIGHTNESS = 17,  // night_brightness (string)
  DAY_START_HOUR = 18,  // day_start_hour (u16)
  NIGHT_START_HOUR = 19,  // night_start_hour (u16)
  COLOR_PROFILE = 20,  // color_profile (string)
  VA_VISUAL_PROFILE = 21,  // va_visual_profile (string)
  HOME_SCREEN_ENABLED = 22,  // home_screen_enabled (bool)
  START_SCREEN = 23,  // start_screen (string)
  RETURN_TO_HOME_TIMEOUT = 24,  // return_to_home_timeout (string)
  CLOCK_FACE = 25,  // clock_face (string)
  TIME_FORMAT = 26,  // time_format (string)
  WEATHER_BACKGROUND = 27,  // weather_background (bool)
  HOME_SUN = 28,  // home_sun (bool)
  HOME_MOON = 29,  // home_moon (bool)
  WAKE_GREETING = 30,  // wake_greeting (bool)
  ALBUM_ART_STYLE = 31,  // album_art_style (string)
  MUSIC_OVERLAY_STYLE = 32,  // music_overlay_style (string)
  SHOW_SPEAKER_SHORTCUT = 33,  // show_speaker_shortcut (bool)
  SHOW_LIBRARY_SHORTCUT = 34,  // show_library_shortcut (bool)
  ALBUM_SORT_ORDER = 35,  // album_sort_order (string)
  ARTIST_ALBUM_SORT_ORDER = 36,  // artist_album_sort_order (string)
  PODCAST_SORT_ORDER = 37,  // podcast_sort_order (string)
  WW_SENS_OKAY_NABU = 38,  // ww_sens_okay_nabu (string)
  WW_SENS_HEY_LUNA = 39,  // ww_sens_hey_luna (string)
  WW_SENS_HEY_JARVIS = 40,  // ww_sens_hey_jarvis (string)
  WW_SENS_HEY_MYCROFT = 41,  // ww_sens_hey_mycroft (string)
  WW_SENS_ALEXA = 42,  // ww_sens_alexa (string)
  WAKE_SCREEN_ON_ASSIST = 43,  // wake_screen_on_assist (bool)
  ALERT_VOLUME = 44,  // alert_volume (u16)
  VOLUME_STEP_SIZE = 45,  // volume_step_size (u16)
  GAME_SOUND_EFFECTS = 46,  // game_sound_effects (bool)
  BLE_BEACON_ENABLED = 47,  // ble_beacon_enabled (bool)
  BLE_BEACON_MODE = 48,  // ble_beacon_mode (string)
  WEATHER_SIM_CONDITION = 49,  // weather_sim_condition (string)
  THEME_COLOR_HEX = 50,  // theme_color_hex (string)
  COUNT = 51
};

}  // namespace latter_ui
