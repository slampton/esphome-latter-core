#pragma once
// AUTO-GENERATED FROM protocol/latter_v3.json BY tools/gen_protocol.py — DO NOT EDIT MANUALLY.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace latter_core {

static constexpr int kProtocolContractVersion = 3;
static constexpr size_t kProtocolMinRequiredFields = 3;
static constexpr char kProtocolFieldSep = '\x1f';
static constexpr char kProtocolRecordSep = '\x1e';

static constexpr const char *kDefaultDockRoutes = "app:pinned,app:music,app:home_hub,app:cameras,app:area,app:remote,app:games,app:tools";  // defaults.dock_routes
static constexpr const char *kDefaultActivityCard = "1";  // defaults.activity_card
static constexpr const char *kDefaultActivitySources = "calendar,music_queue,stopwatch,alarms,timers";  // defaults.activity_sources
static constexpr const char *kDefaultAreaCategories = "scene,light,cover,fan,switch,climate,media";  // defaults.area_categories
static constexpr const char *kDefaultHomeCategories = "alarm,intercom,thermostat,entry,vacuum";  // defaults.home_categories
static constexpr const char *kDefaultToolsItems = "stopwatch,level,flashlight,calculator,dice,coin_flip";  // defaults.tools_items
static constexpr const char *kDefaultGamesItems = "blocks,snake,drop_four,marbles,archery,bricks,chomp";  // defaults.games_items

enum class ShortcutRouteCap : uint8_t {
  NONE = 0,
  ROUND = 1,
  IMU = 2,
  NO_IMU = 3,
};

enum class RouteId : uint8_t {
  NONE = 0,
  APP_PINNED = 1,
  APP_MUSIC = 2,
  APP_HOME_HUB = 3,
  APP_CAMERAS = 4,
  APP_AREA = 5,
  APP_REMOTE = 6,
  APP_WEATHER = 7,
  APP_TIME = 8,
  APP_CALENDAR = 9,
  APP_TOOLS = 10,
  APP_GAMES = 11,
  APP_CLOCK = 12,
  HOME_HUB_ALARM = 13,
  HOME_HUB_INTERCOM = 14,
  HOME_HUB_THERMOSTAT = 15,
  HOME_HUB_ENTRY = 16,
  HOME_HUB_VACUUM = 17,
  CAMERAS_GRID = 18,
  CAMERAS_SINGLE = 19,
  CAMERAS_CAROUSEL = 20,
  CAMERAS_SETTINGS = 21,
  AREA_PICKER = 22,
  AREA_SCENE = 23,
  AREA_LIGHT = 24,
  AREA_COVER = 25,
  AREA_FAN = 26,
  AREA_SWITCH = 27,
  AREA_CLIMATE = 28,
  AREA_MEDIA = 29,
  AREA_ALL = 30,
  WEATHER_CURRENT = 31,
  WEATHER_FORECAST = 32,
  WEATHER_SUN = 33,
  WEATHER_MOON = 34,
  TIME_CLOCK = 35,
  TIME_TIMERS = 36,
  TIME_ALARMS = 37,
  TIME_STOPWATCH = 38,
  CALENDAR_OVERVIEW = 39,
  CALENDAR_DAY = 40,
  CALENDAR_WEEK = 41,
  CALENDAR_MONTH = 42,
  MUSIC_SPEAKERS = 43,
  MUSIC_LIBRARY = 44,
  MUSIC_FAVORITES = 45,
  MUSIC_PLAYLISTS = 46,
  MUSIC_QUEUE = 47,
  MUSIC_ALBUM_TRACKS = 48,
  MUSIC_LYRICS = 49,
  MUSIC_INFO = 50,
  REMOTE_DEVICES = 51,
  TOOLS_STOPWATCH = 52,
  TOOLS_LEVEL = 53,
  TOOLS_FLASHLIGHT = 54,
  TOOLS_CALCULATOR = 55,
  TOOLS_DICE = 56,
  TOOLS_COIN_FLIP = 57,
  GAMES_MARBLES = 58,
  GAMES_ARCHERY = 59,
  GAMES_BRICKS = 60,
  GAMES_CHOMP = 61,
  GAMES_BLOCKS = 62,
  GAMES_SNAKE = 63,
  GAMES_DROP_FOUR = 64,
  SETTINGS_DISPLAY = 65,
  SETTINGS_SLEEP = 66,
  SETTINGS_SOUND = 67,
  SETTINGS_VOICE = 68,
  SETTINGS_MUSIC = 69,
  SETTINGS_HOME = 70,
  SETTINGS_ACTIONS = 71,
  SETTINGS_UPDATE = 72,
  SETTINGS_INFO = 73,
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
    {RouteId::APP_CAMERAS, "app:cameras", "Cameras", false, ShortcutRouteCap::NONE},
    {RouteId::APP_AREA, "app:area", "Area Control", false, ShortcutRouteCap::NONE},
    {RouteId::APP_REMOTE, "app:remote", "Remote", false, ShortcutRouteCap::NONE},
    {RouteId::APP_WEATHER, "app:weather", "Sky & Weather", false, ShortcutRouteCap::NONE},
    {RouteId::APP_TIME, "app:time", "Time", false, ShortcutRouteCap::NONE},
    {RouteId::APP_CALENDAR, "app:calendar", "Calendar", false, ShortcutRouteCap::NONE},
    {RouteId::APP_TOOLS, "app:tools", "Tools", false, ShortcutRouteCap::NONE},
    {RouteId::APP_GAMES, "app:games", "Games", false, ShortcutRouteCap::NONE},
    {RouteId::APP_CLOCK, "app:clock", "Clock Face", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_ALARM, "home_hub:alarm", "Security Alarm", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_INTERCOM, "home_hub:intercom", "Broadcast", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_THERMOSTAT, "home_hub:thermostat", "Thermostat", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_ENTRY, "home_hub:entry", "Entry & Garage", false, ShortcutRouteCap::NONE},
    {RouteId::HOME_HUB_VACUUM, "home_hub:vacuum", "Robot Vacuum", false, ShortcutRouteCap::NONE},
    {RouteId::CAMERAS_GRID, "cameras:grid", "Cameras: Grid", false, ShortcutRouteCap::NONE},
    {RouteId::CAMERAS_SINGLE, "cameras:single", "Cameras: Live", false, ShortcutRouteCap::NONE},
    {RouteId::CAMERAS_CAROUSEL, "cameras:carousel", "Cameras: Carousel", false, ShortcutRouteCap::NONE},
    {RouteId::CAMERAS_SETTINGS, "cameras:settings", "Cameras: Settings", false, ShortcutRouteCap::NONE},
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
    {RouteId::GAMES_ARCHERY, "games:archery", "Archery", false, ShortcutRouteCap::IMU},
    {RouteId::GAMES_BRICKS, "games:bricks", "Bricks", false, ShortcutRouteCap::IMU},
    {RouteId::GAMES_CHOMP, "games:chomp", "Chomp", false, ShortcutRouteCap::IMU},
    {RouteId::GAMES_BLOCKS, "games:blocks", "Blocks", false, ShortcutRouteCap::NO_IMU},
    {RouteId::GAMES_SNAKE, "games:snake", "Snake", false, ShortcutRouteCap::NO_IMU},
    {RouteId::GAMES_DROP_FOUR, "games:drop_four", "Drop Four", false, ShortcutRouteCap::NO_IMU},
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

// Domain: area_context
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
  BROADCAST_PHRASES = 23,  // broadcast_phrases (records)
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
  CAMERA_STREAM_URL = 39,  // camera_stream_url (url)
  ALERT_CAMERA_ENTITY = 40,  // alert_camera_entity (entity_id)
  SCREENSAVER_PHOTO_URL = 41,  // screensaver_photo_url (url)
  AREA_AMBIENT_LIGHT = 42,  // area_ambient_light (string)
  COUNT = 43
};

