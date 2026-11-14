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

#if __has_include("esphome/core/defines.h")

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

  if (rs.nav.gui_page == 4 && rs.nav.system_menu_mode == 8) {
    const int rssi_i = std::isnan(ctx.wifi_rssi) ? 0 : static_cast<int>(std::round(ctx.wifi_rssi));
    const int mv_c = std::isnan(rs.hw_batt_voltage) ? 0 : static_cast<int>(std::round(rs.hw_batt_voltage * 100.0f));
    const int imu_t = std::isnan(rs.hw_imu_temp) ? 0 : static_cast<int>(std::round(rs.hw_imu_temp * 10.0f));
    sig ^= static_cast<uint32_t>((ctx.free_heap_kb << 16) ^ (rssi_i & 0xFFFF));
    sig ^= static_cast<uint32_t>((mv_c & 0xFFFF) ^ ((imu_t & 0xFFF) << 16) ^
                                 (static_cast<int>(ctx.ip_addr.size() & 0x0FU) << 28));
  } else if (rs.nav.gui_page == 1 && rs.nav.home_subpage == 0) {
    int src_cnt = 0;
    if (ctx.local_media_playing || media_state().state == "playing") src_cnt++;
    if (rs.timers.paused || rs.timers.slots[0].paused || rs.timers.slots[1].paused || rs.timers.slots[2].paused) src_cnt++;
    if (prefs.alarms[0].enabled || prefs.alarms[1].enabled || prefs.alarms[2].enabled) src_cnt++;
    if (calendar_state().count > 0 && !calendar_state().summary.empty()) src_cnt++;
    if (src_cnt >= 2) {
      sig ^= static_cast<uint32_t>(ctx.now_ms / kHomeActivityRotateMs) * 0x9E3779B9U;
    }
  }
  return sig;
}

bool compute_live_sec_active(const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page == 0) return true;
  if (rs.nav.gui_page == 1) {
    if (ctx.home_wake_greeting && ctx.now_ms < 26000U) return true;
    if (rs.stopwatch.running || rs.timers.running ||
        rs.timers.slots[0].running || rs.timers.slots[1].running || rs.timers.slots[2].running) {
      return true;
    }
    return false;
  }
  if (rs.nav.gui_page == 2) {
    return ctx.local_media_playing || (media_state().state == "playing");
  }
  if (rs.nav.gui_page == 4 && rs.nav.system_menu_mode == 5 && rs.nav.update_check_state != 0) {
    return true;
  }
  if (rs.nav.gui_page == 8) {
    const int ts = rs.nav.tools_subpage;
    return (ts == 1 || ts == 3 || ts == 4 || ts == 7 || ts == 8 || ts == 10 || ts == 12);
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
