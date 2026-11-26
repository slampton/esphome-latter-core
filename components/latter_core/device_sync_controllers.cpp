#if __has_include("state/device_runtime_state.h")
#include "state/device_runtime_state.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_link_report.h"
#include "hal_sleep_pm.h"
#include "hal_video_stream.h"
#include "latter_actions.h"
#include "latter_prof.h"
#else
#include "../../include/state/device_runtime_state.h"
#include "../../include/audio_router.h"
#include "../../include/hal_display_lifecycle.h"
#include "../../include/hal_link_report.h"
#include "../../include/hal_sleep_pm.h"
#include "../../include/hal_video_stream.h"
#include "../../include/latter_actions.h"
#include "../../include/latter_prof.h"
#endif

#include <algorithm>
#include <cmath>
#include <string_view>

namespace latter_core {

DeviceCommandDispatch evaluate_device_command(std::string_view sv_cmd,
                                              uint32_t now_ms,
                                              bool panel_is_on,
                                              bool ble_active,
                                              const char *device_name,
                                              int screen_w,
                                              int screen_h) {
  DeviceCommandDispatch out{};
  auto &r = runtime_state();

  if (sv_cmd == "restart") {
    out.restart = true;
  } else if (sv_cmd == "find_my") {
    out.toggle_find_my = true;
  } else if (sv_cmd == "activate_screensaver") {
    out.activate_screensaver = true;
  } else if (sv_cmd == "deactivate_screensaver") {
    r.nav.last_pub_display_power_state = -1;
    if (r.nav.display_power_state != 0) {
      note_wake_reason(WakeReason::NOTIFY);
      out.wake_to_active = true;
    } else {
      out.publish_display_state = true;
    }
  } else if (sv_cmd == "wake_screen") {
    r.nav.last_pub_display_power_state = -1;
    if (r.nav.display_power_state != 0 || !panel_is_on) {
      note_wake_reason(WakeReason::NOTIFY);
      out.wake_to_active = true;
    } else {
      out.sync_brightness = true;
      out.publish_display_state = true;
    }
  } else if (sv_cmd == "screen_off") {
    r.nav.last_pub_display_power_state = -1;
    out.turn_screen_off = true;
  } else if (sv_cmd.starts_with("set_screen_bri:")) {
    bool need_wake = false;
    if (apply_remote_screen_brightness_cmd(sv_cmd, now_ms, panel_is_on, need_wake)) {
      if (need_wake) {
        note_wake_reason(WakeReason::NOTIFY);
        out.wake_to_active = true;
      } else {
        out.sync_brightness = true;
      }
      out.publish_display_state = true;
    }
  } else if (sv_cmd == "set_profiler_on") {
    profiler_start(now_ms);
    out.set_ble_prof_log = true;
    out.ble_prof_log_val = true;
    reset_flight_recorder_window(now_ms);
    LATTER_PROF_LOGI("[PROF:START] Telemetry Profiler on for '%s' (screen=%dx%d, auto_off=24h)",
                     (device_name != nullptr) ? device_name : "", screen_w, screen_h);
    log_boot_summary();
    (void)link_report_log_line(now_ms, last_reset_reason());
    log_heap_summary();
    log_power_and_mem_profile(
        "PROFILER_ON", r.nav.display_power_state, r.hw_usb_connected, r.hw_batt_charging,
        r.hw_batt_level, r.hw_batt_voltage, r.audio.speaker_enabled(),
        r.audio.mww_running(), r.voice.active,
        r.audio.local_mp_playing(),
        ble_active, r.nav.active_game, r.nav.sleep_start_time);
    out.publish_device_telemetry = true;
  } else if (sv_cmd == "set_profiler_off") {
    LATTER_PROF_LOGI("[PROF:STOP] Telemetry Profiler off");
    profiler_stop();
    out.set_ble_prof_log = true;
    out.ble_prof_log_val = false;
    out.publish_device_telemetry = true;
  } else if (sv_cmd == "request_telemetry") {
    r.nav.last_pub_display_power_state = -1;
    r.nav.last_pub_display_bri_pct = -1;
    out.publish_device_telemetry = true;
    out.publish_display_state = true;
  } else if (sv_cmd == "trigger_va") {
    out.start_va = true;
  } else if (sv_cmd == "dismiss_ringing") {
    if (r.alert.active) {
      out.dismiss_notification = true;
    }
    const auto eff = handle_device_cmd_dismiss_ringing(now_ms);
    out.stop_alarm_sound = eff.stop_sound;
    out.sync_brightness = eff.sync_brightness;
    out.apply_power_policy = eff.apply_power;
    display_lifecycle().note_activity(now_ms);
    if (r.nav.display_power_state != 2) {
      out.mark_dirty_full = eff.mark_dirty_full;
      out.schedule_disp = true;
    }
  } else if (sv_cmd == "cancel_timer") {
    const auto eff = handle_device_cmd_cancel_timer(now_ms);
    out.stop_alarm_sound = eff.stop_sound;
    out.sync_brightness = eff.sync_brightness;
    out.apply_power_policy = eff.apply_power;
    if (r.nav.display_power_state != 2) {
      out.mark_dirty_full = eff.mark_dirty_full;
      out.schedule_disp = true;
    }
  } else if (sv_cmd == "reset_stopwatch") {
    const auto eff = handle_device_cmd_reset_stopwatch(now_ms);
    out.apply_power_policy = eff.apply_power;
    if (r.nav.display_power_state != 2) {
      out.mark_dirty_full = eff.mark_dirty_full;
      out.schedule_disp = true;
    }
  } else if ((sv_cmd.size() > 4U && sv_cmd.starts_with("nav:")) ||
             (sv_cmd.size() > 11U && sv_cmd.starts_with("nav_silent:")) ||
             (sv_cmd.size() > 9U && sv_cmd.starts_with("showcase:"))) {
    out.forward_ui_cmd = true;
  }

  return out;
}

DevicePrefsEffectsPlan evaluate_device_prefs_effects(uint32_t now_ms,
                                                     uint32_t last_touch_ms,
                                                     bool on_anchor_root) {
  DevicePrefsEffectsPlan plan{};
  const uint32_t eff = device_prefs_state().last_effects;
  device_prefs_state().last_effects = 0U;
  auto &rs = runtime_state();
  const auto &p = persisted_prefs();

  if (eff & kPrefEffectWakeWordSync) {
    plan.sync_wake_word = true;
    if (std::string_view(pref_wake_word_mode_cstr(p)) == "Disabled" &&
        !rs.audio.local_mp_playing() && !rs.voice.active) {
      plan.media_idle_finish = true;
    }
  }
  if (eff & kPrefEffectWakeWordSens) {
    plan.apply_ww_sens = true;
  }
  if (eff & kPrefEffectThemePreset) {
    plan.apply_theme_preset = true;
  }
  if (eff & kPrefEffectBrightness) {
    rs.nav.ambient_bri_override_pct = -1;
    plan.sync_display_brightness = true;
  }
  if (eff & kPrefEffectScreensaverChange) {
    if (rs.nav.display_power_state == 1 && !p.bedtime_active) {
      if (!screensaver_allowed_for_power(
              p.screensaver_mode, LATTER_IS_STATIONARY != 0, rs.hw_usb_connected)) {
#if LATTER_IS_STATIONARY
        plan.wake_to_active = true;
#else
        plan.turn_screen_off = true;
#endif
      } else {
        plan.apply_power_policy = true;
        plan.sync_display_brightness = true;
        if (std::string_view(effective_screensaver_style_cstr(p, rs, now_ms)) == "Photo Frame" &&
            !area_ctx_state().screensaver_photo_url.empty()) {
          rs.music.screensaver_photo_failed = false;
          plan.fetch_screensaver_photo = true;
        }
        plan.mark_dirty_full = true;
        plan.schedule_disp = true;
      }
    }
  }
  if (eff & kPrefEffectBedtime) {
    if (p.bedtime_active) {
      if (rs.nav.display_power_state == 1) {
        if (std::string_view(pref_bedtime_display_cstr(p)) == "Screen Off") {
          plan.turn_screen_off = true;
        } else {
          plan.sync_display_brightness = true;
          plan.mark_dirty_full = true;
          plan.schedule_disp = true;
        }
      } else if (rs.nav.display_power_state == 0 && !rs.nav.is_hibernating && !rs.nav.ota_in_progress) {
        plan.sync_display_brightness = true;
        const bool untouched_15s =
            (last_touch_ms == 0U) || (now_ms - last_touch_ms >= kBedtimeIdleTimeoutMs);
        const bool busy_now =
            rs.voice.active || rs.audio.va_running() || rs.voice.tts_received ||
            rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active ||
            rs.alert.active || rs.stopwatch.running || (rs.nav.active_game != 0) ||
            is_camera_view_active(rs) ||
            (rs.nav.gui_page == 8 && (rs.nav.tools_subpage == 5 || rs.nav.tools_subpage == 7));
        if (untouched_15s && !busy_now) {
          plan.enter_standby = true;
        } else {
          display_lifecycle().note_activity(now_ms);
        }
      }
    } else {
#if LATTER_IS_STATIONARY
      if (rs.nav.display_power_state != 0 && !rs.nav.ota_in_progress) {
        plan.wake_to_active = true;
      } else {
        plan.sync_display_brightness = true;
      }
#else
      if (rs.nav.display_power_state != 2 && !rs.nav.is_hibernating) {
        if (rs.nav.display_power_state == 1 &&
            !screensaver_allowed_for_power(
                p.screensaver_mode, LATTER_IS_STATIONARY != 0, rs.hw_usb_connected)) {
          plan.turn_screen_off = true;
        } else {
          plan.apply_power_policy = true;
          plan.sync_display_brightness = true;
          if (rs.nav.display_power_state == 1 &&
              std::string_view(effective_screensaver_style_cstr(p, rs, now_ms)) == "Photo Frame" &&
              !area_ctx_state().screensaver_photo_url.empty()) {
            rs.music.screensaver_photo_failed = false;
            plan.fetch_screensaver_photo = true;
          }
        }
      }
#endif
    }
  }
  if (eff & kPrefEffectSleepTimer) {
    if (rs.nav.display_power_state == 0) {
      display_lifecycle().note_activity(now_ms);
    }
  }
  if (eff & kPrefEffectPowerPolicy) {
    plan.apply_power_policy = true;
  }
  if (eff & kPrefEffectNavPolicy) {
    const bool cam_busy =
        cam_call_active() || cam_listen_active() || cam_intercom_ptt_active();
    if (!p.home_screen_enabled && !cam_busy && !on_anchor_root) {
      plan.navigate_to_start_screen = true;
    } else {
      plan.mark_dirty_full = true;
    }
  }
  if ((eff & kPrefEffectRedraw) && rs.nav.display_power_state != 2 && !rs.nav.is_hibernating) {
    if (is_camera_view_active(rs) || (rs.alert.active && rs.cameras.doorbell_live_popup_active)) {
      plan.sync_camera_stream = true;
    }
    if (eff & kPrefEffectWeatherSky) {
      plan.weather_sim_dirty = true;
    }
    if (rs.nav.display_power_state == 0) {
      plan.ensure_anim_poller = true;
    }
    plan.schedule_disp = true;
  }

  return plan;
}

DisplayBrightnessPlan evaluate_display_brightness_sync(uint32_t now_ms,
                                                       bool time_valid,
                                                       int hour,
                                                       bool photo_frame_art_active,
                                                       bool cur_panel_on,
                                                       float cur_panel_bri) {
  DisplayBrightnessPlan plan{};
  auto &rs = runtime_state();
  if (rs.nav.display_power_state == 0 && rs.nav.standby_entered_ms != 0U) {
    note_wake_from_standby(now_ms);
  } else if (rs.nav.display_power_state == 1 && rs.nav.standby_entered_ms == 0U) {
    rs.nav.standby_entered_ms = (now_ms != 0U) ? now_ms : 1U;
  }
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const bool smooth_ramp = rs.nav.ambient_bri_ramp_pending;
  rs.nav.ambient_bri_ramp_pending = false;
  const auto bri_tgt = resolve_active_brightness_target(
      prefs, weather_state().sun_state, time_valid, time_valid ? hour : 12,
      ac.area_ambient_pct, rs.nav.ambient_bri_override_pct);
  float b_target = static_cast<float>(bri_tgt.pct) * 0.01f;
  if ((rs.nav.gui_page == 8 && rs.nav.tools_subpage == 5) || rs.alarms.find_my_active) {
    b_target = 1.00f;
  }
  if (rs.nav.display_power_state == 0 || rs.nav.display_power_state == 1) {
    const float target_bri = resolve_effective_panel_brightness(
        rs, prefs, b_target, photo_frame_art_active, now_ms);
    if (!cur_panel_on || std::fabs(cur_panel_bri - target_bri) >= 0.005f) {
      plan.apply_call = true;
      plan.target_bri = target_bri;
      plan.transition_ms = (smooth_ramp && cur_panel_on && rs.nav.display_power_state == 0) ? 600U : 0U;
    }
  }
  return plan;
}

bool publish_display_state_if_changed(bool time_valid, int hour) {
  if (!link_state().ha_connected) return false;
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto bri_tgt = resolve_active_brightness_target(
      prefs, weather_state().sun_state, time_valid, time_valid ? hour : 12,
      area_ctx_state().area_ambient_pct, rs.nav.ambient_bri_override_pct);
  const int pwr = std::clamp<int>(rs.nav.display_power_state, 0, 2);
  const int bri_pct = std::clamp<int>(bri_tgt.pct, 1, 100);
  if (pwr == rs.nav.last_pub_display_power_state &&
      bri_pct == rs.nav.last_pub_display_bri_pct) {
    return false;
  }
  rs.nav.last_pub_display_power_state = pwr;
  rs.nav.last_pub_display_bri_pct = bri_pct;
  emit_display_state(pwr, bri_pct, pwr == 1);
  return true;
}

bool dismiss_active_alert_banner(uint32_t now_ms) {
  auto &rs = runtime_state();
  auto &ab = rs.alert;
  ab.active = false;
  ab.camera_url.clear();
  ab.camera_entity.clear();
  ab.loaded_camera_url.clear();
  rs.cameras.doorbell_live_popup_active = false;
  if (!is_camera_view_active(rs)) {
    video_client().stop();
  }
  const bool resume_music =
      rs.voice.resume_music_after && !rs.voice.active && !rs.voice.tts_received &&
      !rs.alarms.ringing && !rs.timers.ringing && !rs.alarms.find_my_active;
  audio_arbitrator().arm_media_idle_debounce(now_ms);
  display_lifecycle().note_activity(now_ms);
  return resume_music;
}

void commit_menu_scroll_target() {
  auto &rs = runtime_state();
  rs.nav.menu_scroll_active = false;
  if (rs.nav.gui_page == 4 && rs.nav.system_menu_mode == 1) {
    rs.nav.sleep_menu_page = rs.nav.menu_scroll_target_page;
  } else if (rs.nav.gui_page == 4 && rs.nav.system_menu_mode == 2) {
    rs.nav.display_menu_page = rs.nav.menu_scroll_target_page;
  } else if (rs.nav.gui_page == 4 && rs.nav.system_menu_mode == 0) {
    const int s_pgs = std::max(1, system_dashboard_page_count());
    rs.nav.system_dashboard_page = std::clamp(rs.nav.menu_scroll_target_page, 0, s_pgs - 1);
  } else if (rs.nav.gui_page == 2 && rs.music.view == MusicView::SETTINGS) {
    rs.music.settings_page = rs.nav.menu_scroll_target_page;
  } else if (rs.nav.gui_page == 3 && rs.nav.active_game == 0) {
    const int g_cnt = active_games_launcher_count(persisted_prefs());
    rs.nav.game_carousel_idx =
        (g_cnt > 0) ? std::clamp(rs.nav.menu_scroll_target_page, 0, g_cnt - 1) : 0;
  } else if (rs.nav.gui_page == 8 && rs.nav.tools_subpage == 0) {
    const int t_pgs = std::max(1, active_tools_launcher_page_count(persisted_prefs()));
    rs.nav.tools_menu_page = std::clamp(rs.nav.menu_scroll_target_page, 0, t_pgs - 1);
  } else if (rs.nav.gui_page == PageId::PINNED) {
    const int p_pgs = std::max(1, pinned_total_pages());
    rs.nav.pinned_menu_page = std::clamp(rs.nav.menu_scroll_target_page, 0, p_pgs - 1);
  }
}

}  // namespace latter_core
