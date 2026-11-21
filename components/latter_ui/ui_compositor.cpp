#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#if __has_include("ui_compositor.h")
#include "ui_compositor.h"
#else
#include "../../include/ui_compositor.h"
#endif

#if __has_include("esphome/core/defines.h")
#if __has_include("hal_video_stream.h")
#include "hal_video_stream.h"
#else
#include "../../include/hal_video_stream.h"
#endif
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
      } else if (page != 2) {
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
    if ((page == 1 && (subpage & 0xFF) == 2) || page == 4 || page == 12 ||
        (page == 10 && (subpage & 0x0F) == 2) || (overlay_sig == (1U << 3))) {
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

void mark_music_progress_dirty_band(int y0, int y1) {
  dirty_band().mark_band(y0, y1);
}

uint32_t compute_takeover_signature(bool ota_in_progress, bool is_hibernating, bool panel_is_on,
                                    int display_power_state, bool alert_ringing, bool va_active,
                                    bool pin_pad_open, bool notif_active,
                                    bool shortcut_confirm_open,
                                    bool quick_actions_open) {
  uint32_t sig = 0U;
  if (ota_in_progress) sig |= (1U << 0);
  if (is_hibernating) sig |= (1U << 1);
  if (!panel_is_on) sig |= (1U << 2);
  sig |= (static_cast<uint32_t>(display_power_state & 0x07) << 3);
  if (alert_ringing) sig |= (1U << 6);
  if (va_active) sig |= (1U << 7);
  if (pin_pad_open) sig |= (1U << 8);
  if (notif_active) sig |= (1U << 9);
  if (shortcut_confirm_open) sig |= (1U << 10);
  if (quick_actions_open) sig |= (1U << 11);
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

  // 6. Live MJPEG video streaming: keep 20 ms anim poller alive so consume_frame_dirty()
  //    drives banded video flushes without stopping ui_anim_tick after 40 ms.
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

#if __has_include("esphome/core/defines.h")

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

bool is_music_lyrics_anim_active(uint32_t now_ms, bool local_media_playing) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::MUSIC ||
      rs.music.view != MusicView::LYRICS ||
      rs.music.drilldown != MusicDrilldown::NONE) {
    return false;
  }
  const auto &lyr = rs.music.lyrics;
  if (lyr.scroll_px != lyr.target_scroll_px) return true;
  if (lyr.scroll_anim_start_ms > 0U && (now_ms - lyr.scroll_anim_start_ms) < 260U) return true;
  if (lyr.synced && !lyr.lines.empty() && now_ms >= lyr.manual_override_until_ms) {
    const auto &ms = media_state();
    if (is_music_playing_for_compositor(ms, local_media_playing, now_ms)) {
      const uint32_t pos_ms = ms.interpolated_position_ms(now_ms);
      if (lyr.find_active_line(pos_ms) != lyr.active_line_idx) return true;
    }
  }
  return false;
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
  in.weather_step_pending = DisplayUpdateScheduler::weather_step_pending(now_ms);

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

uint32_t compute_idle_state_signature(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  if (rs.nav.display_power_state == 1) {
    const std::string_view ss_style(effective_screensaver_style_cstr(prefs));
    if (ss_style == "Photo Frame" || ss_style == "Ambient Clock") {
      uint32_t ss_sig = (ss_style == "Photo Frame") ? 2U : 1U;
      if (!prefs.bedtime_active && prefs.screensaver_photo_clock) ss_sig |= (1U << 2);
      if (ctx.screensaver_photo_art.valid()) ss_sig |= (1U << 3);
      if (prefs.is_24h_format) ss_sig |= (1U << 4);
      const auto &ac = area_ctx_state();
      uint32_t uh = 2166136261U;
      for (unsigned char c : ac.screensaver_photo_url) uh = (uh ^ c) * 16777619U;
      return ss_sig ^ (uh << 5);
    }
  }
  const uint32_t batt_u8 = std::isnan(rs.hw_batt_level)
      ? 255U
      : static_cast<uint32_t>(std::clamp<int>(static_cast<int>(std::round(rs.hw_batt_level)), 0, 100));
  if (rs.nav.quick_actions_open) {
    const auto &ac_qa = area_ctx_state();
    const auto bri = resolve_active_brightness_target(
        prefs, weather_state().sun_state, ctx.now.is_valid(),
        ctx.now.is_valid() ? ctx.now.hour : 12,
        ac_qa.area_ambient_pct, rs.nav.ambient_bri_override_pct);
    uint32_t qa_sig = 2166136261U;
    qa_sig = (qa_sig ^ batt_u8) * 16777619U;
    qa_sig = (qa_sig ^ (rs.hw_batt_charging ? 1U : 0U) ^ (rs.hw_usb_connected ? 2U : 0U)) * 16777619U;
    qa_sig = (qa_sig ^ static_cast<uint32_t>(std::clamp(ctx.spk_vol_pct, 0, 100))) * 16777619U;
    qa_sig = (qa_sig ^ static_cast<uint32_t>(bri.pct)) * 16777619U;
    qa_sig = (qa_sig ^ static_cast<uint32_t>(bri.kind)) * 16777619U;
    for (unsigned char c : ac_qa.area_ambient_light) qa_sig = (qa_sig ^ c) * 16777619U;
    qa_sig = (qa_sig ^ (prefs.bedtime_active ? 1U : 0U)) * 16777619U;
    qa_sig = (qa_sig ^ (prefs.do_not_disturb ? 1U : 0U)) * 16777619U;
    qa_sig = (qa_sig ^ static_cast<uint32_t>(prefs.screensaver_mode)) * 16777619U;
    if (ui_focus_active(ctx.now_ms)) {
      qa_sig = (qa_sig ^ (0x80U | static_cast<uint32_t>(ui_focus_index() & 0x3F))) * 16777619U;
    }
    if (is_day_night_sky_wallpaper_url(area_ctx_state().wallpaper_url) &&
        resolve_day_night_sky_is_night(weather_state().sun_state, ctx.now.is_valid(), ctx.now.hour)) {
      qa_sig = (qa_sig ^ 0x5AU) * 16777619U;
    } else if (ctx.home_wallpaper_art.valid()) {
      qa_sig = (qa_sig ^ 0xA5U) * 16777619U;
    }
    return qa_sig;
  }
  uint32_t sig = batt_u8;
  if (is_day_night_sky_wallpaper_url(area_ctx_state().wallpaper_url) &&
      resolve_day_night_sky_is_night(weather_state().sun_state, ctx.now.is_valid(), ctx.now.hour)) {
    sig |= (1U << 28);
  }
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
  if (!rs.cal.feedback_text.empty() && (ctx.now_ms - rs.cal.feedback_ms < 2200U)) sig |= (1U << 26);
  if (!rs.nav.pinned_feedback.empty() && (ctx.now_ms - rs.nav.pinned_feedback_ms < 1800U)) sig |= (1U << 27);

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
    {
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
    if (!rs.stopwatch.running && rs.stopwatch.elapsed_ms > 0U) {
      act_mask |= (1U << 12);
    }
    const int snoozed_slot = (rs.alarms.snooze_until > 0) ? snoozed_alarm_slot() : -1;
    if (rs.alarms.snooze_until > 0) {
      act_mask |= (1U << 3);
    }
    if (rs.alarms.snooze_until == 0 || snoozed_slot >= 1) {
      const int cur_h = ctx.now.is_valid() ? ctx.now.hour : -1;
      const int cur_m = ctx.now.is_valid() ? ctx.now.minute : -1;
      const int cur_dow = ctx.now.is_valid() ? ctx.now.day_of_week : -1;
      for (int ai = 0; ai < 3; ++ai) {
        if (rs.alarms.snooze_until > 0 && (ai + 1) == snoozed_slot) continue;
        const auto &ap = prefs.alarms[ai];
        const int mins_u = next_alarm_minutes_until(
            ap.hour, ap.minute, ap.enabled, ap.recurrence, cur_h, cur_m, cur_dow);
        if (mins_u > 0 && mins_u <= kUpcomingAlarmDeckMinutes) {
          act_mask |= (1U << (6 + ai * 2));
        }
        if (mins_u > 0 && mins_u <= kUpcomingAlarmPinMinutes) {
          act_mask |= (1U << (7 + ai * 2));
        }
      }
    }
    if (calendar_state().count > 0 && !calendar_state().summary.empty()) act_mask |= (1U << 4);
    if (is_home_activity_fading_out(ctx.now_ms) ||
        home_activity_slide_active(ctx.now_ms) ||
        has_pending_home_activity_fade_settle(ctx.now_ms)) {
      act_mask |= (1U << 5);
    }
    sig ^= (act_mask * 0x27D4EB2FU);
    sig ^= (compute_shortcuts_ui_sig() | 1U);
  } else if (rs.nav.gui_page == PageId::PINNED) {
    sig ^= (compute_shortcuts_ui_sig() | 1U);
  } else if (rs.nav.gui_page == PageId::TIME) {
    uint32_t th = 2166136261U;
    for (int i = 0; i < 3; ++i) {
      th = (th ^ prefs.alarms[i].hour) * 16777619U;
      th = (th ^ prefs.alarms[i].minute) * 16777619U;
      th = (th ^ (prefs.alarms[i].enabled ? 1U : 0U)) * 16777619U;
      th = (th ^ prefs.alarms[i].recurrence) * 16777619U;
    }
    th = (th ^ static_cast<uint32_t>(rs.nav.alarm_edit_field)) * 16777619U;
    th = (th ^ (prefs.show_utc_clock ? 1U : 0U)) * 16777619U;
    for (const char *p = prefs.local_clock_label; *p != '\0'; ++p) {
      th = (th ^ static_cast<unsigned char>(*p)) * 16777619U;
    }
    th = (th ^ 0xFFU) * 16777619U;
    for (const char *p = prefs.world_clocks_spec; *p != '\0'; ++p) {
      th = (th ^ static_cast<unsigned char>(*p)) * 16777619U;
    }
    th = (th ^ rs.stopwatch.elapsed_ms ^ (static_cast<uint32_t>(rs.stopwatch.lap_count) << 24)) * 16777619U;
    sig ^= (th | 1U);
  } else if (is_camera_view_active(rs) || rs.nav.gui_page == PageId::HOME_HUB) {
    const auto &ac = area_ctx_state();
    const auto &hh = home_hub_state();
    uint32_t hh_sig = 2166136261U;
    hh_sig = (hh_sig ^ ac.rev) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(ac.alarm_caps)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(hh.thermostat_idx + 1)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(hh.entry_page + 1)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(hh.broadcast_page + 1)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(hh.vacuum_idx + 1)) * 16777619U;
    hh_sig = (hh_sig ^ hh.local_mutation_seq) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(rs.cameras.selected_idx + 1)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(rs.cameras.carousel_idx + 1)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(rs.cameras.grid_page + 1)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(video_client().state())) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(ctx.camera_snapshot_art.width)) * 16777619U;
    hh_sig = (hh_sig ^ static_cast<uint32_t>(ctx.camera_snapshot_art.height)) * 16777619U;
    const uint32_t flags =
        (rs.intercom.cam_call_active ? 1U : 0U) |
        (rs.intercom.cam_listen_active ? 2U : 0U) |
        (rs.intercom.cam_intercom_ptt_active ? 4U : 0U) |
        (rs.cameras.landscape_mode ? 8U : 0U) |
        (!rs.intercom.cam_intercom_status.empty() ? 16U : 0U) |
        (rs.cameras.hud_active ? 32U : 0U) |
        (rs.cameras.carousel_paused ? 64U : 0U) |
        (rs.voice.intercom_turn ? 128U : 0U) |
        (rs.voice.vad_active ? 256U : 0U) |
        (static_cast<uint32_t>(prefs.camera_default_view & 3U) << 9) |
        (static_cast<uint32_t>(prefs.camera_carousel_interval & 3U) << 11) |
        (static_cast<uint32_t>(prefs.camera_framing & 1U) << 13) |
        (static_cast<uint32_t>(prefs.camera_stream_mode & 3U) << 14) |
        (static_cast<uint32_t>(prefs.camera_doorbell_popup & 1U) << 16) |
        (rs.intercom.broadcast_dictation_active ? (1U << 17) : 0U);
    hh_sig = (hh_sig ^ flags) * 16777619U;
    sig ^= (hh_sig | 1U);
  } else if (rs.nav.gui_page == PageId::AREA) {
    uint32_t ar_sig = 2166136261U;
    ar_sig = (ar_sig ^ area_ctx_state().rev) * 16777619U;
    ar_sig = (ar_sig ^ home_hub_state().local_mutation_seq) * 16777619U;
    ar_sig = (ar_sig ^ static_cast<uint32_t>(rs.area.light_insp_bri_pct + 10)) * 16777619U;
    sig ^= (ar_sig | 1U);
  } else if (rs.nav.gui_page == PageId::TOOLS) {
    uint32_t tl_sig = 2166136261U;
    for (unsigned char c : effective_tools_items_sv(prefs)) {
      tl_sig = (tl_sig ^ c) * 16777619U;
    }
    tl_sig = (tl_sig ^ rs.stopwatch.elapsed_ms ^ (static_cast<uint32_t>(rs.stopwatch.lap_count) << 24)) * 16777619U;
    sig ^= (tl_sig | 1U);
  } else if (rs.nav.gui_page == PageId::GAMES) {
    uint32_t gm_sig = 2166136261U;
    for (unsigned char c : effective_games_items_sv(prefs)) {
      gm_sig = (gm_sig ^ c) * 16777619U;
    }
    gm_sig = (gm_sig ^ static_cast<uint32_t>(rs.nav.game_carousel_idx)) * 16777619U;
    sig ^= (gm_sig | 1U);
  } else if (rs.nav.gui_page == PageId::CALENDAR) {
    uint32_t cl_sig = 2166136261U;
    cl_sig = (cl_sig ^ calendar_state().rev) * 16777619U;
    cl_sig = (cl_sig ^ calendar_window_store().rev) * 16777619U;
    cl_sig = (cl_sig ^ (calendar_window_store().loading ? 1U : 0U)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.selected_ymd)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.selected_event_idx + 2)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.dictation_field)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_ymd)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_start_min + 2)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_end_min + 2)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_all_day ? 1 : 0)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_time_field)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_cal_idx)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(rs.cal.draft_rrule_idx)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(prefs.calendar_lookahead_days)) * 16777619U;
    cl_sig = (cl_sig ^ static_cast<uint32_t>(prefs.calendar_week_start)) * 16777619U;
    const bool fb_vis = !rs.cal.feedback_text.empty() &&
                        (ctx.now_ms >= rs.cal.feedback_ms) &&
                        ((ctx.now_ms - rs.cal.feedback_ms) < 4000U);
    cl_sig = (cl_sig ^ (fb_vis ? 1U : 0U)) * 16777619U;
    for (unsigned char c : rs.cal.draft_title) cl_sig = (cl_sig ^ c) * 16777619U;
    for (unsigned char c : rs.cal.draft_location) cl_sig = (cl_sig ^ c) * 16777619U;
    sig ^= (cl_sig | 1U);
  } else if (rs.nav.gui_page == PageId::MUSIC) {
    const auto &ms = media_state();
    const bool is_playing_eff = is_music_playing_for_compositor(ms, ctx.local_media_playing, ctx.now_ms);
    uint32_t mu_sig = 2166136261U;
    auto hash_sv = [&](std::string_view sv) {
      for (unsigned char c : sv) mu_sig = (mu_sig ^ c) * 16777619U;
      mu_sig = (mu_sig ^ 0xFFU) * 16777619U;
    };
    const uint32_t mu_flags =
        (is_playing_eff ? 1U : 0U) |
        (ms.is_local ? 2U : 0U) |
        (ms.shuffle ? 4U : 0U) |
        (ms.muted ? 8U : 0U) |
        (ms.favorite ? 16U : 0U) |
        (static_cast<uint32_t>(ms.repeat_mode & 0x03U) << 5) |
        (static_cast<uint32_t>(prefs.album_art_style & 0x07U) << 7) |
        (static_cast<uint32_t>(prefs.music_overlay_style & 0x07U) << 10) |
        (static_cast<uint32_t>(prefs.album_sort_order & 0x03U) << 13) |
        (static_cast<uint32_t>(prefs.artist_album_sort_order & 0x03U) << 15) |
        (static_cast<uint32_t>(prefs.podcast_sort_order & 0x03U) << 17);
    mu_sig = (mu_sig ^ mu_flags) * 16777619U;
    mu_sig = (mu_sig ^ static_cast<uint32_t>(ms.is_local ? ctx.spk_vol_pct : rs.music.remote_vol_pct)) * 16777619U;
    mu_sig = (mu_sig ^ static_cast<uint32_t>(ms.queue_count)) * 16777619U;
    mu_sig = (mu_sig ^ static_cast<uint32_t>(ms.media_caps)) * 16777619U;
    hash_sv(ms.title);
    hash_sv(ms.artist);
    hash_sv(ms.album);
    hash_sv(ctx.track_title.sv());
    hash_sv(ctx.track_artist.sv());
    hash_sv(ctx.track_album.sv());
    hash_sv(ms.active_speaker);
    hash_sv(ms.speaker_name);
    hash_sv(rs.music.active_speaker_name);
    hash_sv(ms.art_url);
    hash_sv(rs.music.loaded_art_url);
    hash_sv(ctx.music_overlay_style_opt.sv());
    hash_sv(ctx.music_fullscreen_art_opt.sv());
    auto hash_art = [&](ArtImageView art) {
      mu_sig = (mu_sig ^ static_cast<uint32_t>(art.width)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(art.height)) * 16777619U;
      if (art.valid()) {
        const size_t n = static_cast<size_t>(art.width) * static_cast<size_t>(art.height);
        mu_sig = (mu_sig ^ art.data[0]) * 16777619U;
        mu_sig = (mu_sig ^ art.data[n / 2]) * 16777619U;
        mu_sig = (mu_sig ^ art.data[n - 1]) * 16777619U;
      }
    };
    hash_art(ctx.album_art);
    hash_art(ctx.remote_album_art);
    if (!rs.music.action_feedback.empty() && (ctx.now_ms - rs.music.feedback_timer < 3200U)) {
      hash_sv(rs.music.action_feedback);
    }
    if (rs.music.view == MusicView::LYRICS && rs.music.drilldown == MusicDrilldown::NONE) {
      const auto &lyr = rs.music.lyrics;
      mu_sig = (mu_sig ^ lyr.rev) * 16777619U;
      mu_sig = (mu_sig ^ (lyr.loading ? 1U : 0U)) * 16777619U;
      mu_sig = (mu_sig ^ (lyr.synced ? 2U : 0U)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(lyr.active_line_idx + 2)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(lyr.scroll_px)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(lyr.target_scroll_px)) * 16777619U;
      mu_sig = (mu_sig ^ ((ctx.now_ms < lyr.manual_override_until_ms) ? 1U : 0U)) * 16777619U;
    } else if (rs.music.view != MusicView::NOW_PLAYING ||
               rs.music.drilldown != MusicDrilldown::NONE ||
               rs.music.takeover != MusicTakeover::NONE) {
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.drilldown)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.category_page)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.drilldown_artist_page)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.drilldown_album_page)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.info_scroll_px)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.info_scroll_page)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.settings_page)) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.music.action_sheet_slot + 2)) * 16777619U;
      hash_sv(rs.music.target_speaker_id);
      hash_sv(rs.music.target_speaker_name);
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.slot_page_for(latter_rpc::SlotChannel::MUSIC))) * 16777619U;
      mu_sig = (mu_sig ^ static_cast<uint32_t>(rs.slot_total_pages_for(latter_rpc::SlotChannel::MUSIC))) * 16777619U;
      const BrowseSlotItem *slots = rs.music_slots();
      for (size_t si = 0; si < DeviceRuntimeState::kSlotsPerPage; ++si) {
        hash_sv(slots[si].name);
        hash_sv(slots[si].id);
        hash_sv(slots[si].queue_item_id);
        mu_sig = (mu_sig ^ ((slots[si].active ? 1U : 0U) | (slots[si].is_current ? 2U : 0U))) * 16777619U;
      }
    }
    sig ^= (mu_sig | 1U);
  } else if (rs.nav.gui_page == PageId::SYSTEM) {
    uint32_t sy_sig = 2166136261U;
    sy_sig = (sy_sig ^ (prefs.bedtime_active ? 1U : 0U)) * 16777619U;
    sy_sig = (sy_sig ^ (prefs.do_not_disturb ? 2U : 0U)) * 16777619U;
    sy_sig = (sy_sig ^ device_prefs_state().rev ^ prefs.prefs_seq) * 16777619U;
    sig ^= (sy_sig | 1U);
  }
  return sig;
}