// Domain: active_media
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
  VOLUME = 9,  // volume (u16)
  MEDIA_TYPE = 10,  // media_type (string)
  ART_URL = 11,  // art_url (url)
  ACTIVE_SPEAKER = 12,  // active_speaker (entity_id)
  SPEAKER_NAME = 13,  // speaker_name (string)
  LAST_SPEAKER = 14,  // last_speaker (entity_id)
  LOCAL_SPEAKER = 15,  // local_speaker (entity_id)
  IS_LOCAL = 16,  // is_local (bool)
  SHUFFLE = 17,  // shuffle (bool)
  REPEAT = 18,  // repeat (string)
  MUTED = 19,  // muted (bool)
  FAVORITE = 20,  // favorite (bool)
  QUEUE_COUNT = 21,  // queue_count (u16)
  MEDIA_CAPS = 22,  // media_caps (u16)
  IS_DEFAULT_SPEAKER = 23,  // is_default_speaker (bool)
  COUNT = 24
};

// Domain: remote_config
enum class RemoteField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  ACTIVE_SLOT = 3,  // active_slot (u16)
  CONFIGURED_COUNT = 4,  // configured_count (u16)
  SLOTS_SUMMARY = 5,  // slots_summary (records)
  COUNT = 6
};

// Domain: weather_forecast
enum class WeatherField : size_t {
  HEADER_VERSION = 0,
  HEADER_EPOCH = 1,
  HEADER_REV = 2,
  CONDITION = 3,  // condition (string)
  TEMPERATURE = 4,  // temperature (f32)
  HUMIDITY = 5,  // humidity (f32)
  WIND_SPEED = 6,  // wind_speed (f32)
  WIND_BEARING = 7,  // wind_bearing (f32)
  HIGH = 8,  // high (f32)
  LOW = 9,  // low (f32)
  FORECAST_5D = 10,  // forecast_5d (records)
  SUN_STATE = 11,  // sun_state (string)
  SUN_ELEVATION = 12,  // sun_elevation (f32)
  SUN_AZIMUTH = 13,  // sun_azimuth (f32)
  SUN_NEXT_RISING = 14,  // sun_next_rising (string)
  SUN_NEXT_SETTING = 15,  // sun_next_setting (string)
  MOON_PHASE = 16,  // moon_phase (string)
  TEMP_UNIT = 17,  // temp_unit (string)
  COUNT = 18
};

// Domain: calendar_agenda
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

// Domain: device_prefs
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
  SCREENSAVER_MODE = 10,  // screensaver_mode (string)
  SCREENSAVER_STYLE = 11,  // screensaver_style (string)
  AUTO_BRIGHTNESS_MODE = 12,  // auto_brightness_mode (string)
  MANUAL_BRIGHTNESS = 13,  // manual_brightness (u16)
  DAY_BRIGHTNESS = 14,  // day_brightness (u16)
  NIGHT_BRIGHTNESS = 15,  // night_brightness (u16)
  DAY_START_HOUR = 16,  // day_start_hour (u16)
  NIGHT_START_HOUR = 17,  // night_start_hour (u16)
  COLOR_PROFILE = 18,  // color_profile (string)
  VA_VISUAL_PROFILE = 19,  // va_visual_profile (string)
  HOME_SCREEN_ENABLED = 20,  // home_screen_enabled (bool)
  START_SCREEN = 21,  // start_screen (string)
  RETURN_TO_HOME_TIMEOUT = 22,  // return_to_home_timeout (string)
  CLOCK_FACE = 23,  // clock_face (string)
  TIME_FORMAT = 24,  // time_format (string)
  WEATHER_BACKGROUND = 25,  // weather_background (bool)
  HOME_SUN = 26,  // home_sun (bool)
  HOME_MOON = 27,  // home_moon (bool)
  WAKE_GREETING = 28,  // wake_greeting (bool)
  ALBUM_ART_STYLE = 29,  // album_art_style (string)
  MUSIC_OVERLAY_STYLE = 30,  // music_overlay_style (string)
  ALBUM_SORT_ORDER = 31,  // album_sort_order (string)
  ARTIST_ALBUM_SORT_ORDER = 32,  // artist_album_sort_order (string)
  PODCAST_SORT_ORDER = 33,  // podcast_sort_order (string)
  WW_SENS_OKAY_NABU = 34,  // ww_sens_okay_nabu (string)
  WW_SENS_HEY_LUNA = 35,  // ww_sens_hey_luna (string)
  WW_SENS_HEY_JARVIS = 36,  // ww_sens_hey_jarvis (string)
  WW_SENS_HEY_MYCROFT = 37,  // ww_sens_hey_mycroft (string)
  WW_SENS_ALEXA = 38,  // ww_sens_alexa (string)
  WAKE_SCREEN_ON_ASSIST = 39,  // wake_screen_on_assist (bool)
  ALERT_VOLUME = 40,  // alert_volume (u16)
  VOLUME_STEP_SIZE = 41,  // volume_step_size (u16)
  GAME_SOUND_EFFECTS = 42,  // game_sound_effects (bool)
  BLE_BEACON_ENABLED = 43,  // ble_beacon_enabled (bool)
  BLE_BEACON_MODE = 44,  // ble_beacon_mode (string)
  WEATHER_SIM_CONDITION = 45,  // weather_sim_condition (string)
  THEME_COLOR_HEX = 46,  // theme_color_hex (string)
  HOME_CLOCK_TAP_ACTION = 47,  // home_clock_tap_action (string)
  SHOW_UTC_CLOCK = 48,  // show_utc_clock (bool)
  WORLD_CLOCKS = 49,  // world_clocks (string)
  ALARMS_SUMMARY = 50,  // alarms_summary (string)
  LOCAL_CLOCK_LABEL = 51,  // local_clock_label (string)
  TOOLS_ITEMS = 52,  // tools_items (csv)
  GAMES_ITEMS = 53,  // games_items (csv)
  HOME_DATE_TAP_ACTION = 54,  // home_date_tap_action (string)
  CALENDAR_LOOKAHEAD_DAYS = 55,  // calendar_lookahead_days (u16)
  CALENDAR_WEEK_START = 56,  // calendar_week_start (string)
  BEDTIME_ACTIVE = 57,  // bedtime_active (bool)
  BEDTIME_DISPLAY = 58,  // bedtime_display (string)
  BEDTIME_SCHEDULE = 59,  // bedtime_schedule (bool)
  BEDTIME_START_HOUR = 60,  // bedtime_start_hour (u16)
  BEDTIME_END_HOUR = 61,  // bedtime_end_hour (u16)
  BEDTIME_BRIGHTNESS = 62,  // bedtime_brightness (u16)
  CAMERA_DEFAULT_VIEW = 63,  // camera_default_view (string)
  CAMERA_CAROUSEL_INTERVAL = 64,  // camera_carousel_interval (string)
  CAMERA_FRAMING = 65,  // camera_framing (string)
  CAMERA_STREAM_MODE = 66,  // camera_stream_mode (string)
  CAMERA_DOORBELL_POPUP = 67,  // camera_doorbell_popup (string)
  SCREENSAVER_PHOTO_CLOCK = 68,  // screensaver_photo_clock (bool)
  WEATHER_APP_BACKGROUND = 69,  // weather_app_background (bool)
  DO_NOT_DISTURB = 70,  // do_not_disturb (bool)
  MUSIC_AUTO_OPEN = 71,  // music_auto_open (string)
  GLASS_STYLE = 72,  // glass_style (string)
  HOME_SHOW_DATE = 73,  // home_show_date (bool)
  HOME_SHOW_TIME = 74,  // home_show_time (bool)
  HOME_SHOW_WEATHER = 75,  // home_show_weather (bool)
  HOME_WEATHER_TAP_ACTION = 76,  // home_weather_tap_action (string)
  HOME_SHOW_BATTERY = 77,  // home_show_battery (bool)
  COUNT = 78
};

