#include "ui_cadence.h"
#include "ui_compositor.h"
#include "ui_layout.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "hal_video_stream.h"

#include <algorithm>

namespace latter_ui {

namespace {

bool is_music_playing_for_compositor(const MediaState &ms, bool local_media_playing, uint32_t now_ms) {
  return is_music_playing_effective(!ms.is_local, local_media_playing, ms.state, now_ms) ||
         (ms.is_local && (local_media_playing || ms.playing ||
                          ms.playback_state == MediaPlaybackState::Playing || ms.state == "playing")) ||
         (!ms.is_local && (ms.playing || ms.playback_state == MediaPlaybackState::Playing));
}

bool is_music_lyrics_sync_imminent(uint32_t now_ms, bool local_media_playing) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::MUSIC ||
      rs.music.view != MusicView::LYRICS ||
      rs.music.drilldown != MusicDrilldown::NONE) {
    return false;
  }
  const auto &lyr = rs.music.lyrics;
  if (!lyr.synced || lyr.lines.empty() || now_ms < lyr.manual_override_until_ms) return false;
  const auto &ms = media_state();
  if (!is_music_playing_for_compositor(ms, local_media_playing, now_ms)) return false;
  const uint32_t pos_ms = ms.interpolated_position_ms(now_ms);
  return lyr.find_active_line(pos_ms + 280U) != lyr.active_line_idx;
}

}  // namespace

FrameCadenceController &frame_cadence() {
  static FrameCadenceController s_ctrl{};
  return s_ctrl;
}

void FrameCadenceController::reset() {
  prev_audio_game_ = 0;
  last_va_anim_ms_ = 0U;
  prev_tts_active_ = false;
  prev_pause_exp_pending_20ms_ = false;
  last_cond_eval_ms_ = 0U;
  last_sky_enabled_ = false;
  cached_has_motion_ = false;
  idle_anim_ticks_ = 0U;
  prev_home_slot_ = 0U;
  prev_pause_exp_pending_250ms_ = false;
  prev_disp_pwr_ = -1;
  prev_fw_ver_.clear();
}

