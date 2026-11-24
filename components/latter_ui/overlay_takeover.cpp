#include "ui_compositor.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace latter_ui {
using namespace latter_core;

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

void render_active_focus_overlay(Rgb565Surface &it, uint32_t now_ms,
                                 UiColor theme_primary, UiColor col_white) {
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

void render_alert_ringing_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st) {
  const bool pulse = ((st.now_ms / 250U) % 2U) != 0U;
  const UiColor alert_color = st.find_my_active
                                  ? st.col_white
                                  : (st.alarm_ringing ? ctx.col_red : ctx.theme_secondary);
  const UiColor ring_c = pulse ? alert_color
                               : (st.find_my_active ? ctx.theme_secondary : st.col_dark_gray);
  render_bezel_alert_border(it, st.cx, st.cy, ring_c);

  const int cta_h = std::max(44, scale_y(48));

  if (st.find_my_active) {
    const int base_r = scale_min(50);
    const uint32_t max_span = static_cast<uint32_t>(std::max(scale_min(60), SCREEN_MIN_R - scale_min(76)));
    const int radar_r = base_r + static_cast<int>((st.now_ms / 8U) % max_span);
    fast_circle(it, st.cx, st.cy, radar_r, ctx.theme_primary);
    fast_circle(it, st.cx, st.cy, radar_r + 1, pulse ? st.col_white : ctx.theme_secondary);
    const int radar_r2 = base_r + static_cast<int>(((st.now_ms / 8U) + (max_span / 2U)) % max_span);
    fast_circle(it, st.cx, st.cy, radar_r2, ctx.theme_secondary);

    ui::shadow_print(it, st.cx, scale_y(75), ctx.font_small,
                     pulse ? st.col_white : ctx.theme_secondary, UiTextAlign::CENTER, "LOCATING DEVICE");
    ui::shadow_print(it, st.cx, scale_y(156), ctx.font_date, st.col_white, UiTextAlign::CENTER, "LATTER");
    ui::shadow_print(it, st.cx, scale_y(196), ctx.font_date, ctx.theme_primary, UiTextAlign::CENTER, "COMPANION");
    ui::shadow_print(it, st.cx, scale_y(244), ctx.font_small, ctx.theme_secondary,
                     UiTextAlign::CENTER, "Acoustic Beacon Active");

    const int btn_y = scale_y(330);
    const int btn_w = std::min({kMaxContentColW, safe_span(btn_y, 18), scale_x(240)});
    ui::action_pill_centered(it, st.cx, btn_w, btn_y, cta_h,
                             ctx.theme_primary, kChromeGlassTint,
                             "Tap to Dismiss", st.col_white, ctx.font_small,
                             WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 0, cta_h + scale_y(16));
    return;
  }

  if (st.alarm_ringing) {
    ui::shadow_print(it, st.cx, scale_y(75), ctx.font_small,
                     pulse ? ctx.theme_secondary : st.col_white, UiTextAlign::CENTER, "ALARM RINGING");

    char cur_t_buf[16];
    std::snprintf(cur_t_buf, sizeof(cur_t_buf), "%s", kUnknownGlyph);
    char ampm_buf[8] = "";
    if (ctx.now.is_valid()) {
      format_time_hhmm(ctx.now.hour, ctx.now.minute, st.is_24h_format,
                       cur_t_buf, sizeof(cur_t_buf), ampm_buf);
    }
    ui::shadow_print(it, st.cx, scale_y(160), ctx.font_time, st.col_white, UiTextAlign::CENTER, cur_t_buf);
    if (!st.is_24h_format && ampm_buf[0] != '\0') {
      int tw = 0, xo = 0, base = 0, th = 0;
      if (ctx.font_time != nullptr) ctx.font_time->measure(cur_t_buf, &tw, &xo, &base, &th);
      const int ap_x = st.cx + (tw / 2) + scale_x(10);
      ui::shadow_print(it, ap_x, scale_y(175), ctx.font_small, ctx.theme_secondary,
                       UiTextAlign::CENTER_LEFT, ampm_buf);
    }

    const int r_idx = (st.alarm_ringing_idx == 3)
                          ? st.alarm3_recurrence
                          : ((st.alarm_ringing_idx == 2) ? st.alarm2_recurrence : st.alarm1_recurrence);
    static constexpr const char *kRecNames[4] = {"Daily", "Weekdays", "Weekends", "Once"};
    ui::shadow_print(it, st.cx, scale_y(225), ctx.font_small, ctx.theme_primary,
                     UiTextAlign::CENTER, kRecNames[r_idx & 3]);

    const int snz_y = scale_y(295);
    const int snz_w = std::min({kMaxContentColW, safe_span(snz_y, 18), scale_x(232)});
    ui::action_pill_centered(it, st.cx, snz_w, snz_y, cta_h,
                             ctx.theme_secondary, kChromeGlassTint,
                             "Snooze (+5m)", st.col_white, ctx.font_small,
                             WidgetId::ALERT_SNOOZE_BTN, kHitTap | kHitFocusSlot, 0, cta_h + scale_y(12));

    const int dis_y = scale_y(364);
    const int dis_w = std::min({kMaxContentColW, safe_span(dis_y, 18), scale_x(216)});
    ui::action_pill_centered(it, st.cx, dis_w, dis_y, cta_h,
                             ctx.col_red, ui::chrome_danger_bg(),
                             "Dismiss", st.col_white, ctx.font_small,
                             WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 1, cta_h + scale_y(12));
    return;
  }

  ui::shadow_print(it, st.cx, scale_y(75), ctx.font_small,
                   pulse ? ctx.theme_secondary : st.col_white, UiTextAlign::CENTER, "TIMER EXPIRED");
  ui::shadow_print(it, st.cx, scale_y(170), ctx.font_time, st.col_white, UiTextAlign::CENTER, "00:00");
  const std::string t_lbl = st.va_timer_name.empty()
                                ? std::string("Time is up!")
                                : fit_text_to_width_font(st.va_timer_name.str(),
                                                         std::min(kMaxContentColW, safe_span(scale_y(230), 18)),
                                                         ctx.font_small);
  ui::shadow_print(it, st.cx, scale_y(230), ctx.font_small, ctx.theme_secondary,
                   UiTextAlign::CENTER, t_lbl.c_str());

  const int btn_y = scale_y(330);
  const int btn_w = std::min({kMaxContentColW, safe_span(btn_y, 18), scale_x(232)});
  ui::action_pill_centered(it, st.cx, btn_w, btn_y, cta_h,
                           ctx.theme_primary, kChromeGlassTint,
                           "Dismiss", st.col_white, ctx.font_small,
                           WidgetId::ALERT_DISMISS_BTN, kHitTap | kHitFocusSlot, 0, cta_h + scale_y(16));
}