// Domain: shortcuts
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

enum class PrefContractDomain : uint8_t {
  SELECT = 0,
  SWITCH = 1,
  NUMBER = 2,
  TEXT = 3,
};

enum class PrefContractCap : uint8_t {
  DISPLAY = 0,
  DISPLAY_IMU = 1,
  DISPLAY_BATTERY = 2,
  DISPLAY_OR_AUDIO = 3,
  HW_JPEG = 4,
  MIC = 5,
  SPEAKER = 6,
  BEACON = 7,
  BEACON_BATTERY = 8,
};

inline constexpr std::array<std::string_view, 3> kPrefOptsWakeWordMode = {{"Docked Only", "Always On", "Disabled"}};
inline constexpr uint8_t kPrefDefaultWakeWordModeIdx = 0U;
inline constexpr std::string_view kPrefDefaultWakeWordMode = "Docked Only";
inline constexpr std::array<std::string_view, 4> kPrefOptsTapToWake = {{"Single Tap", "Double Tap", "Docked Only", "Disabled"}};
inline constexpr uint8_t kPrefDefaultTapToWakeIdx = 2U;
inline constexpr std::string_view kPrefDefaultTapToWake = "Docked Only";
inline constexpr std::array<std::string_view, 3> kPrefOptsPickupToWake = {{"Always On", "Docked Only", "Disabled"}};
inline constexpr uint8_t kPrefDefaultPickupToWakeIdx = 1U;
inline constexpr std::string_view kPrefDefaultPickupToWake = "Docked Only";
inline constexpr std::array<std::string_view, 6> kPrefOptsBatteryScreenTimeout = {{"10s", "15s", "30s", "1 min", "2 min", "Never"}};
inline constexpr uint8_t kPrefDefaultBatteryScreenTimeoutIdx = 2U;
inline constexpr std::string_view kPrefDefaultBatteryScreenTimeout = "30s";
inline constexpr std::array<std::string_view, 7> kPrefOptsDockedIdleTimeout = {{"10s", "15s", "30s", "1 min", "2 min", "5 min", "Never"}};
inline constexpr uint8_t kPrefDefaultDockedIdleTimeoutIdx = 3U;
inline constexpr std::string_view kPrefDefaultDockedIdleTimeout = "1 min";
inline constexpr std::array<std::string_view, 5> kPrefOptsBatteryHibernateTimeout = {{"5 min", "10 min", "15 min", "30 min", "Disabled"}};
inline constexpr uint8_t kPrefDefaultBatteryHibernateTimeoutIdx = 2U;
inline constexpr std::string_view kPrefDefaultBatteryHibernateTimeout = "15 min";
inline constexpr std::array<std::string_view, 3> kPrefOptsScreensaverMode = {{"Docked Only", "Always", "Never"}};
inline constexpr uint8_t kPrefDefaultScreensaverModeIdx = 0U;
inline constexpr std::string_view kPrefDefaultScreensaverMode = "Docked Only";
inline constexpr std::array<std::string_view, 6> kPrefOptsScreensaverStyle = {{"Ambient Clock", "Photo Frame", "Aurora", "Starfield", "Digital Rain", "Fireworks"}};
inline constexpr uint8_t kPrefDefaultScreensaverStyleIdx = 0U;
inline constexpr std::string_view kPrefDefaultScreensaverStyle = "Ambient Clock";
inline constexpr std::array<std::string_view, 4> kPrefOptsAutoBrightnessMode = {{"Disabled", "Sun Synchronized", "Time Schedule", "Ambient Light"}};
inline constexpr uint8_t kPrefDefaultAutoBrightnessModeIdx = 1U;
inline constexpr std::string_view kPrefDefaultAutoBrightnessMode = "Sun Synchronized";
inline constexpr int32_t kPrefDefaultManualBrightness = 80;  // [1..100, step 1]
inline constexpr int32_t kPrefMinManualBrightness = 1;
inline constexpr int32_t kPrefMaxManualBrightness = 100;
inline constexpr int32_t kPrefStepManualBrightness = 1;
inline constexpr int32_t kPrefDefaultDayBrightness = 90;  // [1..100, step 1]
inline constexpr int32_t kPrefMinDayBrightness = 1;
inline constexpr int32_t kPrefMaxDayBrightness = 100;
inline constexpr int32_t kPrefStepDayBrightness = 1;
inline constexpr int32_t kPrefDefaultNightBrightness = 35;  // [1..100, step 1]
inline constexpr int32_t kPrefMinNightBrightness = 1;
inline constexpr int32_t kPrefMaxNightBrightness = 100;
inline constexpr int32_t kPrefStepNightBrightness = 1;
inline constexpr int32_t kPrefDefaultDayStartHour = 7;  // [0..23, step 1]
inline constexpr int32_t kPrefMinDayStartHour = 0;
inline constexpr int32_t kPrefMaxDayStartHour = 23;
inline constexpr int32_t kPrefStepDayStartHour = 1;
inline constexpr int32_t kPrefDefaultNightStartHour = 20;  // [0..23, step 1]
inline constexpr int32_t kPrefMinNightStartHour = 0;
inline constexpr int32_t kPrefMaxNightStartHour = 23;
inline constexpr int32_t kPrefStepNightStartHour = 1;
inline constexpr std::array<std::string_view, 5> kPrefOptsColorProfile = {{"Steel Blue", "Pink", "Blue", "Green", "Custom"}};
inline constexpr uint8_t kPrefDefaultColorProfileIdx = 0U;
inline constexpr std::string_view kPrefDefaultColorProfile = "Steel Blue";
inline constexpr std::array<std::string_view, 5> kPrefOptsVaVisualProfile = {{"Nebula", "Solaris", "Rings", "Light Bar", "Dot Wave"}};
inline constexpr uint8_t kPrefDefaultVaVisualProfileIdx = 0U;
inline constexpr std::string_view kPrefDefaultVaVisualProfile = "Nebula";
inline constexpr bool kPrefDefaultHomeScreenEnabled = true;
inline constexpr std::array<std::string_view, 13> kPrefOptsStartScreen = {{"Home", "Pinned", "Time App", "Calendar", "Clock Face", "Media", "Home Hub", "Area Controls", "Remote", "Weather", "Cameras", "Games", "Tools"}};
inline constexpr uint8_t kPrefDefaultStartScreenIdx = 0U;
inline constexpr std::string_view kPrefDefaultStartScreen = "Home";
inline constexpr std::array<std::string_view, 7> kPrefOptsReturnToHomeTimeout = {{"15s", "30s", "1 min", "2 min", "5 min", "10 min", "Never"}};
inline constexpr uint8_t kPrefDefaultReturnToHomeTimeoutIdx = 3U;
inline constexpr std::string_view kPrefDefaultReturnToHomeTimeout = "2 min";
inline constexpr std::array<std::string_view, 3> kPrefOptsClockFace = {{"Minimal Digital", "Modern Analog", "Hybrid Digital"}};
inline constexpr uint8_t kPrefDefaultClockFaceIdx = 0U;
inline constexpr std::string_view kPrefDefaultClockFace = "Minimal Digital";
inline constexpr std::array<std::string_view, 2> kPrefOptsTimeFormat = {{"12-Hour (AM/PM)", "24-Hour"}};
inline constexpr uint8_t kPrefDefaultTimeFormatIdx = 0U;
inline constexpr std::string_view kPrefDefaultTimeFormat = "12-Hour (AM/PM)";
inline constexpr bool kPrefDefaultWeatherBackground = true;
inline constexpr bool kPrefDefaultHomeSun = true;
inline constexpr bool kPrefDefaultHomeMoon = true;
inline constexpr bool kPrefDefaultWakeGreeting = true;
inline constexpr std::array<std::string_view, 4> kPrefOptsAlbumArtStyle = {{"Full Screen", "Letterbox", "Standard", "None"}};
inline constexpr uint8_t kPrefDefaultAlbumArtStyleIdx = 0U;
inline constexpr std::string_view kPrefDefaultAlbumArtStyle = "Full Screen";
inline constexpr std::array<std::string_view, 3> kPrefOptsMusicOverlayStyle = {{"Art Only", "Art + Metadata", "Always Visible"}};
inline constexpr uint8_t kPrefDefaultMusicOverlayStyleIdx = 1U;
inline constexpr std::string_view kPrefDefaultMusicOverlayStyle = "Art + Metadata";
inline constexpr std::array<std::string_view, 3> kPrefOptsMusicAutoOpen = {{"Now Playing", "Lyrics", "Off"}};
inline constexpr uint8_t kPrefDefaultMusicAutoOpenIdx = 0U;
inline constexpr std::string_view kPrefDefaultMusicAutoOpen = "Now Playing";
inline constexpr std::array<std::string_view, 3> kPrefOptsAlbumSortOrder = {{"Alphabetical", "Newest on Top", "Oldest on Top"}};
inline constexpr uint8_t kPrefDefaultAlbumSortOrderIdx = 0U;
inline constexpr std::string_view kPrefDefaultAlbumSortOrder = "Alphabetical";
inline constexpr std::array<std::string_view, 3> kPrefOptsArtistAlbumSortOrder = {{"Oldest on Top", "Newest on Top", "Alphabetical"}};
inline constexpr uint8_t kPrefDefaultArtistAlbumSortOrderIdx = 0U;
inline constexpr std::string_view kPrefDefaultArtistAlbumSortOrder = "Oldest on Top";
inline constexpr std::array<std::string_view, 2> kPrefOptsPodcastSortOrder = {{"Newest on Top", "Oldest on Top"}};
inline constexpr uint8_t kPrefDefaultPodcastSortOrderIdx = 0U;
inline constexpr std::string_view kPrefDefaultPodcastSortOrder = "Newest on Top";
inline constexpr std::array<std::string_view, 3> kPrefOptsWwSensOkayNabu = {{"Slightly sensitive", "Moderately sensitive", "Very sensitive"}};
inline constexpr uint8_t kPrefDefaultWwSensOkayNabuIdx = 1U;
inline constexpr std::string_view kPrefDefaultWwSensOkayNabu = "Moderately sensitive";
inline constexpr std::array<std::string_view, 3> kPrefOptsWwSensHeyLuna = {{"Slightly sensitive", "Moderately sensitive", "Very sensitive"}};
inline constexpr uint8_t kPrefDefaultWwSensHeyLunaIdx = 1U;
inline constexpr std::string_view kPrefDefaultWwSensHeyLuna = "Moderately sensitive";
inline constexpr std::array<std::string_view, 3> kPrefOptsWwSensHeyJarvis = {{"Slightly sensitive", "Moderately sensitive", "Very sensitive"}};
inline constexpr uint8_t kPrefDefaultWwSensHeyJarvisIdx = 1U;
inline constexpr std::string_view kPrefDefaultWwSensHeyJarvis = "Moderately sensitive";
inline constexpr std::array<std::string_view, 3> kPrefOptsWwSensHeyMycroft = {{"Slightly sensitive", "Moderately sensitive", "Very sensitive"}};
inline constexpr uint8_t kPrefDefaultWwSensHeyMycroftIdx = 1U;
inline constexpr std::string_view kPrefDefaultWwSensHeyMycroft = "Moderately sensitive";
inline constexpr std::array<std::string_view, 3> kPrefOptsWwSensAlexa = {{"Slightly sensitive", "Moderately sensitive", "Very sensitive"}};
inline constexpr uint8_t kPrefDefaultWwSensAlexaIdx = 1U;
inline constexpr std::string_view kPrefDefaultWwSensAlexa = "Moderately sensitive";
inline constexpr bool kPrefDefaultWakeScreenOnAssist = true;
inline constexpr int32_t kPrefDefaultAlertVolume = 80;  // [20..100, step 5]
inline constexpr int32_t kPrefMinAlertVolume = 20;
inline constexpr int32_t kPrefMaxAlertVolume = 100;
inline constexpr int32_t kPrefStepAlertVolume = 5;
inline constexpr int32_t kPrefDefaultVolumeStepSize = 5;  // [1..10, step 1]
inline constexpr int32_t kPrefMinVolumeStepSize = 1;
inline constexpr int32_t kPrefMaxVolumeStepSize = 10;
inline constexpr int32_t kPrefStepVolumeStepSize = 1;
inline constexpr bool kPrefDefaultGameSoundEffects = true;
inline constexpr std::array<std::string_view, 3> kPrefOptsBleBeaconMode = {{"Handheld Only", "Handheld + Screen Off", "Always"}};
inline constexpr uint8_t kPrefDefaultBleBeaconModeIdx = 0U;
inline constexpr std::string_view kPrefDefaultBleBeaconMode = "Handheld Only";
inline constexpr std::array<std::string_view, 17> kPrefOptsWeatherSimCondition = {{"Live (Home Assistant)", "Clear Night", "Sunny / Clear Day", "Partly Cloudy (Day)", "Partly Cloudy (Night)", "Cloudy / Overcast (Day)", "Cloudy / Overcast (Night)", "Fog / Mist (Day)", "Fog / Mist (Night)", "Rainy (Day)", "Rainy (Night)", "Pouring / Heavy Rain", "Snow (Day)", "Snow (Night)", "Thunderstorm (Day)", "Thunderstorm (Night)", "Windy"}};
inline constexpr uint8_t kPrefDefaultWeatherSimConditionIdx = 0U;
inline constexpr std::string_view kPrefDefaultWeatherSimCondition = "Live (Home Assistant)";
inline constexpr std::array<std::string_view, 3> kPrefOptsHomeClockTapAction = {{"Time App", "Clock Face", "None"}};
inline constexpr uint8_t kPrefDefaultHomeClockTapActionIdx = 0U;
inline constexpr std::string_view kPrefDefaultHomeClockTapAction = "Time App";
inline constexpr std::array<std::string_view, 3> kPrefOptsHomeDateTapAction = {{"Day View", "Overview", "None"}};
inline constexpr uint8_t kPrefDefaultHomeDateTapActionIdx = 1U;
inline constexpr std::string_view kPrefDefaultHomeDateTapAction = "Overview";
inline constexpr std::string_view kPrefDefaultLocalClockLabel = "";
inline constexpr bool kPrefDefaultShowUtcClock = false;
inline constexpr std::string_view kPrefDefaultWorldClocks = "";
inline constexpr std::string_view kPrefDefaultAlarmsSummary = "07:00|0|1;08:30|0|2;09:00|0|2";
inline constexpr int32_t kPrefDefaultCalendarLookaheadDays = 30;  // [7..60, step 1]
inline constexpr int32_t kPrefMinCalendarLookaheadDays = 7;
inline constexpr int32_t kPrefMaxCalendarLookaheadDays = 60;
inline constexpr int32_t kPrefStepCalendarLookaheadDays = 1;
inline constexpr std::array<std::string_view, 2> kPrefOptsCalendarWeekStart = {{"Sunday", "Monday"}};
inline constexpr uint8_t kPrefDefaultCalendarWeekStartIdx = 0U;
inline constexpr std::string_view kPrefDefaultCalendarWeekStart = "Sunday";
inline constexpr bool kPrefDefaultBedtimeActive = false;
inline constexpr std::array<std::string_view, 2> kPrefOptsBedtimeDisplay = {{"Screen Off", "Ambient Clock"}};
inline constexpr uint8_t kPrefDefaultBedtimeDisplayIdx = 0U;
inline constexpr std::string_view kPrefDefaultBedtimeDisplay = "Screen Off";
inline constexpr bool kPrefDefaultBedtimeSchedule = false;
inline constexpr int32_t kPrefDefaultBedtimeStartHour = 22;  // [0..23, step 1]
inline constexpr int32_t kPrefMinBedtimeStartHour = 0;
inline constexpr int32_t kPrefMaxBedtimeStartHour = 23;
inline constexpr int32_t kPrefStepBedtimeStartHour = 1;
inline constexpr int32_t kPrefDefaultBedtimeEndHour = 7;  // [0..23, step 1]
inline constexpr int32_t kPrefMinBedtimeEndHour = 0;
inline constexpr int32_t kPrefMaxBedtimeEndHour = 23;
inline constexpr int32_t kPrefStepBedtimeEndHour = 1;
inline constexpr int32_t kPrefDefaultBedtimeBrightness = 15;  // [1..100, step 1]
inline constexpr int32_t kPrefMinBedtimeBrightness = 1;
inline constexpr int32_t kPrefMaxBedtimeBrightness = 100;
inline constexpr int32_t kPrefStepBedtimeBrightness = 1;
inline constexpr std::array<std::string_view, 3> kPrefOptsCameraDefaultView = {{"Grid", "Single", "Carousel"}};
inline constexpr uint8_t kPrefDefaultCameraDefaultViewIdx = 0U;
inline constexpr std::string_view kPrefDefaultCameraDefaultView = "Grid";
inline constexpr std::array<std::string_view, 4> kPrefOptsCameraCarouselInterval = {{"10s", "15s", "30s", "60s"}};
inline constexpr uint8_t kPrefDefaultCameraCarouselIntervalIdx = 1U;
inline constexpr std::string_view kPrefDefaultCameraCarouselInterval = "15s";
inline constexpr std::array<std::string_view, 2> kPrefOptsCameraFraming = {{"Uncropped", "Fill"}};
inline constexpr uint8_t kPrefDefaultCameraFramingIdx = 0U;
inline constexpr std::string_view kPrefDefaultCameraFraming = "Uncropped";
inline constexpr std::array<std::string_view, 3> kPrefOptsCameraStreamMode = {{"Auto", "Low Bandwidth", "Snapshots"}};
inline constexpr uint8_t kPrefDefaultCameraStreamModeIdx = 0U;
inline constexpr std::string_view kPrefDefaultCameraStreamMode = "Auto";
inline constexpr std::array<std::string_view, 2> kPrefOptsCameraDoorbellPopup = {{"Snapshot", "Live Video"}};
inline constexpr uint8_t kPrefDefaultCameraDoorbellPopupIdx = 0U;
inline constexpr std::string_view kPrefDefaultCameraDoorbellPopup = "Snapshot";
inline constexpr bool kPrefDefaultScreensaverPhotoClock = false;
inline constexpr bool kPrefDefaultWeatherAppBackground = true;
inline constexpr bool kPrefDefaultDoNotDisturb = false;
inline constexpr std::array<std::string_view, 3> kPrefOptsGlassStyle = {{"Auto", "Clear", "Dark"}};
inline constexpr uint8_t kPrefDefaultGlassStyleIdx = 0U;
inline constexpr std::string_view kPrefDefaultGlassStyle = "Auto";
inline constexpr bool kPrefDefaultHomeShowDate = true;
inline constexpr bool kPrefDefaultHomeShowTime = true;
inline constexpr bool kPrefDefaultHomeShowWeather = true;
inline constexpr std::array<std::string_view, 3> kPrefOptsHomeWeatherTapAction = {{"Current Weather", "5-Day Forecast", "None"}};
inline constexpr uint8_t kPrefDefaultHomeWeatherTapActionIdx = 0U;
inline constexpr std::string_view kPrefDefaultHomeWeatherTapAction = "Current Weather";
inline constexpr bool kPrefDefaultHomeShowBattery = true;

