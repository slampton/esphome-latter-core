#if __has_include("state/device_runtime_state.h")
#include "state/device_runtime_state.h"
#else
#include "../../include/state/device_runtime_state.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace latter_core {

int parse_thermostats_summary_records(std::string_view raw,
                                      ThermostatZoneItem *out_items,
                                      int max_items,
                                      bool has_area_col) {
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
    dst.entity_id = v3_unescape_subfield(cols[0]);
    dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : v3_unescape_subfield(cols[1]);
    const size_t off = has_area_col ? 1U : 0U;
    dst.area_id = has_area_col ? v3_unescape_subfield(cols[2]) : std::string{};
    dst.mode = v3_unescape_subfield(cols[2U + off]);
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

int parse_entry_controls_records(std::string_view locks_raw,
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
      dst.entity_id = v3_unescape_subfield(cols[0]);
      dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : v3_unescape_subfield(cols[1]);
      dst.area_id = v3_unescape_subfield(cols[2]);
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
      dst.entity_id = v3_unescape_subfield(cols[0]);
      dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : v3_unescape_subfield(cols[1]);
      dst.area_id = v3_unescape_subfield(cols[2]);
      dst.is_lock = false;
      dst.garage_state = parse_garage_state_str(cols[3]);
    }
  }
  return cnt;
}

int parse_vacuums_summary_records(std::string_view raw,
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
    dst.entity_id = v3_unescape_subfield(cols[0]);
    dst.name = cols[1].empty() ? fallback_entity_label_sv(cols[0]) : v3_unescape_subfield(cols[1]);
    dst.area_id = v3_unescape_subfield(cols[2]);
    dst.state = normalize_vacuum_state_str(cols[3]);
    const uint32_t raw_caps = v3_parse_u32(cols[4], 0U);
    dst.caps = (raw_caps != 0U) ? static_cast<uint16_t>(raw_caps & 0xFFFFU) : kVacuumCapDefault;
  }
  return cnt;
}

// Parse Protocol v3 area_context payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
bool parse_v3_area_ctx_payload(std::string_view payload,
                               AreaContextState &st,
                               HomeHubState &hub) {
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
  st.area_ambient_light       = std::string(f[static_cast<size_t>(AreaCtxField::AREA_AMBIENT_LIGHT)]);
  parse_area_ambient_light(st.area_ambient_light, st.area_ambient_pct, st.area_lux_display);
  const uint32_t raw_alarm_caps = v3_parse_u32(f[static_cast<size_t>(AreaCtxField::ALARM_CAPS)], 0U);
  st.alarm_caps = st.alarm_entity.empty()
                      ? 0U
                      : ((raw_alarm_caps != 0U)
                             ? static_cast<uint16_t>(raw_alarm_caps & 0xFFFFU)
                             : kAlarmCapDefault);
  return true;
}

// Parse Protocol v3 active_media payload ("v3\x1f<epoch>\x1f<rev>\x1f...")
bool parse_v3_media_payload(std::string_view payload,
                            MediaState &st,
                            uint32_t now_ms) {
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
  const std::string_view def_spk_sv = f[static_cast<size_t>(MediaField::IS_DEFAULT_SPEAKER)];
  st.is_default_speaker = def_spk_sv.empty() ? st.is_local : v3_parse_bool(def_spk_sv);
  return true;
}

