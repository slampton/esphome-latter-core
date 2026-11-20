#include "esphome/core/defines.h"
#include "ui_pages.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace latter_ui {

namespace {

const char *alarm_recurrence_label(uint8_t recurrence) {
  switch (recurrence & 3U) {
    case 1: return "Weekdays";
    case 2: return "Weekends";
    case 3: return "Once";
    default: return "Daily";
  }
}

void format_stopwatch_tenths(uint32_t ms, char *out_buf, size_t out_sz) {
  const uint32_t total_s = ms / 1000U;
  const uint32_t tenths = (ms % 1000U) / 100U;
  if (total_s >= 3600U) {
    std::snprintf(out_buf, out_sz, "%u:%02u:%02u.%u",
                  static_cast<unsigned>(total_s / 3600U),
                  static_cast<unsigned>((total_s % 3600U) / 60U),
                  static_cast<unsigned>(total_s % 60U),
                  static_cast<unsigned>(tenths));
  } else {
    std::snprintf(out_buf, out_sz, "%02u:%02u.%u",
                  static_cast<unsigned>(total_s / 60U),
                  static_cast<unsigned>(total_s % 60U),
                  static_cast<unsigned>(tenths));
  }
}

void draw_time_mode_icon(esphome::display::Display &it, int i,
                         int bx, int by, int ir,
                         esphome::Color fg_c, bool shadow, void *) {
  if (i == TimeSubpage::CLOCK) {
    ui::draw_icon_clock_solid(it, bx, by, ir, fg_c, shadow);
  } else if (i == TimeSubpage::TIMERS) {
    ui::draw_icon_hourglass(it, bx, by, ir, fg_c, shadow);
  } else if (i == TimeSubpage::ALARMS) {
    ui::draw_icon_alarm_clock(it, bx, by, ir, fg_c, shadow);
  } else {
    ui::draw_icon_stopwatch(it, bx, by, ir, fg_c, shadow);
  }
}

void render_time_mode_bar(esphome::display::Display &it, const UiFrameContext &ctx, int active_sub) {
  ui::mode_bar(it, ctx.cx, 4, active_sub, 0, ctx.theme_primary, ctx.col_white,
               draw_time_mode_icon, nullptr);
}

void render_time_clock_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const int cx = ctx.cx;

  render_page_header(it, cx, "TIME", ctx.font_small, ctx.col_white);

  WorldClockEntry entries[kMaxWorldClocks + 1]{};
  const int total_wc = collect_active_world_clocks(prefs, ac, entries);
  const bool local_only = (total_wc <= 0);
  const bool has_label = (prefs.local_clock_label[0] != '\0');
  const bool has_bd_shadow = backdrop_active();

  // 1. Optional user-defined Local Clock Label above the local time readout
  const int time_y = local_only ? scale_y(has_label ? 172 : 160)
                                : scale_y(has_label ? 142 : 134);
  const int tz_y = time_y + scale_y(local_only ? 54 : 50);

  if (has_label) {
    const int label_y = local_only ? (time_y - scale_y(50)) : scale_y(94);
    auto *lbl_font = (local_only && SCREEN_W >= 320) ? ctx.font_date : ctx.font_small;
    const std::string lbl_fit = fit_text_to_width_font(
        prefs.local_clock_label, safe_span(label_y, 18), lbl_font);
    if (has_bd_shadow) {
      fast_print(it, cx + 1, label_y + 1, lbl_font, kChromeShadow, TextAlign::CENTER, lbl_fit.c_str());
    }
    fast_print(it, cx, label_y, lbl_font, ctx.theme_secondary, TextAlign::CENTER, lbl_fit.c_str());
  }

