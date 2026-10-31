#pragma once
// AUTO-GENERATED FROM protocol/latter_v3.json BY tools/gen_protocol.py — DO NOT EDIT MANUALLY.

#include <cstddef>
#include <cstdint>

namespace latter_ui {

static constexpr int kProtocolContractVersion = 3;
static constexpr char kProtocolFieldSep = '\x1f';
static constexpr char kProtocolRecordSep = '\x1e';

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
  VPN_ENABLED = 6,  // vpn_enabled (bool)
  VPN_CLIENT_IP = 7,  // vpn_client_ip (string)
  BLE_CLOSEST_PROXY = 8,  // ble_closest_proxy (string)
  DOCK_APPS = 9,  // dock_apps (csv)
  WALLPAPER_URL = 10,  // wallpaper_url (url)
  THEME_COLOR_HEX = 11,  // theme_color_hex (string)
  AVAILABLE_DOMAINS = 12,  // available_domains (csv)
  AREA_TEMP = 13,  // area_temp (string)
  AREA_HUMIDITY = 14,  // area_humidity (string)
  LIGHTS_ON = 15,  // lights_on (u16)
  COVERS_OPEN = 16,  // covers_open (u16)
  ALARM_ENTITY = 17,  // alarm_entity (entity_id)
  ALARM_STATE = 18,  // alarm_state (string)
  THERMOSTAT_ENTITY = 19,  // thermostat_entity (entity_id)
  THERMOSTAT_STATE = 20,  // thermostat_state (string)
  THERMOSTAT_TEMP = 21,  // thermostat_temp (f32)
  THERMOSTAT_CURRENT_TEMP = 22,  // thermostat_current_temp (f32)
  LOCK_ENTITY = 23,  // lock_entity (entity_id)
  LOCK_STATE = 24,  // lock_state (string)
  GARAGE_ENTITY = 25,  // garage_entity (entity_id)
  GARAGE_STATE = 26,  // garage_state (string)
  VACUUM_ENTITY = 27,  // vacuum_entity (entity_id)
  VACUUM_STATE = 28,  // vacuum_state (string)
  CAMERA_SNAPSHOT_URL = 29,  // camera_snapshot_url (url)
  CAMERAS_SUMMARY = 30,  // cameras_summary (records)
  DOORBELL_TALK_STATE = 31,  // doorbell_talk_state (string)
  INTERCOM_TARGETS_SUMMARY = 32,  // intercom_targets_summary (records)
  ALERT_ID = 33,  // alert_id (u32)
  ALERT_TITLE = 34,  // alert_title (string)
  ALERT_MESSAGE = 35,  // alert_message (string)
  ALERT_PRIORITY = 36,  // alert_priority (string)
  ALERT_DURATION = 37,  // alert_duration (u16)
  ALERT_CHIME = 38,  // alert_chime (string)
  ALERT_CAMERA_URL = 39,  // alert_camera_url (url)
  COUNT = 40
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
  COUNT = 11
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

}  // namespace latter_ui
