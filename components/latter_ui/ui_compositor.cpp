#include "ui_cadence.h"
#include "ui_compositor.h"
#include "ui_layout.h"
#include "ui_pages.h"
#include "audio_router.h"
#include "hal_display_lifecycle.h"
#include "hal_panel_power.h"
#include "hal_video_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace latter_ui {
using namespace latter_core;

namespace {

DirtyBandTracker g_dirty_band{};

bool is_music_playing_for_compositor(const MediaState &ms, bool local_media_playing, uint32_t now_ms) {
  return is_music_playing_effective(!ms.is_local, local_media_playing, ms.state, now_ms) ||
         (ms.is_local && (local_media_playing || ms.playing ||
                          ms.playback_state == MediaPlaybackState::Playing || ms.state == "playing")) ||
         (!ms.is_local && (ms.playing || ms.playback_state == MediaPlaybackState::Playing));
}

}  // namespace

bool parse_iso_timestamp_components(
    std::string_view raw, ParsedIsoTimestamp &out) noexcept {
  while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
  while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
  if (raw.size() < 16U || raw == "unknown" || raw == "unavailable") return false;

  auto parse_digits = [&](size_t pos, size_t n, int &val) noexcept -> bool {
    if (pos + n > raw.size()) return false;
    int acc = 0;
    for (size_t i = 0; i < n; ++i) {
      const char ch = raw[pos + i];
      if (ch < '0' || ch > '9') return false;
      acc = acc * 10 + (ch - '0');
    }
    val = acc;
    return true;
  };

  int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
  if (!parse_digits(0, 4, yr) || raw[4] != '-' ||
      !parse_digits(5, 2, mo) || raw[7] != '-' ||
      !parse_digits(8, 2, dy)) {
    return false;
  }
  const char sep = raw[10];
  if (sep != 'T' && sep != 't' && sep != ' ') return false;
  if (!parse_digits(11, 2, hr) || raw[13] != ':' || !parse_digits(14, 2, mn)) {
    return false;
  }
  size_t idx = 16U;
  if (idx < raw.size() && raw[idx] == ':') {
    if (!parse_digits(idx + 1U, 2U, sc)) return false;
    idx += 3U;
  }
  if (idx < raw.size() && (raw[idx] == '.' || raw[idx] == ',')) {
    ++idx;
    if (idx >= raw.size() || raw[idx] < '0' || raw[idx] > '9') return false;
    while (idx < raw.size() && raw[idx] >= '0' && raw[idx] <= '9') ++idx;
  }

  bool has_tz = false;
  int tz_off_sec = 0;
  if (idx < raw.size()) {
    const char zc = raw[idx];
    if (zc == 'Z' || zc == 'z') {
      if (idx + 1U != raw.size()) return false;
      has_tz = true;
    } else if (zc == '+' || zc == '-') {
      const int sign = (zc == '-') ? -1 : 1;
      ++idx;
      int oh = 0, om = 0;
      if (!parse_digits(idx, 2U, oh)) return false;
      idx += 2U;
      if (idx < raw.size() && raw[idx] == ':') ++idx;
      if (idx < raw.size()) {
        if (!parse_digits(idx, 2U, om)) return false;
        idx += 2U;
      }
      if (idx != raw.size() || oh > 23 || om > 59) return false;
      has_tz = true;
      tz_off_sec = sign * (oh * 3600 + om * 60);
    } else {
      return false;
    }
  }

  if (mo < 1 || mo > 12 || dy < 1 || dy > 31 || hr < 0 || hr > 23 ||
      mn < 0 || mn > 59 || sc < 0 || sc > 60) {
    return false;
  }

  const int y = yr - (mo <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (mo > 2 ? mo - 3 : mo + 9) + 2) / 5 + dy - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097LL + static_cast<int64_t>(doe) - 719468LL;
  const int64_t wall_sec = days * 86400LL + static_cast<int64_t>(hr) * 3600LL +
                           static_cast<int64_t>(mn) * 60LL + static_cast<int64_t>(sc);

  out.year = yr;
  out.month = mo;
  out.day = dy;
  out.hour = hr;
  out.minute = mn;
  out.second = sc;
  out.has_tz = has_tz;
  out.tz_offset_sec = tz_off_sec;
  out.utc_epoch_sec = wall_sec - static_cast<int64_t>(tz_off_sec);
  return true;
}

