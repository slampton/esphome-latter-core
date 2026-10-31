#pragma once

// ==============================================================================
// Latter Core SDK: Protocol v3 Domain State & Zero-Allocation Parsers (latter_state.h)
// ==============================================================================

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace latter_ui {

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
  uint32_t last_ha_connect_ms = 0;
  uint32_t last_ha_disconnect_ms = 0;
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
  uint32_t rev = 0;
  std::string area_id = "";
  std::string area_name = "";
  std::string site_id = "";
  bool vpn_enabled = false;
  std::string vpn_client_ip = "";
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
  std::string camera_snapshot_url = "";
  std::string cameras_summary = "";
  std::string doorbell_talk_state = "";
  std::string intercom_targets_summary = "";
  std::string custom_bindings_summary = "";
  int alert_id = -1;
  std::string alert_title = "";
  std::string alert_message = "";
  std::string alert_priority = "normal";
  int alert_duration = 10;
  bool alert_chime = true;
  std::string alert_camera_url = "";
};

struct MediaState {
  uint32_t rev = 0;
  std::string state = "idle";
  std::string title = "";
  std::string artist = "";
  std::string album = "";
  float duration = 0.0f;
  float position = 0.0f;
  float volume = 0.5f;
  std::string media_type = "music";
  int supported_features = 0;
  std::string art_url = "";
  std::string active_speaker = "";
  std::string speaker_name = "";
  std::string last_speaker = "";
  std::string local_speaker = "";
  std::string volume_entity = "";
};

struct RemoteState {
  uint32_t rev = 0;
  int active_slot = 0;
  int configured_count = 0;
  std::string slots_summary = "";
};

struct WeatherState {
  uint32_t rev = 0;
  std::string condition = "";
  float temperature = NAN;
  float humidity = NAN;
  float wind_speed = NAN;
  float wind_bearing = NAN;
  float high = NAN;
  float low = NAN;
  std::string forecast_5d = "";
};

struct CalendarState {
  uint32_t rev = 0;
  std::string summary = "";
  int count = 0;
  std::string next_event = "";
};

struct VaSessionState {
  bool active = false;
  bool tts_received = false;
  uint32_t last_abort_ms = 0;
};

