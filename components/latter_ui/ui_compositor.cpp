#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#if __has_include("ui_compositor.h")
#include "ui_compositor.h"
#else
#include "../../include/ui_compositor.h"
#endif

#if __has_include("esphome/core/defines.h")
#if __has_include("ui_pages.h")
#include "ui_pages.h"
#else
#include "../../include/ui_pages.h"
#endif
#include <cstdio>
#endif

#include <algorithm>

namespace latter_ui {

namespace {
DirtyBandTracker g_dirty_band{};
}  // namespace

DirtyBandTracker &dirty_band() {
  return g_dirty_band;
}

void DirtyBandTracker::align_band(int raw_y0, int raw_y1, int screen_h, int &out_y0, int &out_y1) {
  if (screen_h <= 1) {
    out_y0 = 0;
    out_y1 = 0;
    return;
  }
  const int max_y = screen_h - 1;
  const int lo = std::max(0, std::min(max_y, std::min(raw_y0, raw_y1)));
  const int hi = std::max(0, std::min(max_y, std::max(raw_y0, raw_y1)));
  out_y0 = lo & ~1;
  out_y1 = std::min(max_y, hi | 1);
  if (out_y1 < out_y0) out_y1 = out_y0;
}

void DirtyBandTracker::mark_band(int y0, int y1) {
  int ay0 = 0;
  int ay1 = SCREEN_H - 1;
  align_band(y0, y1, SCREEN_H, ay0, ay1);
  if (!has_pending_band) {
    pending_y0 = ay0;
    pending_y1 = ay1;
    has_pending_band = true;
  } else {
    pending_y0 = std::min(pending_y0, ay0);
    pending_y1 = std::max(pending_y1, ay1);
  }
}

void DirtyBandTracker::mark_full() {
  force_full = true;
  pending_y0 = 0;
  pending_y1 = SCREEN_H - 1;
  has_pending_band = true;
}

void DirtyBandTracker::begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active) {
  const bool nav_changed = (page != last_page) || (subpage != last_subpage) || (overlay_sig != last_overlay_sig);
  last_page = page;
  last_subpage = subpage;
  last_overlay_sig = overlay_sig;

  if (nav_changed || force_full || anim_or_scroll_active || !has_pending_band) {
    active_y0 = 0;
    active_y1 = SCREEN_H - 1;
    active_full = true;
  } else {
    align_band(pending_y0, pending_y1, SCREEN_H, active_y0, active_y1);
    active_full = (active_y0 == 0 && active_y1 == (SCREEN_H - 1));
  }

  active_idle_skip = false;
  has_pending_band = false;
  force_full = false;
  frame_hits().clear();
}

bool DirtyBandTracker::begin_frame(int page, int subpage, uint32_t overlay_sig, bool anim_or_scroll_active,
                                   int hhmm_key, uint32_t idle_state_sig, int home_clock_band_y1,
                                   bool live_sec_active) {
  const bool nav_changed = (page != last_page) || (subpage != last_subpage) || (overlay_sig != last_overlay_sig);
  const bool hhmm_changed = (hhmm_key != last_hhmm);
  const bool state_changed = (idle_state_sig != last_idle_state_sig);

  last_page = page;
  last_subpage = subpage;
  last_overlay_sig = overlay_sig;
  last_hhmm = hhmm_key;
  last_idle_state_sig = idle_state_sig;

  if (nav_changed || force_full || anim_or_scroll_active || live_sec_active || state_changed) {
    active_y0 = 0;
    active_y1 = SCREEN_H - 1;
    active_full = true;
    active_idle_skip = false;
    has_pending_band = false;
    force_full = false;
    frame_hits().clear();
    return true;
  }

  const bool is_home_clock_sub = (page == 1) && ((subpage & 0xFF) == 0);
  if (has_pending_band) {
    int py0 = pending_y0;
    int py1 = pending_y1;
    if (hhmm_changed) {
      if (is_home_clock_sub) {
        py0 = 0;
        py1 = std::max(py1, home_clock_band_y1);
      } else {
        py0 = 0;
        py1 = SCREEN_H - 1;
      }
    }
    align_band(py0, py1, SCREEN_H, active_y0, active_y1);
    active_full = (active_y0 == 0 && active_y1 == (SCREEN_H - 1));
    active_idle_skip = false;
    has_pending_band = false;
    force_full = false;
    frame_hits().clear();
    return true;
  }

  // Static 1 Hz idle poller tick: on Home root (gui_page == 1 && home_subpage == 0), an HH:MM rollover
  // restricts the dirty band to the date+clock vertical zone above HOME_WEATHER_HERO.
  // Activity Overlay (home_subpage == 2) promotes HH:MM rollovers to full-frame so centered glass cards never straddle.
  if (hhmm_changed) {
    if (is_home_clock_sub) {
      align_band(0, home_clock_band_y1, SCREEN_H, active_y0, active_y1);
      active_full = (active_y0 == 0 && active_y1 == (SCREEN_H - 1));
      active_idle_skip = false;
      frame_hits().clear();
      return true;
    }
    if ((page == 1 && (subpage & 0xFF) == 2) || page == 4 ||
        (page == 8 && (((subpage & 0xFF) == 2) || ((subpage & 0xFF) == 9)))) {
      active_y0 = 0;
      active_y1 = SCREEN_H - 1;
      active_full = true;
      active_idle_skip = false;
      frame_hits().clear();
      return true;
    }
  }

  // Unchanged static 1 Hz poll: skip frame render & panel DMA flush, keeping frame_hits() intact!
  active_idle_skip = true;
  active_y0 = 0;
  active_y1 = 0;
  active_full = false;
  return false;
}

