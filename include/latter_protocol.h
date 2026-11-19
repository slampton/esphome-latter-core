#pragma once
// AUTO-GENERATED FROM protocol/latter_v3.json BY tools/gen_protocol.py — DO NOT EDIT MANUALLY.

#include <cstddef>
#include <cstdint>

namespace latter_ui {

static constexpr int kProtocolContractVersion = 3;
static constexpr size_t kProtocolMinRequiredFields = 3;
static constexpr char kProtocolFieldSep = '\x1f';
static constexpr char kProtocolRecordSep = '\x1e';

static constexpr const char *kDefaultDockRoutes = "app:pinned,app:music,app:home_hub,app:area,app:remote,app:games,app:tools";  // defaults.dock_routes
static constexpr const char *kDefaultActivityCard = "1";  // defaults.activity_card
static constexpr const char *kDefaultActivitySources = "calendar,music_queue,stopwatch,alarms,timers";  // defaults.activity_sources
static constexpr const char *kDefaultAreaCategories = "scene,light,cover,fan,switch,climate,media";  // defaults.area_categories
static constexpr const char *kDefaultHomeCategories = "alarm,cameras,intercom,thermostat,entry,vacuum";  // defaults.home_categories
static constexpr const char *kDefaultToolsItems = "stopwatch,level,flashlight,calculator,dice,coin_flip";  // defaults.tools_items
static constexpr const char *kDefaultGamesItems = "marbles,archery,bricks,chomp,2048,memory";  // defaults.games_items

enum class ShortcutRouteCap : uint8_t {
  NONE = 0,
  ROUND = 1,
  IMU = 2,
};

enum class RouteId : uint8_t {
  NONE = 0,
  APP_PINNED = 1,
  APP_MUSIC = 2,
  APP_HOME_HUB = 3,
  APP_AREA = 4,
  APP_REMOTE = 5,
  APP_WEATHER = 6,
  APP_TIME = 7,
  APP_CALENDAR = 8,
  APP_TOOLS = 9,
  APP_GAMES = 10,
  APP_CLOCK = 11,
  HOME_HUB_ALARM = 12,
  HOME_HUB_CAMERAS = 13,
  HOME_HUB_INTERCOM = 14,
  HOME_HUB_THERMOSTAT = 15,
  HOME_HUB_ENTRY = 16,
  HOME_HUB_VACUUM = 17,
  AREA_PICKER = 18,
  AREA_SCENE = 19,
  AREA_LIGHT = 20,
  AREA_COVER = 21,
  AREA_FAN = 22,
  AREA_SWITCH = 23,
  AREA_CLIMATE = 24,
  AREA_MEDIA = 25,
  AREA_ALL = 26,
  WEATHER_CURRENT = 27,
  WEATHER_FORECAST = 28,
  WEATHER_SUN = 29,
  WEATHER_MOON = 30,
  TIME_CLOCK = 31,
  TIME_TIMERS = 32,
  TIME_ALARMS = 33,
  TIME_STOPWATCH = 34,
  CALENDAR_OVERVIEW = 35,
  CALENDAR_DAY = 36,
  CALENDAR_WEEK = 37,
  CALENDAR_MONTH = 38,
  MUSIC_SPEAKERS = 39,
  MUSIC_LIBRARY = 40,
  MUSIC_FAVORITES = 41,
  MUSIC_PLAYLISTS = 42,
  MUSIC_QUEUE = 43,
  MUSIC_ALBUM_TRACKS = 44,
  MUSIC_LYRICS = 45,
  MUSIC_INFO = 46,
  REMOTE_DEVICES = 47,
  TOOLS_STOPWATCH = 48,
  TOOLS_LEVEL = 49,
  TOOLS_FLASHLIGHT = 50,
  TOOLS_CALCULATOR = 51,
  TOOLS_DICE = 52,
  TOOLS_COIN_FLIP = 53,
  GAMES_MARBLES = 54,
  GAMES_ARCHERY = 55,
  GAMES_BRICKS = 56,
  GAMES_CHOMP = 57,
  GAMES_2048 = 58,
  GAMES_MEMORY = 59,
  SETTINGS_DISPLAY = 60,
  SETTINGS_SLEEP = 61,
  SETTINGS_SOUND = 62,
  SETTINGS_VOICE = 63,
  SETTINGS_MUSIC = 64,
  SETTINGS_HOME = 65,
  SETTINGS_ACTIONS = 66,
  SETTINGS_UPDATE = 67,
  SETTINGS_INFO = 68,
  SETTINGS_ROOT = 250,
};

struct ShortcutRouteSpec {
  RouteId id;
  const char *token;
  const char *label;
  bool dock_only;
  ShortcutRouteCap cap;
};

inline constexpr ShortcutRouteSpec kShortcutRoutes[] = {
    {RouteId::APP_PINNED, "app:pinned", "Pinned", true, ShortcutRouteCap::NONE},
    {RouteId::APP_MUSIC, "app:music", "Music", false, ShortcutRouteCap::NONE},
    {RouteId::APP_HOME_HUB, "app:home_hub", "Home Hub", false, ShortcutRouteCap::NONE},
    {RouteId::APP_AREA, "app:area", "Area Control", false, ShortcutRouteCap::NONE},
    {RouteId::APP_REMOTE, "app:remote", "Remote", false, ShortcutRouteCap::NONE},
    {RouteId::APP_WEATHER, "app:weather", "Sky & Weather", false, ShortcutRouteCap::NONE},
    {RouteId::APP_TIME, "app:time", "Time", false, ShortcutRouteCap::NONE},
    {RouteId::APP_CALENDAR, "app:calendar", "Calendar", false, ShortcutRouteCap::NONE},
    {RouteId::APP_TOOLS, "app:tools", "Tools", false, ShortcutRouteCap::NONE},
    {RouteId::APP_GAMES, "app:games", "Games", false, ShortcutRouteCap::NONE},
    {RouteId::APP_CLOCK, "app:clock", "Clock Face", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_ALARM, "home_hub:alarm", "Security Alarm", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_CAMERAS, "home_hub:cameras", "Cameras", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_INTERCOM, "home_hub:intercom", "Intercom", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_THERMOSTAT, "home_hub:thermostat", "Thermostat", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_ENTRY, "home_hub:entry", "Entry & Garage", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_VACUUM, "home_hub:vacuum", "Robot Vacuum", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_PICKER, "area:picker", "Switch Area", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_SCENE, "area:scene", "Area: Scenes", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_LIGHT, "area:light", "Area: Lights", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_COVER, "area:cover", "Area: Blinds & Curtains", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_FAN, "area:fan", "Area: Fans", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_SWITCH, "area:switch", "Area: Switches", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_CLIMATE, "area:climate", "Area: Climate", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_MEDIA, "area:media", "Area: Media", false, ShortcutRouteCap::NONE},
    {RouteId::AREA_ALL, "area:all", "Area: All Devices", false, ShortcutRouteCap::NONE},
    {RouteId::WEATHER_CURRENT, "weather:current", "Current Weather", false, ShortcutRouteCap::NONE},
    {RouteId::WEATHER_FORECAST, "weather:forecast", "5-Day Forecast", false, ShortcutRouteCap::NONE},
    {RouteId::WEATHER_SUN, "weather:sun", "Sun Path", false, ShortcutRouteCap::NONE},
    {RouteId::WEATHER_MOON, "weather:moon", "Moon Phase", false, ShortcutRouteCap::NONE},
    {RouteId::TIME_CLOCK, "time:clock", "World Clocks", false, ShortcutRouteCap::NONE},
    {RouteId::TIME_TIMERS, "time:timers", "Timers", false, ShortcutRouteCap::NONE},
    {RouteId::TIME_ALARMS, "time:alarms", "Alarms", false, ShortcutRouteCap::NONE},
    {RouteId::TIME_STOPWATCH, "time:stopwatch", "Stopwatch", false, ShortcutRouteCap::NONE},
    {RouteId::CALENDAR_OVERVIEW, "calendar:overview", "Upcoming Events", false, ShortcutRouteCap::NONE},
    {RouteId::CALENDAR_DAY, "calendar:day", "Day View", false, ShortcutRouteCap::NONE},
    {RouteId::CALENDAR_WEEK, "calendar:week", "Week View", false, ShortcutRouteCap::NONE},
    {RouteId::CALENDAR_MONTH, "calendar:month", "Month View", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_SPEAKERS, "music:speakers", "Speakers", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_LIBRARY, "music:library", "Music Library", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_FAVORITES, "music:favorites", "Favorites", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_PLAYLISTS, "music:playlists", "Playlists", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_QUEUE, "music:queue", "Playback Queue", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_ALBUM_TRACKS, "music:album_tracks", "Album Tracks", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_LYRICS, "music:lyrics", "Music Lyrics", false, ShortcutRouteCap::NONE},
    {RouteId::MUSIC_INFO, "music:info", "Track Info", false, ShortcutRouteCap::NONE},
    {RouteId::REMOTE_DEVICES, "remote:devices", "Remote Devices", false, ShortcutRouteCap::NONE},
    {RouteId::TOOLS_STOPWATCH, "tools:stopwatch", "Analog Chronograph", false, ShortcutRouteCap::ROUND},
    {RouteId::TOOLS_LEVEL, "tools:level", "Bubble Level", false, ShortcutRouteCap::IMU},
    {RouteId::TOOLS_FLASHLIGHT, "tools:flashlight", "Flashlight", false, ShortcutRouteCap::NONE},
    {RouteId::TOOLS_CALCULATOR, "tools:calculator", "Calculator", false, ShortcutRouteCap::NONE},
    {RouteId::TOOLS_DICE, "tools:dice", "Dice Roll", false, ShortcutRouteCap::NONE},
    {RouteId::TOOLS_COIN_FLIP, "tools:coin_flip", "Coin Flip", false, ShortcutRouteCap::NONE},
    {RouteId::GAMES_MARBLES, "games:marbles", "Marbles", false, ShortcutRouteCap::NONE},
    {RouteId::GAMES_ARCHERY, "games:archery", "Archery", false, ShortcutRouteCap::NONE},
    {RouteId::GAMES_BRICKS, "games:bricks", "Bricks", false, ShortcutRouteCap::NONE},
    {RouteId::GAMES_CHOMP, "games:chomp", "Chomp", false, ShortcutRouteCap::NONE},
    {RouteId::GAMES_2048, "games:2048", "2048", false, ShortcutRouteCap::NONE},
    {RouteId::GAMES_MEMORY, "games:memory", "Memory Match", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_DISPLAY, "settings:display", "Display Settings", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_SLEEP, "settings:sleep", "Sleep & Wake", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_SOUND, "settings:sound", "Sound Settings", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_VOICE, "settings:voice", "Voice Settings", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_MUSIC, "settings:music", "Music Settings", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_HOME, "settings:home", "Home Settings", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_ACTIONS, "settings:actions", "Device Actions", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_UPDATE, "settings:update", "Software Update", false, ShortcutRouteCap::NONE},
    {RouteId::SETTINGS_INFO, "settings:info", "System Info", false, ShortcutRouteCap::NONE},
};

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
  WALLPAPER_URL = 7,  // wallpaper_url (url)
  AVAILABLE_DOMAINS = 8,  // available_domains (csv)
  AREA_TEMP = 9,  // area_temp (string)
  AREA_HUMIDITY = 10,  // area_humidity (string)
  LIGHTS_ON = 11,  // lights_on (u16)
  COVERS_OPEN = 12,  // covers_open (u16)
  ALARM_ENTITY = 13,  // alarm_entity (entity_id)
  ALARM_STATE = 14,  // alarm_state (string)
  THERMOSTATS_SUMMARY = 15,  // thermostats_summary (records)
  LOCKS_SUMMARY = 16,  // locks_summary (records)
  GARAGES_SUMMARY = 17,  // garages_summary (records)
  VACUUMS_SUMMARY = 18,  // vacuums_summary (records)
  AREA_CLIMATES_SUMMARY = 19,  // area_climates_summary (records)
  CAMERA_SNAPSHOT_URL = 20,  // camera_snapshot_url (url)
  CAMERAS_SUMMARY = 21,  // cameras_summary (records)
  DOORBELL_TALK_STATE = 22,  // doorbell_talk_state (string)
  INTERCOM_TARGETS_SUMMARY = 23,  // intercom_targets_summary (records)
  ALERT_ID = 24,  // alert_id (u32)
  ALERT_TITLE = 25,  // alert_title (string)
  ALERT_MESSAGE = 26,  // alert_message (string)
  ALERT_PRIORITY = 27,  // alert_priority (string)
  ALERT_DURATION = 28,  // alert_duration (u16)
  ALERT_CHIME = 29,  // alert_chime (bool)
  ALERT_CAMERA_URL = 30,  // alert_camera_url (url)
  ACTIVITY_CARD = 31,  // activity_card (bool)
  ACTIVITY_SOURCES = 32,  // activity_sources (csv)
  SECURITY_PIN_POLICY = 33,  // security_pin_policy (string)
  HOME_CATEGORIES = 34,  // home_categories (csv)
  AREA_ICON = 35,  // area_icon (string)
  LOCAL_TZ_INFO = 36,  // local_tz_info (string)
  WORLD_CLOCKS_SUMMARY = 37,  // world_clocks_summary (string)
  ALARM_CAPS = 38,  // alarm_caps (u16)
  COUNT = 39
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
  ART_URL = 12,  // art_url (url)
  ACTIVE_SPEAKER = 13,  // active_speaker (entity_id)
  SPEAKER_NAME = 14,  // speaker_name (string)
  LAST_SPEAKER = 15,  // last_speaker (entity_id)
  LOCAL_SPEAKER = 16,  // local_speaker (entity_id)
  IS_LOCAL = 17,  // is_local (bool)
  SHUFFLE = 18,  // shuffle (bool)
  REPEAT = 19,  // repeat (string)
  MUTED = 20,  // muted (bool)
  FAVORITE = 21,  // favorite (bool)
  QUEUE_COUNT = 22,  // queue_count (u16)
  MEDIA_CAPS = 23,  // media_caps (u16)
  COUNT = 24
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
  CALENDARS_META = 5,  // calendars_meta (records)
  LOCATIONS_CATALOG = 6,  // locations_catalog (records)
  COUNT = 7
};