struct PowerState {
  int profile_idx = 0;
  bool hibernating = false;
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

inline VaSessionState &va_session_state() {
  static VaSessionState st;
  return st;
}

inline PowerState &power_state() {
  static PowerState st;
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
  if (sv.empty() || sv == "None" || sv == "unknown" || sv == "unavailable") return fallback;
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
  if (sv.empty() || sv == "None" || sv == "unknown" || sv == "unavailable") return fallback;
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
  if (sv.empty() || sv == "None" || sv == "none" || sv == "unknown" || sv == "unavailable") return fallback;
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

inline bool apply_if_newer(uint32_t &cur_rev, uint32_t incoming_rev) {
  if (incoming_rev == 0 || incoming_rev >= cur_rev) {
    cur_rev = incoming_rev;
    return true;
  }
  return false;
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
  if (sv.empty() || sv == "none" || sv == "unknown" || sv == "unavailable") return "";
  if (sv == "cleaning" || sv == "Cleaning") return "Cleaning";
  if (sv == "paused" || sv == "Paused") return "Paused";
  if (sv == "returning" || sv == "Returning") return "Returning";
  if (sv == "docked" || sv == "Docked" || sv == "idle") return "Docked";
  return std::string(sv);
}

// Parse Protocol v3 area_context payload ("v3\x1f<rev>\x1f...")
inline bool parse_v3_area_ctx_payload(std::string_view payload, AreaContextState &st, WholeHomeState &wh) {
  std::string_view f[42];
  size_t n = v3_split_fields(payload, f, 42, '\x1f');
  if (n < 39 || f[0] != "v3") return false;
  uint32_t rev = v3_parse_u32(f[1], 0);
  if (!apply_if_newer(st.rev, rev)) return false;

  st.area_id          = std::string(f[2]);
  st.area_name        = std::string(f[3]);
  st.site_id          = std::string(f[4]);
  st.vpn_enabled      = v3_parse_bool(f[5]);
  st.vpn_client_ip    = std::string(f[6]);
  st.ble_closest_proxy = std::string(f[7]);
  st.dock_apps        = std::string(f[8]);
  st.wallpaper_url    = std::string(f[9]);
  st.theme_color_hex  = std::string(f[10]);

  // Support both 39-field and 40-field (with available_domains at f[11]) layouts
  size_t off = (n >= 40) ? 1 : 0;
  if (off == 1) {
    st.available_domains = std::string(f[11]);
  }
  st.area_temp        = v3_parse_float(f[11 + off], NAN);
  st.area_humidity    = v3_parse_float(f[12 + off], NAN);
  st.lights_on        = v3_parse_int(f[13 + off], 0);
  st.covers_open      = v3_parse_int(f[14 + off], 0);
  st.alarm_entity     = std::string(f[15 + off]);
  st.alarm_state      = std::string(f[16 + off]);

  std::string_view th_mode = f[18 + off];
  if (th_mode != "none" && th_mode != "unknown" && th_mode != "unavailable") {
    wh.thermostat_mode = std::string(th_mode);
  } else {
    wh.thermostat_mode = "";
  }
  float th_temp = v3_parse_float(f[19 + off], NAN);
  if (!std::isnan(th_temp) && th_temp >= 40.0f && th_temp <= 100.0f) {
    wh.thermostat_target_f = static_cast<int>(std::round(th_temp));
  }
  wh.thermostat_current_f = v3_parse_float(f[20 + off], NAN);
  wh.lock_state           = parse_lock_state_str(f[22 + off]);
  wh.garage_state         = parse_garage_state_str(f[24 + off]);
  wh.vacuum_state         = normalize_vacuum_state_str(f[26 + off]);

  st.camera_snapshot_url      = std::string(f[27 + off]);
  st.cameras_summary          = std::string(f[28 + off]);
  st.doorbell_talk_state      = std::string(f[29 + off]);
  st.intercom_targets_summary = std::string(f[30 + off]);
  st.custom_bindings_summary  = std::string(f[31 + off]);
  st.alert_id                 = v3_parse_int(f[32 + off], st.alert_id);
  st.alert_title              = std::string(f[33 + off]);
  st.alert_message            = std::string(f[34 + off]);
  st.alert_priority           = std::string(f[35 + off]);
  st.alert_duration           = v3_parse_int(f[36 + off], 10);
  st.alert_chime              = v3_parse_bool(f[37 + off]);
  if (38 + off < n) {
    st.alert_camera_url       = std::string(f[38 + off]);
  }
  return true;
}

// Parse Protocol v3 active_media payload ("v3\x1f<rev>\x1f...")
inline bool parse_v3_media_payload(std::string_view payload, MediaState &st) {
  std::string_view f[18];
  size_t n = v3_split_fields(payload, f, 18, '\x1f');
  if (n < 14 || f[0] != "v3") return false;
  uint32_t rev = v3_parse_u32(f[1], 0);
  if (!apply_if_newer(st.rev, rev)) return false;

  st.state              = std::string(f[2]);
  st.title              = std::string(f[3]);
  st.artist             = std::string(f[4]);
  st.album              = std::string(f[5]);
  st.duration           = v3_parse_float(f[6], 0.0f);
  st.position           = v3_parse_float(f[7], 0.0f);
  st.volume             = v3_parse_float(f[8], st.volume);
  st.media_type         = std::string(f[9]);
  st.supported_features = v3_parse_int(f[10], 0);
  st.art_url            = std::string(f[11]);
  st.active_speaker     = std::string(f[12]);
  st.speaker_name       = std::string(f[13]);
  if (n > 14) st.last_speaker  = std::string(f[14]);
  if (n > 15) st.local_speaker = std::string(f[15]);
  if (n > 16) st.volume_entity = std::string(f[16]);
  return true;
}

// Parse Protocol v3 remote_config payload ("v3\x1f<rev>\x1f<active_slot>\x1f<count>\x1f<slots_summary>")
inline bool parse_v3_remote_payload(std::string_view payload, RemoteState &st) {
  std::string_view f[6];
  size_t n = v3_split_fields(payload, f, 6, '\x1f');
  if (n < 5 || f[0] != "v3") return false;
  uint32_t rev = v3_parse_u32(f[1], 0);
  if (!apply_if_newer(st.rev, rev)) return false;

  int slot_1based     = v3_parse_int(f[2], 1);
  st.active_slot      = (slot_1based > 0) ? (slot_1based - 1) : 0;
  st.configured_count = v3_parse_int(f[3], 0);
  st.slots_summary    = std::string(f[4]);
  return true;
}

// Parse Protocol v3 weather payload ("v3\x1f<rev>\x1f<cond>\x1f<temp>\x1f<hum>\x1f<ws>\x1f<wb>\x1f<hi>\x1f<lo>\x1f<forecast_5d>")
inline bool parse_v3_weather_payload(std::string_view payload, WeatherState &st) {
  std::string_view f[11];
  size_t n = v3_split_fields(payload, f, 11, '\x1f');
  if (n < 10 || f[0] != "v3") return false;
  uint32_t rev = v3_parse_u32(f[1], 0);
  if (!apply_if_newer(st.rev, rev)) return false;

  st.condition    = std::string(f[2]);
  st.temperature  = v3_parse_float(f[3], NAN);
  st.humidity     = v3_parse_float(f[4], NAN);
  st.wind_speed   = v3_parse_float(f[5], NAN);
  st.wind_bearing = v3_parse_float(f[6], NAN);
  st.high         = v3_parse_float(f[7], NAN);
  st.low          = v3_parse_float(f[8], NAN);
  st.forecast_5d  = std::string(f[9]);
  return true;
}

// Parse Protocol v3 calendar payload ("v3\x1f<rev>\x1f<summary>\x1f<count>\x1f<next_event>")
inline bool parse_v3_calendar_payload(std::string_view payload, CalendarState &st) {
  std::string_view f[6];
  size_t n = v3_split_fields(payload, f, 6, '\x1f');
  if (n < 5 || f[0] != "v3") return false;
  uint32_t rev = v3_parse_u32(f[1], 0);
  if (!apply_if_newer(st.rev, rev)) return false;

  st.summary    = std::string(f[2]);
  st.count      = v3_parse_int(f[3], 0);
  st.next_event = std::string(f[4]);
  return true;
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
