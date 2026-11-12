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

  has_pending_band = false;
  force_full = false;
  frame_hits().clear();
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