bool render_system_takeovers(Rgb565Surface &it, const UiFrameContext &ctx,
                             const SystemTakeoverState &st) {
  // 1. OTA Firmware Update In Progress
  if (st.ota_in_progress) {
    frame_base(it, st.col_bg);
    const int pct = std::max(0, std::min(100, st.ota_progress_pct));
    if constexpr (LATTER_ROUND_DISPLAY != 0) {
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
    fast_print(it, st.cx, st.cy - scale_y(60), ctx.font_date, st.col_white, UiTextAlign::CENTER, "FIRMWARE UPDATE");
    char prog_buf[32];
    std::snprintf(prog_buf, sizeof(prog_buf), "%d%%", pct);
    fast_print(it, st.cx, st.cy + scale_y(8), ctx.font_time, st.col_white, UiTextAlign::CENTER, prog_buf);
    const char *status_msg = "Preparing flash...";
    if (pct >= 99) status_msg = "Rebooting device...";
    else if (pct >= 85) status_msg = "Verifying image...";
    else if (pct >= 10) status_msg = "Writing flash memory...";
    fast_print(it, st.cx, st.cy + scale_y(65), ctx.font_small, st.col_slate, UiTextAlign::CENTER, status_msg);
    return true;
  }

  // 2. Hibernation Shutdown Countdown
  if (st.is_hibernating) {
    frame_base(it, st.col_bg);
    if (st.hibernate_countdown_val > 0) {
      fast_print(it, st.cx, st.cy - scale_y(65), ctx.font_date, st.col_white, UiTextAlign::CENTER, "HIBERNATING");
      char c_buf[16];
      std::snprintf(c_buf, sizeof(c_buf), "%d", st.hibernate_countdown_val);
      fast_print(it, st.cx, st.cy + scale_y(5), ctx.font_chrono_time, st.theme_secondary, UiTextAlign::CENTER, c_buf);
      fast_print(it, st.cx, st.cy + scale_y(65), ctx.font_small, st.col_slate, UiTextAlign::CENTER, "Tap screen to stay awake");
    } else {
      fast_print(it, st.cx, st.cy - scale_y(10), ctx.font_date, st.col_white, UiTextAlign::CENTER, "HIBERNATING");
      fast_print(it, st.cx, st.cy + scale_y(30), ctx.font_small, st.col_slate, UiTextAlign::CENTER, "Shutting down...");
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
    const UiColor brd_c = sc.is_danger ? st.col_red : st.theme_primary;
    const UiColor bg_c = sc.is_danger ? ui::chrome_danger_bg() : kChromeGlassTint;
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
#if LATTER_GUI_UNIVERSAL
    render_docked_screensaver(it, ctx, st);
#endif
    return true;
  }

  return false;
}

}  // namespace latter_ui
