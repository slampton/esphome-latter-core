#include "esphome/core/defines.h"
#include "ui_pages.h"

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

bool csv_contains_iana_zone(const char *csv, std::string_view target) {
  if (csv == nullptr || target.empty()) return false;
  std::string_view sv(csv);
  size_t pos = 0;
  while (pos <= sv.size()) {
    const size_t comma = sv.find(',', pos);
    std::string_view tok = (comma == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, comma - pos);
    while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
    while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
    if (tok == target) return true;
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return false;
}

int count_custom_world_clocks(const char *csv) {
  if (csv == nullptr || csv[0] == '\0') return 0;
  int count = 0;
  std::string_view sv(csv);
  size_t pos = 0;
  while (pos <= sv.size() && count < static_cast<int>(kMaxWorldClocks)) {
    const size_t comma = sv.find(',', pos);
    std::string_view tok = (comma == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, comma - pos);
    while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
    while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
    if (!tok.empty() && tok != "Etc/UTC" && tok != "UTC") {
      ++count;
    }
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return count;
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

void render_time_mode_bar(esphome::display::Display &it, const UiFrameContext &ctx, int active_sub) {
  using esphome::Color;
  const int cx = ctx.cx;
  int win_xs[4] = {0, 0, 0, 0};
  bool has_l = false;
  bool has_r = false;
  get_mode_bar_window_xs(4, 0, cx, win_xs, has_l, has_r);
  render_in_app_mode_bar_track(it, cx, win_xs, 4, active_sub, false, false, 4, active_sub,
                               ctx.theme_primary, ctx.col_white, 0);

  const int by = layout().app_dock_y;
  for (int i = 0; i < 4; ++i) {
    const int bx = win_xs[i];
    const bool is_act = (i == active_sub);
    const Color fg_c = is_act ? ctx.theme_primary : ctx.col_white;
    const Color cut_c = sample_glass_surface_color(bx - 4, by - 4, 9, 9);
    const Color acc_c = is_act ? ctx.theme_secondary : fg_c;
    if (i == TimeSubpage::CLOCK) {
      ui::draw_icon_clock_face(it, bx, by, std::max(8, scale_s(10)), fg_c);
    } else if (i == TimeSubpage::TIMERS) {
      ui::draw_icon_hourglass(it, bx, by, fg_c, acc_c, std::max(8, scale_s(11)));
    } else if (i == TimeSubpage::ALARMS) {
      ui::draw_icon_alarm_clock(it, bx, by, fg_c, cut_c, std::max(7, scale_s(10)));
    } else {
      ui::draw_icon_stopwatch(it, bx, by, fg_c, acc_c, std::max(7, scale_s(10)));
    }
  }
}

void render_time_clock_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const int cx = ctx.cx;

  // On-device IANA World Clock Picker Drilldown (24 major global cities, 5 per page)
  if (rs.nav.world_clock_picker_page >= 0) {
    render_page_header(it, cx, "ADD WORLD CLOCK", ctx.font_small, ctx.theme_primary);
    const int total_pages = (static_cast<int>(kWorldClockCatalogCount) + 4) / 5;
    const int pg = std::clamp(static_cast<int>(rs.nav.world_clock_picker_page), 0, total_pages - 1);
    const int base_idx = pg * 5;
    char row_buf[64];
    for (int r = 0; r < 5; ++r) {
      const int cat_idx = base_idx + r;
      if (cat_idx >= static_cast<int>(kWorldClockCatalogCount)) break;
      const auto &item = kWorldClockCatalog[cat_idx];
      const bool selected = csv_contains_iana_zone(prefs.world_clocks_csv, item.iana);
      const std::string off_str = format_utc_offset_str(item.std_offset_min);
      std::snprintf(row_buf, sizeof(row_buf), "%s (%s)", item.city, off_str.c_str());

      const int y_mid = layout().slot5_y(r);
      const std::string fit_lbl = fit_text_to_width_font(
          row_buf, layout().list_card_w - scale_x(24), ctx.font_small);
      render_modern_card(it, cx, y_mid, layout().list_card_w, 44, 12,
                         selected ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                         selected ? ctx.theme_primary : ui::chrome_card_brd_off(),
                         fit_lbl.c_str(),
                         selected ? ctx.theme_primary : ctx.col_white,
                         ctx.font_small);
      frame_hits().add_centered_rect(
          cx, y_mid, layout().list_card_w, layout().slot5_h,
          static_cast<uint16_t>(WidgetId::TIME_ZONE_PICK_BASE + r),
          kHitTap | kHitFocusSlot,
          static_cast<int16_t>(cat_idx));
    }
    render_vertical_page_pips(it, pg > 0, pg + 1 < total_pages,
                              ctx.col_bg, ctx.col_white, kChromePipInactive);
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  render_page_header(it, cx, "TIME", ctx.font_small, ctx.theme_primary);
  frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(180), scale_y(32),
                                 WidgetId::TIME_MODE_BTN, kHitTap);

  // 1. Live HH:MM + right-stacked :SS (+ AM/PM in 12h mode) — no date, no battery
  const int time_y = scale_y(114);
  const bool has_bd_shadow = backdrop_active();
  if (ctx.now.is_valid()) {
    char hhmm_buf[16];
    char ap_buf[8] = "";
    format_time_hhmm(ctx.now.hour, ctx.now.minute, prefs.is_24h_format,
                     hhmm_buf, sizeof(hhmm_buf), ap_buf, prefs.is_24h_format);
    char ss_buf[8];
    std::snprintf(ss_buf, sizeof(ss_buf), ":%02d", std::clamp<int>(ctx.now.second, 0, 59));

    auto *main_font = ctx.font_time;
    auto *sec_font = (SCREEN_W < 320) ? ctx.font_small : ctx.font_date;
    int main_w = 0, sec_w = 0, ap_w = 0, x_off = 0, base = 0, th = 0;
    it.get_text_bounds(0, 0, hhmm_buf, main_font, TextAlign::TOP_LEFT, &x_off, &base, &main_w, &th);
    it.get_text_bounds(0, 0, ss_buf, sec_font, TextAlign::TOP_LEFT, &x_off, &base, &sec_w, &th);
    if (!prefs.is_24h_format) {
      it.get_text_bounds(0, 0, ap_buf, ctx.font_small, TextAlign::TOP_LEFT, &x_off, &base, &ap_w, &th);
    }
    const int stack_w = std::max(sec_w, ap_w);
    const int gap = scale_x(6);
    if (main_w + gap + stack_w > safe_span(time_y, 14)) {
      main_font = ctx.font_chrono_time;
      sec_font = ctx.font_small;
      it.get_text_bounds(0, 0, hhmm_buf, main_font, TextAlign::TOP_LEFT, &x_off, &base, &main_w, &th);
      it.get_text_bounds(0, 0, ss_buf, sec_font, TextAlign::TOP_LEFT, &x_off, &base, &sec_w, &th);
    }
    const int total_w = main_w + gap + std::max(sec_w, ap_w);
    const int x_start = cx - (total_w / 2);
    const int stack_x = x_start + main_w + gap;

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
      const int ss_y = time_y - scale_y(12);
      const int ap_y = time_y + scale_y(14);
      if (has_bd_shadow) {
        fast_print(it, stack_x + 1, ss_y + 1, sec_font, kChromeShadow, TextAlign::CENTER_LEFT, ss_buf);
        fast_print(it, stack_x + 1, ap_y + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER_LEFT, ap_buf);
      }
      fast_print(it, stack_x, ss_y, sec_font, ctx.theme_primary, TextAlign::CENTER_LEFT, ss_buf);
      fast_print(it, stack_x, ap_y, ctx.font_small, ctx.col_slate, TextAlign::CENTER_LEFT, ap_buf);
    }
  } else {
    render_time_readout(it, cx, "--:--", ctx.font_time, ctx.col_slate);
  }

  // 2. Local Time Zone + UTC Offset + DST / Standard Indicator Line
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

  const int tz_y = scale_y(166);
  std::string tz_line;
  if (ltz.valid) {
    const char *dst_tag = local_is_dst ? "DST" : "Standard";
    if (SCREEN_W < 300) {
      tz_line = ltz.abbr + " (" + off_lbl + ") \xE2\x80\xA2 " + (local_is_dst ? "DST" : "Std");
    } else {
      tz_line = ltz.city + " \xE2\x80\xA2 " + ltz.abbr + " (" + off_lbl + ") \xE2\x80\xA2 " + dst_tag;
    }
  } else if (ctx.now.is_valid()) {
    tz_line = std::string("Local \xE2\x80\xA2 ") + off_lbl + " \xE2\x80\xA2 " +
              (local_is_dst ? "DST" : "Standard");
  } else {
    tz_line = "Syncing time zone...";
  }
  auto *tz_font = (SCREEN_W < 300) ? ctx.font_micro : ctx.font_small;
  tz_line = fit_text_to_width_font(tz_line, safe_span(tz_y, 16), tz_font);
  if (has_bd_shadow) {
    fast_print(it, cx + 1, tz_y + 1, tz_font, kChromeShadow, TextAlign::CENTER, tz_line.c_str());
  }
  fast_print(it, cx, tz_y, tz_font, ctx.col_slate, TextAlign::CENTER, tz_line.c_str());

  // 3. Compact Action Row: Dedicated [UTC] 1-tap toggle + [+ Zone] picker button
  const int act_y = scale_y(198);
  const int custom_cnt = count_custom_world_clocks(prefs.world_clocks_csv);
  const int pill_r = std::max(10, scale_y(14));
  const int pill_gap = scale_x(6);
  const int pill_w = std::min(scale_x(122), (safe_span(act_y, 20) - pill_gap * 2) / 2);
  auto *pill_font = (SCREEN_W < 300) ? ctx.font_micro : ctx.font_small;

  const char *utc_lbl = prefs.show_utc_clock ? "UTC: On" : "UTC";
  const char *zone_lbl = (custom_cnt < static_cast<int>(kMaxWorldClocks)) ? "+ Zone" : "Zones (4)";
  ui::pill_button(it, cx - pill_gap - pill_w, cx - pill_gap, act_y, pill_r,
                  prefs.show_utc_clock ? ctx.theme_primary : ui::chrome_card_brd_off(),
                  prefs.show_utc_clock ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                  utc_lbl,
                  prefs.show_utc_clock ? ctx.theme_primary : ctx.col_white,
                  pill_font,
                  WidgetId::TIME_UTC_TOGGLE_BTN, kHitTap | kHitFocusSlot);
  ui::pill_button(it, cx + pill_gap, cx + pill_gap + pill_w, act_y, pill_r,
                  (custom_cnt > 0) ? ui::chrome_card_brd_on() : ui::chrome_card_brd_off(),
                  ui::chrome_card_bg_off(),
                  zone_lbl,
                  ctx.col_white,
                  pill_font,
                  WidgetId::TIME_ADD_ZONE_BTN, kHitTap | kHitFocusSlot);

  // 4. World Clocks Area (up to 2 cards per page above mode bar, or clean Local TZ status card)
  WorldClockEntry entries[kMaxWorldClocks + 1]{};
  const int total_wc = collect_active_world_clocks(prefs, ac, entries);
  if (total_wc <= 0) {
    const int card_cy = scale_y(278);
    const int card_h = scale_y(72);
    const int card_w = std::min((SCREEN_W < 380) ? (SCREEN_W - 24) : layout().area_card3_w,
                                safe_span(card_cy, 18));
    render_modern_card(it, cx, card_cy, card_w, card_h, 14,
                       ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                       "", ctx.col_white, ctx.font_small);
    const std::string primary_tz = fit_text_to_width_font(
        ltz.valid ? ltz.iana : std::string("Local System Clock"),
        card_w - scale_x(24), ctx.font_small);
    const std::string secondary_tz = fit_text_to_width_font(
        off_lbl + " \xE2\x80\xA2 " +
            (local_is_dst ? "Daylight Saving (DST)" : "Standard Time"),
        card_w - scale_x(24), ctx.font_micro);
    fast_print(it, cx, card_cy - scale_y(11), ctx.font_small, ctx.col_white,
               TextAlign::CENTER, primary_tz.c_str());
    fast_print(it, cx, card_cy + scale_y(12), ctx.font_micro, ctx.col_slate,
               TextAlign::CENTER, secondary_tz.c_str());
    return;
  }

  const int total_pages = (total_wc + 1) / 2;
  const int cur_pg = std::clamp(static_cast<int>(rs.nav.world_clock_page), 0, total_pages - 1);
  const int base_idx = cur_pg * 2;
  const int card_h = scale_y(46);

  for (int row = 0; row < 2; ++row) {
    const int idx = base_idx + row;
    if (idx >= total_wc) break;
    const auto &wc = entries[idx];
    const int cy_card = scale_y(row == 0 ? 252 : 306);
    const int card_w = std::min((SCREEN_W < 380) ? (SCREEN_W - 24) : layout().area_card3_w,
                                safe_span(cy_card, 18));
    const int x0 = cx - (card_w / 2);
    const int x1 = x0 + card_w;
    const int del_w = std::max(30, scale_x(38));
    const int del_div_x = x1 - del_w;

    render_modern_card(it, cx, cy_card, card_w, card_h, 12,
                       wc.is_pinned_utc ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                       wc.is_pinned_utc ? ui::chrome_card_brd_on() : ui::chrome_card_brd_off(),
                       "", ctx.col_white, ctx.font_small);

    // Subtle divider separating the remove [x] action on the right
    const int div_pad = std::max(6, card_h / 5);
    fast_vline(it, del_div_x, cy_card - (card_h / 2) + div_pad,
               card_h - 2 * div_pad, ui::chrome_card_brd_off());

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
      format_time_hhmm(rem_h, rem_m, prefs.is_24h_format, hhmm, sizeof(hhmm), ap, wc.is_pinned_utc || prefs.is_24h_format);
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

    const int pad_x = scale_x(12);
    const int time_right_x = del_div_x - scale_x(8);
    const int text_max_w = std::max(scale_x(60), (time_right_x - time_w - scale_x(8)) - (x0 + pad_x));
    const std::string city_fit = fit_text_to_width_font(wc.city, text_max_w, ctx.font_small);
    std::string sub_meta = std::string(day_lbl) + " \xE2\x80\xA2 " + wc.abbr;
    if (wc.is_dst) sub_meta += " \xE2\x80\xA2 DST";
    const std::string sub_fit = fit_text_to_width_font(sub_meta, text_max_w, ctx.font_micro);

    fast_print(it, x0 + pad_x, cy_card - scale_y(8), ctx.font_small,
               ctx.col_white, TextAlign::CENTER_LEFT, city_fit.c_str());
    fast_print(it, x0 + pad_x, cy_card + scale_y(10), ctx.font_micro,
               ctx.col_slate, TextAlign::CENTER_LEFT, sub_fit.c_str());
    fast_print(it, time_right_x, cy_card, wc_time_font,
               wc.is_pinned_utc ? ctx.theme_secondary : ctx.theme_primary,
               TextAlign::CENTER_RIGHT, rem_time_buf);

    // Crisp vector 'x' remove glyph in right action cell
    const int del_cx = (del_div_x + x1) / 2;
    const int xr = std::max(4, scale_s(5));
    ui::draw_aa_line(it, static_cast<float>(del_cx - xr), static_cast<float>(cy_card - xr),
                     static_cast<float>(del_cx + xr), static_cast<float>(cy_card + xr),
                     1.6f, ctx.col_slate);
    ui::draw_aa_line(it, static_cast<float>(del_cx - xr), static_cast<float>(cy_card + xr),
                     static_cast<float>(del_cx + xr), static_cast<float>(cy_card - xr),
                     1.6f, ctx.col_slate);
    frame_hits().add_rect(del_div_x, cy_card - (card_h / 2) - 2, x1 + 4, cy_card + (card_h / 2) + 2,
                          static_cast<uint16_t>(WidgetId::TIME_WORLD_DEL_BASE + row),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, cur_pg > 0, cur_pg + 1 < total_pages,
                              ctx.col_bg, ctx.col_white, kChromePipInactive);
  }
}

void render_time_timers_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  auto *ts = timer_slots();
  const int v_slot = timer_view_slot();

  if (v_slot == 0) {
    render_page_header(it, cx, "TIMERS", ctx.font_small, ctx.theme_primary);
    frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(180), scale_y(32),
                                   WidgetId::TIME_MODE_BTN, kHitTap);

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

  // Single-Timer Detail View (hides mode bar, exposes < Back + +1m quick-extend)
  const int s_idx = std::clamp(v_slot - 1, 0, 2);
  const auto &s = ts[s_idx];
  render_page_header(it, cx, format_timer_header_upper(s_idx), ctx.font_small, ctx.theme_primary);

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
    if (backdrop_active()) {
      fast_print(it, cx + 1, scale_y(134) + 1, ctx.font_date, kChromeShadow, TextAlign::CENTER, t_buf);
    }
    fast_print(it, cx, scale_y(134), ctx.font_date, ctx.col_white, TextAlign::CENTER, t_buf);
  } else {
    std::snprintf(t_buf, sizeof(t_buf), "%02d:%02d", disp_sec / 60, disp_sec % 60);
    render_time_readout(it, cx, t_buf, ctx.font_time, ctx.col_white);
  }

  const int p_y = scale_y(214);
  if (s.running) {
    ui::pill_button(it, cx - scale_x(82), cx + scale_x(82), p_y, scale_y(21),
                    ctx.col_gold, Color(65, 45, 0), "PAUSE", ctx.col_white, ctx.font_btn_bold,
                    WidgetId::TIMER_PRIMARY_BTN);
  } else {
    ui::pill_button(it, cx - scale_x(82), cx + scale_x(82), p_y, scale_y(21),
                    ctx.theme_primary, ui::chrome_card_bg_on(),
                    s.paused ? "RESUME" : "START", ctx.col_white, ctx.font_btn_bold,
                    WidgetId::TIMER_PRIMARY_BTN);
  }

  if (!s.running && !s.paused) {
    static const char *const kTimerStepLabels[4] = {"-5m", "-1m", "+1m", "+5m"};
    ui::stepper_row(it, cx, scale_y(264), 38, kTimerStepLabels, ctx.font_small,
                    ctx.col_white, WidgetId::STEPPER_BASE);
  } else {
    const int ext_y = scale_y(282);
    const int gap = scale_x(8);
    const int btn_w = std::min(scale_x(118), (safe_span(ext_y, 20) - gap * 2) / 2);
    ui::pill_button(it, cx - gap - btn_w, cx - gap, ext_y, scale_y(18),
                    ctx.theme_secondary, ui::chrome_card_bg_on(),
                    "+1M", ctx.theme_secondary, ctx.font_small,
                    WidgetId::TIMER_PLUS_1M_BTN);
    ui::pill_button(it, cx + gap, cx + gap + btn_w, ext_y, scale_y(18),
                    kChromeDangerBrd, ui::chrome_danger_bg(),
                    "RESET", ctx.col_white, ctx.font_small,
                    WidgetId::TIMER_RESET_BTN);
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
    render_page_header(it, cx, "ALARMS", ctx.font_small, ctx.theme_primary);
    frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(180), scale_y(32),
                                   WidgetId::TIME_MODE_BTN, kHitTap);

    int y_shift = 0;
    if (is_snoozed) {
      const uint32_t rem_s = rs.alarms.snooze_until - now_epoch;
      char snz_buf[40];
      std::snprintf(snz_buf, sizeof(snz_buf), "Snoozed %u:%02u \xE2\x80\xA2 Dismiss",
                    static_cast<unsigned>(rem_s / 60U), static_cast<unsigned>(rem_s % 60U));
      const int snz_y = scale_y(92);
      const int snz_w = std::min(scale_x(250), safe_span(snz_y, 20));
      render_modern_card(it, cx, snz_y, snz_w, 30, 12,
                         Color(48, 34, 6), ctx.col_gold, snz_buf, ctx.col_gold, ctx.font_micro);
      frame_hits().add_centered_rect(cx, snz_y, snz_w, scale_y(32),
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

  // 1-Minute Precision Alarm Editor (edit_idx == 1..3)
  const int a_idx = std::clamp(edit_idx - 1, 0, 2);
  const auto &ap = prefs.alarms[a_idx];
  char hdr_buf[24];
  std::snprintf(hdr_buf, sizeof(hdr_buf), "EDIT ALARM %d", a_idx + 1);
  render_page_header(it, cx, hdr_buf, ctx.font_small, ctx.theme_primary);

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

  const int time_y = scale_y(132);
  auto *num_font = (SCREEN_W < 300) ? ctx.font_chrono_time : ctx.font_time;
  int hh_w = 0, col_w = 0, mm_w = 0, ap_w = 0, x_off = 0, base = 0, th = 0;
  it.get_text_bounds(0, 0, hh_buf, num_font, TextAlign::TOP_LEFT, &x_off, &base, &hh_w, &th);
  it.get_text_bounds(0, 0, ":", num_font, TextAlign::TOP_LEFT, &x_off, &base, &col_w, &th);
  it.get_text_bounds(0, 0, mm_buf, num_font, TextAlign::TOP_LEFT, &x_off, &base, &mm_w, &th);
  if (!prefs.is_24h_format) {
    it.get_text_bounds(0, 0, ap_buf, ctx.font_date, TextAlign::TOP_LEFT, &x_off, &base, &ap_w, &th);
  }
  const int ap_gap = prefs.is_24h_format ? 0 : scale_x(8);
  const int total_w = hh_w + col_w + mm_w + ap_gap + ap_w;
  const int x_start = cx - (total_w / 2);
  const int hh_cx = x_start + (hh_w / 2);
  const int col_x = x_start + hh_w;
  const int mm_x = col_x + col_w;
  const int mm_cx = mm_x + (mm_w / 2);

  fast_print(it, x_start, time_y, num_font,
             hh_focused ? ctx.col_white : ctx.col_slate, TextAlign::CENTER_LEFT, hh_buf);
  fast_print(it, col_x, time_y, num_font, ctx.col_slate, TextAlign::CENTER_LEFT, ":");
  fast_print(it, mm_x, time_y, num_font,
             !hh_focused ? ctx.col_white : ctx.col_slate, TextAlign::CENTER_LEFT, mm_buf);
  if (!prefs.is_24h_format) {
    fast_print(it, mm_x + mm_w + ap_gap, time_y + scale_y(8), ctx.font_date,
               ctx.theme_secondary, TextAlign::CENTER_LEFT, ap_buf);
  }

  // Active field underline + generous hit zones for HH vs MM
  const int ul_y = time_y + (th / 2) + scale_y(6);
  const int ul_x0 = hh_focused ? x_start : mm_x;
  const int ul_w = hh_focused ? hh_w : mm_w;
  fill_rect_row_major(it, ul_x0, ul_y, std::max(16, ul_w), std::max(2, scale_y(3)), ctx.theme_primary);

  const int hit_h = std::max(48, th + scale_y(18));
  frame_hits().add_centered_rect(hh_cx, time_y, std::max(56, hh_w + scale_x(16)), hit_h,
                                 WidgetId::ALARM_HH_BTN, kHitTap | kHitFocusSlot);
  frame_hits().add_centered_rect(mm_cx, time_y, std::max(56, mm_w + scale_x(16)), hit_h,
                                 WidgetId::ALARM_MM_BTN, kHitTap | kHitFocusSlot);

  static const char *const kHourStepLabels[4] = {"-6h", "-1h", "+1h", "+6h"};
  static const char *const kMinStepLabels[4] = {"-5m", "-1m", "+1m", "+5m"};
  ui::stepper_row(it, cx, scale_y(198), 38,
                  hh_focused ? kHourStepLabels : kMinStepLabels,
                  ctx.font_small, ctx.col_white, WidgetId::STEPPER_BASE);

  char rec_lbl[32];
  std::snprintf(rec_lbl, sizeof(rec_lbl), "Repeat: %s", alarm_recurrence_label(ap.recurrence));
  const int rec_y = scale_y(280);
  const int rec_w = std::min(scale_x(232), safe_span(rec_y, 20));
  render_modern_card(it, cx, rec_y, rec_w, 42, 14,
                     ui::chrome_card_bg_off(), ui::chrome_card_brd_on(),
                     rec_lbl, ctx.theme_secondary, ctx.font_small);
  frame_hits().add_centered_rect(cx, rec_y, rec_w, scale_y(44),
                                 WidgetId::ALARM_RECUR_BTN, kHitTap | kHitFocusSlot);

  render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
}

void render_time_stopwatch_tab(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const int cx = ctx.cx;

  render_page_header(it, cx, "STOPWATCH", ctx.font_small, ctx.theme_primary);
  frame_hits().add_centered_rect(cx, layout().hdr_y, scale_x(180), scale_y(32),
                                 WidgetId::TIME_MODE_BTN, kHitTap);

  const uint32_t cur_sw_ms = rs.stopwatch.running
      ? (ctx.now_ms - rs.stopwatch.start_time_ms)
      : rs.stopwatch.elapsed_ms;
  const float sw_prog = std::fmod(static_cast<float>(cur_sw_ms) / 60000.0f, 1.0f);
  render_progress_arc_r224(it, cx, ctx.cy, sw_prog,
                           rs.stopwatch.running ? ctx.col_green : ctx.theme_primary);

  char sw_buf[24];
  format_stopwatch_tenths(cur_sw_ms, sw_buf, sizeof(sw_buf));
  const int sw_y = scale_y(118);
  auto *sw_font = (cur_sw_ms >= 3600000U || SCREEN_W < 320) ? ctx.font_chrono_time : ctx.font_time;
  int bw = 0, x_off = 0, base = 0, th = 0;
  it.get_text_bounds(0, 0, sw_buf, sw_font, TextAlign::TOP_LEFT, &x_off, &base, &bw, &th);
  if (bw > safe_span(sw_y, 16)) {
    sw_font = ctx.font_chrono_time;
  }
  if (backdrop_active()) {
    fast_print(it, cx + 1, sw_y + 1, sw_font, kChromeShadow, TextAlign::CENTER, sw_buf);
  }
  fast_print(it, cx, sw_y, sw_font, ctx.col_white, TextAlign::CENTER, sw_buf);

  // Side-by-side 2-pill control row
  const int ctrl_y = scale_y(190);
  const int gap = scale_x(8);
  const int btn_w = std::min(scale_x(122), (safe_span(ctrl_y, 20) - gap * 2) / 2);
  const int pill_r = std::max(14, scale_y(18));
  auto *btn_font = (SCREEN_W < 300) ? ctx.font_small : ctx.font_btn_bold;

  if (rs.stopwatch.running) {
    ui::pill_button(it, cx - gap - btn_w, cx - gap, ctrl_y, pill_r,
                    ctx.col_gold, Color(65, 45, 0),
                    "PAUSE", ctx.col_white, btn_font, WidgetId::SW_PRIMARY_BTN);
    ui::pill_button(it, cx + gap, cx + gap + btn_w, ctrl_y, pill_r,
                    ctx.theme_secondary, ui::chrome_card_bg_on(),
                    "LAP", ctx.theme_secondary, btn_font, WidgetId::SW_LAP_BTN);
  } else {
    ui::pill_button(it, cx - gap - btn_w, cx - gap, ctrl_y, pill_r,
                    ctx.col_green, Color(0, 56, 28),
                    (cur_sw_ms > 0U) ? "RESUME" : "START", ctx.col_white, btn_font,
                    WidgetId::SW_PRIMARY_BTN);
    if (cur_sw_ms > 0U) {
      ui::pill_button(it, cx + gap, cx + gap + btn_w, ctrl_y, pill_r,
                      kChromeDangerBrd, ui::chrome_danger_bg(),
                      "RESET", ctx.col_white, btn_font, WidgetId::SW_RESET_BTN);
    } else {
      render_pill_button(it, cx + gap, cx + gap + btn_w, ctrl_y, pill_r,
                         ui::chrome_card_brd_off(), ui::chrome_card_bg_off(),
                         "LAP", ctx.col_slate, btn_font);
    }
  }

  // Recent Lap Splits List (up to 3 visible rows cleanly above the 4-icon mode bar)
  const int total_laps = static_cast<int>(rs.stopwatch.lap_count);
  if (total_laps <= 0) {
    const int empty_y = scale_y(274);
    fast_print(it, cx, empty_y, ctx.font_micro, ctx.col_slate,
               TextAlign::CENTER, "Tap LAP while running to record splits");
    return;
  }

  const int vis_laps = std::min(3, std::min(total_laps, static_cast<int>(StopwatchRuntimeState::kMaxLapSplits)));
  const int card_cy = scale_y(280);
  const int card_h = scale_y(96);
  const int card_w = std::min((SCREEN_W < 380) ? (SCREEN_W - 28) : layout().area_card3_w,
                              safe_span(scale_y(312), 20));
  render_modern_card(it, cx, card_cy, card_w, card_h, 12,
                     ui::chrome_card_bg_off(), ui::chrome_card_brd_off(),
                     "", ctx.col_white, ctx.font_small);

  const int left_x = cx - (card_w / 2) + scale_x(14);
  const int right_x = cx + (card_w / 2) - scale_x(14);
  auto *lap_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
  for (int k = 0; k < vis_laps; ++k) {
    const int lap_num = total_laps - k;
    const int ring_idx = (total_laps - 1 - k) % static_cast<int>(StopwatchRuntimeState::kMaxLapSplits);
    const uint32_t split_ms = rs.stopwatch.lap_splits_ms[ring_idx];
    const int row_y = card_cy + (k - 1) * scale_y(28);

    char lap_lbl[20];
    std::snprintf(lap_lbl, sizeof(lap_lbl), "Lap %d", lap_num);
    char split_lbl[24];
    format_stopwatch_tenths(split_ms, split_lbl, sizeof(split_lbl));

    fast_print(it, left_x, row_y, lap_font, ctx.col_slate, TextAlign::CENTER_LEFT, lap_lbl);
    fast_print(it, right_x, row_y, lap_font,
               (k == 0) ? ctx.theme_secondary : ctx.col_white,
               TextAlign::CENTER_RIGHT, split_lbl);
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
      (sub == TimeSubpage::CLOCK && rs.nav.world_clock_picker_page >= 0) ||
      (sub == TimeSubpage::TIMERS && timer_view_slot() != 0) ||
      (sub == TimeSubpage::ALARMS && rs.alarms.edit_idx != 0);
  if (!in_drilldown) {
    render_time_mode_bar(it, ctx, sub);
  }
}

}  // namespace latter_ui