// Parse Protocol v3 remote_config payload ("v3\x1f<epoch>\x1f<rev>\x1f<active_slot>\x1f<count>\x1f<slots_summary>")
bool parse_v3_remote_payload(std::string_view payload, RemoteState &st) {
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
bool parse_v3_weather_payload(std::string_view payload, WeatherState &st) {
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
  st.temp_unit        = normalize_weather_temp_unit(f[static_cast<size_t>(WeatherField::TEMP_UNIT)]);
  return true;
}

// Parse Protocol v3 calendar payload
// ("v3\x1f<epoch>\x1f<rev>\x1f<summary>\x1f<count>\x1f<calendars_meta>\x1f<locations_catalog>")
bool parse_v3_calendar_payload(std::string_view payload, CalendarState &st) {
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

namespace {

bool parse_strict_nonneg_int(std::string_view raw, int &out) noexcept {
  const std::string_view sv = trim_ascii_ws(raw);
  if (sv.empty()) return false;
  int val = 0;
  for (char ch : sv) {
    if (ch < '0' || ch > '9') return false;
    if (val > 100000) return false;
    val = val * 10 + (ch - '0');
  }
  out = val;
  return true;
}

std::string clamp_utf8_prefix(std::string_view sv, size_t max_bytes) {
  if (sv.size() <= max_bytes) return std::string(sv);
  size_t cut = max_bytes;
  while (cut > 0U && (static_cast<uint8_t>(sv[cut]) & 0xC0U) == 0x80U) {
    --cut;
  }
  return std::string(sv.substr(0U, cut));
}

void parse_pinned_page_titles_blob(std::string_view blob, std::vector<PinnedPageTitle> &dst) {
  dst.clear();
  if (blob.empty()) return;
  size_t start = 0;
  while (start <= blob.size() && dst.size() < ShortcutsState::kMaxPinnedPageTitles) {
    const size_t pos = blob.find(kProtocolRecordSep, start);
    const std::string_view rec =
        (pos == std::string_view::npos) ? blob.substr(start) : blob.substr(start, pos - start);
    const size_t bar = rec.find('|');
    if (bar != std::string_view::npos) {
      int pg_idx = -1;
      if (parse_strict_nonneg_int(rec.substr(0U, bar), pg_idx) &&
          pg_idx >= 0 && pg_idx < kTileGridMaxLogicalPages) {
        const std::string unescaped = v3_unescape_subfield(rec.substr(bar + 1U));
        const std::string_view trimmed = trim_ascii_ws(unescaped);
        if (!trimmed.empty()) {
          std::string clamped = clamp_utf8_prefix(trimmed, 24U);
          bool updated = false;
          for (auto &existing : dst) {
            if (static_cast<int>(existing.page) == pg_idx) {
              existing.title = std::move(clamped);
              updated = true;
              break;
            }
          }
          if (!updated) {
            dst.push_back(PinnedPageTitle{static_cast<uint8_t>(pg_idx), std::move(clamped)});
          }
        }
      }
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1U;
  }
}

}  // namespace

bool parse_shortcut_record(std::string_view rec, ShortcutItem &out_item) {
  if (rec.empty()) return false;
  std::string_view cols[14]{};
  const size_t n = v3_split_fields(rec, cols, 14, '|');
  if (n < 4) return false;
  out_item = ShortcutItem{};
  const std::string_view kind_sv = cols[0];
  const std::string_view target_sv = cols[1];
  const std::string_view action_sv = cols[2];
  const std::string_view label_sv = cols[3];
  const std::string_view icon_sv = (n >= 5) ? cols[4] : std::string_view{};
  const std::string_view confirm_sv = (n >= 6) ? cols[5] : std::string_view{};
  const std::string_view state_sv = (n >= 7) ? cols[6] : std::string_view{};
  const std::string_view badge_sv = (n >= 8) ? trim_ascii_ws(cols[7]) : std::string_view{};
  const std::string_view accent_sv = (n >= 9) ? trim_ascii_ws(cols[8]) : std::string_view{};

  int p_val = -1;
  int c_val = -1;
  int r_val = -1;
  const bool explicit_pos =
      (n >= 12) &&
      parse_strict_nonneg_int(cols[9], p_val) && p_val >= 0 && p_val < kTileGridMaxLogicalPages &&
      parse_strict_nonneg_int(cols[10], c_val) && c_val >= 0 && c_val < kTileGridMaxCols &&
      parse_strict_nonneg_int(cols[11], r_val) && r_val >= 0 && r_val < kTileGridMaxRows;
  const int8_t parsed_page = explicit_pos ? static_cast<int8_t>(p_val) : static_cast<int8_t>(-1);
  const int8_t parsed_col  = explicit_pos ? static_cast<int8_t>(c_val) : static_cast<int8_t>(-1);
  const int8_t parsed_row  = explicit_pos ? static_cast<int8_t>(r_val) : static_cast<int8_t>(-1);
  int cs_val = 1;
  int rs_val = 1;
  const uint8_t parsed_col_span =
      ((n >= 13) && parse_strict_nonneg_int(cols[12], cs_val))
          ? static_cast<uint8_t>(std::clamp(cs_val, 1, kTileGridMaxCols))
          : 1U;
  const uint8_t parsed_row_span =
      ((n >= 14) && parse_strict_nonneg_int(cols[13], rs_val))
          ? static_cast<uint8_t>(std::clamp(rs_val, 1, kTileGridMaxRows))
          : 1U;

  auto parse_badge_style = [](std::string_view sv) noexcept -> ShortcutBadgeStyle {
    if (eq_ascii_ci_sv(sv, "inverted")) return ShortcutBadgeStyle::INVERTED;
    return ShortcutBadgeStyle::GLASS;
  };
  auto parse_accent = [](std::string_view sv) noexcept -> ShortcutAccent {
    if (eq_ascii_ci_sv(sv, "primary")) return ShortcutAccent::PRIMARY;
    if (eq_ascii_ci_sv(sv, "gold")) return ShortcutAccent::GOLD;
    if (eq_ascii_ci_sv(sv, "green")) return ShortcutAccent::GREEN;
    if (eq_ascii_ci_sv(sv, "red")) return ShortcutAccent::RED;
    if (eq_ascii_ci_sv(sv, "orange")) return ShortcutAccent::ORANGE;
    return ShortcutAccent::DEFAULT;
  };
  const ShortcutBadgeStyle parsed_badge = parse_badge_style(badge_sv);
  const ShortcutAccent parsed_accent = parse_accent(accent_sv);

  if (kind_sv == "route") {
    out_item.kind = ShortcutKind::ROUTE;
    if (target_sv == "app:settings") {
      out_item.route_id = RouteId::SETTINGS_ROOT;
      out_item.target = "app:settings";
      out_item.label = label_sv.empty() ? "Settings" : v3_unescape_subfield(label_sv);
    } else {
      const auto *spec = find_shortcut_route(target_sv);
      if (spec == nullptr) return false;
      out_item.route_id = spec->id;
      out_item.target = spec->token;
      out_item.label = label_sv.empty() ? std::string(spec->label) : v3_unescape_subfield(label_sv);
    }
    out_item.icon = v3_unescape_subfield(icon_sv);
    out_item.confirm = false;
    out_item.state = -1;
    out_item.badge_style = parsed_badge;
    out_item.accent = parsed_accent;
    out_item.page = parsed_page;
    out_item.col = parsed_col;
    out_item.row = parsed_row;
    out_item.col_span = parsed_col_span;
    out_item.row_span = parsed_row_span;
    return true;
  }

  if (kind_sv == "entity") {
    if (target_sv.empty() || target_sv.find('.') == std::string_view::npos || action_sv.empty()) {
      return false;
    }
    out_item.kind = ShortcutKind::ENTITY;
    out_item.route_id = RouteId::NONE;
    out_item.target = v3_unescape_subfield(target_sv);
    out_item.action = v3_unescape_subfield(action_sv);
    out_item.label = label_sv.empty() ? out_item.target : v3_unescape_subfield(label_sv);
    out_item.icon = v3_unescape_subfield(icon_sv);
    out_item.confirm = v3_parse_bool(confirm_sv);
    const int parsed_state = v3_parse_int(state_sv, -1);
    out_item.state = static_cast<int8_t>(std::clamp(parsed_state, -1, 2));
    out_item.badge_style = parsed_badge;
    out_item.accent = parsed_accent;
    out_item.page = parsed_page;
    out_item.col = parsed_col;
    out_item.row = parsed_row;
    out_item.col_span = parsed_col_span;
    out_item.row_span = parsed_row_span;
    return true;
  }

  return false;
}

void append_shortcut_records_blob(std::string_view blob,
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
bool parse_v3_shortcuts_payload(std::string_view payload, ShortcutsState &st) {
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
    st.staged_pinned_page_titles.clear();
    if (pinned_total > 0U) {
      st.staging_pinned_items.reserve(pinned_total);
    }
    append_shortcut_records_blob(f[static_cast<size_t>(ShortcutsField::DOCK_ITEMS)],
                                 st.staging_dock_items, ShortcutsState::kMaxDockItems, false);
    append_shortcut_records_blob(f[static_cast<size_t>(ShortcutsField::PINNED_ITEMS)],
                                 st.staging_pinned_items, ShortcutsState::kMaxPinnedItems, true);
    parse_pinned_page_titles_blob(f[static_cast<size_t>(ShortcutsField::PINNED_PAGE_TITLES)],
                                  st.staged_pinned_page_titles);
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
      st.staged_pinned_page_titles.clear();
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
  st.pinned_page_titles = std::move(st.staged_pinned_page_titles);
  st.staging_epoch = 0;
  st.staging_rev = 0;
  st.staging_chunk_count = 0;
  st.staging_next_chunk = 0;
  st.staging_pinned_total = 0;
  st.staging_dock_items.clear();
  st.staging_pinned_items.clear();
  st.staged_pinned_page_titles.clear();
  return true;
}

void build_shortcut_confirm_copy(const ShortcutItem &item,
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

int collect_effective_dock_items(ShortcutItem *out_items,
                                 int max_items,
                                 const ShortcutsState &st) {
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

}  // namespace latter_core