void format_hhmm_clock_to(int hr, int mn, bool is_24h, char *out, size_t out_sz) noexcept {
  if (out == nullptr || out_sz == 0U) return;
  const int h = ((hr % 24) + 24) % 24;
  const int m = std::clamp(mn, 0, 59);
  if (is_24h) {
    std::snprintf(out, out_sz, "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(out, out_sz, "%d:%02d %s", h12, m, (h >= 12) ? "PM" : "AM");
  }
}

bool format_iso_time_with_offset(
    std::string_view iso_str, bool is_24h, int local_utc_offset_min,
    char *out, size_t out_sz) noexcept {
  if (out == nullptr || out_sz == 0U) return false;
  ParsedIsoTimestamp ts{};
  if (!parse_iso_timestamp_components(iso_str, ts)) {
    std::snprintf(out, out_sz, "%s", kUnknownGlyph);
    return false;
  }
  if (!ts.has_tz) {
    format_hhmm_clock_to(ts.hour, ts.minute, is_24h, out, out_sz);
    return true;
  }
  const int64_t local_sec = ts.utc_epoch_sec + static_cast<int64_t>(local_utc_offset_min) * 60LL;
  const int rem = static_cast<int>(((local_sec % 86400LL) + 86400LL) % 86400LL);
  format_hhmm_clock_to(rem / 3600, (rem % 3600) / 60, is_24h, out, out_sz);
  return true;
}

SystemTakeoverState SystemTakeoverState::from_context(const UiFrameContext &ctx) {
  const auto &rs = ctx.state();
  const auto &prefs = ctx.prefs();
  SystemTakeoverState st{};
  st.cx = ctx.cx;
  st.cy = ctx.cy;
  st.now_ms = ctx.now_ms;
  st.is_24h_format = prefs.is_24h_format;
  st.ota_in_progress = rs.nav.ota_in_progress;
  st.ota_progress_pct = rs.nav.ota_progress_pct;
  st.is_hibernating = rs.nav.is_hibernating;
  st.hibernate_countdown_val = rs.nav.hibernate_countdown_val;
  st.panel_is_on = ctx.panel_is_on;
  st.display_power_state = rs.nav.display_power_state;
  const bool sc_on = rs.showcase.is_active(ctx.now_ms);
  st.alarm_ringing = rs.alarms.ringing || (sc_on && rs.showcase.silent_alarm);
  st.alarm_ringing_idx = rs.alarms.ringing_idx;
  st.alarm1_recurrence = prefs.alarms[0].recurrence;
  st.alarm2_recurrence = prefs.alarms[1].recurrence;
  st.alarm3_recurrence = prefs.alarms[2].recurrence;
  st.timer_ringing = rs.timers.ringing;
  st.find_my_active = rs.alarms.find_my_active || (sc_on && rs.showcase.silent_find_my);
  st.va_timer_name = rs.timers.va_timer_name;
  st.va_active = rs.voice.active;
  st.va_status_text = rs.voice.status_text;
  st.va_tts_received = rs.voice.tts_received;
  st.va_vad_active = rs.voice.vad_active;
  st.va_intercom_turn = rs.voice.intercom_turn;
  if (!rs.voice.active && rs.voice.status_text.empty()) {
    const uint8_t sc_va = rs.showcase.effective_va_phase(ctx.now_ms);
    if (sc_va != 0U) {
      st.va_active = true;
      st.va_vad_active = (sc_va == static_cast<uint8_t>(ShowcaseVaStage::VAD));
      st.va_tts_received = (sc_va == static_cast<uint8_t>(ShowcaseVaStage::SPEAKING));
      if (sc_va == static_cast<uint8_t>(ShowcaseVaStage::THINKING)) {
        st.va_status_text = UiStrView("Thinking...");
      } else if (sc_va == static_cast<uint8_t>(ShowcaseVaStage::SPEAKING)) {
        st.va_status_text = UiStrView("Responding...");
      } else {
        st.va_status_text = UiStrView("Listening...");
      }
    }
  }
  st.va_visual_profile_opt =
      (sc_on && rs.showcase.va_profile_override >= 0 &&
       rs.showcase.va_profile_override < static_cast<int>(kPrefOptsVaVisualProfile.size()))
          ? UiStrView(pref_opt_cstr(
                kPrefOptsVaVisualProfile,
                static_cast<uint8_t>(rs.showcase.va_profile_override)))
          : ctx.va_visual_profile_opt;
  st.notif_active = rs.alert.active;
  st.notif_title = rs.alert.title;
  st.notif_message = rs.alert.message;
  st.notif_priority = rs.alert.priority;
  st.notif_camera_url = rs.alert.camera_url;
  st.notif_camera_entity = rs.alert.camera_entity;
  st.current_loaded_camera_url = rs.alert.loaded_camera_url;
  const bool sc_ss_override =
      sc_on && rs.showcase.screensaver_style_override >= 0 &&
      rs.showcase.screensaver_style_override < static_cast<int>(kPrefOptsScreensaverStyle.size());
  st.screensaver_style_opt =
      sc_ss_override
          ? UiStrView(pref_opt_cstr(
                kPrefOptsScreensaverStyle,
                static_cast<uint8_t>(rs.showcase.screensaver_style_override)))
          : (prefs.bedtime_active ? UiStrView("Ambient Clock") : ctx.screensaver_style_opt);
  st.screensaver_photo_clock = (!prefs.bedtime_active || sc_ss_override) && prefs.screensaver_photo_clock;
  st.quick_actions_open = rs.nav.quick_actions_open;
  st.theme_primary = ctx.theme_primary;
  st.theme_secondary = ctx.theme_secondary;
  st.col_bg = ctx.col_bg;
  st.col_black = ctx.col_black;
  st.col_white = ctx.col_white;
  st.col_slate = ctx.col_slate;
  st.col_dark_gray = ctx.col_dark_gray;
  st.col_gold = ctx.col_gold;
  st.col_red = ctx.col_red;
  st.col_solaris_gold = ctx.col_solaris_gold;
  return st;
}

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
  const auto &rs = ctx.state();
  const auto &prefs = ctx.prefs();
  if (rs.nav.display_power_state == 1) {
    const std::string_view ss_style(effective_screensaver_style_cstr(prefs, rs, ctx.now_ms));
    if (ss_style == "Photo Frame" || ss_style == "Ambient Clock") {
      uint32_t ss_sig = (ss_style == "Photo Frame") ? 2U : 1U;
      const bool sc_ss_override =
          rs.showcase.is_active(ctx.now_ms) && rs.showcase.screensaver_style_override >= 0;
      if ((!prefs.bedtime_active || sc_ss_override) && prefs.screensaver_photo_clock) ss_sig |= (1U << 2);
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
    if (is_day_night_sky_wallpaper_url(area_ctx_state().wallpaper_url)) {
      const auto &ws_qa = weather_state();
      if (has_known_day_night_phase(ws_qa.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        qa_sig = (qa_sig ^ (resolve_day_night_sky_is_night(ws_qa.sun_state, ctx.now.is_valid(), ctx.now.hour) ? 0x5AU : 0x3CU)) * 16777619U;
      }
    } else if (ctx.home_wallpaper_art.valid()) {
      qa_sig = (qa_sig ^ 0xA5U) * 16777619U;
    }
    qa_sig = (qa_sig ^ static_cast<uint32_t>(prefs.glass_style & 0x03U)) * 16777619U;
    return qa_sig;
  }
  uint32_t sig = batt_u8 | (static_cast<uint32_t>(prefs.glass_style & 0x03U) << 29);
  {
    const bool sky_backdrop_candidate =
        is_day_night_sky_wallpaper_url(area_ctx_state().wallpaper_url) ||
        prefs.weather_background || prefs.weather_app_background;
    if (sky_backdrop_candidate) {
      const auto &ws_sig = weather_state();
      if (has_known_day_night_phase(ws_sig.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        sig |= (1U << 31);
        if (resolve_day_night_sky_is_night(ws_sig.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
          sig |= (1U << 28);
        }
      }
    }
  }
  if (rs.hw_batt_charging) sig |= (1U << 8);
  if (rs.hw_usb_connected) sig |= (1U << 9);
  if (offline_pill_due(ctx)) sig |= (1U << 10);
  if (ui_focus_active(ctx.now_ms)) {
    sig |= (1U << 11) | (static_cast<uint32_t>(ui_focus_index() & 0x3F) << 20);
  }
  sig |= (static_cast<uint32_t>(rs.nav.update_check_state & 0x03) << 12);
  const bool music_shows_action_fb =
      (rs.nav.gui_page == PageId::MUSIC) && (rs.music.view == MusicView::NOW_PLAYING) &&
      (rs.music.drilldown == MusicDrilldown::NONE) && (rs.music.takeover == MusicTakeover::NONE);
  if (music_shows_action_fb && !rs.music.action_feedback.empty() && (ctx.now_ms - rs.music.feedback_timer < 3200U)) sig |= (1U << 14);
  if (!rs.area.action_feedback.empty() && (ctx.now_ms - rs.area.feedback_timer < 3200U)) sig |= (1U << 15);
  if (!rs.remote.action_feedback.empty() && (ctx.now_ms - rs.remote.feedback_timer < 3200U)) sig |= (1U << 16);
  if (!rs.intercom.broadcast_feedback.empty() && (ctx.now_ms - rs.intercom.broadcast_feedback_ms < 2200U)) sig |= (1U << 17);
  if (rs.music.hud_active) sig |= (1U << 18);
  if (ctx.ble_active) sig |= (1U << 19);
  if (!rs.nav.pinned_feedback.empty() && (ctx.now_ms - rs.nav.pinned_feedback_ms < 1800U)) sig |= (1U << 27);

  if (rs.nav.gui_page == PageId::SYSTEM && rs.nav.system_menu_mode == SystemMenuMode::SYSTEM_INFO) {
    const int rssi_i = std::isnan(ctx.wifi_rssi) ? 0 : static_cast<int>(std::round(ctx.wifi_rssi));
    const int mv_c = std::isnan(rs.hw_batt_voltage) ? 0 : static_cast<int>(std::round(rs.hw_batt_voltage * 100.0f));
    const int imu_t = std::isnan(rs.hw_imu_temp) ? 0 : static_cast<int>(std::round(rs.hw_imu_temp * 10.0f));
    sig ^= static_cast<uint32_t>((ctx.free_heap_kb << 16) ^ (rssi_i & 0xFFFF));
    sig ^= static_cast<uint32_t>((mv_c & 0xFFFF) ^ ((imu_t & 0xFFF) << 16) ^
                                 (static_cast<int>(ctx.ip_addr.size() & 0x0FU) << 28));
  } else if (rs.nav.gui_page == PageId::HOME &&
             (rs.nav.home_subpage == HomeSubpage::MAIN || rs.nav.home_subpage == HomeSubpage::SETTINGS)) {
    const uint32_t home_prefs_bits =
        (prefs.home_show_date ? (1U << 0) : 0U) |
        (prefs.home_show_time ? (1U << 1) : 0U) |
        (prefs.home_show_weather ? (1U << 2) : 0U) |
        (prefs.home_show_battery ? (1U << 3) : 0U) |
        (prefs.weather_background ? (1U << 4) : 0U) |
        (prefs.weather_app_background ? (1U << 5) : 0U) |
        (prefs.home_sun ? (1U << 6) : 0U) |
        (prefs.home_moon ? (1U << 7) : 0U) |
        (prefs.wake_greeting ? (1U << 8) : 0U) |
        ((static_cast<uint32_t>(prefs.home_clock_tap_action) & 0x03U) << 9) |
        ((static_cast<uint32_t>(prefs.home_date_tap_action) & 0x03U) << 11) |
        ((static_cast<uint32_t>(prefs.home_weather_tap_action) & 0x03U) << 13);
    sig ^= (home_prefs_bits * 0x9E3779B1U);
    if (rs.nav.home_subpage == HomeSubpage::MAIN) {
      if ((rs.nav.wake_greeting_pending && ctx.now.is_valid()) ||
          is_wake_greeting_visible(ctx.now_ms, ctx.home_wake_greeting)) {
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
      if (rs.timers.running || rs.timers.slots[0].running || rs.timers.slots[1].running || rs.timers.slots[2].running) {
        act_mask |= (1U << 13);
      }
      if (!rs.stopwatch.running && rs.stopwatch.elapsed_ms > 0U) {
        act_mask |= (1U << 12);
      }
      if (rs.stopwatch.running) {
        act_mask |= (1U << 14);
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
    }
  } else if (rs.nav.gui_page == PageId::PINNED) {
    sig ^= (compute_shortcuts_ui_sig() | 1U);
    sig ^= (static_cast<uint32_t>(rs.nav.pinned_menu_page + 1) * 2654435761U);
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
    if (rs.nav.time_subpage == TimeSubpage::CLOCK) {
      for (const char *p = area_ctx_state().local_tz_info.c_str(); *p != '\0'; ++p) {
        th = (th ^ static_cast<unsigned char>(*p)) * 16777619U;
      }
    }
    th = (th ^ rs.stopwatch.elapsed_ms ^ (static_cast<uint32_t>(rs.stopwatch.lap_count) << 24)) * 16777619U;
    th = (th ^ static_cast<uint32_t>(timer_view_slot() + 1)) * 16777619U;
    th = (th ^ static_cast<uint32_t>(rs.timers.duration_sec)) * 16777619U;
    th = (th ^ static_cast<uint32_t>(rs.timers.remaining_sec)) * 16777619U;
    th = (th ^ ((rs.timers.running ? 1U : 0U) | (rs.timers.paused ? 2U : 0U))) * 16777619U;
    for (int i = 0; i < 3; ++i) {
      const auto &ts = rs.timers.slots[i];
      th = (th ^ static_cast<uint32_t>(ts.duration_sec)) * 16777619U;
      th = (th ^ static_cast<uint32_t>(ts.remaining_sec)) * 16777619U;
      th = (th ^ ((ts.running ? 1U : 0U) | (ts.paused ? 2U : 0U))) * 16777619U;
    }
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
    const auto &ac = area_ctx_state();
    uint32_t ar_sig = 2166136261U;
    auto hash_ar = [&](std::string_view sv) {
      for (unsigned char c : sv) ar_sig = (ar_sig ^ c) * 16777619U;
      ar_sig = (ar_sig ^ 0xFFU) * 16777619U;
    };
    hash_ar(rs.area.active_area_id);
    hash_ar(rs.area.active_area_name);
    hash_ar(rs.area.domain_filter);
    hash_ar(rs.area.browse_mode);
    hash_ar(area_available_domains_csv());
    hash_ar(ac.area_climates_summary);
    const int qt = std::isnan(ac.area_temp) ? 0x7FFF : static_cast<int>(std::lround(ac.area_temp * 10.0f));
    const int qh = std::isnan(ac.area_humidity) ? 0x7FFF : static_cast<int>(std::lround(ac.area_humidity * 10.0f));
    ar_sig = (ar_sig ^ static_cast<uint32_t>(qt)) * 16777619U;
    ar_sig = (ar_sig ^ static_cast<uint32_t>(qh)) * 16777619U;
    ar_sig = (ar_sig ^ home_hub_state().local_mutation_seq) * 16777619U;
    ar_sig = (ar_sig ^ static_cast<uint32_t>(rs.area.light_insp_bri_pct + 10)) * 16777619U;
    ar_sig = (ar_sig ^ static_cast<uint32_t>(area_cat_dock_page() + 1)) * 16777619U;
    if (!rs.area.action_feedback.empty() && (ctx.now_ms - rs.area.feedback_timer < 3200U)) {
      hash_ar(rs.area.action_feedback);
    }
    const bool show_areas = (rs.nav.area_subpage == AreaSubpage::PICKER) || (rs.area.browse_mode == "areas");
    const auto ch = show_areas ? latter_rpc::SlotChannel::AREA_LIST : latter_rpc::SlotChannel::AREA_ENTITIES;
    const size_t max_slots =
        show_areas ? DeviceRuntimeState::kListSlotsPerPage : DeviceRuntimeState::kCardSlotsPerPage;
    ar_sig = (ar_sig ^ static_cast<uint32_t>(rs.slot_page_for(ch) + 1)) * 16777619U;
    ar_sig = (ar_sig ^ static_cast<uint32_t>(rs.slot_total_pages_for(ch) + 1)) * 16777619U;
    const BrowseSlotItem *slots = rs.slots_for(ch);
    for (size_t si = 0; si < max_slots; ++si) {
      hash_ar(slots[si].id);
      hash_ar(slots[si].name);
      hash_ar(slots[si].type);
      ar_sig = (ar_sig ^ static_cast<uint32_t>((slots[si].active ? 1 : 0) | ((slots[si].level + 2) << 1))) *
               16777619U;
    }
    sig ^= (ar_sig | 1U);
  } else if (rs.nav.gui_page == PageId::TOOLS) {
    uint32_t tl_sig = 2166136261U;
    for (unsigned char c : effective_tools_items_sv(prefs)) tl_sig = (tl_sig ^ c) * 16777619U;
    tl_sig = (tl_sig ^ static_cast<uint32_t>(rs.nav.tools_menu_page + 1)) * 16777619U;
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
                        ((ctx.now_ms - rs.cal.feedback_ms) < 2400U);
    cl_sig = (cl_sig ^ (fb_vis ? 1U : 0U)) * 16777619U;
    if (fb_vis) {
      for (unsigned char c : rs.cal.feedback_text) cl_sig = (cl_sig ^ c) * 16777619U;
    }
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
        (static_cast<uint32_t>(prefs.podcast_sort_order & 0x03U) << 17) |
        (static_cast<uint32_t>(prefs.music_auto_open & 0x03U) << 19);
    mu_sig = (mu_sig ^ mu_flags) * 16777619U;
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
    hash_sv(ctx.music_auto_open_opt.sv());
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
    if (music_shows_action_fb && !rs.music.action_feedback.empty() && (ctx.now_ms - rs.music.feedback_timer < 3200U)) {
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
    sy_sig = (sy_sig ^ static_cast<uint32_t>(rs.music.settings_page)) * 16777619U;
    sy_sig = (sy_sig ^ device_prefs_state().rev ^ prefs.prefs_seq) * 16777619U;
    sig ^= (sy_sig | 1U);
  }
  return sig;
}

bool compute_live_sec_active(const UiFrameContext &ctx) {
  const auto &rs = ctx.state();
  if (rs.nav.quick_actions_open) return false;
  static uint32_t s_last_music_np_pos_sec = 0xFFFFFFFFU;
  static int s_last_music_np_dur_sec = -1;
  static int s_last_music_audio_vol_pct = -1;
  static uint32_t s_last_home_live_sec_sig = 0xFFFFFFFFU;
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != HomeSubpage::MAIN) {
    s_last_home_live_sec_sig = 0xFFFFFFFFU;
  }
  if (rs.nav.gui_page != PageId::MUSIC ||
      rs.music.view != MusicView::NOW_PLAYING ||
      rs.music.drilldown != MusicDrilldown::NONE ||
      rs.music.takeover != MusicTakeover::NONE) {
    s_last_music_np_pos_sec = 0xFFFFFFFFU;
    s_last_music_np_dur_sec = -1;
  }
  if (rs.nav.gui_page != PageId::MUSIC ||
      rs.music.view != MusicView::AUDIO ||
      rs.music.drilldown != MusicDrilldown::NONE ||
      rs.music.takeover != MusicTakeover::NONE) {
    s_last_music_audio_vol_pct = -1;
  }
  if (rs.nav.gui_page == PageId::CLOCK) return true;
  if (rs.nav.gui_page == PageId::TIME) {
    if (rs.nav.time_subpage == TimeSubpage::CLOCK) return true;
    if (rs.nav.time_subpage == TimeSubpage::TIMERS) {
      return rs.timers.running || rs.timers.slots[0].running ||
             rs.timers.slots[1].running || rs.timers.slots[2].running;
    }
    if (rs.nav.time_subpage == TimeSubpage::STOPWATCH) return rs.stopwatch.running;
    return false;
  }
  if (rs.nav.gui_page == PageId::HOME) {
    if (ctx.home_wake_greeting &&
        ((rs.nav.wake_greeting_pending && ctx.now.is_valid()) || is_wake_greeting_visible(ctx.now_ms, true))) {
      return true;
    }
    if (is_doorbell_or_intercom_active()) return true;
    const bool has_live_timer_or_sw =
        rs.stopwatch.running || rs.timers.running ||
        rs.timers.slots[0].running || rs.timers.slots[1].running || rs.timers.slots[2].running;
    if (!has_live_timer_or_sw) {
      s_last_home_live_sec_sig = 0xFFFFFFFFU;
      return false;
    }
    if (rs.nav.home_subpage == HomeSubpage::ACTIVITY_OVERLAY) return true;
    if (rs.nav.home_subpage != HomeSubpage::MAIN) return false;
    uint32_t cur_sig = 2166136261U;
    auto mix_u32 = [&](uint32_t v) {
      cur_sig ^= v;
      cur_sig *= 16777619U;
    };
    if (rs.timers.running) {
      mix_u32(0x10U);
      mix_u32(static_cast<uint32_t>(rs.timers.remaining_sec));
    }
    for (int i = 0; i < 3; ++i) {
      if (rs.timers.slots[i].running) {
        mix_u32(static_cast<uint32_t>(0x20 + i));
        mix_u32(static_cast<uint32_t>(rs.timers.slots[i].remaining_sec));
      }
    }
    if (rs.stopwatch.running) {
      const uint32_t sw_el_ms =
          (ctx.now_ms >= rs.stopwatch.start_time_ms) ? (ctx.now_ms - rs.stopwatch.start_time_ms) : 0U;
      mix_u32(0x40U);
      mix_u32(sw_el_ms / 1000U);
    }
    if (cur_sig == 0xFFFFFFFFU) cur_sig = 0xFFFFFFFEU;
    if (cur_sig != s_last_home_live_sec_sig) {
      s_last_home_live_sec_sig = cur_sig;
      int act_y0 = 0, act_y1 = 0;
      if (home_activity_band(act_y0, act_y1)) {
        mark_dirty_band(act_y0, act_y1);
      }
    }
    return false;
  }
  if (rs.nav.gui_page == PageId::MUSIC) {
    const auto &ms = media_state();
    const auto &prefs = ctx.prefs();
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
      const int cur_sub = compute_active_subpage_id(rs);
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
    if (rs.music.view == MusicView::AUDIO &&
        rs.music.drilldown == MusicDrilldown::NONE &&
        rs.music.takeover == MusicTakeover::NONE) {
      const int cur_vol = std::clamp(ms.is_local ? ctx.spk_vol_pct : rs.music.remote_vol_pct, 0, 100);
      const int cur_sub = compute_active_subpage_id(rs);
      if (dirty_band().last_page != PageId::MUSIC || dirty_band().last_subpage != cur_sub ||
          s_last_music_audio_vol_pct < 0) {
        s_last_music_audio_vol_pct = cur_vol;
        return false;
      }
      if (cur_vol != s_last_music_audio_vol_pct) {
        s_last_music_audio_vol_pct = cur_vol;
        const bool dock_or_scroll =
            rs.nav.menu_scroll_active ||
            any_dock_anim_active(PageId::MUSIC, 0, ctx.now_ms) ||
            any_vert_bounce_active(PageId::MUSIC, ctx.now_ms);
        if (dock_or_scroll) return true;
        int vy0 = 0, vy1 = SCREEN_H - 1;
        music_volume_dirty_band(vy0, vy1);
        mark_dirty_band(vy0, vy1);
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


namespace {

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
  cached_has_fast_motion_ = false;
  cached_sky_is_fog_ = false;
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
                                     !in.speaker_audio_busy && !in.is_chip_p4 && in.mww_running) &&
                  (in.sky_fast_motion || !in.sky_stepping ||
                   (in.now_ms - in.last_ambient_ms >= kCalmSkyFrameGapMs)))) {
        out.record_ambient_ms = true;
        if (on_home_main && !in.sim_just_changed && in.sky_stepping && !in.sky_fast_motion && in.dock_y0 > 1) {
          calm_home_sky_band(
              in.dock_y0, in.act_y0,
              in.has_act_band && !in.act_sliding && !in.act_fade_settle,
              in.sky_is_fog, SCREEN_H,
              out.band_y0, out.band_y1);
          out.schedule_anim_band = true;
        } else {
          out.schedule_anim_full = true;
        }
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
    const bool deck_rotates = (in.activity_mask != 0U) && (in.home_rotate_due_ms > 0U);
    const bool home_rotate_imminent =
        on_home_main && deck_rotates && (in.home_rotate_due_ms <= 300U);
    prev_home_slot_ = cur_home_slot;
    if (home_rotate_imminent) {
      out.ensure_anim_poller = true;
    }
    if (home_slot_rotated && deck_rotates) {
      out.ensure_anim_poller = true;
      if (in.has_act_band) {
        out.band_y0 = in.act_y0;
        out.band_y1 = in.act_y1;
        out.schedule_band_now = true;
      } else {
        out.schedule_disp_now = true;
      }
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
        in.screensaver_style != "Photo Frame") {
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

  if (m.view == MusicView::LYRICS) {
    if (elapsed_ms >= 6000U) {
      m.hud_active = false;
      return {true, false};
    }
    return {};
  }

  const uint32_t timeout_ms = (m.view == MusicView::AUDIO) ? 5000U : 10000U;
  if (elapsed_ms >= timeout_ms) {
    m.drilldown = MusicDrilldown::NONE;
    m.view = MusicView::NOW_PLAYING;
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
  const bool sc_on = rs.showcase.is_active(now_ms);
  const uint8_t sc_va_phase = rs.showcase.effective_va_phase(now_ms);
  const bool va_visible = rs.voice.active || !rs.voice.status_text.empty() || (sc_va_phase != 0U);
  const bool motion_power = rs.hw_usb_connected || sc_on;

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
      clock_smooth_sweep_active(motion_power, now_ms, latest_in_20ms,
                                effective_clock_face(prefs, rs, now_ms));
  in.interactive_slide =
      rs.nav.menu_scroll_active ||
      any_vert_bounce_active(rs.nav.gui_page, now_ms) ||
      any_page_horiz_bounce_active(rs.nav.gui_page, rs.nav.home_subpage, now_ms) ||
      is_music_lyrics_anim_active(now_ms, speaker_audio_busy);
  in.dock_anim_active = any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage, now_ms);
  in.dock_y0 = dock_dirty_band_y0();
  in.dock_y1 = SCREEN_H - 1;
  in.find_my_active = rs.alarms.find_my_active || (sc_on && rs.showcase.silent_find_my);
  in.ringing_or_critical =
      rs.alarms.find_my_active || rs.alarms.ringing || rs.timers.ringing ||
      (sc_on && (rs.showcase.silent_find_my || rs.showcase.silent_alarm)) ||
      (rs.alert.active && rs.alert.is_critical());
  in.va_visible = va_visible;
  in.va_running = va_running;
  in.tts_received = rs.voice.tts_received || (sc_va_phase == static_cast<uint8_t>(ShowcaseVaStage::SPEAKING));
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

  const std::string_view sim_opt(effective_weather_sim_cstr(device_prefs_state(), rs, now_ms));
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
      cached_has_fast_motion_ =
          cached_has_motion_ &&
          evaluate_sky_has_fast_motion(sim_opt, weather_state().condition,
                                       weather_state().sun_state, now_ms);
      cached_sky_is_fog_ =
          cached_has_motion_ &&
          evaluate_sky_is_fog(sim_opt, weather_state().condition, weather_state().sun_state);
    }
    const uint32_t latest_input_ms =
        std::max(touch_gesture().last_touch_ms, ui_focus_last_ms());
    in.sky_stepping =
        in.sky_enabled &&
        weather_sky_motion_stepping(motion_power, now_ms, latest_input_ms, cached_has_motion_);
    in.sky_fast_motion = cached_has_fast_motion_;
    in.sky_is_fog = cached_sky_is_fog_;
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

  const std::string_view sim_opt(effective_weather_sim_cstr(device_prefs_state(), rs, now_ms));
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
  const bool sc_on = rs.showcase.is_active(now_ms);
  const bool motion_power = rs.hw_usb_connected || sc_on;
  const bool va_visible = rs.voice.active || !rs.voice.status_text.empty() || rs.showcase.has_va_preview(now_ms);
  bool music_hud_changed = false;
  if (rs.nav.display_power_state == 0 && !rs.nav.is_hibernating && !va_visible &&
      rs.nav.gui_page == PageId::MUSIC) {
    if (rs.music.hud_active &&
        touch_gesture().start_page == PageId::MUSIC &&
        touch_gesture().last_touch_ms <= now_ms &&
        touch_gesture().last_touch_ms > rs.music.hud_timer) {
      rs.music.hud_timer = touch_gesture().last_touch_ms;
    }
    if (!(sc_on && rs.showcase.music_hud_lock)) {
      const std::string_view ov_opt(pref_music_overlay_style_cstr(prefs));
      const auto fade_res = step_music_hud_auto_fade(
          rs.music, now_ms, ov_opt, touch_gesture().active);
      if (fade_res.sprung_back_to_now_playing) {
        mode_bar_anim().snap(0);
      }
      music_hud_changed = fade_res.changed;
    }
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
          weather_sky_motion_stepping(motion_power, now_ms, latest_in, has_motion);
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
        clock_smooth_sweep_active(motion_power, now_ms, latest_in_clock,
                                  effective_clock_face(prefs, rs, now_ms));
    in.home_overlay_motion_due =
        home_activity_slide_active(now_ms) ||
        has_pending_home_activity_fade_settle(now_ms) ||
        is_home_activity_carousel_animating(now_ms);
    in.nav_scroll_or_sim_dirty =
        any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage, now_ms) ||
        any_vert_bounce_active(rs.nav.gui_page, now_ms) ||
        any_page_horiz_bounce_active(rs.nav.gui_page, rs.nav.home_subpage, now_ms) ||
        is_music_lyrics_anim_active(now_ms, false) ||
        is_music_lyrics_sync_imminent(now_ms, false) ||
        rs.nav.menu_scroll_active || weather_sim_dirty();
  }

  in.va_busy = va_visible || va_running || rs.voice.tts_received;
  in.alert_or_critical_busy =
      rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active ||
      (sc_on && (rs.showcase.silent_find_my || rs.showcase.silent_alarm)) ||
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
  const bool sc_on = rs.showcase.is_active(now_ms);
  CadenceScreensaverInputs in{};
  in.now_ms = now_ms;
  in.display_power_state = rs.nav.display_power_state;
  in.is_hibernating = rs.nav.is_hibernating;
  in.va_visible = rs.voice.active || !rs.voice.status_text.empty() || rs.showcase.has_va_preview(now_ms);
  in.screensaver_style = effective_screensaver_style_cstr(persisted_prefs(), rs, now_ms);
  in.speaker_audio_busy = speaker_audio_busy;
  in.alert_ringing = rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active ||
                     (sc_on && (rs.showcase.silent_find_my || rs.showcase.silent_alarm));
  in.mww_running = mww_running;
  in.is_chip_p4 = (LATTER_CHIP_P4 != 0);
  in.render_in_flight = disp_render_in_flight(now_ms);
  in.last_ambient_ms = last_ambient_anim_ms();
  in.last_disp_end_ms = last_disp_update_end_ms();
  return evaluate_screensaver_20ms(in);
}

void populate_frame_context_state(UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  ctx.rs_ptr = &rs;
  ctx.prefs_ptr = &prefs;

  ctx.local_media_playing = rs.audio.local_mp_playing();
  ctx.spk_vol_pct = static_cast<int>(std::round(rs.audio.local_mp_volume_or(0.0f) * 100.0f));
  ctx.alert_vol_pct = (prefs.alert_volume < 20) ? kPrefDefaultAlertVolume
                                                : static_cast<int>(prefs.alert_volume);
  ctx.vol_step_pct = (prefs.volume_step_size < 1 || prefs.volume_step_size > 10)
                         ? 5
                         : static_cast<int>(prefs.volume_step_size);

  ctx.track_title = rs.music.last_sendspin_title;
  ctx.track_artist = rs.music.last_sendspin_artist;
  ctx.track_album = rs.music.last_sendspin_album;

  ctx.clock_face_style_opt = effective_clock_face_cstr(prefs, rs, ctx.now_ms);
  ctx.weather_sim_opt = effective_weather_sim_cstr(device_prefs_state(), rs, ctx.now_ms);
  ctx.music_overlay_style_opt = pref_music_overlay_style_cstr(prefs);
  ctx.music_auto_open_opt = pref_music_auto_open_cstr(prefs);
  ctx.music_fullscreen_art_opt = pref_album_art_style_cstr(prefs);
  ctx.music_album_sort_opt = pref_album_sort_order_cstr(prefs);
  ctx.music_artist_album_sort_opt = pref_artist_album_sort_order_cstr(prefs);
  ctx.music_podcast_sort_opt = pref_podcast_sort_order_cstr(prefs);
  ctx.theme_palette_opt = pref_color_profile_cstr(prefs);
  ctx.glass_style_opt = pref_glass_style_cstr(prefs);
  ctx.tap_wake_opt = pref_tap_to_wake_cstr(prefs);
  ctx.pickup_wake_opt = pref_pickup_to_wake_cstr(prefs);
  ctx.display_off_timeout_opt = pref_battery_screen_timeout_cstr(prefs);
  ctx.screensaver_mode_opt = pref_screensaver_mode_cstr(prefs);
  ctx.screensaver_style_opt = effective_screensaver_style_cstr(prefs, rs, ctx.now_ms);
  ctx.screensaver_docked_timeout_opt = pref_docked_idle_timeout_cstr(prefs);
  ctx.hibernate_timeout_opt = pref_battery_hibernate_timeout_cstr(prefs);
  ctx.bedtime_display_opt = pref_bedtime_display_cstr(prefs);
  ctx.default_start_app_opt = pref_start_screen_cstr(prefs);
  ctx.auto_brightness_opt = pref_auto_brightness_mode_cstr(prefs);
  ctx.manual_brightness_pct = static_cast<int>(prefs.manual_brightness_pct());
  ctx.day_brightness_pct = static_cast<int>(prefs.day_brightness_pct());
  ctx.night_brightness_pct = static_cast<int>(prefs.night_brightness_pct());
  ctx.va_wake_word_mode_opt = pref_wake_word_mode_cstr(prefs);
  ctx.va_visual_profile_opt = effective_va_visual_profile_cstr(prefs, rs, ctx.now_ms);
  ctx.ww_sens_okay_nabu_opt = pref_ww_sens_cstr(prefs.ww_sens_okay_nabu);
  ctx.ww_sens_hey_luna_opt = pref_ww_sens_cstr(prefs.ww_sens_hey_luna);
  ctx.ww_sens_hey_jarvis_opt = pref_ww_sens_cstr(prefs.ww_sens_hey_jarvis);
  ctx.ww_sens_hey_mycroft_opt = pref_ww_sens_cstr(prefs.ww_sens_hey_mycroft);
  ctx.ww_sens_alexa_opt = pref_ww_sens_cstr(prefs.ww_sens_alexa);

  ctx.day_start_hr = static_cast<int>(prefs.day_start_hour);
  ctx.night_start_hr = static_cast<int>(prefs.night_start_hour);
  ctx.home_weather_bg = prefs.weather_background;
  ctx.weather_app_bg = prefs.weather_app_background;
  ctx.home_wake_greeting = prefs.wake_greeting;
  ctx.home_sun_arc = prefs.home_sun;
  ctx.home_moon_phase = prefs.home_moon;
  ctx.game_sound_enabled = prefs.game_sound_effects;
  ctx.va_wake_screen = prefs.wake_screen_on_assist;
  ctx.screensaver_photo_clock = prefs.screensaver_photo_clock;
  ctx.bedtime_active = prefs.bedtime_active;
  ctx.do_not_disturb = prefs.do_not_disturb;
}

bool prepare_frame_plan(const UiFrameContext &ctx, FrameRenderPlan &out) {
  out = FrameRenderPlan{};
  const auto &rs = ctx.state();
  const auto &prefs = ctx.prefs();
  const int prof_sub = compute_active_subpage_id(rs);

  const auto &ac_wall = area_ctx_state();
  const bool has_day_night_sky = is_day_night_sky_wallpaper_url(ac_wall.wallpaper_url);
  const bool has_wallpaper = (!ac_wall.wallpaper_url.empty() &&
                              !has_day_night_sky &&
                              ac_wall.wallpaper_url == rs.music.loaded_wallpaper_url &&
                              ctx.home_wallpaper_art.width > 0);
  out.cam_live_canvas =
      (rs.nav.gui_page == PageId::CAMERAS &&
       rs.cameras.tab != CamerasTab::SETTINGS &&
       !rs.cameras.quick_reply_open);
  const bool tools_full_bleed =
      (rs.nav.gui_page == PageId::TOOLS &&
       rs.nav.tools_subpage == ToolsSubpage::FLASHLIGHT) ||
      out.cam_live_canvas;
  const bool weather_sim_active =
      (!ctx.weather_sim_opt.empty() && ctx.weather_sim_opt != "Live (Home Assistant)");
  out.bd_mode = resolve_backdrop_mode(
      rs.nav.gui_page, rs.nav.home_subpage, ctx.home_weather_bg, ctx.weather_app_bg,
      weather_sim_active, has_wallpaper, has_day_night_sky,
      tools_full_bleed, ctx.music_fullscreen_art_opt == "None");
  out.weather_sky_active = (out.bd_mode == BackdropMode::LiveWeatherSky);
  const auto &ws_cad = weather_state();
  const bool weather_ready_cad =
      weather_sim_active ||
      (!std::isnan(ws_cad.temperature) && !ws_cad.condition.empty() &&
       !ws_cad.sun_state.empty() && ws_cad.sun_state != "unknown" &&
       ws_cad.sun_state != "unavailable");
  const uint32_t latest_input_ms = std::max(
      touch_gesture().last_touch_ms, ui_focus_last_ms());
  const bool motion_power_gui = rs.hw_usb_connected || rs.showcase.is_active(ctx.now_ms);
  out.on_home_main =
      (rs.nav.gui_page == PageId::HOME &&
       rs.nav.home_subpage == HomeSubpage::MAIN);
  const bool w_has_motion =
      out.weather_sky_active &&
      evaluate_sky_has_motion(ctx.weather_sim_opt, ws_cad.condition, ws_cad.sun_state, ctx.now_ms);
  out.w_has_fast_motion =
      w_has_motion &&
      evaluate_sky_has_fast_motion(ctx.weather_sim_opt, ws_cad.condition, ws_cad.sun_state, ctx.now_ms);
  out.w_is_fog =
      w_has_motion &&
      evaluate_sky_is_fog(ctx.weather_sim_opt, ws_cad.condition, ws_cad.sun_state);
  out.weather_sky_stepping =
      out.weather_sky_active && weather_ready_cad &&
      weather_sky_motion_stepping(motion_power_gui, ctx.now_ms, latest_input_ms, w_has_motion);
  const bool weather_sky_full_hint =
      out.weather_sky_stepping && (out.w_has_fast_motion || !out.on_home_main);

  const bool clock_sweep_hint =
      (rs.nav.gui_page == PageId::CLOCK) &&
      clock_smooth_sweep_active(
          motion_power_gui, ctx.now_ms, latest_input_ms,
          effective_clock_face(prefs, rs, ctx.now_ms));
  const bool time_sw_hint =
      ((rs.nav.gui_page == PageId::TIME &&
        rs.nav.time_subpage == TimeSubpage::STOPWATCH) ||
       (rs.nav.gui_page == PageId::TOOLS &&
        rs.nav.tools_subpage == ToolsSubpage::STOPWATCH)) &&
      rs.stopwatch.running;

  out.takeover_st = acquire_takeover_state(ctx);
  out.takeover_sig = compute_takeover_signature(out.takeover_st, rs);
  const bool photo_frame_art_ready =
      resolve_photo_frame_art(
          ctx.screensaver_photo_art, ctx.home_wallpaper_art,
          ac_wall.screensaver_photo_url, rs.music.loaded_wallpaper_url).valid();
  if (is_photo_frame_initial_load_pending(
          out.takeover_sig, out.takeover_st.screensaver_style_opt,
          ac_wall.screensaver_photo_url, photo_frame_art_ready,
          rs.music.screensaver_photo_failed,
          rs.nav.standby_entered_ms, ctx.now_ms)) {
    return false;
  }

  out.dock_anim_now =
      any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage, ctx.now_ms);
  out.page_horiz_bounce_now =
      any_page_horiz_bounce_active(rs.nav.gui_page, rs.nav.home_subpage, ctx.now_ms);
  out.vert_bounce_now =
      any_vert_bounce_active(rs.nav.gui_page, ctx.now_ms);
  out.lyrics_anim_now =
      is_music_lyrics_anim_active(ctx.now_ms, ctx.local_media_playing);
  out.greet_fading_now =
      is_wake_greeting_fading(ctx.now_ms, ctx.home_wake_greeting);
  out.has_act_band = home_activity_band(out.act_y0, out.act_y1);

  out.alert_ringing = rs.alarms.ringing || rs.timers.ringing || rs.alarms.find_my_active ||
                      rs.showcase.has_silent_alert(ctx.now_ms);
  out.va_visible = should_va_takeover_preempt(
      rs.voice.active, !rs.voice.status_text.empty(), rs.voice.intercom_turn,
      rs.nav.gui_page, rs.cameras.doorbell_live_popup_active) ||
      rs.showcase.has_va_preview(ctx.now_ms);

  const bool static_screensaver_only =
      (out.takeover_sig == (1U << 3)) &&
      (out.takeover_st.screensaver_style_opt == "Ambient Clock" ||
       out.takeover_st.screensaver_style_opt == "Photo Frame");
  const bool quick_actions_only = (out.takeover_sig == (1U << 11));
  const bool anim_or_scroll =
      ((out.takeover_sig != 0U) && !static_screensaver_only && !quick_actions_only) ||
      (!quick_actions_only &&
       (weather_sky_full_hint ||
        clock_sweep_hint ||
        time_sw_hint ||
        out.lyrics_anim_now ||
        out.greet_fading_now ||
        out.vert_bounce_now ||
        out.page_horiz_bounce_now ||
        rs.nav.menu_scroll_active ||
        (rs.nav.gui_page == 3 && rs.nav.active_game > 0)));
  const bool photo_frame_no_clock =
      static_screensaver_only &&
      (out.takeover_st.screensaver_style_opt == "Photo Frame") &&
      !out.takeover_st.screensaver_photo_clock &&
      photo_frame_art_ready;
  const bool home_no_clock_or_date =
      (out.takeover_sig == 0U) &&
      (rs.nav.gui_page == PageId::HOME) &&
      (rs.nav.home_subpage == HomeSubpage::MAIN) &&
      !prefs.home_show_date && !prefs.home_show_time;
  const int hhmm_key = (photo_frame_no_clock || quick_actions_only || home_no_clock_or_date)
                           ? -1
                           : compute_hhmm_key(ctx.now);
  const uint32_t idle_state_sig = compute_idle_state_signature(ctx);
  const bool live_sec_active = compute_live_sec_active(ctx);
  const int home_clock_band_y1 =
      (layout().home_clock_y + layout().home_weather_y) / 2 + dp_px(10);
  if (!dirty_band().begin_frame(
          rs.nav.gui_page, prof_sub, out.takeover_sig, anim_or_scroll,
          hhmm_key, idle_state_sig, home_clock_band_y1, live_sec_active)) {
    return false;
  }

  out.band_y0 = dirty_band().band_y0();
  out.band_y1 = dirty_band().band_y1();
  return true;
}

void render_composed_frame(Rgb565Surface &it, const UiFrameContext &ctx, const FrameRenderPlan &plan) {
  if (render_system_takeovers(it, ctx, plan.takeover_st)) {
    return;
  }

  const auto &rs = ctx.state();
  if (rs.nav.gui_page != 1 && rs.nav.gui_page != 2) {
    // Unified BackdropMode across System (4), Weather (5), Area (6), Remote (7), Tools (8),
    // Home Hub (9), Time (10), Pinned (11), Calendar (12), Cameras (13)
    // Skip P4 between-frames PPA clear while full-screen live video is active so DPI scanout keeps full PSRAM bandwidth!
    if (plan.cam_live_canvas) {
      frame_base_skip();
    } else if (plan.bd_mode == BackdropMode::DayNightSky) {
      const auto &ws_bd = weather_state();
      if (has_known_day_night_phase(ws_bd.sun_state, ctx.now.is_valid(), ctx.now.hour)) {
        const bool dns_night = resolve_day_night_sky_is_night(
            ws_bd.sun_state, ctx.now.is_valid(), ctx.now.hour);
        render_static_day_night_sky_base(it, ctx.cx, ctx.cy, dns_night);
      } else {
        frame_base(it, ctx.col_bg);
      }
    } else {
      const bool use_page_wallpaper = (plan.bd_mode == BackdropMode::CustomWallpaper);
      if (rs.nav.gui_page != 5 || use_page_wallpaper) {
        render_page_wallpaper_base(it, ctx.home_wallpaper_art, use_page_wallpaper, ctx.cx, ctx.col_bg);
      }
    }
  }

  render_active_page(it, ctx);

  const bool cam_hud_hidden =
      (rs.nav.gui_page == PageId::CAMERAS &&
       (rs.cameras.tab == CamerasTab::SINGLE ||
        rs.cameras.tab == CamerasTab::CAROUSEL) &&
       (!rs.cameras.hud_active || rs.voice.intercom_turn) &&
       !rs.intercom.cam_call_active && !rs.intercom.cam_listen_active);
  const bool show_home_pill =
      !rs.nav.is_hibernating && !rs.nav.ota_in_progress && !plan.va_visible &&
      !cam_hud_hidden && !rs.voice.intercom_turn &&
      rs.nav.display_power_state == 0 &&
      can_go_home(
          rs.nav.gui_page, rs.nav.system_menu_mode, rs.nav.active_game,
          rs.nav.area_subpage, rs.area.browse_mode == "areas", rs.nav.remote_subpage, rs.nav.tools_subpage,
          rs.alarms.edit_idx, plan.alert_ringing) &&
      !music_art_only(rs.nav.gui_page, ctx.music_overlay_style_opt == "Always Visible",
                      rs.music.hud_active);

  if (!rs.nav.is_hibernating && !rs.nav.ota_in_progress && !plan.va_visible &&
      rs.nav.display_power_state == 0 && !plan.alert_ringing) {
    render_active_focus_overlay(it, ctx.now_ms, ctx.theme_primary, ctx.col_white);
  }

  if (show_home_pill) {
    render_home_indicator_bar(it, ctx.cx, layout().home_bar_y);
  }

  if (!rs.nav.is_hibernating && !rs.nav.ota_in_progress && rs.nav.display_power_state == 0) {
    render_offline_pill(it, ctx);
  }
}

PostFrameSchedule compute_post_frame_schedule(const UiFrameContext &ctx, const FrameRenderPlan &plan) {
  PostFrameSchedule out{};
  if (plan.takeover_sig != 0U) {
    return out;
  }
  const auto &rs = ctx.state();
  const bool coin_toss_active =
      (rs.nav.gui_page == PageId::TOOLS) &&
      (rs.nav.tools_subpage == ToolsSubpage::COIN_FLIP) &&
      coin_state().anim_active;
  const bool act_sliding_now = home_activity_slide_active(ctx.now_ms);

  if (rs.nav.menu_scroll_active || plan.vert_bounce_now || plan.page_horiz_bounce_now ||
      coin_toss_active || plan.greet_fading_now || plan.lyrics_anim_now) {
    out.anim_full = true;
  } else if (plan.dock_anim_now || any_dock_anim_active(rs.nav.gui_page, rs.nav.home_subpage, ctx.now_ms)) {
    out.anim_band = true;
    out.anim_band_y0 = dock_dirty_band_y0();
    out.anim_band_y1 = SCREEN_H - 1;
  } else if (plan.weather_sky_active && !plan.va_visible && !plan.alert_ringing) {
    if (act_sliding_now) {
      out.anim_full = true;
    } else if (plan.weather_sky_stepping) {
      const bool spk_busy = rs.audio.speaker_busy();
      const bool mww_busy = !spk_busy && !LATTER_CHIP_P4 && rs.audio.mww_running();
      const uint32_t target_gap_ms = min_ambient_frame_gap_ms(spk_busy, false, false, mww_busy);
      out.weather_gap_ms = weather_sky_frame_gap_ms(target_gap_ms, plan.w_has_fast_motion);
      out.record_ambient_ms = true;
      if (plan.on_home_main && !plan.w_has_fast_motion) {
        out.weather_band = true;
        calm_home_sky_band(
            dock_dirty_band_y0(), plan.act_y0,
            plan.has_act_band && !act_sliding_now,
            plan.w_is_fog, SCREEN_H,
            out.weather_band_y0, out.weather_band_y1);
      } else {
        out.weather_full = true;
      }
    }
  } else if (act_sliding_now && plan.has_act_band) {
    out.anim_band = true;
    out.anim_band_y0 = plan.act_y0;
    out.anim_band_y1 = plan.act_y1;
  }

  if (plan.on_home_main && !plan.va_visible && !plan.alert_ringing) {
    if (is_wake_greeting_visible(ctx.now_ms, ctx.home_wake_greeting) &&
        !plan.greet_fading_now &&
        !out.weather_band && !out.weather_full &&
        !is_weather_step_pending(ctx.now_ms)) {
      const uint32_t g_el = (ctx.now_ms >= wake_greeting_start_ms())
                                ? (ctx.now_ms - wake_greeting_start_ms())
                                : 0U;
      if (g_el < kWakeGreetingHoldMs) {
        out.wake_greeting_hold_ms = kWakeGreetingHoldMs - g_el;
      }
    }
    const uint32_t rot_due_ms = home_activity_next_rotate_due_ms(ctx.now_ms);
    if (rot_due_ms > 0U && plan.has_act_band) {
      out.activity_rotate_ms = rot_due_ms;
      out.activity_rotate_y0 = plan.act_y0;
      out.activity_rotate_y1 = plan.act_y1;
    }
  }
  return out;
}

}  // namespace latter_ui