void mark_dirty_band(int y0, int y1) {
  dirty_band().mark_band(y0, y1);
}

void mark_dirty_full() {
  dirty_band().mark_full();
}

uint32_t compute_takeover_signature(bool ota_in_progress, bool is_hibernating, bool panel_is_on,
                                    int display_power_state, bool alert_ringing, bool va_active,
                                    bool pin_pad_open, bool notif_active) {
  uint32_t sig = 0U;
  if (ota_in_progress) sig |= (1U << 0);
  if (is_hibernating) sig |= (1U << 1);
  if (panel_is_on) sig |= (1U << 2);
  sig |= (static_cast<uint32_t>(display_power_state & 0x07) << 3);
  if (alert_ringing) sig |= (1U << 6);
  if (va_active) sig |= (1U << 7);
  if (pin_pad_open) sig |= (1U << 8);
  if (notif_active) sig |= (1U << 9);
  return sig;
}

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

  // 1. Tools Suite (Stopwatch, Dice, Bubble Level, Coin Flip)
  if (in.panel_on && in.display_power_state == 0 && in.gui_page == 8 && !in.va_visible &&
      in.tools_anim_active) {
    out.anim_driving_disp = true;
    if (!in.render_in_flight && (in.now_ms - in.last_ambient_ms >= interactive_gap)) {
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

  // 3. Dock slide, menu scroll, Find My radar, and ringing/critical alert pulse
  if (in.panel_on && in.display_power_state == 0 && !in.va_visible &&
      (in.interactive_slide || in.ringing_or_critical)) {
    out.anim_driving_disp = true;
    const uint32_t gap = (in.interactive_slide || in.find_my_active) ? interactive_gap : 120U;
    if (!in.render_in_flight && (in.now_ms - in.last_ambient_ms >= gap)) {
      out.record_ambient_ms = true;
      out.schedule_anim_full = true;
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
    const bool touch_busy = in.touch_active || in.menu_scroll_active;
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
          in.va_busy || in.alert_or_critical_busy ||
          (in.gui_page == 3 && in.active_game > 0) ||
          in.tool_anim_due || in.greet_fade_due || home_rotate_imminent || in.sky_motion_due ||
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

#if __has_include("esphome/core/defines.h")

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
  in.interactive_slide =
      any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage) || rs.nav.menu_scroll_active;
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
  in.weather_step_pending = DisplayUpdateScheduler::weather_step_pending(now_ms);

  const std::string_view sim_opt(pref_weather_sim_cstr());
  const bool weather_sim_active = (sim_opt != "Live (Home Assistant)");
  const auto &ac = area_ctx_state();
  const bool custom_wall = !ac.wallpaper_url.empty() &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           wallpaper_loaded;
  const BackdropMode bd_mode = resolve_backdrop_mode(
      rs.nav.gui_page, rs.nav.home_subpage, prefs.weather_background,
      weather_sim_active, custom_wall);
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

  const bool home_Or_weather_page =
      on_home_main || (rs.nav.gui_page == PageId::WEATHER && in.sky_enabled);
  if (panel_on && rs.nav.display_power_state == 0 && home_Or_weather_page && !va_visible) {
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
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           wallpaper_loaded;
  const BackdropMode bd_mode = resolve_backdrop_mode(
      rs.nav.gui_page, rs.nav.home_subpage, prefs.weather_background,
      weather_sim_active, custom_wall);
  const bool va_visible = rs.voice.active || !rs.voice.status_text.empty();
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
    in.home_overlay_motion_due =
        home_activity_slide_active(now_ms) ||
        has_pending_home_activity_fade_settle(now_ms) ||
        is_home_activity_carousel_animating(now_ms);
    in.nav_scroll_or_sim_dirty =
        any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage) ||
        rs.nav.menu_scroll_active || weather_sim_dirty();
  }

  in.va_busy = va_visible || va_running || rs.voice.tts_received;
  in.alert_or_critical_busy =
      rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active ||
      (rs.alert.active && rs.alert.is_critical());
  in.pending_wallpaper_fetch =
      !ac.wallpaper_url.empty() && ac.wallpaper_url != rs.music.loaded_wallpaper_url;
  in.latest_fw_version = latest_fw_version;

  const auto out = evaluate_250ms(in);
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
  in.screensaver_style = pref_screensaver_style_cstr();
  in.speaker_audio_busy = speaker_audio_busy;
  in.alert_ringing = rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active;
  in.mww_running = mww_running;
  in.is_chip_p4 = (LATTER_CHIP_P4 != 0);
  in.render_in_flight = disp_render_in_flight(now_ms);
  in.last_ambient_ms = last_ambient_anim_ms();
  in.last_disp_end_ms = last_disp_update_end_ms();
  return evaluate_screensaver_20ms(in);
}

uint32_t compute_idle_state_signature(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const uint32_t batt_u8 = std::isnan(rs.hw_batt_level)
      ? 255U
      : static_cast<uint32_t>(std::clamp<int>(static_cast<int>(std::round(rs.hw_batt_level)), 0, 100));
  uint32_t sig = batt_u8;
  if (rs.hw_batt_charging) sig |= (1U << 8);
  if (rs.hw_usb_connected) sig |= (1U << 9);
  if (offline_pill_due(ctx.now_ms)) sig |= (1U << 10);
  if (ui_focus_active(ctx.now_ms)) {
    sig |= (1U << 11) | (static_cast<uint32_t>(ui_focus_index() & 0x3F) << 20);
  }
  sig |= (static_cast<uint32_t>(rs.nav.update_check_state & 0x03) << 12);
  if (!rs.music.action_feedback.empty() && (ctx.now_ms - rs.music.feedback_timer < 3200U)) sig |= (1U << 14);
  if (!rs.area.action_feedback.empty() && (ctx.now_ms - rs.area.feedback_timer < 3200U)) sig |= (1U << 15);
  if (!rs.remote.action_feedback.empty() && (ctx.now_ms - rs.remote.feedback_timer < 3200U)) sig |= (1U << 16);
  if (!rs.intercom.broadcast_feedback.empty() && (ctx.now_ms - rs.intercom.broadcast_feedback_ms < 3600U)) sig |= (1U << 17);
  if (rs.music.hud_active) sig |= (1U << 18);
  if (ctx.ble_active) sig |= (1U << 19);
  if (!cal_feedback_text().empty() && (ctx.now_ms - cal_feedback_ms() < 2200U)) sig |= (1U << 26);

  if (rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == SystemMenuMode::SYSTEM_INFO) {
    const int rssi_i = std::isnan(ctx.wifi_rssi) ? 0 : static_cast<int>(std::round(ctx.wifi_rssi));
    const int mv_c = std::isnan(rs.hw_batt_voltage) ? 0 : static_cast<int>(std::round(rs.hw_batt_voltage * 100.0f));
    const int imu_t = std::isnan(rs.hw_imu_temp) ? 0 : static_cast<int>(std::round(rs.hw_imu_temp * 10.0f));
    sig ^= static_cast<uint32_t>((ctx.free_heap_kb << 16) ^ (rssi_i & 0xFFFF));
    sig ^= static_cast<uint32_t>((mv_c & 0xFFFF) ^ ((imu_t & 0xFFF) << 16) ^
                                 (static_cast<int>(ctx.ip_addr.size() & 0x0FU) << 28));
  } else if (rs.nav.gui_page == PageId::HOME && rs.nav.home_subpage == HomeSubpage::MAIN) {
    if (rs.nav.wake_greeting_pending || is_wake_greeting_visible(ctx.now_ms, ctx.home_wake_greeting)) {
      sig ^= 0x5A17C3E9U ^ (ctx.now_ms << 3);
    }
    if (is_roaming_area_indicator_active()) {
      const auto &ac = area_ctx_state();
      uint32_t ah = 2166136261U;
      for (unsigned char c : ac.area_id) ah = (ah ^ c) * 16777619U;
      for (unsigned char c : ac.area_icon) ah = (ah ^ c) * 16777619U;
      sig ^= (ah | 1U);
    }
    uint32_t act_mask = 0U;
    if (is_doorbell_or_intercom_active()) act_mask |= (1U << 0);
    const auto &ms = media_state();
    const bool is_playing_eff =
        is_music_playing_effective(!ms.is_local, ctx.local_media_playing, ms.state, ctx.now_ms);
    const bool is_paused_st = is_music_paused_effective(is_playing_eff, ms.is_local, ms.state);
    const bool has_track_title = !ms.title.empty() || !ctx.track_title.empty();
    if ((is_playing_eff || !music_queue_cleared()) &&
        audio_session().is_music_visible_on_home_activity(
            is_playing_eff, is_paused_st, has_track_title, ctx.now_ms)) {
      act_mask |= (1U << 1);
    }
    if (rs.timers.paused || rs.timers.slots[0].paused || rs.timers.slots[1].paused || rs.timers.slots[2].paused) {
      act_mask |= (1U << 2);
    }
    if (rs.alarms.snooze_until > 0 ||
        prefs.alarms[0].enabled || prefs.alarms[1].enabled || prefs.alarms[2].enabled) {
      act_mask |= (1U << 3);
    }
    if (calendar_state().count > 0 && !calendar_state().summary.empty()) act_mask |= (1U << 4);
    if (is_home_activity_fading_out(ctx.now_ms)) act_mask |= (1U << 5);
    sig ^= (act_mask * 0x27D4EB2FU);
  }
  return sig;
}

bool compute_live_sec_active(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page == PageId::CLOCK) return true;
  if (rs.nav.gui_page == PageId::HOME) {
    if (ctx.home_wake_greeting &&
        (rs.nav.wake_greeting_pending || is_wake_greeting_visible(ctx.now_ms, true))) {
      return true;
    }
    if (is_doorbell_or_intercom_active()) return true;
    if (rs.stopwatch.running || rs.timers.running ||
        rs.timers.slots[0].running || rs.timers.slots[1].running || rs.timers.slots[2].running) {
      return true;
    }
    return false;
  }
  if (rs.nav.gui_page == PageId::MUSIC) {
    return ctx.local_media_playing || (media_state().playback_state == MediaPlaybackState::Playing) ||
           (media_state().state == "playing");
  }
  if (rs.nav.gui_page == PageId::SYSTEM &&
      rs.nav.system_menu_mode == SystemMenuMode::UPDATE &&
      rs.nav.update_check_state != static_cast<int>(UpdateCheckState::Idle)) {
    return true;
  }
  if (rs.nav.gui_page == PageId::TOOLS) {
    const int ts = rs.nav.tools_subpage;
    return (ts == ToolsSubpage::TIMERS || ts == ToolsSubpage::STOPWATCH ||
            ts == ToolsSubpage::DICE || ts == ToolsSubpage::LEVEL ||
            ts == ToolsSubpage::COIN_FLIP || ts == ToolsSubpage::INTERCOM ||
            ts == ToolsSubpage::CAMERAS);
  }
  return false;
}