struct PrefContractSpec {
  DevicePrefsField field;
  std::string_view key;
  PrefContractDomain domain;
  PrefContractCap capability;
  std::span<const std::string_view> options;
  std::string_view default_str;
  bool default_bool;
  int32_t default_int;
  uint8_t default_option_idx;
  bool has_bounds;
  int32_t min_val;
  int32_t max_val;
  int32_t step;
};

inline constexpr std::array<PrefContractSpec, 70> kPrefContractSpecs = {{
    {DevicePrefsField::WAKE_WORD_MODE, "wake_word_mode", PrefContractDomain::SELECT, PrefContractCap::MIC, std::span<const std::string_view>{kPrefOptsWakeWordMode}, kPrefDefaultWakeWordMode, false, 0, kPrefDefaultWakeWordModeIdx, false, 0, 0, 0},
    {DevicePrefsField::TAP_TO_WAKE, "tap_to_wake", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsTapToWake}, kPrefDefaultTapToWake, false, 2, kPrefDefaultTapToWakeIdx, false, 0, 0, 0},
    {DevicePrefsField::PICKUP_TO_WAKE, "pickup_to_wake", PrefContractDomain::SELECT, PrefContractCap::DISPLAY_IMU, std::span<const std::string_view>{kPrefOptsPickupToWake}, kPrefDefaultPickupToWake, false, 1, kPrefDefaultPickupToWakeIdx, false, 0, 0, 0},
    {DevicePrefsField::BATTERY_SCREEN_TIMEOUT, "battery_screen_timeout", PrefContractDomain::SELECT, PrefContractCap::DISPLAY_BATTERY, std::span<const std::string_view>{kPrefOptsBatteryScreenTimeout}, kPrefDefaultBatteryScreenTimeout, false, 2, kPrefDefaultBatteryScreenTimeoutIdx, false, 0, 0, 0},
    {DevicePrefsField::DOCKED_IDLE_TIMEOUT, "docked_idle_timeout", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsDockedIdleTimeout}, kPrefDefaultDockedIdleTimeout, false, 3, kPrefDefaultDockedIdleTimeoutIdx, false, 0, 0, 0},
    {DevicePrefsField::BATTERY_HIBERNATE_TIMEOUT, "battery_hibernate_timeout", PrefContractDomain::SELECT, PrefContractCap::DISPLAY_BATTERY, std::span<const std::string_view>{kPrefOptsBatteryHibernateTimeout}, kPrefDefaultBatteryHibernateTimeout, false, 2, kPrefDefaultBatteryHibernateTimeoutIdx, false, 0, 0, 0},
    {DevicePrefsField::SCREENSAVER_MODE, "screensaver_mode", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsScreensaverMode}, kPrefDefaultScreensaverMode, false, 0, kPrefDefaultScreensaverModeIdx, false, 0, 0, 0},
    {DevicePrefsField::SCREENSAVER_STYLE, "screensaver_style", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsScreensaverStyle}, kPrefDefaultScreensaverStyle, false, 0, kPrefDefaultScreensaverStyleIdx, false, 0, 0, 0},
    {DevicePrefsField::AUTO_BRIGHTNESS_MODE, "auto_brightness_mode", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsAutoBrightnessMode}, kPrefDefaultAutoBrightnessMode, false, 1, kPrefDefaultAutoBrightnessModeIdx, false, 0, 0, 0},
    {DevicePrefsField::MANUAL_BRIGHTNESS, "manual_brightness", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "80", false, kPrefDefaultManualBrightness, 0U, true, kPrefMinManualBrightness, kPrefMaxManualBrightness, kPrefStepManualBrightness},
    {DevicePrefsField::DAY_BRIGHTNESS, "day_brightness", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "90", false, kPrefDefaultDayBrightness, 0U, true, kPrefMinDayBrightness, kPrefMaxDayBrightness, kPrefStepDayBrightness},
    {DevicePrefsField::NIGHT_BRIGHTNESS, "night_brightness", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "35", false, kPrefDefaultNightBrightness, 0U, true, kPrefMinNightBrightness, kPrefMaxNightBrightness, kPrefStepNightBrightness},
    {DevicePrefsField::DAY_START_HOUR, "day_start_hour", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "7", false, kPrefDefaultDayStartHour, 0U, true, kPrefMinDayStartHour, kPrefMaxDayStartHour, kPrefStepDayStartHour},
    {DevicePrefsField::NIGHT_START_HOUR, "night_start_hour", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "20", false, kPrefDefaultNightStartHour, 0U, true, kPrefMinNightStartHour, kPrefMaxNightStartHour, kPrefStepNightStartHour},
    {DevicePrefsField::COLOR_PROFILE, "color_profile", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsColorProfile}, kPrefDefaultColorProfile, false, 0, kPrefDefaultColorProfileIdx, false, 0, 0, 0},
    {DevicePrefsField::VA_VISUAL_PROFILE, "va_visual_profile", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsVaVisualProfile}, kPrefDefaultVaVisualProfile, false, 0, kPrefDefaultVaVisualProfileIdx, false, 0, 0, 0},
    {DevicePrefsField::HOME_SCREEN_ENABLED, "home_screen_enabled", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeScreenEnabled, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::START_SCREEN, "start_screen", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsStartScreen}, kPrefDefaultStartScreen, false, 0, kPrefDefaultStartScreenIdx, false, 0, 0, 0},
    {DevicePrefsField::RETURN_TO_HOME_TIMEOUT, "return_to_home_timeout", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsReturnToHomeTimeout}, kPrefDefaultReturnToHomeTimeout, false, 3, kPrefDefaultReturnToHomeTimeoutIdx, false, 0, 0, 0},
    {DevicePrefsField::CLOCK_FACE, "clock_face", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsClockFace}, kPrefDefaultClockFace, false, 0, kPrefDefaultClockFaceIdx, false, 0, 0, 0},
    {DevicePrefsField::TIME_FORMAT, "time_format", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsTimeFormat}, kPrefDefaultTimeFormat, false, 0, kPrefDefaultTimeFormatIdx, false, 0, 0, 0},
    {DevicePrefsField::WEATHER_BACKGROUND, "weather_background", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultWeatherBackground, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::HOME_SUN, "home_sun", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeSun, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::HOME_MOON, "home_moon", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeMoon, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::WAKE_GREETING, "wake_greeting", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultWakeGreeting, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::ALBUM_ART_STYLE, "album_art_style", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsAlbumArtStyle}, kPrefDefaultAlbumArtStyle, false, 0, kPrefDefaultAlbumArtStyleIdx, false, 0, 0, 0},
    {DevicePrefsField::MUSIC_OVERLAY_STYLE, "music_overlay_style", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsMusicOverlayStyle}, kPrefDefaultMusicOverlayStyle, false, 1, kPrefDefaultMusicOverlayStyleIdx, false, 0, 0, 0},
    {DevicePrefsField::MUSIC_AUTO_OPEN, "music_auto_open", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsMusicAutoOpen}, kPrefDefaultMusicAutoOpen, false, 0, kPrefDefaultMusicAutoOpenIdx, false, 0, 0, 0},
    {DevicePrefsField::ALBUM_SORT_ORDER, "album_sort_order", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsAlbumSortOrder}, kPrefDefaultAlbumSortOrder, false, 0, kPrefDefaultAlbumSortOrderIdx, false, 0, 0, 0},
    {DevicePrefsField::ARTIST_ALBUM_SORT_ORDER, "artist_album_sort_order", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsArtistAlbumSortOrder}, kPrefDefaultArtistAlbumSortOrder, false, 0, kPrefDefaultArtistAlbumSortOrderIdx, false, 0, 0, 0},
    {DevicePrefsField::PODCAST_SORT_ORDER, "podcast_sort_order", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsPodcastSortOrder}, kPrefDefaultPodcastSortOrder, false, 0, kPrefDefaultPodcastSortOrderIdx, false, 0, 0, 0},
    {DevicePrefsField::WW_SENS_OKAY_NABU, "ww_sens_okay_nabu", PrefContractDomain::SELECT, PrefContractCap::MIC, std::span<const std::string_view>{kPrefOptsWwSensOkayNabu}, kPrefDefaultWwSensOkayNabu, false, 1, kPrefDefaultWwSensOkayNabuIdx, false, 0, 0, 0},
    {DevicePrefsField::WW_SENS_HEY_LUNA, "ww_sens_hey_luna", PrefContractDomain::SELECT, PrefContractCap::MIC, std::span<const std::string_view>{kPrefOptsWwSensHeyLuna}, kPrefDefaultWwSensHeyLuna, false, 1, kPrefDefaultWwSensHeyLunaIdx, false, 0, 0, 0},
    {DevicePrefsField::WW_SENS_HEY_JARVIS, "ww_sens_hey_jarvis", PrefContractDomain::SELECT, PrefContractCap::MIC, std::span<const std::string_view>{kPrefOptsWwSensHeyJarvis}, kPrefDefaultWwSensHeyJarvis, false, 1, kPrefDefaultWwSensHeyJarvisIdx, false, 0, 0, 0},
    {DevicePrefsField::WW_SENS_HEY_MYCROFT, "ww_sens_hey_mycroft", PrefContractDomain::SELECT, PrefContractCap::MIC, std::span<const std::string_view>{kPrefOptsWwSensHeyMycroft}, kPrefDefaultWwSensHeyMycroft, false, 1, kPrefDefaultWwSensHeyMycroftIdx, false, 0, 0, 0},
    {DevicePrefsField::WW_SENS_ALEXA, "ww_sens_alexa", PrefContractDomain::SELECT, PrefContractCap::MIC, std::span<const std::string_view>{kPrefOptsWwSensAlexa}, kPrefDefaultWwSensAlexa, false, 1, kPrefDefaultWwSensAlexaIdx, false, 0, 0, 0},
    {DevicePrefsField::WAKE_SCREEN_ON_ASSIST, "wake_screen_on_assist", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultWakeScreenOnAssist, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::ALERT_VOLUME, "alert_volume", PrefContractDomain::NUMBER, PrefContractCap::SPEAKER, std::span<const std::string_view>{}, "80", false, kPrefDefaultAlertVolume, 0U, true, kPrefMinAlertVolume, kPrefMaxAlertVolume, kPrefStepAlertVolume},
    {DevicePrefsField::VOLUME_STEP_SIZE, "volume_step_size", PrefContractDomain::NUMBER, PrefContractCap::SPEAKER, std::span<const std::string_view>{}, "5", false, kPrefDefaultVolumeStepSize, 0U, true, kPrefMinVolumeStepSize, kPrefMaxVolumeStepSize, kPrefStepVolumeStepSize},
    {DevicePrefsField::GAME_SOUND_EFFECTS, "game_sound_effects", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultGameSoundEffects, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::BLE_BEACON_MODE, "ble_beacon_mode", PrefContractDomain::SELECT, PrefContractCap::BEACON_BATTERY, std::span<const std::string_view>{kPrefOptsBleBeaconMode}, kPrefDefaultBleBeaconMode, false, 0, kPrefDefaultBleBeaconModeIdx, false, 0, 0, 0},
    {DevicePrefsField::WEATHER_SIM_CONDITION, "weather_sim_condition", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsWeatherSimCondition}, kPrefDefaultWeatherSimCondition, false, 0, kPrefDefaultWeatherSimConditionIdx, false, 0, 0, 0},
    {DevicePrefsField::HOME_CLOCK_TAP_ACTION, "home_clock_tap_action", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsHomeClockTapAction}, kPrefDefaultHomeClockTapAction, false, 0, kPrefDefaultHomeClockTapActionIdx, false, 0, 0, 0},
    {DevicePrefsField::HOME_DATE_TAP_ACTION, "home_date_tap_action", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsHomeDateTapAction}, kPrefDefaultHomeDateTapAction, false, 1, kPrefDefaultHomeDateTapActionIdx, false, 0, 0, 0},
    {DevicePrefsField::LOCAL_CLOCK_LABEL, "local_clock_label", PrefContractDomain::TEXT, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, kPrefDefaultLocalClockLabel, false, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::SHOW_UTC_CLOCK, "show_utc_clock", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "0", kPrefDefaultShowUtcClock, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::WORLD_CLOCKS, "world_clocks", PrefContractDomain::TEXT, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, kPrefDefaultWorldClocks, false, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::ALARMS_SUMMARY, "alarms_summary", PrefContractDomain::TEXT, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, kPrefDefaultAlarmsSummary, false, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::CALENDAR_LOOKAHEAD_DAYS, "calendar_lookahead_days", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "30", false, kPrefDefaultCalendarLookaheadDays, 0U, true, kPrefMinCalendarLookaheadDays, kPrefMaxCalendarLookaheadDays, kPrefStepCalendarLookaheadDays},
    {DevicePrefsField::CALENDAR_WEEK_START, "calendar_week_start", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsCalendarWeekStart}, kPrefDefaultCalendarWeekStart, false, 0, kPrefDefaultCalendarWeekStartIdx, false, 0, 0, 0},
    {DevicePrefsField::BEDTIME_ACTIVE, "bedtime_active", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "0", kPrefDefaultBedtimeActive, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::BEDTIME_DISPLAY, "bedtime_display", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsBedtimeDisplay}, kPrefDefaultBedtimeDisplay, false, 0, kPrefDefaultBedtimeDisplayIdx, false, 0, 0, 0},
    {DevicePrefsField::BEDTIME_SCHEDULE, "bedtime_schedule", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "0", kPrefDefaultBedtimeSchedule, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::BEDTIME_START_HOUR, "bedtime_start_hour", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "22", false, kPrefDefaultBedtimeStartHour, 0U, true, kPrefMinBedtimeStartHour, kPrefMaxBedtimeStartHour, kPrefStepBedtimeStartHour},
    {DevicePrefsField::BEDTIME_END_HOUR, "bedtime_end_hour", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "7", false, kPrefDefaultBedtimeEndHour, 0U, true, kPrefMinBedtimeEndHour, kPrefMaxBedtimeEndHour, kPrefStepBedtimeEndHour},
    {DevicePrefsField::BEDTIME_BRIGHTNESS, "bedtime_brightness", PrefContractDomain::NUMBER, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "15", false, kPrefDefaultBedtimeBrightness, 0U, true, kPrefMinBedtimeBrightness, kPrefMaxBedtimeBrightness, kPrefStepBedtimeBrightness},
    {DevicePrefsField::CAMERA_DEFAULT_VIEW, "camera_default_view", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsCameraDefaultView}, kPrefDefaultCameraDefaultView, false, 0, kPrefDefaultCameraDefaultViewIdx, false, 0, 0, 0},
    {DevicePrefsField::CAMERA_CAROUSEL_INTERVAL, "camera_carousel_interval", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsCameraCarouselInterval}, kPrefDefaultCameraCarouselInterval, false, 1, kPrefDefaultCameraCarouselIntervalIdx, false, 0, 0, 0},
    {DevicePrefsField::CAMERA_FRAMING, "camera_framing", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsCameraFraming}, kPrefDefaultCameraFraming, false, 0, kPrefDefaultCameraFramingIdx, false, 0, 0, 0},
    {DevicePrefsField::CAMERA_STREAM_MODE, "camera_stream_mode", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsCameraStreamMode}, kPrefDefaultCameraStreamMode, false, 0, kPrefDefaultCameraStreamModeIdx, false, 0, 0, 0},
    {DevicePrefsField::CAMERA_DOORBELL_POPUP, "camera_doorbell_popup", PrefContractDomain::SELECT, PrefContractCap::HW_JPEG, std::span<const std::string_view>{kPrefOptsCameraDoorbellPopup}, kPrefDefaultCameraDoorbellPopup, false, 0, kPrefDefaultCameraDoorbellPopupIdx, false, 0, 0, 0},
    {DevicePrefsField::SCREENSAVER_PHOTO_CLOCK, "screensaver_photo_clock", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "0", kPrefDefaultScreensaverPhotoClock, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::WEATHER_APP_BACKGROUND, "weather_app_background", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultWeatherAppBackground, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::DO_NOT_DISTURB, "do_not_disturb", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY_OR_AUDIO, std::span<const std::string_view>{}, "0", kPrefDefaultDoNotDisturb, 0, 0U, false, 0, 0, 0},
    {DevicePrefsField::GLASS_STYLE, "glass_style", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsGlassStyle}, kPrefDefaultGlassStyle, false, 0, kPrefDefaultGlassStyleIdx, false, 0, 0, 0},
    {DevicePrefsField::HOME_SHOW_DATE, "home_show_date", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeShowDate, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::HOME_SHOW_TIME, "home_show_time", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeShowTime, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::HOME_SHOW_WEATHER, "home_show_weather", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeShowWeather, 1, 0U, false, 0, 0, 0},
    {DevicePrefsField::HOME_WEATHER_TAP_ACTION, "home_weather_tap_action", PrefContractDomain::SELECT, PrefContractCap::DISPLAY, std::span<const std::string_view>{kPrefOptsHomeWeatherTapAction}, kPrefDefaultHomeWeatherTapAction, false, 0, kPrefDefaultHomeWeatherTapActionIdx, false, 0, 0, 0},
    {DevicePrefsField::HOME_SHOW_BATTERY, "home_show_battery", PrefContractDomain::SWITCH, PrefContractCap::DISPLAY_BATTERY, std::span<const std::string_view>{}, "1", kPrefDefaultHomeShowBattery, 1, 0U, false, 0, 0, 0},
}};