  // 2. Live HH:MM + right-stacked :SS (+ AM/PM in 12h mode with full glyph-bearing clearance)
  if (ctx.now.is_valid()) {
    char hhmm_buf[16];
    char ap_buf[8] = "";
    format_time_hhmm(ctx.now.hour, ctx.now.minute, prefs.is_24h_format,
                     hhmm_buf, sizeof(hhmm_buf), ap_buf, prefs.is_24h_format);
    char ss_buf[8];
    std::snprintf(ss_buf, sizeof(ss_buf), ":%02d", std::clamp<int>(ctx.now.second, 0, 59));

    auto *main_font = ctx.font_time;
    auto *sec_font = (SCREEN_W < 320) ? ctx.font_small : ctx.font_date;
    int main_w = 0, sec_w = 0, ap_w = 0;
    int mx_off = 0, sx_off = 0, ax_off = 0, base = 0, th = 0;
    it.get_text_bounds(0, 0, hhmm_buf, main_font, TextAlign::TOP_LEFT, &mx_off, &base, &main_w, &th);
    it.get_text_bounds(0, 0, ss_buf, sec_font, TextAlign::TOP_LEFT, &sx_off, &base, &sec_w, &th);
    if (!prefs.is_24h_format) {
      it.get_text_bounds(0, 0, ap_buf, ctx.font_small, TextAlign::TOP_LEFT, &ax_off, &base, &ap_w, &th);
    }
    int main_span = std::max(main_w, mx_off + main_w);
    int sec_span = std::max(sec_w, sx_off + sec_w);
    const int ap_span = prefs.is_24h_format ? 0 : std::max(ap_w, ax_off + ap_w);
    const int gap = scale_x(14);
    if (main_span + gap + std::max(sec_span, ap_span) > safe_span(time_y, 14)) {
      main_font = ctx.font_chrono_time;
      sec_font = ctx.font_small;
      it.get_text_bounds(0, 0, hhmm_buf, main_font, TextAlign::TOP_LEFT, &mx_off, &base, &main_w, &th);
      it.get_text_bounds(0, 0, ss_buf, sec_font, TextAlign::TOP_LEFT, &sx_off, &base, &sec_w, &th);
      main_span = std::max(main_w, mx_off + main_w);
      sec_span = std::max(sec_w, sx_off + sec_w);
    }
    const int total_w = main_span + gap + std::max(sec_span, ap_span);
    const int x_start = cx - (total_w / 2);
    const int stack_x = x_start + main_span + gap;

    if (has_bd_shadow) {
      fast_print(it, x_start + 1, time_y + 1, main_font, kChromeShadow, TextAlign::CENTER_LEFT, hhmm_buf);
    }
    fast_print(it, x_start, time_y, main_font, ctx.col_white, TextAlign::CENTER_LEFT, hhmm_buf);

    if (prefs.is_24h_format) {
      if (has_bd_shadow) {
        fast_print(it, stack_x + 1, time_y + scale_y(6) + 1, sec_font, kChromeShadow, TextAlign::CENTER_LEFT, ss_buf);
      }
      fast_print(it, stack_x, time_y + scale_y(6), sec_font, ctx.theme_primary, TextAlign::CENTER_LEFT, ss_buf);
    } else {
      const int ss_y = time_y - scale_y(14);
      const int ap_y = time_y + scale_y(16);
      const int ss_ink_w = text_ink_right_edge(it, sec_font, ":00");
      const int ap_ink_w = text_ink_right_edge(it, ctx.font_small, ap_buf);
      const int ap_x = stack_x + std::max(0, ss_ink_w - ap_ink_w);
      if (has_bd_shadow) {
        fast_print(it, stack_x + 1, ss_y + 1, sec_font, kChromeShadow, TextAlign::CENTER_LEFT, ss_buf);
        fast_print(it, ap_x + 1, ap_y + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER_LEFT, ap_buf);
      }
      fast_print(it, stack_x, ss_y, sec_font, ctx.theme_primary, TextAlign::CENTER_LEFT, ss_buf);
      fast_print(it, ap_x, ap_y, ctx.font_small, ctx.col_slate, TextAlign::CENTER_LEFT, ap_buf);
    }
  } else {
    if (has_bd_shadow) {
      fast_print(it, cx + 1, time_y + 1, ctx.font_time, kChromeShadow, TextAlign::CENTER, "--:--");
    }
    fast_print(it, cx, time_y, ctx.font_time, ctx.col_slate, TextAlign::CENTER, "--:--");
  }

  // 3. Local Time Zone Abbreviation + UTC Offset + DST / Standard Line (no redundant IANA city)
  const LocalTzInfo ltz = parse_local_tz_info(ac.local_tz_info);
  const int local_off_min = ltz.valid
      ? ltz.utc_offset_min
      : (ctx.now.is_valid()
             ? infer_local_utc_offset_min(
                   static_cast<uint32_t>(ctx.now.timestamp),
                   ctx.now.hour, ctx.now.minute, ctx.now.second)
             : 0);
  const bool local_is_dst = ltz.valid ? ltz.is_dst : (ctx.now.is_valid() && ctx.now.is_dst);
  const std::string off_lbl = format_utc_offset_str(local_off_min);

  std::string tz_line;
  if (ltz.valid) {
    const char *dst_tag = (SCREEN_W < 300) ? (local_is_dst ? "DST" : "Std")
                                           : (local_is_dst ? "DST" : "Standard");
    tz_line = ltz.abbr + " (" + off_lbl + ") \xE2\x80\xA2 " + dst_tag;
  } else if (ctx.now.is_valid()) {
    tz_line = off_lbl + " \xE2\x80\xA2 " + (local_is_dst ? "DST" : "Standard");
  } else {
    tz_line = "Syncing time zone...";
  }
  auto *tz_font = (SCREEN_W < 300) ? ctx.font_micro : ctx.font_small;
  tz_line = fit_text_to_width_font(tz_line, safe_span(tz_y, 16), tz_font);
  if (has_bd_shadow) {
    fast_print(it, cx + 1, tz_y + 1, tz_font, kChromeShadow, TextAlign::CENTER, tz_line.c_str());
  }
  fast_print(it, cx, tz_y, tz_font, ctx.col_slate, TextAlign::CENTER, tz_line.c_str());

