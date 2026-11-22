// ==============================================================================
// Latter Touch Runtime Preference Accessors, Wake/Standby Tap Gates & Tick
// (components/latter_ui/touch_lifecycle.cpp)
// ==============================================================================

#include "touch_internal.h"

namespace latter_ui {

bool TouchEngineAccess::allow_active_touch_down(TouchGestureEngine &eng, int tx, int ty, uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &rt = eng.rt_;

  if (rs.nav.ota_in_progress) {
    return false;
  }

  if (rs.nav.is_hibernating) {
    if (rs.nav.hibernate_countdown_val > 0) {
      rs.nav.is_hibernating = false;
      rs.nav.hibernate_countdown_val = 0;
      note_wake_reason(WakeReason::TOUCH);
      rt.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    }
    return false;
  }

  if (rs.nav.display_power_state != 0 || !rt.panel_is_on) {
    const std::string_view tw_mode = rt.select_opt(UiSelectId::TAP_WAKE_MODE);
    if (!accept_screen_off_tap(tw_mode, rt.usb_connected, now_ms)) return false;
    note_wake_reason(WakeReason::TOUCH);
    rt.cmd(UiHostCmd::WAKE_TO_ACTIVE);
    return false;
  }

  if (now_ms - last_screen_wake_ms() < TOUCH_DEBOUNCE_WAKE) {
    if (latter_ui::profiler_enabled()) {
      LATTER_PROF_LOGI("[PROF:TOUCH_BLOCKED] x=%d y=%d page=%d reason=WAKE_DEBOUNCE",
                       tx, ty, rs.nav.gui_page);
    }
    return false;
  }

  if (now_ms - eng.last_touch_activity_ms_ > 1000U) {
    eng.last_touch_activity_ms_ = now_ms;
    display_lifecycle().note_activity(now_ms);
  }
  return true;
}

void TouchGestureEngine::tick(uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &pp = ui::pin_pad_state();
  if (pp.open && pp.last_activity_ms > 0U &&
      static_cast<uint32_t>(now_ms - pp.last_activity_ms) >= ui::PinPadModalState::kAutoDismissMs) {
    TouchEngineAccess::scrub_pin_and_pending(*this, rs, true);
    mark_dirty_full();
    rt_.schedule_disp();
  }
  auto &sc = shortcut_confirm_state();
  if (sc.open && sc.opened_ms > 0U &&
      static_cast<uint32_t>(now_ms - sc.opened_ms) >= ShortcutConfirmState::kAutoDismissMs) {
    sc.clear();
    mark_dirty_full();
    rt_.schedule_disp();
  }
  if (rs.nav.gui_page == PageId::MUSIC && !touch_gesture().active &&
      rs.music.takeover_interacted_ms > 0U) {
    const uint32_t idle_ms = (now_ms >= rs.music.takeover_interacted_ms)
                                 ? (now_ms - rs.music.takeover_interacted_ms)
                                 : 0U;
    if (rs.music.takeover == MusicTakeover::SCRUB && idle_ms >= 5000U) {
      rs.music.takeover = MusicTakeover::NONE;
      rs.music.hud_active = true;
      rs.music.hud_timer = now_ms;
      mark_dirty_full();
      rt_.schedule_disp();
    }
  }
  (void)try_commit_long_press_(now_ms);
}

std::string_view UiTouchRuntime::select_opt(UiSelectId sid) const noexcept {
  using S = UiSelectId;
  const auto &p = persisted_prefs();
  switch (sid) {
    case S::TAP_WAKE_MODE: return pref_tap_to_wake_cstr(p);
    case S::WEATHER_SIM_CONDITION: return pref_weather_sim_cstr();
    case S::MUSIC_OVERLAY_STYLE: return pref_music_overlay_style_cstr(p);
    case S::MUSIC_FULLSCREEN_ART_MODE: return pref_album_art_style_cstr(p);
    case S::MUSIC_ALBUM_SORT_ORDER: return pref_album_sort_order_cstr(p);
    case S::MUSIC_ARTIST_ALBUM_SORT_ORDER: return pref_artist_album_sort_order_cstr(p);
    case S::MUSIC_PODCAST_SORT_ORDER: return pref_podcast_sort_order_cstr(p);
    case S::VA_WAKE_WORD_MODE: return pref_wake_word_mode_cstr(p);
    case S::VA_VISUAL_PROFILE: return pref_va_visual_profile_cstr(p);
    case S::WW_SENS_OKAY_NABU: return pref_ww_sens_cstr(p.ww_sens_okay_nabu);
    case S::WW_SENS_HEY_LUNA: return pref_ww_sens_cstr(p.ww_sens_hey_luna);
    case S::WW_SENS_HEY_JARVIS: return pref_ww_sens_cstr(p.ww_sens_hey_jarvis);
    case S::WW_SENS_HEY_MYCROFT: return pref_ww_sens_cstr(p.ww_sens_hey_mycroft);
    case S::WW_SENS_ALEXA: return pref_ww_sens_cstr(p.ww_sens_alexa);
    case S::THEME_PALETTE: return pref_color_profile_cstr(p);
    case S::PICKUP_WAKE_MODE: return pref_pickup_to_wake_cstr(p);
    case S::DISPLAY_OFF_TIMEOUT: return pref_battery_screen_timeout_cstr(p);
    case S::SCREENSAVER_MODE: return pref_screensaver_mode_cstr(p);
    case S::SCREENSAVER_STYLE: return pref_screensaver_style_cstr(p);
    case S::SCREENSAVER_DOCKED_TIMEOUT: return pref_docked_idle_timeout_cstr(p);
    case S::HIBERNATE_TIMEOUT_MODE: return pref_battery_hibernate_timeout_cstr(p);
    case S::DEFAULT_START_APP: return pref_start_screen_cstr(p);
    case S::CLOCK_FACE_STYLE: return pref_clock_face_cstr(p);
    case S::TIME_FORMAT: return pref_time_format_cstr(p);
    case S::HOME_CLOCK_TAP_ACTION: return pref_home_clock_tap_cstr(p);
    case S::HOME_DATE_TAP_ACTION: return pref_home_date_tap_cstr(p);
    case S::AUTO_BRIGHTNESS_MODE: return pref_auto_brightness_mode_cstr(p);
    case S::BEDTIME_DISPLAY: return pref_bedtime_display_cstr(p);
    case S::CAMERA_DEFAULT_VIEW: return pref_camera_default_view_cstr(p);
    case S::CAMERA_CAROUSEL_INTERVAL: return pref_camera_carousel_interval_cstr(p);
    case S::CAMERA_FRAMING: return pref_camera_framing_cstr(p);
    case S::CAMERA_STREAM_MODE: return pref_camera_stream_mode_cstr(p);
    case S::CAMERA_DOORBELL_POPUP: return pref_camera_doorbell_popup_cstr(p);
  }
  return {};
}

void UiTouchRuntime::set_select_opt(UiSelectId sid, std::string_view opt, bool /*pub*/) const {
  using S = UiSelectId;
  using K = PrefKey;
  const uint32_t now_ms = touch_detail::now_or_hal_ms(last_user_touch_ms());
  if (sid == S::WEATHER_SIM_CONDITION) {
    const int idx = find_opt_index(kWeatherSimOpts, opt);
    if (idx >= 0) {
      device_prefs_state().weather_sim_condition = static_cast<uint8_t>(idx);
      if (opt != "Live (Home Assistant)" && !persisted_prefs().weather_background) {
        uint32_t bg_eff = 0U;
        latter_actions::record_local_pref_edit(K::WEATHER_BACKGROUND, "1", now_ms, bg_eff);
      }
      if (link_state().ha_connected) {
        std::string chg = "weather_sim_condition=";
        chg.append(opt.data(), opt.size());
        latter_actions::emit_set_device_prefs(persisted_prefs().prefs_seq, chg);
      }
      night_meteor_epoch_initialized() = false;
      weather_sim_dirty() = true;
      record_ambient_frame_ms(0U);
      cmd(UiHostCmd::NOTIFY_WEATHER_SIM_CHANGED);
    }
    return;
  }
  K pk = K::COUNT;
  switch (sid) {
    case S::TAP_WAKE_MODE: pk = K::TAP_TO_WAKE; break;
    case S::WEATHER_SIM_CONDITION: break;
    case S::MUSIC_OVERLAY_STYLE: pk = K::MUSIC_OVERLAY_STYLE; break;
    case S::MUSIC_FULLSCREEN_ART_MODE: pk = K::ALBUM_ART_STYLE; break;
    case S::MUSIC_ALBUM_SORT_ORDER: pk = K::ALBUM_SORT_ORDER; break;
    case S::MUSIC_ARTIST_ALBUM_SORT_ORDER: pk = K::ARTIST_ALBUM_SORT_ORDER; break;
    case S::MUSIC_PODCAST_SORT_ORDER: pk = K::PODCAST_SORT_ORDER; break;
    case S::VA_WAKE_WORD_MODE: pk = K::WAKE_WORD_MODE; break;
    case S::VA_VISUAL_PROFILE: pk = K::VA_VISUAL_PROFILE; break;
    case S::WW_SENS_OKAY_NABU: pk = K::WW_SENS_OKAY_NABU; break;
    case S::WW_SENS_HEY_LUNA: pk = K::WW_SENS_HEY_LUNA; break;
    case S::WW_SENS_HEY_JARVIS: pk = K::WW_SENS_HEY_JARVIS; break;
    case S::WW_SENS_HEY_MYCROFT: pk = K::WW_SENS_HEY_MYCROFT; break;
    case S::WW_SENS_ALEXA: pk = K::WW_SENS_ALEXA; break;
    case S::THEME_PALETTE: pk = K::COLOR_PROFILE; break;
    case S::PICKUP_WAKE_MODE: pk = K::PICKUP_TO_WAKE; break;
    case S::DISPLAY_OFF_TIMEOUT: pk = K::BATTERY_SCREEN_TIMEOUT; break;
    case S::SCREENSAVER_MODE: pk = K::SCREENSAVER_MODE; break;
    case S::SCREENSAVER_STYLE: pk = K::SCREENSAVER_STYLE; break;
    case S::SCREENSAVER_DOCKED_TIMEOUT: pk = K::DOCKED_IDLE_TIMEOUT; break;
    case S::HIBERNATE_TIMEOUT_MODE: pk = K::BATTERY_HIBERNATE_TIMEOUT; break;
    case S::DEFAULT_START_APP: pk = K::START_SCREEN; break;
    case S::CLOCK_FACE_STYLE: pk = K::CLOCK_FACE; break;
    case S::TIME_FORMAT: pk = K::TIME_FORMAT; break;
    case S::HOME_CLOCK_TAP_ACTION: pk = K::HOME_CLOCK_TAP_ACTION; break;
    case S::HOME_DATE_TAP_ACTION: pk = K::HOME_DATE_TAP_ACTION; break;
    case S::AUTO_BRIGHTNESS_MODE: pk = K::AUTO_BRIGHTNESS_MODE; break;
    case S::BEDTIME_DISPLAY: pk = K::BEDTIME_DISPLAY; break;
    case S::CAMERA_DEFAULT_VIEW: pk = K::CAMERA_DEFAULT_VIEW; break;
    case S::CAMERA_CAROUSEL_INTERVAL: pk = K::CAMERA_CAROUSEL_INTERVAL; break;
    case S::CAMERA_FRAMING: pk = K::CAMERA_FRAMING; break;
    case S::CAMERA_STREAM_MODE: pk = K::CAMERA_STREAM_MODE; break;
    case S::CAMERA_DOORBELL_POPUP: pk = K::CAMERA_DOORBELL_POPUP; break;
  }
  if (pk == K::COUNT) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, opt, now_ms, eff)) {
    device_prefs_state().last_effects |= eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

bool UiTouchRuntime::sw_state(UiSwitchId swid) const noexcept {
  using W = UiSwitchId;
  const auto &p = persisted_prefs();
  switch (swid) {
    case W::HOME_WEATHER_BG: return p.weather_background;
    case W::WEATHER_APP_BG: return p.weather_app_background;
    case W::HOME_WAKE_GREETING: return p.wake_greeting;
    case W::BLE_AREA_BEACON: return p.ble_beacon_enabled;
    case W::GAME_SOUND: return p.game_sound_effects;
    case W::VA_WAKE_SCREEN: return p.wake_screen_on_assist;
    case W::SCREENSAVER_PHOTO_CLOCK: return p.screensaver_photo_clock;
    case W::BEDTIME_MODE: return p.bedtime_active;
    case W::DO_NOT_DISTURB: return p.do_not_disturb;
  }
  return false;
}

void UiTouchRuntime::sw_toggle(UiSwitchId swid) const {
  using W = UiSwitchId;
  using K = PrefKey;
  const auto &p = persisted_prefs();
  K pk = K::COUNT;
  bool next_val = false;
  switch (swid) {
    case W::HOME_WEATHER_BG: pk = K::WEATHER_BACKGROUND; next_val = !p.weather_background; break;
    case W::WEATHER_APP_BG: pk = K::WEATHER_APP_BACKGROUND; next_val = !p.weather_app_background; break;
    case W::HOME_WAKE_GREETING: pk = K::WAKE_GREETING; next_val = !p.wake_greeting; break;
    case W::BLE_AREA_BEACON: pk = K::BLE_BEACON_ENABLED; next_val = !p.ble_beacon_enabled; break;
    case W::GAME_SOUND: pk = K::GAME_SOUND_EFFECTS; next_val = !p.game_sound_effects; break;
    case W::VA_WAKE_SCREEN: pk = K::WAKE_SCREEN_ON_ASSIST; next_val = !p.wake_screen_on_assist; break;
    case W::SCREENSAVER_PHOTO_CLOCK: pk = K::SCREENSAVER_PHOTO_CLOCK; next_val = !p.screensaver_photo_clock; break;
    case W::BEDTIME_MODE: pk = K::BEDTIME_ACTIVE; next_val = !p.bedtime_active; break;
    case W::DO_NOT_DISTURB: pk = K::DO_NOT_DISTURB; next_val = !p.do_not_disturb; break;
  }
  if (pk == K::COUNT) return;
  const uint32_t now_ms = touch_detail::now_or_hal_ms(last_user_touch_ms());
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, next_val ? "1" : "0", now_ms, eff)) {
    device_prefs_state().last_effects |= eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

float UiTouchRuntime::num_val(UiNumberId nid) const noexcept {
  using N = UiNumberId;
  const auto &p = persisted_prefs();
  switch (nid) {
    case N::VOLUME_STEP_SIZE: return static_cast<float>(p.volume_step_size);
    case N::SPEAKER_VOLUME:
      if (!std::isnan(local_mp_volume) && local_mp_volume >= 0.0f && local_mp_volume <= 1.0f) {
        return std::round(local_mp_volume * 100.0f);
      }
      return static_cast<float>(p.speaker_volume_pct);
    case N::ALERT_VOLUME: return static_cast<float>(p.alert_volume);
    case N::MANUAL_BRIGHTNESS: return static_cast<float>(p.manual_brightness_pct());
    case N::DAY_BRIGHTNESS: return static_cast<float>(p.day_brightness_pct());
    case N::NIGHT_BRIGHTNESS: return static_cast<float>(p.night_brightness_pct());
    case N::BEDTIME_BRIGHTNESS: return static_cast<float>(p.bedtime_brightness_pct());
    case N::DAY_START_HOUR: return static_cast<float>(p.day_start_hour);
    case N::NIGHT_START_HOUR: return static_cast<float>(p.night_start_hour);
  }
  return 0.0f;
}

void UiTouchRuntime::set_num_val(UiNumberId nid, float val) const {
  using N = UiNumberId;
  using K = PrefKey;
  const uint32_t now_ms = touch_detail::now_or_hal_ms(last_user_touch_ms());
  if (nid == N::SPEAKER_VOLUME) {
    const int cl_pct = std::clamp(static_cast<int>(std::lround(val)), 10, 100);
    auto &p = persisted_prefs();
    if (p.speaker_volume_pct != static_cast<uint8_t>(cl_pct)) {
      p.speaker_volume_pct = static_cast<uint8_t>(cl_pct);
      schedule_persisted_prefs_save(now_ms);
    }
    local_mp_volume = static_cast<float>(cl_pct) * 0.01f;
    cmd(UiHostCmd::SET_LOCAL_SPEAKER_VOL_PCT, cl_pct);
    return;
  }
  K pk = K::COUNT;
  int cl_val = static_cast<int>(std::lround(val));
  switch (nid) {
    case N::VOLUME_STEP_SIZE: pk = K::VOLUME_STEP_SIZE; cl_val = std::clamp(cl_val, 1, 10); break;
    case N::SPEAKER_VOLUME: break;
    case N::ALERT_VOLUME: pk = K::ALERT_VOLUME; cl_val = std::clamp(cl_val, 20, 100); break;
    case N::MANUAL_BRIGHTNESS: pk = K::MANUAL_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::DAY_BRIGHTNESS: pk = K::DAY_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::NIGHT_BRIGHTNESS: pk = K::NIGHT_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::BEDTIME_BRIGHTNESS: pk = K::BEDTIME_BRIGHTNESS; cl_val = std::clamp(cl_val, 1, 100); break;
    case N::DAY_START_HOUR: pk = K::DAY_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
    case N::NIGHT_START_HOUR: pk = K::NIGHT_START_HOUR; cl_val = std::clamp(cl_val, 0, 23); break;
  }
  if (pk == K::COUNT) return;
  char buf[16];
  const int n = std::snprintf(buf, sizeof(buf), "%d", cl_val);
  if (n <= 0) return;
  uint32_t eff = 0U;
  if (latter_actions::record_local_pref_edit(pk, std::string_view(buf, static_cast<size_t>(n)), now_ms, eff)) {
    device_prefs_state().last_effects |= eff;
    cmd(UiHostCmd::APPLY_DEVICE_PREFS_EFFECTS);
  }
}

}  // namespace latter_ui