// Domain: device_prefs (sensor.<device>_device_prefs)
enum class DevicePrefsField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  PREFS_SEQ = 3,  // prefs_seq (u32)
  WAKE_WORD_MODE = 4,  // wake_word_mode (string)
  TAP_TO_WAKE = 5,  // tap_to_wake (string)
  PICKUP_TO_WAKE = 6,  // pickup_to_wake (string)
  BATTERY_SCREEN_TIMEOUT = 7,  // battery_screen_timeout (string)
  DOCKED_IDLE_TIMEOUT = 8,  // docked_idle_timeout (string)
  BATTERY_HIBERNATE_TIMEOUT = 9,  // battery_hibernate_timeout (string)
  DOCKED_SCREENSAVER = 10,  // docked_screensaver (bool)
  SCREENSAVER_STYLE = 11,  // screensaver_style (string)
  HOLD_CROWN_POWER_OFF = 12,  // hold_crown_power_off (bool)
  AUTO_BRIGHTNESS_MODE = 13,  // auto_brightness_mode (string)
  MANUAL_BRIGHTNESS = 14,  // manual_brightness (u16)
  DAY_BRIGHTNESS = 15,  // day_brightness (u16)
  NIGHT_BRIGHTNESS = 16,  // night_brightness (u16)
  DAY_START_HOUR = 17,  // day_start_hour (u16)
  NIGHT_START_HOUR = 18,  // night_start_hour (u16)
  COLOR_PROFILE = 19,  // color_profile (string)
  VA_VISUAL_PROFILE = 20,  // va_visual_profile (string)
  HOME_SCREEN_ENABLED = 21,  // home_screen_enabled (bool)
  START_SCREEN = 22,  // start_screen (string)
  RETURN_TO_HOME_TIMEOUT = 23,  // return_to_home_timeout (string)
  CLOCK_FACE = 24,  // clock_face (string)
  TIME_FORMAT = 25,  // time_format (string)
  WEATHER_BACKGROUND = 26,  // weather_background (bool)
  HOME_SUN = 27,  // home_sun (bool)
  HOME_MOON = 28,  // home_moon (bool)
  WAKE_GREETING = 29,  // wake_greeting (bool)
  ALBUM_ART_STYLE = 30,  // album_art_style (string)
  MUSIC_OVERLAY_STYLE = 31,  // music_overlay_style (string)
  ALBUM_SORT_ORDER = 32,  // album_sort_order (string)
  ARTIST_ALBUM_SORT_ORDER = 33,  // artist_album_sort_order (string)
  PODCAST_SORT_ORDER = 34,  // podcast_sort_order (string)
  WW_SENS_OKAY_NABU = 35,  // ww_sens_okay_nabu (string)
  WW_SENS_HEY_LUNA = 36,  // ww_sens_hey_luna (string)
  WW_SENS_HEY_JARVIS = 37,  // ww_sens_hey_jarvis (string)
  WW_SENS_HEY_MYCROFT = 38,  // ww_sens_hey_mycroft (string)
  WW_SENS_ALEXA = 39,  // ww_sens_alexa (string)
  WAKE_SCREEN_ON_ASSIST = 40,  // wake_screen_on_assist (bool)
  ALERT_VOLUME = 41,  // alert_volume (u16)
  VOLUME_STEP_SIZE = 42,  // volume_step_size (u16)
  GAME_SOUND_EFFECTS = 43,  // game_sound_effects (bool)
  BLE_BEACON_ENABLED = 44,  // ble_beacon_enabled (bool)
  BLE_BEACON_MODE = 45,  // ble_beacon_mode (string)
  WEATHER_SIM_CONDITION = 46,  // weather_sim_condition (string)
  THEME_COLOR_HEX = 47,  // theme_color_hex (string)
  HOME_CLOCK_TAP_ACTION = 48,  // home_clock_tap_action (string)
  SHOW_UTC_CLOCK = 49,  // show_utc_clock (bool)
  WORLD_CLOCKS = 50,  // world_clocks (string)
  ALARMS_SUMMARY = 51,  // alarms_summary (string)
  LOCAL_CLOCK_LABEL = 52,  // local_clock_label (string)
  TOOLS_ITEMS = 53,  // tools_items (csv)
  GAMES_ITEMS = 54,  // games_items (csv)
  HOME_DATE_TAP_ACTION = 55,  // home_date_tap_action (string)
  CALENDAR_LOOKAHEAD_DAYS = 56,  // calendar_lookahead_days (u16)
  CALENDAR_WEEK_START = 57,  // calendar_week_start (string)
  BEDTIME_ACTIVE = 58,  // bedtime_active (bool)
  BEDTIME_DISPLAY = 59,  // bedtime_display (string)
  BEDTIME_SCHEDULE = 60,  // bedtime_schedule (bool)
  BEDTIME_START_HOUR = 61,  // bedtime_start_hour (u16)
  BEDTIME_END_HOUR = 62,  // bedtime_end_hour (u16)
  BEDTIME_BRIGHTNESS = 63,  // bedtime_brightness (u16)
  COUNT = 64
};

// Domain: shortcuts (sensor.<device>_shortcuts)
enum class ShortcutsField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  CHUNK_INDEX = 3,  // chunk_index (u16)
  CHUNK_COUNT = 4,  // chunk_count (u16)
  DOCK_ITEMS = 5,  // dock_items (records)
  PINNED_TOTAL = 6,  // pinned_total (u16)
  PINNED_ITEMS = 7,  // pinned_items (records)
  COUNT = 8
};

}  // namespace latter_ui