Cadence20msActions FrameCadenceController::evaluate_20ms(const Cadence20msInputs &in) {
  Cadence20msActions out{};
  const uint32_t interactive_gap = interactive_gap_for(in.is_chip_p4, in.mww_running);

  // 1. Tools Suite (Stopwatch, Dice, Bubble Level, Coin Flip) & Time App Digital Stopwatch
  if (in.panel_on && in.display_power_state == 0 && !in.va_visible &&
      ((in.gui_page == 8 && in.tools_anim_active) || (in.gui_page == 10 && in.time_anim_active))) {
    out.anim_driving_disp = true;
    if (!in.render_in_flight && (in.now_ms - in.last_ambient_ms >= interactive_gap)) {
      out.record_ambient_ms = true;
      out.schedule_anim_full = true;
    }
  }

  // 1b. Standalone Clock Face smooth ~20 FPS mechanical second-hand sweep (power-aware)
  if (in.panel_on && in.display_power_state == 0 && in.gui_page == 0 &&
      in.clock_smooth_sweep && !in.va_visible) {
    out.anim_driving_disp = true;
    if (ambient_step_ready(in.now_ms, in.last_ambient_ms, in.last_disp_end_ms,
                           in.render_in_flight, in.is_chip_p4, in.speaker_audio_busy,
                           in.va_visible, in.ringing_or_critical,
                           !in.speaker_audio_busy && !in.is_chip_p4 && in.mww_running)) {
      out.record_ambient_ms = true;
      out.schedule_anim_full = true;
    }
  }

  // 2. Games audio & physics loop
  const int cur_audio_game = (in.gui_page == 3) ? in.active_game : 0;
  if (cur_audio_game != prev_audio_game_) {
    prev_audio_game_ = cur_audio_game;
    if (cur_audio_game > 0 && in.game_sfx_enabled) {
      out.enable_game_audio_path = true;
    }
  }
  if (cur_audio_game > 0 && in.pending_game_sfx > 0) {
    out.clear_pending_game_sfx = true;
    if (in.game_sfx_enabled) {
      out.play_game_sfx_id = in.pending_game_sfx;
    }
  }
  if (in.panel_on && in.display_power_state == 0 && cur_audio_game > 0 && !in.va_visible) {
    out.anim_driving_disp = true;
    if (!in.render_in_flight && (in.now_ms - in.last_ambient_ms >= interactive_gap)) {
      out.record_ambient_ms = true;
      out.schedule_anim_full = true;
    }
  }

  // 3. Dock slide (localized band), menu scroll, Find My radar, and ringing/critical alert pulse
  if (in.panel_on && in.display_power_state == 0 && !in.va_visible &&
      (in.interactive_slide || in.dock_anim_active || in.ringing_or_critical)) {
    out.anim_driving_disp = true;
    const uint32_t gap =
        (in.interactive_slide || in.dock_anim_active || in.find_my_active) ? interactive_gap : 120U;
    if (!in.render_in_flight && (in.now_ms - in.last_ambient_ms >= gap)) {
      out.record_ambient_ms = true;
      if (in.dock_anim_active && !in.interactive_slide && !in.ringing_or_critical &&
          in.dock_y1 > in.dock_y0) {
        out.band_y0 = in.dock_y0;
        out.band_y1 = in.dock_y1;
        out.schedule_anim_band = true;
      } else {
        out.schedule_anim_full = true;
      }
    }
  }

  // 4. Audio-aware Voice Assistant loop (reserves 1 quiet tick on TTS start so DAC unmutes cleanly)
  if (in.va_visible) {
    out.anim_driving_disp = true;
    if (in.tts_received != prev_tts_active_) {
      prev_tts_active_ = in.tts_received;
      last_va_anim_ms_ = in.now_ms;
      out.va_transition_holdoff = true;
      out.sync_disp_poller = true;
      out.sync_disp_poller_val = true;
      idle_anim_ticks_ = 0U;
      return out;
    }
    const uint32_t va_min_gap = (in.tts_received || in.tts_audio_active) ? 94U : 54U;
    if (!in.render_in_flight && (in.now_ms - last_va_anim_ms_ >= va_min_gap)) {
      last_va_anim_ms_ = in.now_ms;
      out.record_ambient_ms = true;
      out.schedule_anim_full = true;
    }
  }

  // 5. Home paused-music expiry & Atmospheric Motion Wallpaper (Home & Sky/Weather)
  const bool on_home_main = (in.gui_page == 1 && in.home_subpage == 0);
  const bool pause_exp_imminent = (in.pause_exp_due_ms > 0U && in.pause_exp_due_ms <= 120U);
  const bool pause_exp_crossed =
      (on_home_main && prev_pause_exp_pending_20ms_ && in.pause_exp_due_ms == 0U);
  prev_pause_exp_pending_20ms_ = (on_home_main && in.pause_exp_due_ms > 0U);

  if (in.panel_on && in.display_power_state == 0 && !in.va_visible &&
      ((in.pause_exp_due_ms > 0U && in.pause_exp_due_ms <= 25U) || pause_exp_crossed)) {
    if (in.has_act_band) {
      out.band_y0 = in.act_y0;
      out.band_y1 = in.act_y1;
      if (in.pause_exp_due_ms > 0U) {
        out.schedule_activity_rotate = true;
        out.activity_rotate_delay_ms = in.pause_exp_due_ms;
      } else {
        out.schedule_band_now = true;
      }
    } else if (pause_exp_crossed) {
      out.schedule_disp_now = true;
    }
  }

  const bool home_card_rotate_window =
      (on_home_main && in.home_rotate_due_ms > 0U &&
       (in.home_rotate_due_ms <= 120U ||
        (in.rotate_period_ms > 0U && (in.now_ms % in.rotate_period_ms) <= 220U)));
  const bool ui_overlay_animating =
      in.greet_fading || in.act_sliding || in.act_fade_settle || home_card_rotate_window ||
      pause_exp_imminent || pause_exp_crossed;
  const bool home_weather_active = on_home_main && (in.sky_enabled || ui_overlay_animating);
  const bool app_weather_active = (in.gui_page == 5 && in.sky_enabled);

  if (in.panel_on && in.display_power_state == 0 &&
      (home_weather_active || app_weather_active) && !in.va_visible) {
    if (in.motion_mag > 0.055f) {
      out.bump_user_touch_ms = true;
    }
    const bool touch_busy = in.touch_active || in.menu_scroll_active || in.dock_anim_active;
    if (!touch_busy && (in.sim_just_changed || ui_overlay_animating || in.sky_stepping)) {
      out.anim_driving_disp = true;
      if (in.greet_fading || in.act_sliding || in.act_fade_settle) {
        if (!in.greet_fading && !in.sky_stepping && in.has_act_band) {
          out.band_y0 = in.act_y0;
          out.band_y1 = in.act_y1;
          out.schedule_anim_band = true;
        } else {
          out.schedule_anim_full = true;
        }
      } else if (in.sim_just_changed ||
                 (!in.weather_step_pending &&
                  ambient_step_ready(in.now_ms, in.last_ambient_ms, in.last_disp_end_ms,
                                     in.render_in_flight, in.is_chip_p4, in.speaker_audio_busy,
                                     in.va_visible, in.ringing_or_critical,
                                     !in.speaker_audio_busy && !in.is_chip_p4 && in.mww_running))) {
        out.record_ambient_ms = true;
        out.schedule_anim_full = true;
      }
    }
  }

  // 6. Live MJPEG video streaming: keep 20 ms anim poller alive
  if (in.panel_on && in.display_power_state == 0 && in.video_stream_active) {
    out.anim_driving_disp = true;
  }

  if (in.display_power_state == 0 || in.va_visible) {
    out.sync_disp_poller = true;
    out.sync_disp_poller_val = out.anim_driving_disp;
  }

  const bool keep_anim_warm =
      out.anim_driving_disp || in.va_running || in.tts_received ||
      (in.display_power_state == 0 && in.touch_active);
  if (keep_anim_warm) {
    idle_anim_ticks_ = 0U;
  } else if (++idle_anim_ticks_ >= 2U) {
    idle_anim_ticks_ = 0U;
    out.stop_anim_poller = true;
  }
  return out;
}

