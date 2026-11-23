#if __has_include("state/state_nvs_snapshot.h")
#include "state/state_nvs_snapshot.h"
#include "state/device_runtime_state.h"
#include "latter_rpc.h"
#else
#include "../../include/state/state_nvs_snapshot.h"
#include "../../include/state/device_runtime_state.h"
#include "../../include/latter_rpc.h"
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

void seed_default_prefs_if_needed(PersistedDevicePrefs &p) {
  if (!p.defaults_seeded) {
    p.defaults_seeded = true;
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