void render_active_focus_overlay(esphome::display::Display &it, uint32_t now_ms,
                                 esphome::Color theme_primary, esphome::Color col_white) {
  if (!ui_focus_active(now_ms)) return;
  if (frame_hits().focus_count() <= 0) return;
  const HitRegion *r = frame_hits().focus_at(ui_focus_index());
  if (r == nullptr) return;

  if ((r->flags & kHitFocusCircle) != 0) {
    render_focus_ring_circle(it, r->cx(), r->cy(), std::max(8, r->radius()), theme_primary, col_white);
  } else {
    render_focus_ring_rect(it, r->cx(), r->cy(),
                           std::max(12, r->width()),
                           std::max(12, r->height()),
                           theme_primary, col_white);
  }
}

bool render_system_takeovers(esphome::display::Display &it, const UiFrameContext &ctx,
                             const SystemTakeoverState &st) {
  using esphome::display::TextAlign;

  // 1. OTA Firmware Update In Progress
  if (st.ota_in_progress) {
    frame_base(it, st.col_bg);
    const int pct = std::max(0, std::min(100, st.ota_progress_pct));
    if (LATTER_ROUND_DISPLAY) {
      for (int i = 0; i <= pct; i++) {
        const float rad = (static_cast<float>(i) / 100.0f) * 2.0f * PI_F - HALF_PI_F;
        float s = 0.0f;
        float c = 1.0f;
        fast_sincos(rad, s, c);
        fast_line(it, st.cx + static_cast<int>(scale_min(220) * c),
                  st.cy + static_cast<int>(scale_min(220) * s),
                  st.cx + static_cast<int>(scale_min(228) * c),
                  st.cy + static_cast<int>(scale_min(228) * s), st.theme_secondary);
      }
    } else {
      const int ota_bw = std::min(scale_w(280), SCREEN_W - 48);
      const int ota_bx = st.cx - (ota_bw / 2);
      const int ota_fill = (ota_bw * pct) / 100;
      fill_rect_row_major(it, ota_bx, st.cy + scale_y(104), ota_bw, scale_y(10), st.col_dark_gray);
      if (ota_fill > 0) {
        fill_rect_row_major(it, ota_bx, st.cy + scale_y(104), ota_fill, scale_y(10), st.theme_primary);
      }
    }
    fast_print(it, st.cx, st.cy - scale_y(60), ctx.font_date, st.theme_primary, TextAlign::CENTER, "FIRMWARE UPDATE");
    char prog_buf[32];
    std::snprintf(prog_buf, sizeof(prog_buf), "%d%%", pct);
    fast_print(it, st.cx, st.cy + scale_y(8), ctx.font_time, st.col_white, TextAlign::CENTER, prog_buf);
    const char *status_msg = "Preparing flash...";
    if (pct >= 99) status_msg = "Rebooting device...";
    else if (pct >= 85) status_msg = "Verifying image...";
    else if (pct >= 10) status_msg = "Writing flash memory...";
    fast_print(it, st.cx, st.cy + scale_y(65), ctx.font_small, st.col_slate, TextAlign::CENTER, status_msg);
    return true;
  }

  // 2. Hibernation Shutdown Countdown
  if (st.is_hibernating) {
    frame_base(it, st.col_bg);
    if (st.hibernate_countdown_val > 0) {
      fast_print(it, st.cx, st.cy - scale_y(65), ctx.font_date, st.theme_primary, TextAlign::CENTER, "HIBERNATING");
      char c_buf[16];
      std::snprintf(c_buf, sizeof(c_buf), "%d", st.hibernate_countdown_val);
      fast_print(it, st.cx, st.cy + scale_y(5), ctx.font_chrono_time, st.theme_secondary, TextAlign::CENTER, c_buf);
      fast_print(it, st.cx, st.cy + scale_y(65), ctx.font_small, st.col_slate, TextAlign::CENTER, "Tap screen to stay awake");
    } else {
      fast_print(it, st.cx, st.cy - scale_y(10), ctx.font_date, st.theme_primary, TextAlign::CENTER, "HIBERNATING");
      fast_print(it, st.cx, st.cy + scale_y(30), ctx.font_small, st.col_slate, TextAlign::CENTER, "Shutting down...");
    }
    return true;
  }

  // 3. Panel Off / Standby: skip buffer writes until wake frame
  if (!st.panel_is_on || st.display_power_state == 2) {
    frame_base_skip();
    return true;
  }

  // 4. ALARM / TIMER RINGING TAKEOVER (Highest Active Priority — always above Voice Assistant!)
  if (st.alarm_ringing || st.timer_ringing || st.find_my_active) {
    frame_base(it, st.col_bg);
    render_alert_ringing_overlay(it, ctx, st);
    render_active_focus_overlay(it, st.now_ms, st.theme_primary, st.col_white);
    return true;
  }

  // 5. VOICE ASSISTANT TAKEOVER
  if (st.va_active || !st.va_status_text.empty()) {
    frame_base(it, st.col_bg);
    render_voice_assistant_overlay(it, ctx, st);
    return true;
  }

  // 6. 3-TIER SECURITY PIN PAD MODAL
  if (ui::pin_pad_state().open) {
    frame_base(it, st.col_bg);
    ui::pin_pad_modal(it, st.cx, st.cy, st.now_ms, ctx.font_date, ctx.font_date, ctx.font_small,
                      st.theme_primary, st.col_white, st.col_slate, st.col_red);
    render_active_focus_overlay(it, st.now_ms, st.theme_primary, st.col_white);
    return true;
  }

  // 7. PUSH NOTIFICATION & CAMERA SNAPSHOT TAKEOVER
  if (st.notif_active) {
    frame_base(it, st.col_bg);
    render_notification_overlay(it, ctx, st);
    render_active_focus_overlay(it, st.now_ms, st.theme_primary, st.col_white);
    return true;
  }

  // 8. DOCKED SCREENSAVER (Mode 1)
  if (st.display_power_state == 1) {
    frame_base(it, st.col_bg);
    render_docked_screensaver(it, ctx, st);
    return true;
  }

  return false;
}

#endif  // __has_include("esphome/core/defines.h")

}  // namespace latter_ui