Cadence250msActions FrameCadenceController::evaluate_250ms(const Cadence250msInputs &in) {
  Cadence250msActions out{};
  if (in.display_power_state == 0 && !in.is_hibernating) {
    const uint32_t period = (in.rotate_period_ms > 0U) ? in.rotate_period_ms : 8000U;
    const uint32_t cur_home_slot = in.now_ms / period;
    const bool on_home_main = (in.gui_page == 1 && in.home_subpage == 0);
    const bool home_slot_rotated = (on_home_main && cur_home_slot != prev_home_slot_);
    const bool home_rotate_imminent =
        on_home_main && in.activity_mask != 0U &&
        ((period - (in.now_ms % period)) <= 300U);
    prev_home_slot_ = cur_home_slot;
    if (home_rotate_imminent) {
      out.ensure_anim_poller = true;
    }
    if (home_slot_rotated && in.activity_mask != 0U) {
      out.ensure_anim_poller = true;
      out.schedule_disp_now = true;
    }

    const uint32_t pause_exp_due = on_home_main ? in.pause_exp_due_ms : 0U;
    const bool pause_exp_imminent = (pause_exp_due > 0U && pause_exp_due <= 350U);
    const bool pause_exp_crossed =
        (on_home_main && prev_pause_exp_pending_250ms_ && pause_exp_due == 0U);
    prev_pause_exp_pending_250ms_ = on_home_main && (pause_exp_due > 0U);

    if (pause_exp_imminent) {
      out.ensure_anim_poller = true;
      if ((in.home_rotate_due_ms == 0U || pause_exp_due < in.home_rotate_due_ms) && in.has_act_band) {
        out.band_y0 = in.act_y0;
        out.band_y1 = in.act_y1;
        out.schedule_activity_rotate = true;
        out.activity_rotate_delay_ms = pause_exp_due;
      }
    } else if (pause_exp_crossed) {
      out.ensure_anim_poller = true;
      if (in.has_act_band) {
        out.band_y0 = in.act_y0;
        out.band_y1 = in.act_y1;
        out.schedule_band_now = true;
      } else {
        out.schedule_disp_now = true;
      }
    }

    if (in.sky_canvas_active && in.motion_mag > 0.055f) {
      out.bump_user_touch_ms = true;
    }

    if (!in.anim_poller_running) {
      const bool need_anim_wake =
          in.va_busy || in.alert_or_critical_busy || in.video_stream_active ||
          (in.gui_page == 3 && in.active_game > 0) ||
          in.tool_anim_due || in.time_anim_due || in.clock_sweep_due ||
          in.greet_fade_due || home_rotate_imminent || in.sky_motion_due ||
          pause_exp_imminent || pause_exp_crossed ||
          in.home_overlay_motion_due || in.nav_scroll_or_sim_dirty;
      if (need_anim_wake) {
        out.ensure_anim_poller = true;
      }
    }
  } else if (in.va_busy && !in.anim_poller_running) {
    out.ensure_anim_poller = true;
  }

  if (in.display_power_state != prev_disp_pwr_) {
    const int prev_pwr = prev_disp_pwr_;
    prev_disp_pwr_ = in.display_power_state;
    if (prev_pwr != 0 && prev_disp_pwr_ == 0 && in.pending_wallpaper_fetch) {
      out.fetch_wallpaper = true;
    }
  }

  if (in.gui_page == 4 && (in.system_menu_mode == 5 || in.system_menu_mode == 6) &&
      in.display_power_state == 0 && !in.is_hibernating) {
    if (in.latest_fw_version != prev_fw_ver_) {
      prev_fw_ver_.assign(in.latest_fw_version.data(), in.latest_fw_version.size());
      out.fw_version_changed = true;
    }
  }
  return out;
}

