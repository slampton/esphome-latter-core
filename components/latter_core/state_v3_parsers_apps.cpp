#if __has_include("state/device_runtime_state.h")
#include "state/device_runtime_state.h"
#include "latter_rpc.h"
#else
#include "../../include/state/device_runtime_state.h"
#include "../../include/latter_rpc.h"
#endif

namespace latter_ui {

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
  return true;
}

bool parse_v3_active_media_payload(std::string_view payload,
                                          MediaState &st,
                                   uint32_t now_ms) {
  return parse_v3_media_payload(payload, st, now_ms);
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

bool parse_shortcut_record(std::string_view rec, ShortcutItem &out_item) {
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

int parse_calendar_sources_meta(std::string_view raw,
                                       CalendarSourceMeta *out,
                                       int max_out) noexcept {
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
    copy_bounded_cstr(item.entity_id, v3_unescape_subfield(cols[0]));
    copy_bounded_cstr(item.label, v3_unescape_subfield(cols[1].empty() ? cols[0] : cols[1]));
    item.color_idx = static_cast<uint8_t>(std::clamp(cn >= 3 ? v3_parse_int(cols[2], count) : count, 0, 5));
    item.can_create = (cn >= 4) && v3_parse_bool(cols[3]);
    item.can_delete = (cn >= 5) && v3_parse_bool(cols[4]);
    out[count++] = item;
  }
  return count;
}

int collect_writable_calendar_sources(std::string_view raw,
                                             CalendarSourceMeta *out,
                                             int *out_source_indices,
                                             int max_out) noexcept {
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
        copy_bounded_cstr(item.entity_id, v3_unescape_subfield(cols[0]));
        copy_bounded_cstr(item.label, v3_unescape_subfield(cols[1].empty() ? cols[0] : cols[1]));
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

int parse_calendar_locations_catalog(std::string_view raw,
                                            std::string_view *out,
                                            int max_out) noexcept {
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


// Populates up to `kMaxCalendarSummaryPreviewEvents` preview cards from `calendar_state().summary`
// into PSRAM/heap scratch (`< 256 B` stack, zero mutation of `calendar_window_store()`).
int populate_calendar_summary_preview(
    std::string_view summary,
    int32_t today_ymd,
    CalendarWindowEvent *out_events,
    int max_out) noexcept {
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
    copy_bounded_cstr(dst.title, f[1].empty() ? std::string("Event") : v3_unescape_subfield(f[1]));
    copy_bounded_cstr(dst.location, v3_unescape_subfield(f[2]));
    ++count;
  }
  return count;
}

bool parse_calendar_window_event_record(std::string_view rec, CalendarWindowEvent &out) noexcept {
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

bool apply_calendar_view_action(int req_id,
                                       int chunk_index,
                                       int chunk_count,
                                       std::string_view window_start,
                                       std::string_view window_end,
                                       std::string_view events,
                                       CalendarWindowStore &store,
                                DeviceRuntimeState &rs) {
  if (!latter_rpc::accept_req_id(latter_rpc::SlotChannel::CALENDAR, req_id)) {
    return false;
  }
  if (req_id > 0 && store.last_applied_req_id > 0 && req_id < store.last_applied_req_id) {
    return false;
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

  if (req_id > store.last_applied_req_id) {
    store.last_applied_req_id = req_id;
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
  if (rs.cal.selected_event_idx >= store.count) {
    rs.cal.selected_event_idx = -1;
    if (rs.cal.drilldown == CalDrilldown::DETAIL) {
      rs.cal.drilldown = CalDrilldown::NONE;
      rs.cal.delete_confirm_open = false;
    }
  }
  store.rev++;
  return true;
}

// Parse Contract v3 packed browse/area slots ("name\x1fid\x1ftype\x1factive\x1flevel[\x1fqueue_item_id\x1fis_current]\x1e...")
size_t parse_v3_slots_payload(std::string_view slots_blob, BrowseSlotItem *out_slots, size_t max_slots) {
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

void append_music_lyrics_records_blob(std::string_view blob,
                                             std::vector<MusicLyricLine> &dst,
                                             size_t max_total) {
  const std::string_view trimmed = trim_ascii_ws(blob);
  if (trimmed.empty() || dst.size() >= max_total) return;

  // Canonical Protocol v3 record format: 0x1E-joined "<t_ms>\x1f<text>" lines.
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
}

bool apply_music_lyrics_action(int req_id,
                                      int chunk_index,
                                      int chunk_count,
                                      std::string_view track_key,
                                      bool synced,
                                      std::string_view lines_blob,
                                      MusicLyricsState &st) {
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


// Shared slot-response applier for both set_browse_slots and set_area_slots.
bool apply_slots_action(int req_id, std::string_view mode,
                               int page, int total_pages, std::string_view slots_blob,
                               uint32_t now_ms, bool force_area_entities,
                        DeviceRuntimeState &rt) {
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

void sync_multi_timer_globals(bool &out_any_running, bool &out_any_paused,
                                     int &out_min_rem_sec, int &out_active_dur_sec,
                                     uint32_t &out_min_target_ms, int view_slot) {
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


int upsert_voice_timer_slot(const std::string &id_str, const std::string &name_str,
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


void parse_latter_remote_slots_summary(const std::string &summary) {
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
      t_ents[idx] = v3_unescape_subfield(f[2]);
      t_names[idx] = f[1].empty() ? format_remote_entity_fallback(t_ents[idx]) : v3_unescape_subfield(f[1]);
    }
  }
  remote_control_page() = 0;
}

int collect_configured_remote_slots(int out_slots[MAX_REMOTE_SLOTS]) {
  const std::string *t_ents = remote_slots_ent();
  int count = 0;
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) {
      out_slots[count++] = i;
    }
  }
  return count;
}

int resolve_active_remote_slot(int active_slot) {
  const std::string *t_ents = remote_slots_ent();
  if (active_slot >= 0 && active_slot < MAX_REMOTE_SLOTS && is_configured_remote_slot(t_ents[active_slot])) {
    return active_slot;
  }
  for (int i = 0; i < MAX_REMOTE_SLOTS; i++) {
    if (is_configured_remote_slot(t_ents[i])) return i;
  }
  return 0;
}


std::string resolve_active_carousel_camera_area_id(const std::string &cameras_summary) {
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

std::string resolve_active_carousel_camera_id(const std::string &cameras_summary,
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

std::string build_carousel_camera_url(const std::string &base_cam_url,
                                             const std::string &cameras_summary,
                                             uint32_t now_ms) {
  if (base_cam_url.empty()) return "";
  const auto &rs = runtime_state();
  const std::string ent = resolve_active_carousel_camera_id(cameras_summary, base_cam_url);
  return build_camera_snapshot_url(
      base_cam_url, ent, rs.cameras.effective_fill(persisted_prefs()), cam_landscape_mode(), now_ms);
}

}  // namespace latter_ui
