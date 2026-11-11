#pragma once

// ==============================================================================
// Latter Core SDK: Protocol v3 Domain State & Zero-Allocation Parsers (latter_state.h)
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "latter_protocol.h"

namespace latter_ui {

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
  int covers_open = 0;
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
  std::string activity_card = "";     // "1" / "0"; empty = not set -> kDefaultActivityCard
  std::string activity_sources = "";  // csv of source keys; empty = not set -> kDefaultActivitySources
  std::string security_pin_policy = "";  // "allow" | "require_pin" | "block"
};

struct MediaState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string state = "";
  std::string title = "";
  std::string artist = "";
  std::string album = "";
  float duration = 0.0f;
  float position = 0.0f;
  uint32_t position_updated_at = 0;
  float volume = 0.5f;
  bool has_volume = false;
  std::string media_type = "";
  int supported_features = 0;
  std::string art_url = "";
  std::string active_speaker = "";
  std::string speaker_name = "";
  std::string last_speaker = "";
  std::string local_speaker = "";
  std::string volume_entity = "";
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

struct CalendarState {
  uint32_t epoch = 0;
  uint32_t rev = 0;
  std::string summary = "";
  int count = 0;
  std::string next_event = "";
};

struct BrowseSlotItem {
  std::string name = "";
  std::string id = "";
  std::string type = "";
  bool active = false;
  int level = -1;
};

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
  media_state().epoch = 0;
  media_state().rev = 0;
  remote_state().epoch = 0;
  remote_state().rev = 0;
  weather_state().epoch = 0;
  weather_state().rev = 0;
  calendar_state().epoch = 0;
  calendar_state().rev = 0;
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
  st.covers_open       = v3_parse_int(f[static_cast<size_t>(AreaCtxField::COVERS_OPEN)], 0);
  st.alarm_entity      = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_ENTITY)]);
  st.alarm_state       = std::string(f[static_cast<size_t>(AreaCtxField::ALARM_STATE)]);
  st.thermostat_entity = std::string(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_ENTITY)]);

  wh.thermostat_mode = std::string(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_STATE)]);
  float th_temp = v3_parse_float(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_TEMP)], NAN);
  wh.thermostat_target_f = (!std::isnan(th_temp) && th_temp >= 40.0f && th_temp <= 100.0f)
                               ? static_cast<int>(std::round(th_temp))
                               : -1;  // no setpoint: unknown
  wh.thermostat_current_f = v3_parse_float(f[static_cast<size_t>(AreaCtxField::THERMOSTAT_CURRENT_TEMP)], NAN);
  st.lock_entity          = std::string(f[static_cast<size_t>(AreaCtxField::LOCK_ENTITY)]);
  wh.lock_state           = parse_lock_state_str(f[static_cast<size_t>(AreaCtxField::LOCK_STATE)]);
  st.garage_entity        = std::string(f[static_cast<size_t>(AreaCtxField::GARAGE_ENTITY)]);
  wh.garage_state         = parse_garage_state_str(f[static_cast<size_t>(AreaCtxField::GARAGE_STATE)]);
  st.vacuum_entity        = std::string(f[static_cast<size_t>(AreaCtxField::VACUUM_ENTITY)]);
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
  return true;
}

// Home Activity Card sources enabled by the Device's Latter setting (area_context.activity_card /
// activity_sources). One bit per Home activity kind: 1 << 1 timers, 1 << 2 stopwatch,
// 1 << 3 music_queue, 1 << 4 alarms, 1 << 5 calendar. 0 = the card is off. An empty field (not set,
// or no payload yet) takes the spec default; unknown keys are ignored.
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

  st.state               = std::string(f[static_cast<size_t>(MediaField::STATE)]);
  st.title               = std::string(f[static_cast<size_t>(MediaField::TITLE)]);
  st.artist              = std::string(f[static_cast<size_t>(MediaField::ARTIST)]);
  st.album               = std::string(f[static_cast<size_t>(MediaField::ALBUM)]);
  st.duration            = v3_parse_float(f[static_cast<size_t>(MediaField::DURATION)], 0.0f);
  st.position            = v3_parse_float(f[static_cast<size_t>(MediaField::POSITION)], 0.0f);
  st.position_updated_at = v3_parse_u32(f[static_cast<size_t>(MediaField::POSITION_UPDATED_AT)], 0);
  float raw_vol          = v3_parse_float(f[static_cast<size_t>(MediaField::VOLUME)], NAN);
  if (!std::isnan(raw_vol)) {
    st.volume     = std::max(0.0f, std::min(1.0f, raw_vol * 0.01f));
    st.has_volume = true;
  } else {
    st.has_volume = false;
  }
  st.media_type          = std::string(f[static_cast<size_t>(MediaField::MEDIA_TYPE)]);
  st.supported_features  = v3_parse_int(f[static_cast<size_t>(MediaField::SUPPORTED_FEATURES)], 0);
  st.art_url             = std::string(f[static_cast<size_t>(MediaField::ART_URL)]);
  st.active_speaker      = std::string(f[static_cast<size_t>(MediaField::ACTIVE_SPEAKER)]);
  st.speaker_name        = std::string(f[static_cast<size_t>(MediaField::SPEAKER_NAME)]);
  st.last_speaker        = std::string(f[static_cast<size_t>(MediaField::LAST_SPEAKER)]);
  st.local_speaker       = std::string(f[static_cast<size_t>(MediaField::LOCAL_SPEAKER)]);
  st.volume_entity       = std::string(f[static_cast<size_t>(MediaField::VOLUME_ENTITY)]);
  st.is_local            = v3_parse_bool(f[static_cast<size_t>(MediaField::IS_LOCAL)]);
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

  st.summary    = std::string(f[static_cast<size_t>(CalendarField::SUMMARY)]);
  st.count      = v3_parse_int(f[static_cast<size_t>(CalendarField::EVENT_COUNT)], 0);
  st.next_event = std::string(f[static_cast<size_t>(CalendarField::NEXT_EVENT)]);
  return true;
}

// Calendar agenda summary (Protocol v3 calendar_agenda field 3): up to 5 records joined by
// kProtocolRecordSep, each "time|title|location|all_day". ha-latter replaces '|' and every control
// character in each subfield, so a record always splits into exactly 4 parts. time is a preformatted
// local label ("TODAY 14:30", "TOM ALL DAY", "NOV 20 08:05"); all_day is "1" or "0"; location may be
// empty. The views point into summary, which must outlive them. Empty summary = no events.
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

// Overload for ESPHome 2026.10+ api action string parameters (esphome::StringRef)
template <typename StrT, typename = decltype(std::declval<const StrT &>().c_str())>
inline size_t parse_v3_slots_payload(const StrT &slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
  return parse_v3_slots_payload(std::string_view(slots_blob.c_str(), slots_blob.size()), out_slots, max_slots);
}

}  // namespace latter_ui