CadenceScreensaverActions FrameCadenceController::evaluate_screensaver_20ms(
    const CadenceScreensaverInputs &in) const {
  CadenceScreensaverActions out{};
  bool driving = false;
  if (in.display_power_state == 1 && !in.va_visible && !in.is_hibernating) {
    if (!in.screensaver_style.empty() &&
        in.screensaver_style != "Ambient Clock" &&
        in.screensaver_style != "Photo Frame" &&
        in.screensaver_style != "Screen Off") {
      driving = true;
      if (ambient_step_ready(in.now_ms, in.last_ambient_ms, in.last_disp_end_ms,
                             in.render_in_flight, in.is_chip_p4, in.speaker_audio_busy,
                             in.va_visible, in.alert_ringing,
                             !in.speaker_audio_busy && !in.is_chip_p4 && in.mww_running)) {
        out.record_ambient_ms = true;
        out.schedule_anim_full = true;
      }
    }
  }
  if (in.display_power_state == 1 && !in.va_visible) {
    out.sync_disp_poller = true;
    out.sync_disp_poller_val = driving;
  }
  return out;
}

MusicHudAutoFadeResult step_music_hud_auto_fade(
    MusicRuntimeState &m,
    uint32_t now_ms,
    std::string_view music_overlay_style_opt,
    bool touch_active) noexcept {
  if (touch_active ||
      m.drilldown != MusicDrilldown::NONE ||
      m.takeover != MusicTakeover::NONE ||
      !m.hud_active) {
    return {};
  }
  const uint32_t hud_last_ms = m.hud_timer;
  const uint32_t elapsed_ms = (now_ms >= hud_last_ms) ? (now_ms - hud_last_ms) : 0U;
  const bool always_vis = (music_overlay_style_opt == "Always Visible");

  if (m.view == MusicView::NOW_PLAYING) {
    if (!always_vis && elapsed_ms >= 6000U) {
      m.hud_active = false;
      return {true, false};
    }
    return {};
  }

  if (m.view == MusicView::LYRICS || m.view == MusicView::INFO) {
    if (elapsed_ms >= 6000U) {
      m.hud_active = false;
      return {true, false};
    }
    return {};
  }

  const uint32_t timeout_ms = (m.view == MusicView::VOLUME) ? 5000U : 10000U;
  if (elapsed_ms >= timeout_ms) {
    m.view = MusicView::NOW_PLAYING;
    m.carousel_page = 0U;
    m.mode_bar_win_start = 0;
    m.hud_active = always_vis;
    return {true, true};
  }
  return {};
}