inline constexpr const PrefContractSpec *find_pref_contract(std::string_view key) noexcept {
  for (const auto &spec : kPrefContractSpecs) {
    if (spec.key == key) return &spec;
  }
  return nullptr;
}

inline constexpr const PrefContractSpec *find_pref_contract(DevicePrefsField field) noexcept {
  for (const auto &spec : kPrefContractSpecs) {
    if (spec.field == field) return &spec;
  }
  return nullptr;
}

inline constexpr int find_pref_option_index(std::span<const std::string_view> options,
                                            std::string_view value) noexcept {
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i] == value) return static_cast<int>(i);
  }
  return -1;
}

inline constexpr int find_pref_option_index(const PrefContractSpec &spec,
                                            std::string_view value) noexcept {
  return find_pref_option_index(spec.options, value);
}

inline constexpr std::string_view pref_option_at_clamped(std::span<const std::string_view> options,
                                                         size_t idx,
                                                         size_t fallback = 0U) noexcept {
  if (options.empty()) return {};
  if (idx < options.size()) return options[idx];
  return options[(fallback < options.size()) ? fallback : 0U];
}

inline constexpr std::string_view pref_option_at_clamped(const PrefContractSpec &spec,
                                                         size_t idx) noexcept {
  return pref_option_at_clamped(spec.options, idx, spec.default_option_idx);
}

inline constexpr int32_t clamp_pref_number(const PrefContractSpec &spec, int32_t value) noexcept {
  if (!spec.has_bounds) return value;
  if (value < spec.min_val) return spec.min_val;
  if (value > spec.max_val) return spec.max_val;
  return value;
}

}  // namespace latter_core