bool compute_live_sec_active(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  if (rs.nav.quick_actions_open) return false;
  static uint32_t s_last_music_np_pos_sec = 0xFFFFFFFFU;
  static int s_last_music_np_dur_sec = -1;
  if (rs.nav.gui_page != PageId::MUSIC ||
      rs.music.view != MusicView::NOW_PLAYING ||
      rs.music.drilldown != MusicDrilldown::NONE ||
      rs.music.takeover != MusicTakeover::NONE) {
    s_last_music_np_pos_sec = 0xFFFFFFFFU;
    s_last_music_np_dur_sec = -1;
  }
  if (rs.nav.gui_page == PageId::CLOCK) return true;
  if (rs.nav.gui_page == PageId::TIME) {
    if (rs.nav.time_subpage == TimeSubpage::CLOCK) return true;
    if (rs.nav.time_subpage == TimeSubpage::TIMERS) return true;
    if (rs.nav.time_subpage == TimeSubpage::STOPWATCH) return rs.stopwatch.running;
    return false;
  }
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
    const auto &ms = media_state();
    const auto &prefs = persisted_prefs();
    const bool is_playing_eff = is_music_playing_for_compositor(ms, ctx.local_media_playing, ctx.now_ms);
    if (rs.music.view == MusicView::LYRICS && rs.music.drilldown == MusicDrilldown::NONE) {
      return is_music_lyrics_anim_active(ctx.now_ms, ctx.local_media_playing);
    }
    if (rs.music.view == MusicView::NOW_PLAYING && rs.music.drilldown == MusicDrilldown::NONE) {
      if (rs.music.takeover == MusicTakeover::SCRUB) {
        if (is_playing_eff) return true;
        return rs.music.takeover_interacted_ms > 0U &&
               (ctx.now_ms - rs.music.takeover_interacted_ms) >= 4800U;
      }
      if (rs.music.takeover != MusicTakeover::NONE) {
        return false;
      }
      const UiStrView ov_opt = ctx.music_overlay_style_opt.empty()
                                   ? UiStrView(pref_music_overlay_style_cstr(prefs))
                                   : ctx.music_overlay_style_opt;
      const bool always_vis = (ov_opt == "Always Visible");
      const bool show_controls = always_vis || rs.music.hud_active;
      const bool show_metadata = always_vis || (ov_opt == "Art + Metadata") || rs.music.hud_active;
      if (!show_metadata && !show_controls) {
        // Art Only auto-hidden: 0 FPS static frame (zero 1 Hz DMA flushes while playing!)
        s_last_music_np_pos_sec = 0xFFFFFFFFU;
        s_last_music_np_dur_sec = -1;
        return false;
      }
      const uint32_t cur_pos_sec = ms.interpolated_position_ms(ctx.now_ms) / 1000U;
      const int cur_dur_sec = (ms.duration > 0.0f && !std::isnan(ms.duration))
                                  ? static_cast<int>(ms.duration + 0.5f)
                                  : 0;
      const int cur_sub = compute_active_subpage_id();
      if (dirty_band().last_page != PageId::MUSIC || dirty_band().last_subpage != cur_sub ||
          s_last_music_np_pos_sec == 0xFFFFFFFFU) {
        s_last_music_np_pos_sec = cur_pos_sec;
        s_last_music_np_dur_sec = cur_dur_sec;
        return false;
      }
      if (cur_pos_sec != s_last_music_np_pos_sec || cur_dur_sec != s_last_music_np_dur_sec) {
        s_last_music_np_pos_sec = cur_pos_sec;
        s_last_music_np_dur_sec = cur_dur_sec;
        const bool dock_or_scroll =
            rs.nav.menu_scroll_active ||
            any_dock_anim_active(PageId::MUSIC, 0, ctx.now_ms) ||
            any_vert_bounce_active(PageId::MUSIC, ctx.now_ms);
        if (dock_or_scroll) return true;
        mark_music_progress_dirty_band(music_metadata_band_y0(), music_metadata_band_y1());
      }
      return false;
    }
    return false;
  }
  if (rs.nav.gui_page == PageId::SYSTEM &&
      rs.nav.system_menu_mode == SystemMenuMode::UPDATE &&
      rs.nav.update_check_state != static_cast<int>(UpdateCheckState::Idle)) {
    return true;
  }
  if (is_camera_view_active(rs)) {
    return rs.voice.intercom_turn ||
           (rs.cameras.tab == CamerasTab::CAROUSEL && !rs.cameras.carousel_paused);
  }
  if (rs.nav.gui_page == PageId::HOME_HUB) {
    const auto &ac = area_ctx_state();
    if (home_hub_tab() == HomeHubTab::ALARM &&
        (rs.area.alarm_state == "arming" || rs.area.alarm_state == "pending" ||
         rs.area.alarm_state == "triggered" || ac.alarm_state == "arming" ||
         ac.alarm_state == "pending" || ac.alarm_state == "triggered")) {
      return true;
    }
    if (home_hub_tab() == HomeHubTab::INTERCOM &&
        rs.intercom.broadcast_dictation_active) {
      return true;
    }
  }
  if (rs.nav.gui_page == PageId::TOOLS) {
    const int ts = rs.nav.tools_subpage;
    return (ts == ToolsSubpage::STOPWATCH ||
            ts == ToolsSubpage::DICE || ts == ToolsSubpage::LEVEL ||
            ts == ToolsSubpage::COIN_FLIP);
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
    fast_print(it, st.cx, st.cy - scale_y(60), ctx.font_date, st.col_white, TextAlign::CENTER, "FIRMWARE UPDATE");
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
      fast_print(it, st.cx, st.cy - scale_y(65), ctx.font_date, st.col_white, TextAlign::CENTER, "HIBERNATING");
      char c_buf[16];
      std::snprintf(c_buf, sizeof(c_buf), "%d", st.hibernate_countdown_val);
      fast_print(it, st.cx, st.cy + scale_y(5), ctx.font_chrono_time, st.theme_secondary, TextAlign::CENTER, c_buf);
      fast_print(it, st.cx, st.cy + scale_y(65), ctx.font_small, st.col_slate, TextAlign::CENTER, "Tap screen to stay awake");
    } else {
      fast_print(it, st.cx, st.cy - scale_y(10), ctx.font_date, st.col_white, TextAlign::CENTER, "HIBERNATING");
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

  // 5. VOICE ASSISTANT TAKEOVER (exempt during live doorbell PTT reply — D-066!)
  if ((st.va_active || !st.va_status_text.empty()) &&
      should_va_takeover_preempt(st.va_active, !st.va_status_text.empty(),
                                 st.va_intercom_turn, runtime_state().nav.gui_page,
                                 runtime_state().cameras.doorbell_live_popup_active)) {
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

  // 7. PUSH NOTIFICATION & DOORBELL CAMERA POPUP TAKEOVER
  if (st.notif_active) {
    const auto &rs = runtime_state();
    if (rs.cameras.doorbell_live_popup_active) {
      frame_base_skip();
    } else {
      frame_base(it, st.col_bg);
    }
    render_notification_overlay(it, ctx, st);
    render_active_focus_overlay(it, st.now_ms, st.theme_primary, st.col_white);
    return true;
  }

  // 8. SHORTCUT ENTITY ACTION CONFIRMATION MODAL
  if (shortcut_confirm_state().open) {
    const auto &sc = shortcut_confirm_state();
    frame_base(it, st.col_bg);
    const esphome::Color brd_c = sc.is_danger ? st.col_red : st.theme_primary;
    const esphome::Color bg_c  = sc.is_danger ? ui::chrome_danger_bg() : kChromeGlassTint;
    ui::confirm_modal(it, st.cx, sc.title.c_str(), sc.line1.c_str(), sc.line2.c_str(),
                      "Cancel", sc.verb_btn.c_str(), brd_c, bg_c,
                      ctx.font_date, ctx.font_small,
                      st.theme_primary, st.col_white, st.col_slate,
                      WidgetId::CONFIRM_CANCEL, WidgetId::CONFIRM_ACCEPT);
    render_active_focus_overlay(it, st.now_ms, st.theme_primary, st.col_white);
    return true;
  }

  // 9. QUICK ACTIONS OVERLAY (D-074)
  if (st.quick_actions_open) {
    render_quick_actions_overlay(it, ctx, st);
    render_active_focus_overlay(it, st.now_ms, st.theme_primary, st.col_white);
    return true;
  }

  // 10. DOCKED SCREENSAVER (Mode 1)
  if (st.display_power_state == 1) {
    frame_base(it, st.col_bg);
    render_docked_screensaver(it, ctx, st);
    return true;
  }

  return false;
}

UiFrameContext &acquire_frame_context() {
  static UiFrameContext *s_ctx = new UiFrameContext{};
  *s_ctx = UiFrameContext{};
  return *s_ctx;
}

const SystemTakeoverState &acquire_takeover_state(const UiFrameContext &ctx) {
  static SystemTakeoverState *s_st = new SystemTakeoverState{};
  *s_st = SystemTakeoverState::from_context(ctx);
  return *s_st;
}

#endif  // __has_include("esphome/core/defines.h")

}  // namespace latter_ui