Cadence20msActions FrameCadenceController::step_20ms(
    uint32_t now_ms, bool panel_on, bool wallpaper_loaded,
    bool mww_running, bool va_running, bool tts_audio_active,
    bool speaker_audio_busy, uint32_t rotate_period_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const bool va_visible = rs.voice.active || !rs.voice.status_text.empty();

  Cadence20msInputs in{};
  in.now_ms = now_ms;
  in.panel_on = panel_on;
  in.display_power_state = rs.nav.display_power_state;
  in.gui_page = rs.nav.gui_page;
  in.home_subpage = rs.nav.home_subpage;
  in.active_game = rs.nav.active_game;
  in.game_sfx_enabled = prefs.game_sound_effects;
  in.pending_game_sfx = pending_game_sfx();
  in.tools_anim_active =
      (rs.nav.tools_subpage == ToolsSubpage::STOPWATCH && rs.stopwatch.running) ||
      (rs.nav.tools_subpage == ToolsSubpage::DICE && dice_state().anim_active) ||
      (rs.nav.tools_subpage == ToolsSubpage::LEVEL) ||
      (rs.nav.tools_subpage == ToolsSubpage::COIN_FLIP && coin_state().anim_active);
  in.time_anim_active =
      (rs.nav.time_subpage == TimeSubpage::STOPWATCH && rs.stopwatch.running);
  const uint32_t latest_in_20ms =
      std::max(touch_gesture().last_touch_ms, ui_focus_last_ms());
  in.clock_smooth_sweep =
      (rs.nav.gui_page == PageId::CLOCK) &&
      clock_smooth_sweep_active(rs.hw_usb_connected, now_ms, latest_in_20ms, prefs.clock_face);
  in.interactive_slide =
      rs.nav.menu_scroll_active || any_vert_bounce_active(rs.nav.gui_page, now_ms) ||
      is_music_lyrics_anim_active(now_ms, speaker_audio_busy);
  in.dock_anim_active = any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage, now_ms);
  in.dock_y0 = dock_dirty_band_y0();
  in.dock_y1 = SCREEN_H - 1;
  in.find_my_active = rs.alarms.find_my_active;
  in.ringing_or_critical =
      rs.alarms.find_my_active || rs.alarms.ringing || rs.timers.ringing ||
      (rs.alert.active && rs.alert.is_critical());
  in.va_visible = va_visible;
  in.va_running = va_running;
  in.tts_received = rs.voice.tts_received;
  in.tts_audio_active = tts_audio_active;
  in.touch_active = touch_gesture().active;
  in.menu_scroll_active = rs.nav.menu_scroll_active;
  in.mww_running = mww_running;
  in.is_chip_p4 = (LATTER_CHIP_P4 != 0);
  in.speaker_audio_busy = speaker_audio_busy;
  in.render_in_flight = disp_render_in_flight(now_ms);
  in.last_ambient_ms = last_ambient_anim_ms();
  in.last_disp_end_ms = last_disp_update_end_ms();
  in.weather_step_pending = is_weather_step_pending(now_ms);

  const std::string_view sim_opt(pref_weather_sim_cstr());
  const bool weather_sim_active = (sim_opt != "Live (Home Assistant)");
  const auto &ac = area_ctx_state();
  const bool custom_wall = !ac.wallpaper_url.empty() &&
                           !is_day_night_sky_wallpaper_url(ac.wallpaper_url) &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           wallpaper_loaded;
  const bool day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
  const BackdropMode bd_mode = resolve_backdrop_mode(
      rs.nav.gui_page, rs.nav.home_subpage, prefs.weather_background,
      prefs.weather_app_background, weather_sim_active, custom_wall, day_night_sky);
  in.sky_enabled = (bd_mode == BackdropMode::LiveWeatherSky);
  in.motion_mag = rs.hw_motion_mag;
  in.rotate_period_ms = rotate_period_ms;

  const bool on_home_main =
      (rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == HomeSubpage::MAIN);
  in.home_rotate_due_ms = on_home_main ? home_activity_next_rotate_due_ms(now_ms) : 0U;
  in.pause_exp_due_ms =
      on_home_main ? audio_session().paused_music_expiry_due_ms(now_ms) : 0U;
  in.has_act_band = home_activity_band(in.act_y0, in.act_y1);
  in.greet_fading = is_wake_greeting_fading(now_ms, prefs.wake_greeting);
  in.act_sliding = home_activity_slide_active(now_ms);
  in.act_fade_settle = has_pending_home_activity_fade_settle(now_ms);
  in.video_stream_active = video_client().is_active();

  const bool home_or_weather_page =
      on_home_main || (rs.nav.gui_page == PageId::WEATHER && in.sky_enabled);
  if (panel_on && rs.nav.display_power_state == 0 && home_or_weather_page && !va_visible) {
    in.sim_just_changed = weather_sim_dirty() ||
                          (in.sky_enabled != last_sky_enabled_) ||
                          (in.last_ambient_ms == 0U);
    if (in.sim_just_changed || (now_ms - last_cond_eval_ms_ >= 250U)) {
      weather_sim_dirty() = false;
      last_sky_enabled_ = in.sky_enabled;
      last_cond_eval_ms_ = now_ms;
      cached_has_motion_ =
          in.sky_enabled &&
          evaluate_sky_has_motion(sim_opt, weather_state().condition,
                                  weather_state().sun_state, now_ms);
    }
    const uint32_t latest_input_ms =
        std::max(touch_gesture().last_touch_ms, ui_focus_last_ms());
    in.sky_stepping =
        in.sky_enabled &&
        weather_sky_motion_stepping(rs.hw_usb_connected, now_ms, latest_input_ms, cached_has_motion_);
  }

  const auto out = evaluate_20ms(in);
  if (is_music_lyrics_sync_imminent(now_ms, speaker_audio_busy)) {
    idle_anim_ticks_ = 0U;
  }
  if (out.clear_pending_game_sfx) {
    pending_game_sfx() = 0;
  }
  if (out.bump_user_touch_ms) {
    last_user_touch_ms() = now_ms;
  }
  return out;
}