  if (local_only) {
    rs.nav.world_clock_page = 0;
    return;
  }

  // 4. Studio-Configured World Clock Readout Rows (plain canvas text; up to 2 rows per page)
  const int total_pages = (total_wc + 1) / 2;
  const int cur_pg = std::clamp(static_cast<int>(rs.nav.world_clock_page), 0, total_pages - 1);
  rs.nav.world_clock_page = cur_pg;
  const int base_idx = cur_pg * 2;
  const int vis_rows = std::min(2, total_wc - base_idx);
  const int bounce_y = edge_bounce().offset_px(
      PageId::TIME, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);
  const int wc_safe_w = (total_wc == 1)
      ? safe_span(scale_y(258), 20)
      : std::min(safe_span(scale_y(246), 20), safe_span(scale_y(298), 20));
  const int row_w = std::min((SCREEN_W < 380) ? (SCREEN_W - 28) : layout().area_card3_w, wc_safe_w);
  const int x0 = cx - (row_w / 2);
  const int x1 = x0 + row_w;

  for (int row = 0; row < vis_rows; ++row) {
    const int idx = base_idx + row;
    const auto &wc = entries[idx];
    const int cy_row = ((total_wc == 1) ? scale_y(258) : scale_y(row == 0 ? 246 : 298)) + bounce_y;

    char rem_time_buf[20] = "--:--";
    const char *day_lbl = "Today";
    if (ctx.now.is_valid()) {
      int rem_h = 0;
      int rem_m = 0;
      int day_delta = 0;
      compute_world_clock_wall_time(static_cast<uint32_t>(ctx.now.timestamp),
                                    local_off_min, wc.utc_offset_min,
                                    rem_h, rem_m, day_delta);
      char hhmm[12];
      char ap[6] = "";
      format_time_hhmm(rem_h, rem_m, prefs.is_24h_format, hhmm, sizeof(hhmm), ap,
                       wc.is_pinned_utc || prefs.is_24h_format);
      if (prefs.is_24h_format || wc.is_pinned_utc) {
        std::snprintf(rem_time_buf, sizeof(rem_time_buf), "%s", hhmm);
      } else {
        std::snprintf(rem_time_buf, sizeof(rem_time_buf), "%s %s", hhmm, ap);
      }
      if (day_delta > 0) day_lbl = "+1d";
      else if (day_delta < 0) day_lbl = "-1d";
    }

    int time_w = 0, x_off = 0, base = 0, th = 0;
    auto *wc_time_font = (SCREEN_W < 320) ? ctx.font_small : ctx.font_btn_bold;
    it.get_text_bounds(0, 0, rem_time_buf, wc_time_font, TextAlign::TOP_LEFT, &x_off, &base, &time_w, &th);
    const int time_span = std::max(time_w, x_off + time_w);

    const int text_max_w = std::max(scale_x(64), (x1 - time_span - scale_x(12)) - x0);
    const std::string city_fit = fit_text_to_width_font(wc.city, text_max_w, ctx.font_small);
    std::string sub_meta = std::string(day_lbl) + " \xE2\x80\xA2 " + wc.abbr;
    if (wc.is_dst) sub_meta += " \xE2\x80\xA2 DST";
    const std::string sub_fit = fit_text_to_width_font(sub_meta, text_max_w, ctx.font_micro);

    const Color rem_col = wc.is_pinned_utc ? ctx.theme_secondary : ctx.theme_primary;
    if (has_bd_shadow) {
      fast_print(it, x0 + 1, cy_row - scale_y(8) + 1, ctx.font_small,
                 kChromeShadow, TextAlign::CENTER_LEFT, city_fit.c_str());
      fast_print(it, x0 + 1, cy_row + scale_y(10) + 1, ctx.font_micro,
                 kChromeShadow, TextAlign::CENTER_LEFT, sub_fit.c_str());
      fast_print(it, x1 + 1, cy_row + 1, wc_time_font,
                 kChromeShadow, TextAlign::CENTER_RIGHT, rem_time_buf);
    }
    fast_print(it, x0, cy_row - scale_y(8), ctx.font_small,
               ctx.col_white, TextAlign::CENTER_LEFT, city_fit.c_str());
    fast_print(it, x0, cy_row + scale_y(10), ctx.font_micro,
               ctx.col_slate, TextAlign::CENTER_LEFT, sub_fit.c_str());
    fast_print(it, x1, cy_row, wc_time_font,
               rem_col, TextAlign::CENTER_RIGHT, rem_time_buf);
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, cur_pg > 0, cur_pg + 1 < total_pages,
                              ctx.col_bg, ctx.col_white, kChromePipInactive,
                              layout().mode_bar_zone_top());
  }
}

