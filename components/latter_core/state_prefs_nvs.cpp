#if __has_include("state/state_nvs_snapshot.h")
#include "state/state_nvs_snapshot.h"
#include "state/device_runtime_state.h"
#else
#include "../../include/state/state_nvs_snapshot.h"
#include "../../include/state/device_runtime_state.h"
#endif

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif
#ifdef USE_ESP32
#include <nvs.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latter_core {

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
  std::string_view seen[7]{};
  int count = 0;
  size_t start = 0;
  while (start <= sv.size() && count < 7) {
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
  out.reserve(64);
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
#ifdef USE_ESP32
  nvs_handle_t nvs = 0;
  if (nvs_open(kPersistedPrefsNvsNamespace, NVS_READONLY, &nvs) == ESP_OK) {
    size_t required_size = 0;
    const esp_err_t sz_err = nvs_get_blob(nvs, kPersistedPrefsNvsKey, nullptr, &required_size);
    bool ok = false;
    if (sz_err == ESP_OK && required_size >= kPersistedPrefsMinSize && required_size <= 4096U) {
      if (required_size <= sizeof(PersistedDevicePrefs)) {
        alignas(PersistedDevicePrefs) uint8_t raw[sizeof(PersistedDevicePrefs)]{};
        size_t read_len = required_size;
        if (nvs_get_blob(nvs, kPersistedPrefsNvsKey, raw, &read_len) == ESP_OK) {
          ok = load_persisted_prefs_from_buffer(raw, read_len, persisted_prefs());
        }
      } else {
        std::vector<uint8_t> raw(required_size);
        size_t read_len = required_size;
        if (nvs_get_blob(nvs, kPersistedPrefsNvsKey, raw.data(), &read_len) == ESP_OK &&
            read_len >= sizeof(PersistedDevicePrefs)) {
          ok = load_persisted_prefs_from_buffer(raw.data(), sizeof(PersistedDevicePrefs), persisted_prefs());
        }
      }
    }
    nvs_close(nvs);
    if (ok) return true;
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
#ifdef USE_ESP32
  nvs_handle_t nvs = 0;
  if (nvs_open(kPersistedPrefsNvsNamespace, NVS_READWRITE, &nvs) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(nvs, kPersistedPrefsNvsKey, &p, sizeof(p));
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return err == ESP_OK;
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
    case PrefKey::MUSIC_AUTO_OPEN: return pref_music_auto_open_cstr(p);
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
    case PrefKey::GLASS_STYLE: return pref_glass_style_cstr(p);
    case PrefKey::HOME_SHOW_DATE: return p.home_show_date ? "1" : "0";
    case PrefKey::HOME_SHOW_TIME: return p.home_show_time ? "1" : "0";
    case PrefKey::HOME_SHOW_WEATHER: return p.home_show_weather ? "1" : "0";
    case PrefKey::HOME_WEATHER_TAP_ACTION: return pref_home_weather_tap_cstr(p);
    case PrefKey::HOME_SHOW_BATTERY: return p.home_show_battery ? "1" : "0";
    case PrefKey::COUNT: break;
  }
  return "";
}

bool set_pref_by_key_str(PersistedDevicePrefs &p, PrefKey key, std::string_view sv,
                         PrefMask &changed_mask, uint32_t &effects_mask) {
  if (sv.empty() &&
      key != PrefKey::WORLD_CLOCKS &&
      key != PrefKey::LOCAL_CLOCK_LABEL &&
      key != PrefKey::TOOLS_ITEMS &&
      key != PrefKey::GAMES_ITEMS) {
    return false;
  }
  auto apply_enum = [&]<size_t N>(uint8_t &slot, const std::array<std::string_view, N> &opts, uint32_t eff) -> bool {
    const int idx = find_pref_option_index(std::span<const std::string_view>{opts}, sv);
    if (idx < 0) return false;
    uint8_t u = static_cast<uint8_t>(idx);
    if (key == PrefKey::PICKUP_TO_WAKE && (!LATTER_HAS_IMU || LATTER_IS_STATIONARY)) u = 2;
    if (key == PrefKey::BATTERY_HIBERNATE_TIMEOUT && (!LATTER_HAS_BATTERY || LATTER_IS_STATIONARY)) u = 4;
    if (key == PrefKey::CLOCK_FACE && !LATTER_ROUND_DISPLAY && u == 1) u = 0;
    if (key == PrefKey::START_SCREEN && !p.home_screen_enabled && u == 0) u = kStartScreenTimeAppIdx;
    if (key == PrefKey::CAMERA_DOORBELL_POPUP && !LATTER_HAS_HW_JPEG) u = 0;
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
  auto apply_num = [&](uint8_t &slot, int32_t lo, int32_t hi, uint32_t eff) -> bool {
    int parsed = v3_parse_int(sv, -9999);
    if (parsed == -9999) return false;
    uint8_t cl = static_cast<uint8_t>(std::clamp<int32_t>(parsed, lo, hi));
    if (slot == cl) return false;
    slot = cl;
    changed_mask |= pref_key_bit(key);
    effects_mask |= (eff | kPrefEffectRedraw);
    return true;
  };

  switch (key) {
    case PrefKey::WAKE_WORD_MODE:
      return apply_enum(p.wake_word_mode, kPrefOptsWakeWordMode, kPrefEffectWakeWordSync);
    case PrefKey::TAP_TO_WAKE:
      return apply_enum(p.tap_to_wake, kPrefOptsTapToWake, kPrefEffectPowerPolicy);
    case PrefKey::PICKUP_TO_WAKE:
      return apply_enum(p.pickup_to_wake, kPrefOptsPickupToWake, kPrefEffectPowerPolicy);
    case PrefKey::BATTERY_SCREEN_TIMEOUT:
      return apply_enum(p.battery_screen_timeout, kPrefOptsBatteryScreenTimeout, kPrefEffectSleepTimer);
    case PrefKey::DOCKED_IDLE_TIMEOUT:
      return apply_enum(p.docked_idle_timeout, kPrefOptsDockedIdleTimeout, kPrefEffectSleepTimer);
    case PrefKey::BATTERY_HIBERNATE_TIMEOUT:
      return apply_enum(p.battery_hibernate_timeout, kPrefOptsBatteryHibernateTimeout, kPrefEffectPowerPolicy);
    case PrefKey::SCREENSAVER_MODE:
      return apply_enum(p.screensaver_mode, kPrefOptsScreensaverMode,
                        kPrefEffectSleepTimer | kPrefEffectScreensaverChange);
    case PrefKey::SCREENSAVER_STYLE:
      return apply_enum(p.screensaver_style, kPrefOptsScreensaverStyle, kPrefEffectScreensaverChange);
    case PrefKey::AUTO_BRIGHTNESS_MODE:
      return apply_enum(p.auto_brightness_mode, kPrefOptsAutoBrightnessMode, kPrefEffectBrightness);
    case PrefKey::MANUAL_BRIGHTNESS:
      return apply_num(p.manual_brightness, kPrefMinManualBrightness, kPrefMaxManualBrightness, kPrefEffectBrightness);
    case PrefKey::DAY_BRIGHTNESS:
      return apply_num(p.day_brightness, kPrefMinDayBrightness, kPrefMaxDayBrightness, kPrefEffectBrightness);
    case PrefKey::NIGHT_BRIGHTNESS:
      return apply_num(p.night_brightness, kPrefMinNightBrightness, kPrefMaxNightBrightness, kPrefEffectBrightness);
    case PrefKey::DAY_START_HOUR:
      return apply_num(p.day_start_hour, kPrefMinDayStartHour, kPrefMaxDayStartHour, kPrefEffectBrightness);
    case PrefKey::NIGHT_START_HOUR:
      return apply_num(p.night_start_hour, kPrefMinNightStartHour, kPrefMaxNightStartHour, kPrefEffectBrightness);
    case PrefKey::COLOR_PROFILE:
      return apply_enum(p.color_profile, kPrefOptsColorProfile, kPrefEffectThemePreset);
    case PrefKey::VA_VISUAL_PROFILE:
      return apply_enum(p.va_visual_profile, kPrefOptsVaVisualProfile, 0);
    case PrefKey::START_SCREEN:
      return apply_enum(p.start_screen, kPrefOptsStartScreen, kPrefEffectNavPolicy);
    case PrefKey::CLOCK_FACE:
      return apply_enum(p.clock_face, kPrefOptsClockFace, 0);
    case PrefKey::TIME_FORMAT:
      return apply_enum(p.time_format, kPrefOptsTimeFormat, 0);
    case PrefKey::WEATHER_BACKGROUND:
      return apply_bool(p.weather_background, kPrefEffectWeatherSky);
    case PrefKey::HOME_SUN:
      return apply_bool(p.home_sun, 0);
    case PrefKey::HOME_MOON:
      return apply_bool(p.home_moon, 0);
    case PrefKey::WAKE_GREETING:
      return apply_bool(p.wake_greeting, 0);
    case PrefKey::ALBUM_ART_STYLE:
      return apply_enum(p.album_art_style, kPrefOptsAlbumArtStyle, 0);
    case PrefKey::MUSIC_OVERLAY_STYLE:
      return apply_enum(p.music_overlay_style, kPrefOptsMusicOverlayStyle, 0);
    case PrefKey::MUSIC_AUTO_OPEN:
      return apply_enum(p.music_auto_open, kPrefOptsMusicAutoOpen, 0);
    case PrefKey::ALBUM_SORT_ORDER:
      return apply_enum(p.album_sort_order, kPrefOptsAlbumSortOrder, 0);
    case PrefKey::ARTIST_ALBUM_SORT_ORDER:
      return apply_enum(p.artist_album_sort_order, kPrefOptsArtistAlbumSortOrder, 0);
    case PrefKey::PODCAST_SORT_ORDER:
      return apply_enum(p.podcast_sort_order, kPrefOptsPodcastSortOrder, 0);
    case PrefKey::WW_SENS_OKAY_NABU:
      return apply_enum(p.ww_sens_okay_nabu, kPrefOptsWwSensOkayNabu, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_HEY_LUNA:
      return apply_enum(p.ww_sens_hey_luna, kPrefOptsWwSensHeyLuna, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_HEY_JARVIS:
      return apply_enum(p.ww_sens_hey_jarvis, kPrefOptsWwSensHeyJarvis, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_HEY_MYCROFT:
      return apply_enum(p.ww_sens_hey_mycroft, kPrefOptsWwSensHeyMycroft, kPrefEffectWakeWordSens);
    case PrefKey::WW_SENS_ALEXA:
      return apply_enum(p.ww_sens_alexa, kPrefOptsWwSensAlexa, kPrefEffectWakeWordSens);
    case PrefKey::WAKE_SCREEN_ON_ASSIST:
      return apply_bool(p.wake_screen_on_assist, 0);
    case PrefKey::ALERT_VOLUME:
      return apply_num(p.alert_volume, kPrefMinAlertVolume, kPrefMaxAlertVolume, 0);
    case PrefKey::VOLUME_STEP_SIZE:
      return apply_num(p.volume_step_size, kPrefMinVolumeStepSize, kPrefMaxVolumeStepSize, 0);
    case PrefKey::GAME_SOUND_EFFECTS:
      return apply_bool(p.game_sound_effects, 0);
    case PrefKey::BLE_BEACON_ENABLED:
      return apply_bool(p.ble_beacon_enabled, kPrefEffectPowerPolicy);
    case PrefKey::BLE_BEACON_MODE:
      return apply_enum(p.ble_beacon_mode, kPrefOptsBleBeaconMode, kPrefEffectPowerPolicy);
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
      return apply_enum(p.return_to_home_timeout, kPrefOptsReturnToHomeTimeout, 0);
    case PrefKey::HOME_CLOCK_TAP_ACTION:
      return apply_enum(p.home_clock_tap_action, kPrefOptsHomeClockTapAction, 0);
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
      changed_mask |= pref_key_bit(key);
      effects_mask |= (kPrefEffectNavPolicy | kPrefEffectRedraw);
      return true;
    }
    case PrefKey::GAMES_ITEMS: {
      const std::string norm = normalize_games_items_csv(sv);
      if (effective_games_items_sv(p) == norm && p.games_items[0] != '\0') return false;
      copy_bounded_cstr(p.games_items, norm);
      changed_mask |= pref_key_bit(key);
      effects_mask |= (kPrefEffectNavPolicy | kPrefEffectRedraw);
      return true;
    }
    case PrefKey::HOME_DATE_TAP_ACTION:
      return apply_enum(p.home_date_tap_action, kPrefOptsHomeDateTapAction, 0);
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
      return apply_enum(p.calendar_week_start, kPrefOptsCalendarWeekStart, 0);
    case PrefKey::BEDTIME_ACTIVE:
      return apply_bool(p.bedtime_active,
                        kPrefEffectBedtime | kPrefEffectBrightness | kPrefEffectSleepTimer);
    case PrefKey::BEDTIME_DISPLAY:
      return apply_enum(p.bedtime_display, kPrefOptsBedtimeDisplay,
                        kPrefEffectBedtime | kPrefEffectBrightness | kPrefEffectSleepTimer);
    case PrefKey::BEDTIME_SCHEDULE:
      return apply_bool(p.bedtime_schedule, kPrefEffectBedtime);
    case PrefKey::BEDTIME_START_HOUR:
      return apply_num(p.bedtime_start_hour, kPrefMinBedtimeStartHour, kPrefMaxBedtimeStartHour, kPrefEffectBedtime);
    case PrefKey::BEDTIME_END_HOUR:
      return apply_num(p.bedtime_end_hour, kPrefMinBedtimeEndHour, kPrefMaxBedtimeEndHour, kPrefEffectBedtime);
    case PrefKey::BEDTIME_BRIGHTNESS:
      return apply_num(p.bedtime_brightness, kPrefMinBedtimeBrightness, kPrefMaxBedtimeBrightness, kPrefEffectBrightness);
    case PrefKey::CAMERA_DEFAULT_VIEW:
      return apply_enum(p.camera_default_view, kPrefOptsCameraDefaultView, 0);
    case PrefKey::CAMERA_CAROUSEL_INTERVAL:
      return apply_enum(p.camera_carousel_interval, kPrefOptsCameraCarouselInterval, 0);
    case PrefKey::CAMERA_FRAMING:
      return apply_enum(p.camera_framing, kPrefOptsCameraFraming, 0);
    case PrefKey::CAMERA_STREAM_MODE:
      return apply_enum(p.camera_stream_mode, kPrefOptsCameraStreamMode, 0);
    case PrefKey::CAMERA_DOORBELL_POPUP:
      return apply_enum(p.camera_doorbell_popup, kPrefOptsCameraDoorbellPopup, 0);
    case PrefKey::SCREENSAVER_PHOTO_CLOCK:
      return apply_bool(p.screensaver_photo_clock, kPrefEffectScreensaverChange);
    case PrefKey::WEATHER_APP_BACKGROUND:
      return apply_bool(p.weather_app_background, kPrefEffectWeatherSky);
    case PrefKey::DO_NOT_DISTURB:
      return apply_bool(p.do_not_disturb, kPrefEffectNone);
    case PrefKey::GLASS_STYLE:
      return apply_enum(p.glass_style, kPrefOptsGlassStyle, 0);
    case PrefKey::HOME_SHOW_DATE:
      return apply_bool(p.home_show_date, 0);
    case PrefKey::HOME_SHOW_TIME:
      return apply_bool(p.home_show_time, 0);
    case PrefKey::HOME_SHOW_WEATHER:
      return apply_bool(p.home_show_weather, 0);
    case PrefKey::HOME_WEATHER_TAP_ACTION:
      return apply_enum(p.home_weather_tap_action, kPrefOptsHomeWeatherTapAction, 0);
    case PrefKey::HOME_SHOW_BATTERY:
      return apply_bool(p.home_show_battery, 0);
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

// Only emit locally modified dirty prefs during device_hello. Clean boots (prefs_seq == 0
// with an empty dirty_mask) return "" so hello stays compact and never dumps ~60 unchanged defaults.
std::string build_hello_dirty_prefs(const PersistedDevicePrefs &p) {
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

  const uint32_t in_seq = v3_parse_u32(f[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)], 0);
  const bool hello_in_flight = hello_sync_state().in_flight;
  const bool preserve_dirty = hello_in_flight || (in_seq <= p.prefs_seq);

  PrefMask changed_mask{};
  auto apply_f = [&](DevicePrefsField df, PrefKey pk) {
    if (preserve_dirty && !(p.dirty_mask & pref_key_bit(pk)).empty()) {
      return;
    }
    set_pref_by_key_str(p, pk, f[static_cast<size_t>(df)], changed_mask, out_effects);
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
  apply_f(DevicePrefsField::MUSIC_AUTO_OPEN, PrefKey::MUSIC_AUTO_OPEN);
  apply_f(DevicePrefsField::GLASS_STYLE, PrefKey::GLASS_STYLE);
  apply_f(DevicePrefsField::HOME_SHOW_DATE, PrefKey::HOME_SHOW_DATE);
  apply_f(DevicePrefsField::HOME_SHOW_TIME, PrefKey::HOME_SHOW_TIME);
  apply_f(DevicePrefsField::HOME_SHOW_WEATHER, PrefKey::HOME_SHOW_WEATHER);
  apply_f(DevicePrefsField::HOME_WEATHER_TAP_ACTION, PrefKey::HOME_WEATHER_TAP_ACTION);
  apply_f(DevicePrefsField::HOME_SHOW_BATTERY, PrefKey::HOME_SHOW_BATTERY);

  const std::string_view sim_sv = f[static_cast<size_t>(DevicePrefsField::WEATHER_SIM_CONDITION)];
  if (!sim_sv.empty()) {
    const int sim_idx = find_pref_option_index(std::span<const std::string_view>{kPrefOptsWeatherSimCondition}, sim_sv);
    if (sim_idx >= 0 && st.weather_sim_condition != static_cast<uint8_t>(sim_idx)) {
      st.weather_sim_condition = static_cast<uint8_t>(sim_idx);
      out_effects |= (kPrefEffectWeatherSky | kPrefEffectRedraw);
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

  bool meta_changed = false;
  if (in_seq > p.prefs_seq && (!hello_in_flight || p.dirty_mask.empty())) {
    p.prefs_seq = in_seq;
    if (!hello_in_flight && p.dirty_mask.any()) {
      p.dirty_mask.clear();
    }
    meta_changed = true;
  }

  if (changed_mask.any() || theme_rgb_changed || meta_changed) {
    schedule_persisted_prefs_save(now_ms);
  }
  return true;
}

}  // namespace latter_core