Cadence250msActions FrameCadenceController::step_250ms(
    uint32_t now_ms, bool wallpaper_loaded, bool va_running,
    std::string_view latest_fw_version, uint32_t rotate_period_ms) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();

  Cadence250msInputs in{};
  in.now_ms = now_ms;
  in.display_power_state = rs.nav.display_power_state;
  in.is_hibernating = rs.nav.is_hibernating;
  in.gui_page = rs.nav.gui_page;
  in.home_subpage = rs.nav.home_subpage;
  in.system_menu_mode = rs.nav.system_menu_mode;
  in.active_game = rs.nav.active_game;
  in.activity_mask = activity_source_mask();
  in.rotate_period_ms = rotate_period_ms;

  const bool on_home_main =
      (rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == HomeSubpage::MAIN);
  in.home_rotate_due_ms = on_home_main ? home_activity_next_rotate_due_ms(now_ms) : 0U;
  in.pause_exp_due_ms =
      on_home_main ? audio_session().paused_music_expiry_due_ms(now_ms) : 0U;
  in.has_act_band = home_activity_band(in.act_y0, in.act_y1);

  const std::string_view sim_opt(pref_weather_sim_cstr());
  const bool weather_sim_active = (sim_opt != "Live (Home Assistant)");
  const auto &ac = area_ctx_state();
  const bool custom_wall = !ac.wallpaper_url.empty() &&
                           !is_day_night_sky_wallpaper_url(ac.wallpaper_url) &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           wallpaper_loaded;
  const bool day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
  const BackdropMode bd_mode = resolve_backdrop_mode(
      rs.nav.gui_page, rs.nav.home_subpage, prefs.weather_background,
      prefs.weather_app_background, weather_sim_active, custom_wall, day_night_sky);
  const bool va_visible = rs.voice.active || !rs.voice.status_text.empty();
  bool music_hud_changed = false;
  if (rs.nav.display_power_state == 0 && !rs.nav.is_hibernating && !va_visible &&
      rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.hud_active &&
        touch_gesture().start_page == PageId::MUSIC &&
        touch_gesture().last_touch_ms <= now_ms &&
        touch_gesture().last_touch_ms > rs.music.hud_timer) {
      rs.music.hud_timer = touch_gesture().last_touch_ms;
    }
    const std::string_view ov_opt(pref_music_overlay_style_cstr(prefs));
    const auto fade_res = step_music_hud_auto_fade(
        rs.music, now_ms, ov_opt, touch_gesture().active);
    if (fade_res.sprung_back_to_now_playing) {
      mode_bar_anim().snap(0);
    }
    music_hud_changed = fade_res.changed;
  }
  in.sky_canvas_active = !va_visible && (bd_mode == BackdropMode::LiveWeatherSky);
  in.motion_mag = rs.hw_motion_mag;
  in.anim_poller_running = ui_anim_poller_running();

  if (rs.nav.display_power_state == 0 && !rs.nav.is_hibernating && !in.anim_poller_running) {
    if (in.sky_canvas_active) {
      const uint32_t latest_in =
          std::max(touch_gesture().last_touch_ms, ui_focus_last_ms());
      const bool has_motion =
          evaluate_sky_has_motion(sim_opt, weather_state().condition,
                                  weather_state().sun_state, now_ms);
      in.sky_motion_due =
          weather_sky_motion_stepping(rs.hw_usb_connected, now_ms, latest_in, has_motion);
    }
    const uint32_t greet_age_ms = now_ms - wake_greeting_start_ms();
    in.greet_fade_due =
        prefs.wake_greeting && wake_greeting_latched() && on_home_main &&
        (greet_age_ms >= (kWakeGreetingHoldMs - 260U) &&
         greet_age_ms <= (kWakeGreetingTotalMs + 250U));
    in.tool_anim_due =
        (rs.nav.gui_page == PageId::TOOLS) &&
        ((rs.nav.tools_subpage == ToolsSubpage::STOPWATCH && rs.stopwatch.running) ||
         (rs.nav.tools_subpage == ToolsSubpage::DICE && dice_state().anim_active) ||
         (rs.nav.tools_subpage == ToolsSubpage::LEVEL) ||
         (rs.nav.tools_subpage == ToolsSubpage::COIN_FLIP && coin_state().anim_active));
    in.time_anim_due =
        (rs.nav.gui_page == PageId::TIME) &&
        (rs.nav.time_subpage == TimeSubpage::STOPWATCH && rs.stopwatch.running);
    const uint32_t latest_in_clock =
        std::max(touch_gesture().last_touch_ms, ui_focus_last_ms());
    in.clock_sweep_due =
        (rs.nav.gui_page == PageId::CLOCK) &&
        clock_smooth_sweep_active(rs.hw_usb_connected, now_ms, latest_in_clock, prefs.clock_face);
    in.home_overlay_motion_due =
        home_activity_slide_active(now_ms) ||
        has_pending_home_activity_fade_settle(now_ms) ||
        is_home_activity_carousel_animating(now_ms);
    in.nav_scroll_or_sim_dirty =
        any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage, now_ms) ||
        any_vert_bounce_active(rs.nav.gui_page, now_ms) ||
        is_music_lyrics_anim_active(now_ms, false) ||
        is_music_lyrics_sync_imminent(now_ms, false) ||
        rs.nav.menu_scroll_active || weather_sim_dirty();
  }

  in.va_busy = va_visible || va_running || rs.voice.tts_received;
  in.alert_or_critical_busy =
      rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active ||
      (rs.alert.active && rs.alert.is_critical());
  in.pending_wallpaper_fetch =
      !ac.wallpaper_url.empty() &&
      !is_day_night_sky_wallpaper_url(ac.wallpaper_url) &&
      ac.wallpaper_url != rs.music.loaded_wallpaper_url;
  in.video_stream_active = video_client().is_active();
  in.latest_fw_version = latest_fw_version;

  auto out = evaluate_250ms(in);
  if (music_hud_changed) {
    out.schedule_disp_now = true;
  }
  if (out.bump_user_touch_ms) {
    last_user_touch_ms() = now_ms;
  }
  if (out.fw_version_changed && rs.nav.update_check_state == static_cast<int>(UpdateCheckState::Checking)) {
    rs.nav.update_check_state = static_cast<int>(UpdateCheckState::Checked);
  }
  return out;
}

CadenceScreensaverActions FrameCadenceController::step_screensaver_20ms(
    uint32_t now_ms, bool speaker_audio_busy, bool mww_running) const {
  const auto &rs = runtime_state();
  CadenceScreensaverInputs in{};
  in.now_ms = now_ms;
  in.display_power_state = rs.nav.display_power_state;
  in.is_hibernating = rs.nav.is_hibernating;
  in.va_visible = rs.voice.active || !rs.voice.status_text.empty();
  in.screensaver_style = effective_screensaver_style_cstr();
  in.speaker_audio_busy = speaker_audio_busy;
  in.alert_ringing = rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active;
  in.mww_running = mww_running;
  in.is_chip_p4 = (LATTER_CHIP_P4 != 0);
  in.render_in_flight = disp_render_in_flight(now_ms);
  in.last_ambient_ms = last_ambient_anim_ms();
  in.last_disp_end_ms = last_disp_update_end_ms();
  return evaluate_screensaver_20ms(in);
}

}  // namespace latter_ui