void render_time_timers_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  auto *ts = timer_slots();
  const int v_slot = timer_view_slot();

  if (v_slot == 0) {
    render_page_header(it, cx, "TIMERS", ctx.font_small, ctx.col_white);

    int arc_slot = -1;
    int arc_rem = 999999;
    for (int i = 0; i < 3; ++i) {
      if (ts[i].running && ts[i].duration_sec > 0) {
        const int r_s = (ts[i].target_ms > ctx.now_ms)
                            ? static_cast<int>((ts[i].target_ms - ctx.now_ms + 999U) / 1000U)
                            : 0;
        if (r_s < arc_rem) {
          arc_rem = r_s;
          arc_slot = i;
        }
      }
    }
    if (arc_slot >= 0) {
      float prog = 0.0f;
      if (ctx.now_ms < ts[arc_slot].target_ms) {
        prog = static_cast<float>(ts[arc_slot].target_ms - ctx.now_ms) /
               (static_cast<float>(ts[arc_slot].duration_sec) * 1000.0f);
      }
      render_progress_arc_r224(it, cx, ctx.cy, prog, ctx.theme_secondary);
    }

    const int card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : layout().area_card3_w;
    const int card_h = layout().area_card3_h;
    auto *sec_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    for (int i = 0; i < 3; ++i) {
      const auto &s = ts[i];
      const int cy_c = layout().area_card3_y(i);
      const bool active = s.running || s.paused;
      const int disp_s = s.running
          ? ((s.target_ms > ctx.now_ms) ? static_cast<int>((s.target_ms - ctx.now_ms + 999U) / 1000U) : 0)
          : (s.paused ? s.remaining_sec : s.duration_sec);

      char t_str[20];
      if (disp_s >= 3600) {
        std::snprintf(t_str, sizeof(t_str), "%d:%02d:%02d",
                      disp_s / 3600, (disp_s % 3600) / 60, disp_s % 60);
      } else {
        std::snprintf(t_str, sizeof(t_str), "%02d:%02d", disp_s / 60, disp_s % 60);
      }
      const char *st_lbl = s.running ? "Running" : (s.paused ? "Paused" : "Ready");
      const std::string slot_lbl = format_timer_slot_label(i) + " \xE2\x80\xA2 " + st_lbl;
      const Color st_col = s.running ? ctx.theme_secondary : (s.paused ? ctx.col_gold : ctx.col_slate);
      const Color border_acc = s.running ? ctx.theme_secondary
                                         : (s.paused ? ctx.col_gold : ui::chrome_card_brd_off());

      ui::split_action_card_row(
          it, cx, cy_c, card_w, card_h,
          t_str, ctx.font_date, active ? ctx.col_white : ctx.col_slate,
          slot_lbl.c_str(), sec_font, st_col,
          active, border_acc,
          false, s.running,
          static_cast<uint16_t>(WidgetId::TIMER_TAB_BASE + i),
          static_cast<uint16_t>(WidgetId::TIMER_PRESET_BASE + i),
          static_cast<int16_t>(i));
    }
    return;
  }

  // Single-Timer Detail View (time rests on horizontal center line, generous stepper spacing)
  const int s_idx = std::clamp(v_slot - 1, 0, 2);
  const auto &s = ts[s_idx];
  render_page_header(it, cx, format_timer_header_upper(s_idx), ctx.font_small, ctx.col_white);

  if ((s.running || s.paused) && s.duration_sec > 0) {
    float prog = 0.0f;
    if (s.running && ctx.now_ms < s.target_ms) {
      prog = static_cast<float>(s.target_ms - ctx.now_ms) /
             (static_cast<float>(s.duration_sec) * 1000.0f);
    } else if (s.paused) {
      prog = static_cast<float>(s.remaining_sec) / static_cast<float>(s.duration_sec);
    }
    render_progress_arc_r224(it, cx, ctx.cy, prog,
                             s.paused ? ctx.col_gold : ctx.theme_secondary);
  }

  const int disp_sec = s.running
      ? ((s.target_ms > ctx.now_ms) ? static_cast<int>((s.target_ms - ctx.now_ms + 999U) / 1000U) : 0)
      : (s.paused ? s.remaining_sec : s.duration_sec);
  char t_buf[20];
  if (disp_sec >= 3600) {
    std::snprintf(t_buf, sizeof(t_buf), "%d:%02d:%02d",
                  disp_sec / 3600, (disp_sec % 3600) / 60, disp_sec % 60);
    render_time_readout(it, cx, t_buf, ctx.font_chrono_time, ctx.col_white, scale_y(168));
  } else {
    std::snprintf(t_buf, sizeof(t_buf), "%02d:%02d", disp_sec / 60, disp_sec % 60);
    render_time_readout(it, cx, t_buf, ctx.font_time, ctx.col_white, scale_y(164));
  }

  const int p_y = scale_y(236);
  const int p_h = std::max(44, scale_y(44));
  const int p_w = std::min({kMaxContentColW, scale_x(192), safe_span(p_y, 24)});
  if (s.running) {
    ui::action_pill_centered(it, cx, p_w, p_y, p_h,
                             ctx.col_gold, ui::chrome_card_bg_on(),
                             "PAUSE", ctx.col_white, ctx.font_btn_bold,
                             WidgetId::TIMER_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  } else {
    ui::action_pill_centered(it, cx, p_w, p_y, p_h,
                             ctx.theme_primary, ui::chrome_card_bg_on(),
                             s.paused ? "RESUME" : "START", ctx.col_white, ctx.font_btn_bold,
                             WidgetId::TIMER_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  }

  if (!s.running && !s.paused) {
    static const char *const kTimerStepLabels[4] = {"-5m", "-1m", "+1m", "+5m"};
    const int step_h = (SCREEN_H < 400) ? 32 : scale_y(38);
    ui::stepper_row(it, cx, scale_y(298), step_h, kTimerStepLabels, ctx.font_small,
                    ctx.col_white, WidgetId::STEPPER_BASE);
  } else {
    const int ext_y = scale_y(316);
    const int ext_h = std::max(40, scale_y(40));
    const ui::ActionPillSpec specs[2] = {
        {"+1M", WidgetId::TIMER_PLUS_1M_BTN, ctx.theme_secondary, ui::chrome_card_bg_on(), ctx.theme_secondary, 0, nullptr},
        {"RESET", WidgetId::TIMER_RESET_BTN, kChromeDangerBrd, ui::chrome_danger_bg(), ctx.col_white, 0, nullptr},
    };
    ui::action_pill_row(it, ext_y, ext_h, kMaxContentColW, scale_x(136), scale_x(16), specs, 2, ctx.font_small, 24);
  }

  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
}

void render_time_alarms_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int cx = ctx.cx;
  const int edit_idx = rs.alarms.edit_idx;
  const uint32_t now_epoch = ctx.now.is_valid()
      ? static_cast<uint32_t>(ctx.now.timestamp)
      : (ctx.now_ms / 1000U);
  const bool is_snoozed = (rs.alarms.snooze_until > now_epoch);

  if (edit_idx == 0) {
    render_page_header(it, cx, "ALARMS", ctx.font_small, ctx.col_white);

    int y_shift = 0;
    if (is_snoozed) {
      const uint32_t rem_s = rs.alarms.snooze_until - now_epoch;
      char snz_buf[40];
      std::snprintf(snz_buf, sizeof(snz_buf), "Snoozed %u:%02u \xE2\x80\xA2 Dismiss",
                    static_cast<unsigned>(rem_s / 60U), static_cast<unsigned>(rem_s % 60U));
      const int snz_y = scale_y(92);
      const int snz_h = std::max(30, scale_y(32));
      const int snz_w = std::min({kMaxContentColW, scale_x(256), safe_span(snz_y, 20)});
      ui::action_pill_centered(it, cx, snz_w, snz_y, snz_h,
                               ctx.col_gold, ui::chrome_card_bg_on(),
                               snz_buf, ctx.col_gold, ctx.font_small,
                               WidgetId::ALARM_SNOOZE_CANCEL, kHitTap | kHitFocusSlot);
      y_shift = scale_y(14);
    }

    const int card_w = (SCREEN_W < 380) ? (SCREEN_W - 24) : layout().area_card3_w;
    const int card_h = is_snoozed ? std::max(scale_y(46), layout().area_card3_h - scale_y(8))
                                  : layout().area_card3_h;
    auto *sec_font = (SCREEN_H < 400 || is_snoozed) ? ctx.font_micro : ctx.font_small;
    for (int i = 0; i < 3; ++i) {
      const auto &ap = prefs.alarms[i];
      const int cy_c = layout().area_card3_y(i) + (is_snoozed ? (y_shift - i * scale_y(4)) : 0);
      char t_str[16];
      char ap_str[8] = "";
      format_time_hhmm(ap.hour, ap.minute, prefs.is_24h_format, t_str, sizeof(t_str), ap_str);
      char full_t[24];
      if (prefs.is_24h_format) {
        std::snprintf(full_t, sizeof(full_t), "%s", t_str);
      } else {
        std::snprintf(full_t, sizeof(full_t), "%s %s", t_str, ap_str);
      }

      ui::split_action_card_row(
          it, cx, cy_c, card_w, card_h,
          full_t, ctx.font_date, ap.enabled ? ctx.col_white : ctx.col_slate,
          alarm_recurrence_label(ap.recurrence), sec_font,
          ap.enabled ? ctx.theme_secondary : ctx.col_slate,
          ap.enabled, ap.enabled ? ctx.theme_primary : ui::chrome_card_brd_off(),
          true, ap.enabled,
          static_cast<uint16_t>(WidgetId::ALARM_CARD_BASE + i),
          static_cast<uint16_t>(WidgetId::ALARM_TOGGLE_BASE + i),
          static_cast<int16_t>(i));
    }
    return;
  }

  // 1-Minute Precision Alarm Editor (center-line lowered + wider stepper & repeat spacing)
  const int a_idx = std::clamp(edit_idx - 1, 0, 2);
  const auto &ap = prefs.alarms[a_idx];
  char hdr_buf[24];
  std::snprintf(hdr_buf, sizeof(hdr_buf), "EDIT ALARM %d", a_idx + 1);
  render_page_header(it, cx, hdr_buf, ctx.font_small, ctx.col_white);

  const bool hh_focused = (rs.nav.alarm_edit_field == 0);
  char hh_buf[8];
  char mm_buf[8];
  char ap_buf[8] = "";
  if (prefs.is_24h_format) {
    std::snprintf(hh_buf, sizeof(hh_buf), "%02u", static_cast<unsigned>(ap.hour));
  } else {
    int h12 = ap.hour % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(hh_buf, sizeof(hh_buf), "%d", h12);
    std::strcpy(ap_buf, (ap.hour >= 12) ? "PM" : "AM");
  }
  std::snprintf(mm_buf, sizeof(mm_buf), "%02u", static_cast<unsigned>(ap.minute));

  const int time_y = scale_y(150);
  auto *num_font = (SCREEN_W < 300) ? ctx.font_chrono_time : ctx.font_time;
  int hh_w = 0, col_w = 0, mm_w = 0, ap_w = 0;
  int hx_off = 0, cx_off = 0, mx_off = 0, ax_off = 0, base = 0, num_th = 0, ap_th = 0;
  it.get_text_bounds(0, 0, hh_buf, num_font, TextAlign::TOP_LEFT, &hx_off, &base, &hh_w, &num_th);
  it.get_text_bounds(0, 0, ":", num_font, TextAlign::TOP_LEFT, &cx_off, &base, &col_w, &num_th);
  it.get_text_bounds(0, 0, mm_buf, num_font, TextAlign::TOP_LEFT, &mx_off, &base, &mm_w, &num_th);
  if (!prefs.is_24h_format) {
    it.get_text_bounds(0, 0, ap_buf, ctx.font_date, TextAlign::TOP_LEFT, &ax_off, &base, &ap_w, &ap_th);
  }
  const int hh_span = std::max(hh_w, hx_off + hh_w);
  const int col_span = std::max(col_w, cx_off + col_w) + scale_x(4);
  const int mm_span = std::max(mm_w, mx_off + mm_w);
  const int ap_span = prefs.is_24h_format ? 0 : std::max(ap_w, ax_off + ap_w);
  const int ap_gap = prefs.is_24h_format ? 0 : scale_x(14);
  const int total_w = hh_span + col_span + mm_span + ap_gap + ap_span;
  const int x_start = cx - (total_w / 2);
  const int hh_cx = x_start + (hh_span / 2);
  const int col_x = x_start + hh_span + scale_x(2);
  const int mm_x = x_start + hh_span + col_span;
  const int mm_cx = mm_x + (mm_span / 2);

  // Slow 1 Hz blink on the focused field (stays solid white during active tapping/stepping)
  const uint32_t latest_in = std::max(last_user_touch_ms(), ui_focus_last_ms());
  const bool blink_dim = alarm_editor_blink_dim(ctx.now_ms, latest_in);
  const Color active_col = blink_dim ? mix_color_q8(ctx.col_bg, ctx.col_slate, 84) : ctx.col_white;
  const Color hh_col = hh_focused ? active_col : ctx.col_slate;
  const Color mm_col = !hh_focused ? active_col : ctx.col_slate;
  const bool has_bd_shadow = backdrop_active();

  if (has_bd_shadow) {
    if (!(hh_focused && blink_dim)) {
      fast_print(it, x_start + 1, time_y + 1, num_font, kChromeShadow, TextAlign::CENTER_LEFT, hh_buf);
    }
    fast_print(it, col_x + 1, time_y + 1, num_font, kChromeShadow, TextAlign::CENTER_LEFT, ":");
    if (!(!hh_focused && blink_dim)) {
      fast_print(it, mm_x + 1, time_y + 1, num_font, kChromeShadow, TextAlign::CENTER_LEFT, mm_buf);
    }
  }
  fast_print(it, x_start, time_y, num_font, hh_col, TextAlign::CENTER_LEFT, hh_buf);
  fast_print(it, col_x, time_y, num_font, ctx.col_slate, TextAlign::CENTER_LEFT, ":");
  fast_print(it, mm_x, time_y, num_font, mm_col, TextAlign::CENTER_LEFT, mm_buf);
  if (!prefs.is_24h_format) {
    const int ap_x = mm_x + mm_span + ap_gap;
    if (has_bd_shadow) {
      fast_print(it, ap_x + 1, time_y + scale_y(8) + 1, ctx.font_date,
                 kChromeShadow, TextAlign::CENTER_LEFT, ap_buf);
    }
    fast_print(it, ap_x, time_y + scale_y(8), ctx.font_date,
               ctx.theme_secondary, TextAlign::CENTER_LEFT, ap_buf);
  }

  // Active field underline placed strictly below the exact rendered ink bottom of "00"
  int ul_x_dummy = 0, ul_y0 = 0, ul_w_dummy = 0, ul_ink_h = 0;
  it.get_text_bounds(0, time_y, "00", num_font, TextAlign::CENTER_LEFT,
                     &ul_x_dummy, &ul_y0, &ul_w_dummy, &ul_ink_h);
  const int ul_y = ul_y0 + ul_ink_h + std::max(4, scale_y(6));
  const int ul_x0 = hh_focused ? (x_start + hx_off) : (mm_x + mx_off);
  const int ul_w = hh_focused ? hh_w : mm_w;
  fill_rect_row_major(it, ul_x0, ul_y, std::max(16, ul_w), std::max(2, scale_y(3)), ctx.theme_primary);

  const int hit_h = std::max(48, ul_ink_h + scale_y(20));
  frame_hits().add_centered_rect(hh_cx, time_y, std::max(56, hh_span + scale_x(16)), hit_h,
                                 WidgetId::ALARM_HH_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(mm_cx, time_y, std::max(56, mm_span + scale_x(16)), hit_h,
                                 WidgetId::ALARM_MM_BTN, kHitTap | kHitFocusSlot);

  static const char *const kHourStepLabels[4] = {"-6h", "-1h", "+1h", "+6h"};
  static const char *const kMinStepLabels[4] = {"-5m", "-1m", "+1m", "+5m"};
  const int step_h = (SCREEN_H < 400) ? 32 : scale_y(38);
  ui::stepper_row(it, cx, scale_y(226), step_h,
                  hh_focused ? kHourStepLabels : kMinStepLabels,
                  ctx.font_small, ctx.col_white, WidgetId::STEPPER_BASE);

  char rec_lbl[32];
  std::snprintf(rec_lbl, sizeof(rec_lbl), "Repeat: %s", alarm_recurrence_label(ap.recurrence));
  const int rec_y = scale_y(320);
  const int rec_h = std::max(40, scale_y(44));
  const int rec_w = ui::compute_uniform_stack_w(rec_y, rec_y, rec_h, std::min(kMaxContentColW, layout().list_card_w), 20);
  ui::setting_cycle_row(it, cx, rec_y, rec_w, rec_h, rec_lbl, true,
                        ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                        WidgetId::ALARM_RECUR_BTN, kHitTap | kHitFocusSlot);

  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
}

void render_time_stopwatch_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const int cx = ctx.cx;
  const bool has_bd_shadow = backdrop_active();

  render_page_header(it, cx, "STOPWATCH", ctx.font_small, ctx.col_white);

  const uint32_t cur_sw_ms = rs.stopwatch.running
      ? (ctx.now_ms - rs.stopwatch.start_time_ms)
      : rs.stopwatch.elapsed_ms;
  const float sw_prog = std::fmod(static_cast<float>(cur_sw_ms) / 60000.0f, 1.0f);
  render_progress_arc_r224(it, cx, ctx.cy, sw_prog,
                           rs.stopwatch.running ? ctx.col_green : ctx.theme_primary);

  const int sw_y = scale_y(176);
  const int btn_y = scale_y(250);

  // 1. Recent Lap Splits ABOVE the stopwatch readout (plain canvas text, up to 2 rows)
  const int total_laps = static_cast<int>(rs.stopwatch.lap_count);
  if (total_laps > 0) {
    const int vis_laps = std::min(2, std::min(total_laps, static_cast<int>(StopwatchRuntimeState::kMaxLapSplits)));
    const int lap_y0 = scale_y(92);
    const int lap_step = scale_y(26);
    const int span_w = std::min((SCREEN_W < 380) ? (SCREEN_W - 36) : scale_x(216),
                                safe_span(lap_y0, 24));
    const int left_x = cx - (span_w / 2);
    const int right_x = cx + (span_w / 2);
    auto *lap_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    for (int k = 0; k < vis_laps; ++k) {
      const int lap_num = total_laps - k;
      const int ring_idx = (total_laps - 1 - k) % static_cast<int>(StopwatchRuntimeState::kMaxLapSplits);
      const uint32_t split_ms = rs.stopwatch.lap_splits_ms[ring_idx];
      const int row_y = lap_y0 + k * lap_step;

      char lap_lbl[20];
      std::snprintf(lap_lbl, sizeof(lap_lbl), "Lap %d", lap_num);
      char split_lbl[24];
      format_stopwatch_tenths(split_ms, split_lbl, sizeof(split_lbl));

      const Color split_col = (k == 0) ? ctx.theme_secondary : ctx.col_white;
      if (has_bd_shadow) {
        fast_print(it, left_x + 1, row_y + 1, lap_font, kChromeShadow, TextAlign::CENTER_LEFT, lap_lbl);
        fast_print(it, right_x + 1, row_y + 1, lap_font, kChromeShadow, TextAlign::CENTER_RIGHT, split_lbl);
      }
      fast_print(it, left_x, row_y, lap_font, ctx.col_slate, TextAlign::CENTER_LEFT, lap_lbl);
      fast_print(it, right_x, row_y, lap_font, split_col, TextAlign::CENTER_RIGHT, split_lbl);
    }
  }

  // 2. Main Digital Stopwatch Readout
  char sw_buf[24];
  format_stopwatch_tenths(cur_sw_ms, sw_buf, sizeof(sw_buf));
  auto *sw_font = (cur_sw_ms >= 3600000U || SCREEN_W < 320) ? ctx.font_chrono_time : ctx.font_time;
  int bw = 0, x_off = 0, base = 0, th = 0;
  it.get_text_bounds(0, 0, sw_buf, sw_font, TextAlign::TOP_LEFT, &x_off, &base, &bw, &th);
  if (bw > safe_span(sw_y, 16)) {
    sw_font = ctx.font_chrono_time;
  }
  if (has_bd_shadow) {
    fast_print(it, cx + 1, sw_y + 1, sw_font, kChromeShadow, TextAlign::CENTER, sw_buf);
  }
  fast_print(it, cx, sw_y, sw_font, ctx.col_white, TextAlign::CENTER, sw_buf);

  // 3. Primary Stopwatch Action Pills (D-048 / D-070: zero disabled/non-interactive glass pills)
  const int btn_h = std::max(44, scale_y(44));
  const int btn_gap = std::max(12, scale_x(16));
  auto *btn_font = (SCREEN_W < 340) ? ctx.font_small : ctx.font_btn_bold;

  if (rs.stopwatch.running) {
    const ui::ActionPillSpec specs[2] = {
        {"PAUSE", WidgetId::SW_PRIMARY_BTN, ctx.col_gold, ui::chrome_card_bg_on(), ctx.col_white, 0, nullptr},
        {"LAP", WidgetId::SW_LAP_BTN, ctx.theme_secondary, ui::chrome_card_bg_on(), ctx.theme_secondary, 0, nullptr},
    };
    ui::action_pill_row(it, btn_y, btn_h, kMaxContentColW, scale_x(148), btn_gap, specs, 2, btn_font, 24);
  } else if (cur_sw_ms > 0U) {
    const ui::ActionPillSpec specs[2] = {
        {"RESUME", WidgetId::SW_PRIMARY_BTN, ctx.col_green, ui::chrome_card_bg_on(), ctx.col_white, 0, nullptr},
        {"RESET", WidgetId::SW_RESET_BTN, kChromeDangerBrd, ui::chrome_danger_bg(), ctx.col_white, 0, nullptr},
    };
    ui::action_pill_row(it, btn_y, btn_h, kMaxContentColW, scale_x(148), btn_gap, specs, 2, btn_font, 24);
  } else {
    const int start_w = std::min({kMaxContentColW, scale_x(192), safe_span(btn_y, 24)});
    ui::action_pill_centered(it, cx, start_w, btn_y, btn_h,
                             ctx.col_green, ui::chrome_card_bg_on(),
                             "START", ctx.col_white, btn_font,
                             WidgetId::SW_PRIMARY_BTN, kHitTap | kHitFocusSlot);
  }
}

}  // namespace

void render_time_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const int sub = std::clamp(rs.nav.time_subpage, 0, 3);

  switch (sub) {
    case TimeSubpage::CLOCK:
      render_time_clock_tab(it, ctx);
      break;
    case TimeSubpage::TIMERS:
      render_time_timers_tab(it, ctx);
      break;
    case TimeSubpage::ALARMS:
      render_time_alarms_tab(it, ctx);
      break;
    case TimeSubpage::STOPWATCH:
    default:
      render_time_stopwatch_tab(it, ctx);
      break;
  }

  const bool in_drilldown =
      (sub == TimeSubpage::TIMERS && timer_view_slot() != 0) ||
      (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx != 0);
  if (!in_drilldown) {
    render_time_mode_bar(it, ctx, sub);
  }
}

}  // namespace latter_ui
