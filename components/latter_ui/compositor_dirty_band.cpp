#include "ui_compositor.h"
#include "audio_router.h"
#include "hal_video_stream.h"

#include <algorithm>
#include <cmath>

namespace latter_ui {

namespace {

DirtyBandTracker g_dirty_band{};

bool is_music_playing_for_compositor(const MediaState &ms, bool local_media_playing, uint32_t now_ms) {
  return is_music_playing_effective(!ms.is_local, local_media_playing, ms.state, now_ms) ||
         (ms.is_local && (local_media_playing || ms.playing ||
                          ms.playback_state == MediaPlaybackState::Playing || ms.state == "playing")) ||
         (!ms.is_local && (ms.playing || ms.playback_state == MediaPlaybackState::Playing));
}

}  // namespace

DirtyBandTracker &dirty_band() { return g_dirty_band; }

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
  if (runtime_state().nav.ui_dirty_full) {
    runtime_state().nav.ui_dirty_full = false;
    force_full = true;
  }
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
  if (runtime_state().nav.ui_dirty_full) {
    runtime_state().nav.ui_dirty_full = false;
    force_full = true;
  }
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

  active_idle_skip = true;
  active_y0 = 0;
  active_y1 = 0;
  active_full = false;
  return false;
}

void mark_dirty_band(int y0, int y1) { dirty_band().mark_band(y0, y1); }
void mark_dirty_full() { dirty_band().mark_full(); }
void mark_music_progress_dirty_band(int y0, int y1) { dirty_band().mark_band(y0, y1); }

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
  if (offline_pill_due(ctx)) sig |= (1U << 10);
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
        if (mins_u > 0 && mins_u <= kUpcomingAlarmDeckMinutes) act_mask |= (1U << (6 + ai * 2));
        if (mins_u > 0 && mins_u <= kUpcomingAlarmPinMinutes) act_mask |= (1U << (7 + ai * 2));
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
    for (const char *p = prefs.local_clock_label; *p != '\0'; ++p) th = (th ^ static_cast<unsigned char>(*p)) * 16777619U;
    th = (th ^ 0xFFU) * 16777619U;
    for (const char *p = prefs.world_clocks_spec; *p != '\0'; ++p) th = (th ^ static_cast<unsigned char>(*p)) * 16777619U;
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
    for (unsigned char c : effective_tools_items_sv(prefs)) tl_sig = (tl_sig ^ c) * 16777619U;
    tl_sig = (tl_sig ^ rs.stopwatch.elapsed_ms ^ (static_cast<uint32_t>(rs.stopwatch.lap_count) << 24)) * 16777619U;
    sig ^= (tl_sig | 1U);
  } else if (rs.nav.gui_page == PageId::GAMES) {
    uint32_t gm_sig = 2166136261U;
    for (unsigned char c : effective_games_items_sv(prefs)) gm_sig = (gm_sig ^ c) * 16777619U;
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

}  // namespace latter_ui
