#if __has_include("state/state_nvs_snapshot.h")
#include "state/state_nvs_snapshot.h"
#else
#include "../../include/state/state_nvs_snapshot.h"
#endif

namespace latter_ui {

bool parse_area_ambient_light(std::string_view raw, int8_t &out_pct, std::string &out_lux) {
  out_pct = -1;
  out_lux.clear();
  if (raw.empty()) return false;
  const size_t sep = raw.find('|');
  const std::string_view pct_sv = (sep == std::string_view::npos) ? raw : raw.substr(0, sep);
  if (pct_sv.empty()) return false;
  int parsed = 0;
  for (char ch : pct_sv) {
    if (ch < '0' || ch > '9') return false;
    parsed = parsed * 10 + (ch - '0');
    if (parsed > 100) return false;
  }
  out_pct = static_cast<int8_t>(parsed);
  if (sep != std::string_view::npos && sep + 1 < raw.size()) {
    const std::string_view lux_sv = utf8_safe_prefix(raw.substr(sep + 1), 12U);
    out_lux.assign(lux_sv.data(), lux_sv.size());
  }
  return true;
}

std::string normalize_tools_items_csv(std::string_view sv) {
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

std::string normalize_games_items_csv(std::string_view sv) {
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

bool load_persisted_prefs() {
  auto &st = device_prefs_state();
  st.loaded = true;
  st.nvs_save_pending = false;
#if LATTER_HAS_ESP_PREFS
  if (esphome::global_preferences == nullptr) return false;
  PersistedDevicePrefs loaded{};
  if (persisted_prefs_handle().load(&loaded) &&
      load_persisted_prefs_from_buffer(&loaded, sizeof(loaded), persisted_prefs())) {
    return true;
  }
#endif
  persisted_prefs() = PersistedDevicePrefs{};
  clamp_prefs_to_capabilities(persisted_prefs());
  return false;
}

void ensure_persisted_prefs_loaded() {
  if (!device_prefs_state().loaded) {
    load_persisted_prefs();
  }
}

bool save_persisted_prefs() {
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

bool flush_persisted_prefs_if_dirty() {
  if (!device_prefs_state().nvs_save_pending) return false;
  return save_persisted_prefs();
}

bool tick_persisted_prefs_flush(uint32_t now_ms, uint32_t debounce_ms) {
  auto &st = device_prefs_state();
  if (!st.nvs_save_pending) return false;
  if (static_cast<uint32_t>(now_ms - st.nvs_dirty_since_ms) < debounce_ms) return false;
  return save_persisted_prefs();
}

void seed_default_prefs_if_needed(
    std::string_view def_clock_face,
    std::string_view def_start_app,
    std::string_view def_screensaver_style,
    std::string_view def_art_mode,
    std::string_view def_album_sort,
    std::string_view def_artist_album_sort,
    std::string_view def_podcast_sort,
    PersistedDevicePrefs &p) {
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

const char *pref_key_name(PrefKey key) {
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
    case PrefKey::DO_NOT_DISTURB: return "do_not_disturb";
    case PrefKey::COUNT: break;
  }
  return "";
}

std::string get_pref_value_str(PrefKey key, const PersistedDevicePrefs &p) {
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
    case PrefKey::DO_NOT_DISTURB: return p.do_not_disturb ? "1" : "0";
    case PrefKey::COUNT: break;
  }
  return "";
}

bool set_pref_by_key_str(PersistedDevicePrefs &p, PrefKey key, std::string_view sv,
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
    case PrefKey::DO_NOT_DISTURB: {
      const bool chg = apply_bool(p.do_not_disturb, kPrefEffectNone);
      if (chg && &p == &persisted_prefs()) {
        device_prefs_state().do_not_disturb = p.do_not_disturb;
      }
      return chg;
    }
    case PrefKey::COUNT:
      break;
  }
  return false;
}

std::string encode_prefs_mask(PrefMask mask, const PersistedDevicePrefs &p) {
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

std::string format_dirty_prefs(const PersistedDevicePrefs &p) {
  return encode_prefs_mask(p.dirty_mask, p);
}

// On first-time adoption (prefs_seq == 0), advertise compiled board defaults (excluding
// ble_beacon_enabled, world_clocks, alarms_summary, local_clock_label, tools_items,
// games_items, bedtime_active, and do_not_disturb unless explicitly edited locally) so
// Latter seeds board-appropriate defaults without clobbering Studio-configured lists.
std::string build_hello_dirty_prefs(const PersistedDevicePrefs &p) {
  if (p.prefs_seq == 0) {
    constexpr PrefMask kAllMask = pref_mask_all(static_cast<uint8_t>(PrefKey::COUNT));
    constexpr PrefMask kExcludeUnlessDirty =
        pref_key_bit(PrefKey::BLE_BEACON_ENABLED) |
        pref_key_bit(PrefKey::WORLD_CLOCKS) |
        pref_key_bit(PrefKey::ALARMS_SUMMARY) |
        pref_key_bit(PrefKey::LOCAL_CLOCK_LABEL) |
        pref_key_bit(PrefKey::TOOLS_ITEMS) |
        pref_key_bit(PrefKey::GAMES_ITEMS) |
        pref_key_bit(PrefKey::BEDTIME_ACTIVE) |
        pref_key_bit(PrefKey::DO_NOT_DISTURB);
    const PrefMask seed_mask = (kAllMask & ~kExcludeUnlessDirty) | p.dirty_mask;
    return encode_prefs_mask(seed_mask, p);
  }
  return encode_prefs_mask(p.dirty_mask, p);
}

// Parse Protocol v3 device_prefs payload ("v3\x1f<epoch>\x1f<rev>\x1f<prefs_seq>\x1f...")
bool parse_v3_device_prefs_payload(std::string_view payload,
                                          uint32_t &out_effects,
                                          uint32_t now_ms,
                                   PersistedDevicePrefs &p,
                                   DevicePrefsState &st) {
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
  apply_f(DevicePrefsField::DO_NOT_DISTURB, PrefKey::DO_NOT_DISTURB);
  st.tools_items = std::string(effective_tools_items_sv(p));
  st.games_items = std::string(effective_games_items_sv(p));
  st.weather_app_background = p.weather_app_background;
  st.do_not_disturb = p.do_not_disturb;

  const std::string_view sim_sv = f[static_cast<size_t>(DevicePrefsField::WEATHER_SIM_CONDITION)];
  if (!sim_sv.empty()) {
    int sim_idx = find_opt_index(kWeatherSimOpts, sim_sv);
    if (sim_idx >= 0 && st.weather_sim_condition != static_cast<uint8_t>(sim_idx)) {
      st.weather_sim_condition = static_cast<uint8_t>(sim_idx);
      out_effects |= kPrefEffectRedraw;
    }
  }

  bool theme_rgb_changed = false;
  const std::string_view hex_sv = f[static_cast<size_t>(DevicePrefsField::THEME_COLOR_HEX)];
  if (hex_sv.size() == 7 && hex_sv[0] == '#') {
    uint32_t new_rgb = parse_hex_rgb_u24(hex_sv, p.custom_theme_rgb);
    if (new_rgb != p.custom_theme_rgb) {
      p.custom_theme_rgb = new_rgb;
      theme_rgb_changed = true;
      if (p.color_profile == 4) {
        out_effects |= (kPrefEffectThemePreset | kPrefEffectRedraw);
      }
    }
  }

  const uint32_t in_seq = v3_parse_u32(f[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)], 0);
  bool meta_changed = false;
  if (in_seq > p.prefs_seq) {
    p.prefs_seq = in_seq;
    if (p.dirty_mask.any()) {
      p.dirty_mask.clear();
    }
    meta_changed = true;
  }

  if (changed_mask.any() || theme_rgb_changed || meta_changed) {
    schedule_persisted_prefs_save(now_ms);
  }
  return true;
}

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

}  // namespace latter_ui
