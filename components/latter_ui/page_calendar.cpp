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

static constexpr const char *kShortMonths[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static constexpr const char *kFullMonths[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};

static constexpr const char *kShortWeekdaysSun0[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static constexpr const char *kUpperWeekdaysSun0[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static constexpr const char *kSingleWeekdaysSun0[7] = {
    "S", "M", "T", "W", "T", "F", "S"};

int wrap_cal_lines(std::string_view text, int max_w, esphome::display::BaseFont *font,
                   std::string out_lines[], int max_lines) {
  const std::string clean = sanitize_utf8_for_font(std::string(text));
  int line_cnt = 0;
  size_t pos = 0;
  while (pos < clean.size() && line_cnt < max_lines) {
    while (pos < clean.size() && (clean[pos] == ' ' || clean[pos] == '\n' || clean[pos] == '\r')) {
      pos++;
    }
    if (pos >= clean.size()) break;
    const size_t nl = clean.find('\n', pos);
    if (line_cnt == max_lines - 1) {
      const std::string tail = (nl == std::string::npos) ? clean.substr(pos) : clean.substr(pos, nl - pos);
      out_lines[line_cnt++] = fit_text_to_width_font(tail, max_w, font);
      break;
    }
    const size_t seg_limit = (nl == std::string::npos) ? clean.size() : nl;
    size_t best_end = pos;
    size_t scan = pos;
    while (scan < seg_limit) {
      const size_t nxt_space = clean.find(' ', scan);
      const size_t word_end = (nxt_space == std::string::npos || nxt_space > seg_limit) ? seg_limit : nxt_space;
      const std::string cand = clean.substr(pos, word_end - pos);
      int w = 0, xo = 0, b = 0, h = 0;
      font->measure(cand.c_str(), &w, &xo, &b, &h);
      if (w <= max_w) {
        best_end = word_end;
        scan = (word_end >= seg_limit) ? seg_limit : (word_end + 1);
      } else {
        break;
      }
    }
    if (best_end == pos) {
      out_lines[line_cnt++] = fit_text_to_width_font(clean.substr(pos, seg_limit - pos), max_w, font);
      pos = (nl == std::string::npos) ? clean.size() : (nl + 1);
    } else {
      out_lines[line_cnt++] = clean.substr(pos, best_end - pos);
      pos = best_end;
    }
  }
  return line_cnt;
}

int32_t resolve_today_ymd(const UiFrameContext &ctx) {
  if (ctx.now.is_valid() && ctx.now.year >= 2020) {
    return make_ymd(ctx.now.year, ctx.now.month, ctx.now.day_of_month);
  }
  return 20260101;
}

esphome::Color cal_source_color(const UiFrameContext &ctx, uint8_t color_idx) {
  switch (color_idx % 6U) {
    case 0: return ctx.theme_primary;
    case 1: return ctx.col_green;
    case 2: return ctx.col_gold;
    case 3: return ctx.theme_secondary;
    case 4: return mix_color_q8(ctx.theme_primary, ctx.col_red, 148);
    default: return ctx.col_solaris_gold;
  }
}

esphome::Color event_accent_color(const UiFrameContext &ctx,
                                  uint8_t cal_idx,
                                  const CalendarSourceMeta *sources,
                                  int source_cnt) {
  if (sources != nullptr && static_cast<int>(cal_idx) < source_cnt) {
    return cal_source_color(ctx, sources[cal_idx].color_idx);
  }
  return cal_source_color(ctx, cal_idx);
}

void format_cal_clock_min(int mins, bool is_24h, char *out, size_t cap, bool include_ampm = true) {
  const int clamped = std::clamp(mins, 0, 1440);
  const int h24 = (clamped == 1440) ? 0 : (clamped / 60);
  const int mm = clamped % 60;
  if (is_24h) {
    if (clamped == 1440) {
      std::snprintf(out, cap, "24:00");
    } else {
      std::snprintf(out, cap, "%02d:%02d", h24, mm);
    }
    return;
  }
  int h12 = h24 % 12;
  if (h12 == 0) h12 = 12;
  const char *ap = (h24 >= 12 && clamped < 1440) ? "PM" : "AM";
  if (include_ampm) {
    std::snprintf(out, cap, "%d:%02d %s", h12, mm, ap);
  } else {
    std::snprintf(out, cap, "%d:%02d", h12, mm);
  }
}

void format_cal_event_time_range(const CalendarWindowEvent &ev, bool is_24h, char *out, size_t cap) {
  if (ev.all_day != 0U) {
    std::snprintf(out, cap, "All Day");
    return;
  }
  if (ev.start_min >= 0 && ev.end_min >= 0) {
    const bool same_meridiem = !is_24h &&
        ((ev.start_min < 720 && ev.end_min < 720) ||
         (ev.start_min >= 720 && ev.end_min >= 720 && ev.end_min < 1440));
    char s_buf[16];
    char e_buf[16];
    format_cal_clock_min(ev.start_min, is_24h, s_buf, sizeof(s_buf), !same_meridiem);
    format_cal_clock_min(ev.end_min, is_24h, e_buf, sizeof(e_buf), true);
    std::snprintf(out, cap, "%s – %s", s_buf, e_buf);
    return;
  }
  if (ev.start_min >= 0) {
    format_cal_clock_min(ev.start_min, is_24h, out, cap, true);
    return;
  }
  std::snprintf(out, cap, "Scheduled");
}

void format_cal_short_date(int32_t ymd, int32_t today_ymd, char *out, size_t cap) {
  int y = 2026, m = 1, d = 1;
  (void)split_ymd(ymd, y, m, d);
  const char *mon = kShortMonths[std::clamp(m - 1, 0, 11)];
  if (ymd == today_ymd) {
    std::snprintf(out, cap, "Today, %s %d", mon, d);
    return;
  }
  if (ymd == add_days_ymd(today_ymd, 1)) {
    std::snprintf(out, cap, "Tomorrow, %s %d", mon, d);
    return;
  }
  const int wd = weekday_sun0_from_ymd(ymd);
  std::snprintf(out, cap, "%s, %s %d", kShortWeekdaysSun0[wd], mon, d);
}

void format_cal_badge_date(int32_t ymd, int32_t today_ymd, char *out, size_t cap) {
  if (ymd == today_ymd) {
    std::snprintf(out, cap, "TODAY");
    return;
  }
  if (ymd == add_days_ymd(today_ymd, 1)) {
    std::snprintf(out, cap, "TMRW");
    return;
  }
  int y = 2026, m = 1, d = 1;
  (void)split_ymd(ymd, y, m, d);
  const int diff = days_between_ymd(today_ymd, ymd);
  if (diff >= 0 && diff < 7) {
    const int wd = weekday_sun0_from_ymd(ymd);
    std::snprintf(out, cap, "%s %d", kUpperWeekdaysSun0[wd], d);
  } else {
    std::snprintf(out, cap, "%s %d", kShortMonths[std::clamp(m - 1, 0, 11)], d);
  }
}

bool is_event_live_now(const CalendarWindowEvent &ev, int32_t today_ymd, int now_min) {
  if (!calendar_event_overlaps_ymd(ev, today_ymd) || now_min < 0) return false;
  if (ev.all_day != 0U) return false;
  if (ev.start_min < 0) return false;
  const int eff_end = (ev.end_min > ev.start_min) ? ev.end_min : (ev.start_min + 60);
  return now_min >= ev.start_min && now_min < eff_end;
}

void draw_shadow_text(esphome::display::Display &it, int x, int y,
                      esphome::display::BaseFont *font, esphome::Color col,
                      esphome::display::TextAlign align, const char *txt) {
  if (backdrop_active()) {
    fast_print(it, x + 1, y + 1, font, kChromeShadow, align, txt);
  }
  fast_print(it, x, y, font, col, align, txt);
}

void render_calendar_mode_bar(esphome::display::Display &it, const UiFrameContext &ctx, int active_sub) {
  using esphome::Color;
  const int cx = ctx.cx;
  int win_xs[4] = {0, 0, 0, 0};
  bool has_l = false;
  bool has_r = false;
  get_mode_bar_window_xs(4, 0, cx, win_xs, has_l, has_r);
  render_in_app_mode_bar_track(it, cx, win_xs, 4, active_sub, false, false, 4, active_sub,
                               ctx.theme_primary, ctx.col_white, 0);

  const int by = layout().app_dock_y;
  const int ir = ui::dock_icon_r(layout().dock_r);
  for (int i = 0; i < 4; ++i) {
    const int bx = win_xs[i];
    const Color fg_c = (i == active_sub) ? ctx.theme_primary : ctx.col_white;
    if (i == CalendarSubpage::OVERVIEW) {
      ui::draw_icon_cal_agenda(it, bx, by, ir, fg_c, true);
    } else if (i == CalendarSubpage::DAY) {
      ui::draw_icon_cal_day(it, bx, by, ir, fg_c, true);
    } else if (i == CalendarSubpage::WEEK) {
      ui::draw_icon_cal_week(it, bx, by, ir, fg_c, true);
    } else {
      ui::draw_icon_calendar(it, bx, by, ir, fg_c, true);
    }
  }
}

struct CalSubheaderGeom {
  int sub_y{0};
  int content_top{0};
  int content_bot{0};
};

CalSubheaderGeom render_calendar_subheader(esphome::display::Display &it,
                                           const UiFrameContext &ctx,
                                           const char *page_title,
                                           const char *period_label,
                                           bool period_is_away_from_today) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &rs = runtime_state();
  const auto &ly = layout();
  const int cx = ctx.cx;

  const bool show_fb = !rs.cal.feedback_text.empty() &&
                       (ctx.now_ms >= rs.cal.feedback_ms) &&
                       ((ctx.now_ms - rs.cal.feedback_ms) < 2400U);
  if (show_fb) {
    const std::string fit_fb = fit_text_to_width_font(rs.cal.feedback_text, safe_span(ly.hdr_y, 24), ctx.font_small);
    render_page_header(it, cx, fit_fb, ctx.font_small, ctx.theme_secondary);
  } else {
    render_page_header(it, cx, page_title, ctx.font_small, ctx.theme_primary);
  }

  const int sub_y = ly.sub_hdr_y + (LATTER_ROUND_DISPLAY ? scale_y(8) : scale_y(10));
  const int btn_r = (SCREEN_W < 360) ? 15 : scale_s(17);
  const int btn_d = 2 * btn_r + 1;
  const int safe_w = band_safe_span_for(
      sub_y, btn_d, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
  const int bar_w = std::min(safe_w, (SCREEN_W > 520) ? scale_x(392) : 348);
  const int bar_x0 = cx - bar_w / 2;
  const int bar_x1 = bar_x0 + bar_w;

  const int prev_cx = bar_x0 + btn_r;
  const int add_cx  = bar_x1 - btn_r;
  const int next_cx = add_cx - (2 * btn_r + std::max(6, scale_x(8)));
  const int hit_pad = std::max(4, (mm_to_px(7.0f) - btn_d) / 2);

  // [ ‹ ] Prev button
  render_glass_circle(it, prev_cx, sub_y, btn_r, kChromeGlassTint, kChromeCardBrdOn);
  const int ch_s = std::max(4, scale_s(5));
  for (int t = -1; t <= 0; ++t) {
    fast_line(it, prev_cx + ch_s / 2 + t, sub_y - ch_s, prev_cx - ch_s / 2 + t, sub_y, ctx.col_white);
    fast_line(it, prev_cx - ch_s / 2 + t, sub_y, prev_cx + ch_s / 2 + t, sub_y + ch_s, ctx.col_white);
  }
  frame_hits().add_circle(prev_cx, sub_y, btn_r, WidgetId::CAL_NAV_PREV,
                          kHitTap | kHitFocusCircle, -1, hit_pad);

  // [ › ] Next button
  render_glass_circle(it, next_cx, sub_y, btn_r, kChromeGlassTint, kChromeCardBrdOn);
  for (int t = 0; t <= 1; ++t) {
    fast_line(it, next_cx - ch_s / 2 + t, sub_y - ch_s, next_cx + ch_s / 2 + t, sub_y, ctx.col_white);
    fast_line(it, next_cx + ch_s / 2 + t, sub_y, next_cx - ch_s / 2 + t, sub_y + ch_s, ctx.col_white);
  }
  frame_hits().add_circle(next_cx, sub_y, btn_r, WidgetId::CAL_NAV_NEXT,
                          kHitTap | kHitFocusCircle, 1, hit_pad);

  // [ + ] Add Event button
  render_glass_circle(it, add_cx, sub_y, btn_r, kChromeGlassTint, ctx.theme_primary);
  const int plus_arm = std::max(4, scale_s(6));
  const int plus_th = std::max(2, scale_s(2));
  fill_rect_row_major(it, add_cx - plus_arm, sub_y - plus_th / 2, 2 * plus_arm + 1, plus_th, ctx.col_white);
  fill_rect_row_major(it, add_cx - plus_th / 2, sub_y - plus_arm, plus_th, 2 * plus_arm + 1, ctx.col_white);
  frame_hits().add_circle(add_cx, sub_y, btn_r, WidgetId::CAL_ADD_BTN,
                          kHitTap | kHitFocusCircle, 0, hit_pad);

  // Center Period Readout (interactive Today snap-back pill when navigated away from Today)
  const int mid_x0 = prev_cx + btn_r + std::max(6, scale_x(8));
  const int mid_x1 = next_cx - btn_r - std::max(6, scale_x(8));
  const int mid_cx = (mid_x0 + mid_x1) / 2;
  const int mid_w = std::max(40, mid_x1 - mid_x0);
  auto *mid_font = (SCREEN_W < 360 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

  if (period_is_away_from_today) {
    const int pill_h = std::max(28, btn_d - 2);
    const std::string fit_p = fit_text_to_width_font(period_label, mid_w - scale_x(14), mid_font);
    render_card_box(it, mid_x0, sub_y - pill_h / 2, mid_w, pill_h,
                    kChromeGlassTint, ctx.theme_primary, pill_h / 2);
    fast_print(it, mid_cx, sub_y, mid_font, ctx.col_white, TextAlign::CENTER, fit_p.c_str());
    frame_hits().add_rect(mid_x0, sub_y - pill_h / 2, mid_w, pill_h,
                          WidgetId::CAL_TODAY_BTN, kHitTap | kHitFocusSlot, 0, hit_pad);
  } else {
    const std::string fit_p = fit_text_to_width_font(period_label, mid_w - 4, mid_font);
    draw_shadow_text(it, mid_cx, sub_y, mid_font, ctx.col_white, TextAlign::CENTER, fit_p.c_str());
  }

  CalSubheaderGeom g{};
  g.sub_y = sub_y;
  g.content_top = sub_y + btn_r + std::max(8, scale_y(10));
  g.content_bot = ly.mode_bar_zone_top() - std::max(6, scale_y(8));
  return g;
}

void render_event_list_cards(esphome::display::Display &it,
                             const UiFrameContext &ctx,
                             const CalSubheaderGeom &geom,
                             const int *ev_indices,
                             int total_events,
                             int &io_page,
                             int32_t today_ymd,
                             int now_min,
                             bool is_24h,
                             bool show_date_badge,
                             const CalendarWindowStore &store,
                             const CalendarSourceMeta *sources,
                             int source_cnt,
                             const char *empty_msg) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int avail_h = std::max(80, geom.content_bot - geom.content_top);
  const int cy_mid = (geom.content_top + geom.content_bot) / 2;
  if (total_events <= 0) {
    io_page = 0;
    if (store.loading && store.window_start_ymd == 0) {
      render_empty_state(it, ctx.cx, cy_mid, "Syncing calendar...", ctx.font_small, ctx.theme_secondary);
    } else {
      render_empty_state(it, ctx.cx, cy_mid, empty_msg, ctx.font_small, ctx.col_slate);
    }
    return;
  }

  const int per_page = (SCREEN_H > 520) ? 5 : (LATTER_ROUND_DISPLAY ? 3 : 4);
  const int total_pages = std::max(1, (total_events + per_page - 1) / per_page);
  io_page = std::clamp(io_page, 0, total_pages - 1);

  const int gap = std::max(5, scale_y(6));
  const int max_card_h = scale_y(64);
  const int min_card_h = std::max(44, mm_to_px(7.0f));
  const int card_h = std::clamp((avail_h - (per_page - 1) * gap) / per_page, min_card_h, max_card_h);
  const int stride = card_h + gap;
  const int vis_cnt = std::min(per_page, total_events - io_page * per_page);
  const int block_h = per_page * card_h + (per_page - 1) * gap;
  const int start_y0 = geom.content_top + std::max(0, (avail_h - block_h) / 2);

  const int bounce_y = edge_bounce().offset_px(
      PageId::CALENDAR, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);

  auto *sub_font = (ctx.font_micro != nullptr && card_h < scale_y(54)) ? ctx.font_micro : ctx.font_small;
  const int pip_reserve = (total_pages > 1) ? scale_x(30) : 0;

  for (int r = 0; r < vis_cnt; ++r) {
    const int ev_idx = ev_indices[io_page * per_page + r];
    if (ev_idx < 0 || ev_idx >= store.count) continue;
    const auto &ev = store.events[ev_idx];

    const int cy_row = start_y0 + r * stride + card_h / 2 + bounce_y;
    const int safe_w = band_safe_span_for(
        cy_row, card_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14 + pip_reserve);
    const int card_w = std::min(safe_w, (SCREEN_W > 520) ? scale_x(384) : 344);
    const int card_x = ctx.cx - (card_w / 2) - (pip_reserve / 3);
    const int card_y = cy_row - card_h / 2;

    const bool live_now = is_event_live_now(ev, today_ymd, now_min);
    const Color accent = event_accent_color(ctx, ev.cal_idx, sources, source_cnt);
    const Color brd_c = live_now ? ctx.theme_primary : kChromeCardBrdOn;

    render_card_box(it, card_x, card_y, card_w, card_h, kChromeGlassTint, brd_c, scale_s(12));

    // Left calendar color bar
    const int bar_w = std::max(3, scale_x(4));
    const int bar_pad_y = std::max(6, scale_y(8));
    fill_rect_row_major(it, card_x + std::max(6, scale_x(8)), card_y + bar_pad_y,
                        bar_w, std::max(12, card_h - 2 * bar_pad_y), accent);

    const int text_x = card_x + std::max(15, scale_x(18));
    const int right_x = card_x + card_w - std::max(10, scale_x(12));

    char badge_buf[20] = "";
    if (live_now) {
      std::snprintf(badge_buf, sizeof(badge_buf), "NOW");
    } else if (show_date_badge) {
      format_cal_badge_date(ev.start_ymd, today_ymd, badge_buf, sizeof(badge_buf));
    }

    int badge_w = 0;
    if (badge_buf[0] != '\0') {
      int xo = 0, bl = 0, th = 0;
      sub_font->measure(badge_buf, &badge_w, &xo, &bl, &th);
      badge_w += std::max(8, scale_x(10));
    }

    const int title_y = cy_row - std::max(9, card_h / 5);
    const int meta_y  = cy_row + std::max(10, card_h / 4);

    const std::string fit_title = fit_text_to_width_font(
        ev.title, std::max(40, (right_x - text_x) - badge_w), ctx.font_small);
    fast_print(it, text_x, title_y, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT, fit_title.c_str());

    if (badge_buf[0] != '\0') {
      const Color b_col = (live_now || ev.start_ymd == today_ymd) ? ctx.theme_primary : ctx.theme_secondary;
      fast_print(it, right_x, title_y, sub_font, b_col, TextAlign::CENTER_RIGHT, badge_buf);
    }

    char tm_buf[36];
    format_cal_event_time_range(ev, is_24h, tm_buf, sizeof(tm_buf));
    std::string meta_str(tm_buf);
    if (ev.location[0] != '\0') {
      meta_str += " • ";
      meta_str += ev.location;
    } else if (source_cnt > 1 && static_cast<int>(ev.cal_idx) < source_cnt &&
               sources[ev.cal_idx].label[0] != '\0') {
      meta_str += " • ";
      meta_str += sources[ev.cal_idx].label;
    }
    const std::string fit_meta = fit_text_to_width_font(meta_str, std::max(40, right_x - text_x), sub_font);
    fast_print(it, text_x, meta_y, sub_font, ctx.col_slate, TextAlign::CENTER_LEFT, fit_meta.c_str());

    frame_hits().add_rect(card_x, card_y, card_w, card_h,
                          static_cast<uint16_t>(WidgetId::CAL_EVENT_BASE + r),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(ev_idx));
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, io_page > 0, (io_page + 1) < total_pages,
                              ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
  }
}

void render_overview_tab(esphome::display::Display &it,
                         const UiFrameContext &ctx,
                         DeviceRuntimeState &rs,
                         const PersistedDevicePrefs &prefs,
                         int32_t today_ymd,
                         int now_min,
                         const CalendarWindowStore &store,
                         const CalendarSourceMeta *sources,
                         int source_cnt) {
  const int lookahead = static_cast<int>(snap_calendar_lookahead_days(prefs.calendar_lookahead_days));
  char period_buf[32];
  std::snprintf(period_buf, sizeof(period_buf), "Next %d Days", lookahead);
  const auto geom = render_calendar_subheader(it, ctx, "UPCOMING", period_buf, false);

  int idxs[kMaxCalendarWindowEvents]{};
  const int cnt = collect_calendar_overview_events(
      today_ymd, now_min, lookahead, idxs, kMaxCalendarWindowEvents, store);

  // Cold-boot preview fallback from calendar_state().summary while full window RPC is in flight
  if (cnt == 0 && store.window_start_ymd == 0 && !calendar_state().summary.empty()) {
    CalendarRecord recs[5]{};
    const size_t n = parse_calendar_records(calendar_state().summary, recs, 5);
    if (n > 0) {
      CalendarWindowStore temp_store{};
      temp_store.count = static_cast<int>(n);
      for (size_t i = 0; i < n; ++i) {
        auto & dst = temp_store.events[i];
        dst.start_ymd = today_ymd;
        dst.end_ymd = today_ymd;
        dst.all_day = recs[i].all_day ? 1U : 0U;
        copy_bounded_cstr(dst.title, recs[i].title);
        copy_bounded_cstr(dst.location, recs[i].location);
        idxs[i] = static_cast<int>(i);
      }
      render_event_list_cards(it, ctx, geom, idxs, temp_store.count, rs.cal.overview_page,
                              today_ymd, now_min, prefs.is_24h_format, true,
                              temp_store, sources, source_cnt, "No upcoming events — tap +");
      return;
    }
  }

  render_event_list_cards(it, ctx, geom, idxs, cnt, rs.cal.overview_page,
                          today_ymd, now_min, prefs.is_24h_format, true,
                          store, sources, source_cnt, "No upcoming events — tap +");
}

void render_day_tab(esphome::display::Display &it,
                    const UiFrameContext &ctx,
                    DeviceRuntimeState &rs,
                    const PersistedDevicePrefs &prefs,
                    int32_t today_ymd,
                    int now_min,
                    const CalendarWindowStore &store,
                    const CalendarSourceMeta *sources,
                    int source_cnt) {
  char period_buf[36];
  format_cal_short_date(rs.cal.selected_ymd, today_ymd, period_buf, sizeof(period_buf));
  const bool away = (rs.cal.selected_ymd != today_ymd);
  const auto geom = render_calendar_subheader(it, ctx, "DAY VIEW", period_buf, away);

  int idxs[kMaxCalendarWindowEvents]{};
  const int cnt = collect_calendar_day_events(rs.cal.selected_ymd, idxs, kMaxCalendarWindowEvents, store);
  render_event_list_cards(it, ctx, geom, idxs, cnt, rs.cal.day_page,
                          today_ymd, now_min, prefs.is_24h_format, false,
                          store, sources, source_cnt, "No events on this day — tap +");
}

void render_week_tab(esphome::display::Display &it,
                     const UiFrameContext &ctx,
                     DeviceRuntimeState &rs,
                     const PersistedDevicePrefs &prefs,
                     int32_t today_ymd,
                     int now_min,
                     const CalendarWindowStore &store,
                     const CalendarSourceMeta *sources,
                     int source_cnt) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const bool monday_first = (prefs.calendar_week_start == 1U);
  const int32_t week_start = start_of_week_ymd(rs.cal.selected_ymd, monday_first);
  const int32_t week_end   = add_days_ymd(week_start, 6);
  const int32_t today_week = start_of_week_ymd(today_ymd, monday_first);

  int sy = 2026, sm = 1, sd = 1;
  int ey = 2026, em = 1, ed = 1;
  (void)split_ymd(week_start, sy, sm, sd);
  (void)split_ymd(week_end, ey, em, ed);
  char period_buf[36];
  if (sm == em) {
    std::snprintf(period_buf, sizeof(period_buf), "%s %d – %d",
                  kShortMonths[std::clamp(sm - 1, 0, 11)], sd, ed);
  } else {
    std::snprintf(period_buf, sizeof(period_buf), "%s %d – %s %d",
                  kShortMonths[std::clamp(sm - 1, 0, 11)], sd,
                  kShortMonths[std::clamp(em - 1, 0, 11)], ed);
  }

  const auto geom = render_calendar_subheader(it, ctx, "WEEK VIEW", period_buf, week_start != today_week);

  if constexpr (SCREEN_W > 520) {
    // Desk (720x720): Full 7-Column Interactive Week Board
    const int board_h = std::max(180, geom.content_bot - geom.content_top);
    const int safe_w = band_safe_span_for(
        (geom.content_top + geom.content_bot) / 2, board_h, SCREEN_W, SCREEN_H,
        LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);
    const int gap = scale_x(6);
    const int col_w = std::max(64, (safe_w - 6 * gap) / 7);
    const int total_w = 7 * col_w + 6 * gap;
    const int x0_board = ctx.cx - total_w / 2;
    auto *chip_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

    for (int d = 0; d < 7; ++d) {
      const int32_t d_ymd = add_days_ymd(week_start, d);
      int yy = 2026, mm = 1, dd = 1;
      (void)split_ymd(d_ymd, yy, mm, dd);
      const int wd = weekday_sun0_from_ymd(d_ymd);
      const bool is_today = (d_ymd == today_ymd);
      const bool is_sel = (d_ymd == rs.cal.selected_ymd);

      const int col_x = x0_board + d * (col_w + gap);
      const int col_cx = col_x + col_w / 2;
      const Color brd_c = is_today ? ctx.theme_primary : (is_sel ? ctx.theme_secondary : kChromeCardBorder);

      render_card_box(it, col_x, geom.content_top, col_w, board_h, kChromeGlassTint, brd_c, scale_s(12));

      const int wd_y = geom.content_top + scale_y(16);
      const int num_y = geom.content_top + scale_y(40);
      fast_print(it, col_cx, wd_y, chip_font,
                 is_today ? ctx.theme_primary : ctx.col_slate,
                 TextAlign::CENTER, kUpperWeekdaysSun0[wd]);

      if (is_today) {
        ui::draw_aa_disc(it, static_cast<float>(col_cx), static_cast<float>(num_y),
                         static_cast<float>(scale_s(13)), ctx.theme_primary);
      }
      char num_buf[8];
      std::snprintf(num_buf, sizeof(num_buf), "%d", dd);
      fast_print(it, col_cx, num_y, ctx.font_small,
                 is_today ? ctx.col_black : ctx.col_white, TextAlign::CENTER, num_buf);

      fast_hline(it, col_x + 6, geom.content_top + scale_y(58), col_w - 12, kChromeCardBorder);

      int day_idxs[16]{};
      const int day_cnt = collect_calendar_day_events(d_ymd, day_idxs, 16, store);
      const int chip_top_start = geom.content_top + scale_y(66);
      const int chip_h = scale_y(46);
      const int chip_gap = scale_y(6);
      const int max_chips = std::max(1, (geom.content_bot - scale_y(10) - chip_top_start) / (chip_h + chip_gap));
      const int vis_chips = std::min(day_cnt, max_chips);

      for (int c = 0; c < vis_chips; ++c) {
        const auto &ev = store.events[day_idxs[c]];
        const int cy_top = chip_top_start + c * (chip_h + chip_gap);
        const Color acc = event_accent_color(ctx, ev.cal_idx, sources, source_cnt);
        fill_rect_row_major(it, col_x + 5, cy_top, col_w - 10, chip_h, kChromeCardBgOn);
        fill_rect_row_major(it, col_x + 5, cy_top, 4, chip_h, acc);

        char t_short[12];
        if (ev.all_day != 0U) {
          std::snprintf(t_short, sizeof(t_short), "All Day");
        } else if (ev.start_min >= 0) {
          format_cal_clock_min(ev.start_min, prefs.is_24h_format, t_short, sizeof(t_short), !prefs.is_24h_format);
        } else {
          std::snprintf(t_short, sizeof(t_short), "Event");
        }
        const std::string fit_t = fit_text_to_width_font(t_short, col_w - 16, chip_font);
        const std::string fit_n = fit_text_to_width_font(ev.title, col_w - 16, chip_font);
        fast_print(it, col_x + 12, cy_top + chip_h / 3, chip_font, acc, TextAlign::CENTER_LEFT, fit_t.c_str());
        fast_print(it, col_x + 12, cy_top + (chip_h * 3) / 4 - 2, chip_font, ctx.col_white,
                   TextAlign::CENTER_LEFT, fit_n.c_str());
      }
      if (day_cnt > max_chips) {
        char more_buf[16];
        std::snprintf(more_buf, sizeof(more_buf), "+%d more", day_cnt - max_chips);
        fast_print(it, col_cx, geom.content_bot - scale_y(12), chip_font, ctx.theme_secondary,
                   TextAlign::CENTER, more_buf);
      }

      frame_hits().add_rect(col_x, geom.content_top, col_w, board_h,
                            static_cast<uint16_t>(WidgetId::CAL_WEEK_DAY_BASE + d),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(d));
    }
    return;
  }

  // Compact panels (<= 520px): 7-Day Interactive Capsule Strip + Scheduled Week Events below
  const int strip_h = std::max(mm_to_px(7.0f), scale_y(50));
  const int strip_cy = geom.content_top + strip_h / 2;
  const int strip_safe = band_safe_span_for(
      strip_cy, strip_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 12);
  const int strip_w = std::min(strip_safe, 356);
  const int gap = (SCREEN_W < 360) ? 3 : 4;
  const int cap_w = std::max(34, (strip_w - 6 * gap) / 7);
  const int tot_strip_w = 7 * cap_w + 6 * gap;
  const int x0_strip = ctx.cx - tot_strip_w / 2;
  auto *lbl_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

  for (int d = 0; d < 7; ++d) {
    const int32_t d_ymd = add_days_ymd(week_start, d);
    int yy = 2026, mm = 1, dd = 1;
    (void)split_ymd(d_ymd, yy, mm, dd);
    const int wd = weekday_sun0_from_ymd(d_ymd);
    const bool is_today = (d_ymd == today_ymd);
    const bool is_sel = (d_ymd == rs.cal.selected_ymd);

    const int cx_d = x0_strip + d * (cap_w + gap) + cap_w / 2;
    const Color brd_c = is_today ? ctx.theme_primary : (is_sel ? ctx.theme_secondary : kChromeCardBorder);
    render_card_box(it, cx_d - cap_w / 2, strip_cy - strip_h / 2, cap_w, strip_h,
                    kChromeGlassTint, brd_c, scale_s(10));

    fast_print(it, cx_d, strip_cy - strip_h / 4 - 1, lbl_font,
               is_today ? ctx.theme_primary : ctx.col_slate,
               TextAlign::CENTER, kSingleWeekdaysSun0[wd]);

    char dd_buf[8];
    std::snprintf(dd_buf, sizeof(dd_buf), "%d", dd);
    fast_print(it, cx_d, strip_cy + 3, ctx.font_small,
               is_today ? ctx.theme_primary : ctx.col_white, TextAlign::CENTER, dd_buf);

    uint8_t dot_cals[3]{};
    const int n_dots = collect_calendar_day_dots(d_ymd, dot_cals, store);
    if (n_dots > 0) {
      const int dot_y = strip_cy + strip_h / 2 - 6;
      const int dot_sp = 6;
      const int dx0 = cx_d - ((n_dots - 1) * dot_sp) / 2;
      for (int di = 0; di < n_dots; ++di) {
        ui::draw_aa_disc(it, static_cast<float>(dx0 + di * dot_sp), static_cast<float>(dot_y),
                         2.1f, event_accent_color(ctx, dot_cals[di], sources, source_cnt));
      }
    }

    frame_hits().add_centered_rect(cx_d, strip_cy, cap_w, strip_h,
                                   static_cast<uint16_t>(WidgetId::CAL_WEEK_DAY_BASE + d),
                                   kHitTap | kHitFocusSlot, static_cast<int16_t>(d));
  }

  // Collect all scheduled events overlapping [week_start .. week_end]
  int week_idxs[kMaxCalendarWindowEvents]{};
  int week_cnt = 0;
  for (int i = 0; i < store.count && week_cnt < kMaxCalendarWindowEvents; ++i) {
    const auto &ev = store.events[i];
    if (ev.end_ymd >= week_start && ev.start_ymd <= week_end) {
      week_idxs[week_cnt++] = i;
    }
  }

  CalSubheaderGeom list_geom = geom;
  list_geom.content_top = strip_cy + strip_h / 2 + std::max(6, scale_y(8));
  render_event_list_cards(it, ctx, list_geom, week_idxs, week_cnt, rs.cal.week_page,
                          today_ymd, now_min, prefs.is_24h_format, true,
                          store, sources, source_cnt, "No events this week — tap +");
}

void render_month_tab(esphome::display::Display &it,
                      const UiFrameContext &ctx,
                      DeviceRuntimeState &rs,
                      const PersistedDevicePrefs &prefs,
                      int32_t today_ymd,
                      const CalendarWindowStore &store,
                      const CalendarSourceMeta *sources,
                      int source_cnt) {
  using esphome::Color;
  using esphome::display::TextAlign;

  int sel_y = 2026, sel_m = 1, sel_d = 1;
  int tod_y = 2026, tod_m = 1, tod_d = 1;
  (void)split_ymd(rs.cal.selected_ymd, sel_y, sel_m, sel_d);
  (void)split_ymd(today_ymd, tod_y, tod_m, tod_d);

  char period_buf[36];
  std::snprintf(period_buf, sizeof(period_buf), "%s %d",
                kFullMonths[std::clamp(sel_m - 1, 0, 11)], sel_y);
  const bool away = (sel_y != tod_y) || (sel_m != tod_m);
  const auto geom = render_calendar_subheader(it, ctx, "MONTH VIEW", period_buf, away);

  const bool monday_first = (prefs.calendar_week_start == 1U);
  const int32_t first_ymd = make_ymd(sel_y, sel_m, 1);
  const int32_t grid_start = start_of_week_ymd(first_ymd, monday_first);

  auto *hdr_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
  auto *cell_font = (SCREEN_W <= 520 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

  const int wd_y = geom.content_top + std::max(8, scale_y(9));
  const int grid_top = wd_y + std::max(12, scale_y(14));
  const int grid_h = std::max(120, geom.content_bot - grid_top);
  const int row_h = grid_h / 6;

  // Compute safe width across the entire 6-row matrix so all 7 columns stay neatly aligned
  const int safe_top = safe_span(wd_y, 14);
  const int safe_bot = safe_span(grid_top + 6 * row_h - 2, 16);
  const int grid_w = std::min({safe_top, safe_bot, (SCREEN_W > 520) ? scale_x(404) : 344});
  const int col_w = std::max(28, grid_w / 7);
  const int eff_grid_w = col_w * 7;
  const int grid_x0 = ctx.cx - eff_grid_w / 2;

  for (int c = 0; c < 7; ++c) {
    const int wd = monday_first ? ((c + 1) % 7) : c;
    const int cx_c = grid_x0 + c * col_w + col_w / 2;
    const char *lbl = (SCREEN_W > 520) ? kUpperWeekdaysSun0[wd] : kSingleWeekdaysSun0[wd];
    draw_shadow_text(it, cx_c, wd_y, hdr_font, ctx.theme_secondary, TextAlign::CENTER, lbl);
  }

  const int ring_r = std::max(9, std::min(col_w, row_h) / 2 - (SCREEN_W > 520 ? 7 : 3));
  const float dot_r = (SCREEN_W > 520) ? 2.9f : 1.9f;
  const int dot_sp = (SCREEN_W > 520) ? 8 : 5;

  for (int r = 0; r < 6; ++r) {
    const int row_y0 = grid_top + r * row_h;
    const int row_cy = row_y0 + row_h / 2;

    for (int c = 0; c < 7; ++c) {
      const int cell_idx = r * 7 + c;
      const int32_t cell_ymd = add_days_ymd(grid_start, cell_idx);
      int cy_v = 2026, cm_v = 1, cd_v = 1;
      (void)split_ymd(cell_ymd, cy_v, cm_v, cd_v);
      const bool in_month = (cm_v == sel_m && cy_v == sel_y);
      const bool is_today = (cell_ymd == today_ymd);
      const bool is_sel = (cell_ymd == rs.cal.selected_ymd);

      const int cell_x0 = grid_x0 + c * col_w;
      const int cell_cx = cell_x0 + col_w / 2;
      const int num_cy = row_cy - (SCREEN_W > 520 ? 4 : 2);

      if (is_today) {
        ui::draw_aa_ring(it, static_cast<float>(cell_cx), static_cast<float>(num_cy),
                         static_cast<float>(ring_r), (SCREEN_W > 520) ? 2.2f : 1.7f, ctx.theme_primary);
      } else if (is_sel && in_month) {
        ui::draw_aa_ring(it, static_cast<float>(cell_cx), static_cast<float>(num_cy),
                         static_cast<float>(ring_r), 1.3f, kChromeCardBrdOn);
      }

      char d_buf[8];
      std::snprintf(d_buf, sizeof(d_buf), "%d", cd_v);
      const Color fg = is_today ? ctx.theme_primary : (in_month ? ctx.col_white : kChromeSubtle);
      draw_shadow_text(it, cell_cx, num_cy, cell_font, fg, TextAlign::CENTER, d_buf);

      uint8_t dot_cals[3]{};
      const int n_dots = collect_calendar_day_dots(cell_ymd, dot_cals, store);
      if (n_dots > 0) {
        const int dot_y = row_y0 + row_h - (SCREEN_W > 520 ? 7 : 3);
        const int dx0 = cell_cx - ((n_dots - 1) * dot_sp) / 2;
        for (int di = 0; di < n_dots; ++di) {
          const Color dc = event_accent_color(ctx, dot_cals[di], sources, source_cnt);
          ui::draw_aa_disc(it, static_cast<float>(dx0 + di * dot_sp), static_cast<float>(dot_y), dot_r, dc);
        }
      }

      if constexpr (SCREEN_W > 520) {
        frame_hits().add_rect(cell_x0, row_y0, col_w, row_h,
                              static_cast<uint16_t>(WidgetId::CAL_MONTH_CELL_BASE + cell_idx),
                              kHitTap | kHitFocusSlot, static_cast<int16_t>(cell_idx));
      }
    }

    if constexpr (SCREEN_W <= 520) {
      const int hit_pad = std::max(0, (mm_to_px(7.0f) - row_h + 1) / 2);
      frame_hits().add_rect(grid_x0, row_y0, eff_grid_w, row_h,
                            static_cast<uint16_t>(WidgetId::CAL_MONTH_WEEK_BASE + r),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(r), hit_pad);
    }
  }
}

void render_detail_drilldown(esphome::display::Display &it,
                             const UiFrameContext &ctx,
                             DeviceRuntimeState &rs,
                             const PersistedDevicePrefs &prefs,
                             int32_t today_ymd,
                             const CalendarWindowStore &store,
                             const CalendarSourceMeta *sources,
                             int source_cnt) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &ly = layout();
  const int cx = ctx.cx;
  if (rs.cal.selected_event_idx < 0 || rs.cal.selected_event_idx >= store.count) {
    render_page_header(it, cx, "EVENT DETAILS", ctx.font_small, ctx.theme_primary);
    render_empty_state(it, cx, ctx.cy, "Event no longer available", ctx.font_small, ctx.col_slate);
    render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
    return;
  }

  const auto &ev = store.events[rs.cal.selected_event_idx];
  const bool can_del = (ev.uid[0] != '\0') &&
                       (static_cast<int>(ev.cal_idx) < source_cnt) &&
                       sources[ev.cal_idx].can_delete;

  if (rs.cal.delete_confirm_open) {
    char dt_line[48];
    format_cal_short_date(ev.start_ymd, today_ymd, dt_line, sizeof(dt_line));
    ui::confirm_modal(it, cx, "DELETE EVENT?", ev.title, dt_line,
                      "Cancel", "Delete", ctx.col_red, ui::chrome_danger_bg(),
                      ctx.font_small, ctx.font_small,
                      ctx.theme_primary, ctx.col_white, ctx.col_slate,
                      WidgetId::CONFIRM_CANCEL, WidgetId::CONFIRM_ACCEPT);
    return;
  }

  render_page_header(it, cx, "EVENT DETAILS", ctx.font_small, ctx.theme_primary);

  const Color accent = event_accent_color(ctx, ev.cal_idx, sources, source_cnt);
  const int meta_y = scale_y(84);
  char dt_buf[36];
  char tm_buf[36];
  format_cal_short_date(ev.start_ymd, today_ymd, dt_buf, sizeof(dt_buf));
  format_cal_event_time_range(ev, prefs.is_24h_format, tm_buf, sizeof(tm_buf));
  char when_buf[80];
  std::snprintf(when_buf, sizeof(when_buf), "%s  •  %s", dt_buf, tm_buf);
  const std::string fit_when = fit_text_to_width_font(when_buf, safe_span(meta_y, 20), ctx.font_small);
  draw_shadow_text(it, cx, meta_y, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, fit_when.c_str());

  int cur_y = scale_y(114);
  if (static_cast<int>(ev.cal_idx) < source_cnt && sources[ev.cal_idx].label[0] != '\0') {
    auto *src_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
    const std::string src_fit = fit_text_to_width_font(sources[ev.cal_idx].label, safe_span(cur_y, 36), src_font);
    int sw = 0, xo = 0, bl = 0, th = 0;
    src_font->measure(src_fit.c_str(), &sw, &xo, &bl, &th);
    const int tot_w = sw + 14;
    const int dot_x = cx - tot_w / 2 + 4;
    ui::draw_aa_disc(it, static_cast<float>(dot_x), static_cast<float>(cur_y), 3.6f, accent);
    draw_shadow_text(it, dot_x + 10, cur_y, src_font, ctx.col_slate, TextAlign::CENTER_LEFT, src_fit.c_str());
    cur_y += scale_y(28);
  } else {
    cur_y += scale_y(12);
  }

  // Wrapped Title (non-interactive text drawn cleanly over backdrop with 1px shadow)
  const int title_max_w = std::min(safe_span(cur_y + scale_y(14), 22), scale_x(336));
  std::string title_lines[2];
  const int t_cnt = wrap_cal_lines(ev.title, title_max_w, ctx.font_date, title_lines, 2);
  for (int i = 0; i < t_cnt; ++i) {
    draw_shadow_text(it, cx, cur_y, ctx.font_date, ctx.col_white, TextAlign::CENTER, title_lines[i].c_str());
    cur_y += scale_y(30);
  }

  cur_y += scale_y(6);
  fast_hline(it, cx - scale_x(104), cur_y, 2 * scale_x(104), kChromeCardBorder);
  cur_y += scale_y(20);

  const bool has_loc = (ev.location[0] != '\0');
  if (has_loc) {
    const std::string loc_fit = fit_text_to_width_font(ev.location, safe_span(cur_y, 22), ctx.font_small);
    draw_shadow_text(it, cx, cur_y, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, loc_fit.c_str());
    cur_y += scale_y(26);
  }

  const int btn_y = ly.back_y - scale_y(54);
  const int desc_avail = btn_y - scale_y(30) - cur_y;
  if (ev.description[0] != '\0' && desc_avail >= scale_y(20)) {
    auto *desc_font = (ctx.font_micro != nullptr && SCREEN_H <= 520) ? ctx.font_micro : ctx.font_small;
    const int max_desc_lines = (desc_avail >= scale_y(50)) ? 2 : 1;
    std::string desc_lines[2];
    const int d_cnt = wrap_cal_lines(ev.description, safe_span(cur_y + 10, 26), desc_font, desc_lines, max_desc_lines);
    for (int i = 0; i < d_cnt; ++i) {
      draw_shadow_text(it, cx, cur_y, desc_font, ctx.col_slate, TextAlign::CENTER, desc_lines[i].c_str());
      cur_y += scale_y(22);
    }
  } else if (!has_loc && ev.description[0] == '\0') {
    draw_shadow_text(it, cx, cur_y + scale_y(10), ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER, "No additional details");
  }

  // Interactive Action Row (View Map & Delete Event)
  const int btn_h = std::max(40, scale_y(42));
  const int btn_r = btn_h / 2;
  const int row_safe = band_safe_span_for(
      btn_y, btn_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);

  if (has_loc && can_del) {
    const int gap = scale_x(10);
    const int each_w = std::min(scale_x(146), (row_safe - gap) / 2);
    const int map_x1 = cx - gap / 2 - each_w;
    const int map_x2 = cx - gap / 2;
    const int del_x1 = cx + gap / 2;
    const int del_x2 = cx + gap / 2 + each_w;
    ui::pill_button(it, map_x1, map_x2, btn_y, btn_r, ctx.theme_primary, kChromeGlassTint,
                    "View Map", ctx.col_white, ctx.font_small,
                    WidgetId::CAL_VIEW_MAP_BTN, kHitTap | kHitFocusSlot);
    ui::pill_button(it, del_x1, del_x2, btn_y, btn_r, ctx.col_red, ui::chrome_danger_bg(),
                    "Delete", ctx.col_white, ctx.font_small,
                    WidgetId::CAL_DELETE_BTN, kHitTap | kHitFocusSlot);
  } else if (has_loc) {
    const int hw = std::min(scale_x(108), row_safe / 2);
    ui::pill_button(it, cx - hw, cx + hw, btn_y, btn_r, ctx.theme_primary, kChromeGlassTint,
                    "View Map", ctx.col_white, ctx.font_small,
                    WidgetId::CAL_VIEW_MAP_BTN, kHitTap | kHitFocusSlot);
  } else if (can_del) {
    const int hw = std::min(scale_x(108), row_safe / 2);
    ui::pill_button(it, cx - hw, cx + hw, btn_y, btn_r, ctx.col_red, ui::chrome_danger_bg(),
                    "Delete Event", ctx.col_white, ctx.font_small,
                    WidgetId::CAL_DELETE_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}

void render_map_drilldown(esphome::display::Display &it,
                          const UiFrameContext &ctx,
                          const DeviceRuntimeState &rs) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &ly = layout();
  const std::string &base_map_u = rs.cal.detail_map_url;
  const bool has_map_img = !base_map_u.empty() &&
                           ctx.camera_snapshot_art.valid() &&
                           ctx.camera_snapshot_art.width > 16 &&
                           ctx.camera_snapshot_art.height > 16 &&
                           !rs.alert.loaded_camera_url.empty() &&
                           rs.alert.loaded_camera_url.find(base_map_u) != std::string::npos;

  if (has_map_img) {
    render_album_art_canvas(it, ctx.camera_snapshot_art, ctx.cx, false, false, false, false);

    const int map_hdr_w = std::min(scale_x(278), safe_span(scale_y(41), 16));
    char z_prefix[16];
    std::snprintf(z_prefix, sizeof(z_prefix), "z%d • ", rs.cal.map_zoom);
    const std::string loc_hdr = fit_text_to_width_font(
        std::string(z_prefix) + rs.cal.detail_map_location, map_hdr_w - 16, ctx.font_small);
    render_card_box(it, ctx.cx - map_hdr_w / 2, scale_y(24), map_hdr_w, scale_y(34),
                    kChromeGlassTint, ctx.theme_primary, scale_s(10));
    fast_print(it, ctx.cx + 1, scale_y(41) + 1, ctx.font_small, kChromeShadow, TextAlign::CENTER, loc_hdr.c_str());
    fast_print(it, ctx.cx, scale_y(41), ctx.font_small, ctx.col_white, TextAlign::CENTER, loc_hdr.c_str());
    frame_hits().add_rect(ctx.cx - map_hdr_w / 2, scale_y(24), map_hdr_w, scale_y(34),
                          WidgetId::CAL_TODAY_BTN, kHitTap | kHitFocusSlot);

    const int z_by = ly.map_zoom_y;
    const int z_lx = ly.map_zoom_left_x();
    const int z_rx = ly.map_zoom_right_x();
    const int z_r  = scale_s(21);
    render_glass_circle(it, z_lx, z_by, z_r, kChromeGlassTint,
                        (rs.cal.map_zoom > 10) ? ctx.theme_primary : kChromeCardBorder);
    fill_rect_row_major(it, z_lx - scale_s(8), z_by - 1, scale_s(17), std::max(2, scale_s(3)),
                        (rs.cal.map_zoom > 10) ? ctx.col_white : ctx.col_slate);
    frame_hits().add_circle(z_lx, z_by, z_r, WidgetId::CAL_ZOOM_OUT_BTN,
                            kHitTap | kHitFocusCircle, 0, scale_s(16));

    const Color fg_in = (rs.cal.map_zoom < 18) ? ctx.col_white : ctx.col_slate;
    render_glass_circle(it, z_rx, z_by, z_r, kChromeGlassTint,
                        (rs.cal.map_zoom < 18) ? ctx.theme_primary : kChromeCardBorder);
    fill_rect_row_major(it, z_rx - scale_s(8), z_by - 1, scale_s(17), std::max(2, scale_s(3)), fg_in);
    fill_rect_row_major(it, z_rx - 1, z_by - scale_s(8), std::max(2, scale_s(3)), scale_s(17), fg_in);
    frame_hits().add_circle(z_rx, z_by, z_r, WidgetId::CAL_ZOOM_IN_BTN,
                            kHitTap | kHitFocusCircle, 0, scale_s(16));

    render_glass_card(it, ctx.cx - scale_x(64), ly.back_y - scale_y(16), scale_x(128), scale_y(32),
                      scale_s(16), kChromeGlassTint, -1, GlassVariant::Clear, ctx.col_white);
  } else {
    render_page_header(it, ctx.cx, "EVENT MAP", ctx.font_small, ctx.theme_primary);
    render_empty_state(it, ctx.cx, scale_y(222), "Loading OpenStreetMap...", ctx.font_small, ctx.theme_secondary);
  }

  render_back_button(it, ctx.cx, ctx.font_small, ly.back_y, "< Back", ctx.col_white);
}

void render_add_step1(esphome::display::Display &it,
                      const UiFrameContext &ctx,
                      DeviceRuntimeState &rs,
                      const CalendarSourceMeta *writable_sources,
                      int writable_cnt) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &ly = layout();
  const int cx = ctx.cx;
  const bool dictating = cal_dictation_active(rs);
  const bool show_fb = !dictating && !rs.cal.feedback_text.empty() &&
                       (ctx.now_ms >= rs.cal.feedback_ms) &&
                       ((ctx.now_ms - rs.cal.feedback_ms) < 2400U);

  if (dictating) {
    render_page_header(it, cx,
                       (rs.cal.dictation_field == 1U) ? "SPEAK EVENT TITLE..." : "SPEAK LOCATION...",
                       ctx.font_small, ctx.col_red);
  } else if (show_fb) {
    render_page_header(it, cx, rs.cal.feedback_text, ctx.font_small, ctx.theme_secondary);
  } else {
    render_page_header(it, cx, "NEW EVENT", ctx.font_small, ctx.theme_primary);
  }

  const int row_h = std::min(scale_y(44), ly.slot5_stride - 6);
  const int mic_r = row_h / 2;
  const int mic_gap = std::max(6, scale_x(8));

  auto draw_field_with_mic = [&](int slot_idx, const char *prefix, const std::string &val,
                                 const char *placeholder, uint16_t field_wid, uint16_t mic_wid,
                                 uint8_t dict_id) {
    const int ym = ly.slot5_y(slot_idx);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
    const int tot_w = std::min(safe_w, (SCREEN_W > 520) ? scale_x(364) : 332);
    const int x0 = cx - tot_w / 2;
#if LATTER_HAS_MIC
    const int card_w = tot_w - (2 * mic_r + mic_gap);
    const int mic_cx = x0 + card_w + mic_gap + mic_r;
    const bool mic_active = (rs.cal.dictation_field == dict_id);
#else
    const int card_w = tot_w;
    const bool mic_active = false;
    (void)mic_wid;
    (void)dict_id;
#endif
    render_card_box(it, x0, ym - row_h / 2, card_w, row_h,
                    kChromeGlassTint, mic_active ? ctx.col_red : kChromeCardBrdOn, scale_s(12));
    std::string full_lbl(prefix);
    full_lbl += val.empty() ? placeholder : val;
    const std::string fit_l = fit_text_to_width_font(full_lbl, card_w - scale_x(24), ctx.font_small);
    fast_print(it, x0 + scale_x(12), ym, ctx.font_small,
               val.empty() ? ctx.col_slate : ctx.col_white, TextAlign::CENTER_LEFT, fit_l.c_str());
    frame_hits().add_rect(x0, ym - row_h / 2, card_w, row_h, field_wid, kHitTap | kHitFocusSlot);

#if LATTER_HAS_MIC
    render_glass_circle(it, mic_cx, ym, mic_r,
                        mic_active ? ui::chrome_danger_bg() : kChromeGlassTint,
                        mic_active ? ctx.col_red : ctx.theme_primary);
    ui::draw_icon_mic(it, mic_cx, ym, std::max(7, (mic_r * 11) / 20),
                      mic_active ? ctx.col_red : ctx.col_white, true);
    frame_hits().add_circle(mic_cx, ym, mic_r, mic_wid, kHitTap | kHitFocusCircle, 0, 4);
#endif
  };

  draw_field_with_mic(0, "Title: ", rs.cal.draft_title, "Reminder",
                      WidgetId::CAL_ADD_TITLE_BTN, WidgetId::CAL_ADD_TITLE_MIC, 1U);
  draw_field_with_mic(1, "Location: ", rs.cal.draft_location, "None",
                      WidgetId::CAL_ADD_LOC_BTN, WidgetId::CAL_ADD_LOC_MIC, 2U);

  // Row 2: Target Calendar
  {
    const int ym = ly.slot5_y(2);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
    const int card_w = std::min(safe_w, ly.list_card_w);
    const int x0 = cx - card_w / 2;
    const int w_idx = (writable_cnt > 0) ? std::clamp<int>(rs.cal.draft_cal_idx, 0, writable_cnt - 1) : 0;
    const char *cal_name = (writable_cnt > 0) ? writable_sources[w_idx].label : "Default Calendar";
    char cal_buf[64];
    std::snprintf(cal_buf, sizeof(cal_buf), "Calendar: %s", cal_name);
    const Color brd = (writable_cnt > 1) ? kChromeCardBrdOn : kChromeCardBorder;
    render_card_box(it, x0, ym - row_h / 2, card_w, row_h, kChromeGlassTint, brd, scale_s(12));
    if (writable_cnt > 0) {
      ui::draw_aa_disc(it, static_cast<float>(x0 + scale_x(14)), static_cast<float>(ym), 4.0f,
                       cal_source_color(ctx, writable_sources[w_idx].color_idx));
    }
    const int tx_x = x0 + (writable_cnt > 0 ? scale_x(24) : scale_x(12));
    const std::string fit_c = fit_text_to_width_font(cal_buf, card_w - (tx_x - x0) - scale_x(12), ctx.font_small);
    fast_print(it, tx_x, ym, ctx.font_small,
               (writable_cnt > 0) ? ctx.col_white : ctx.col_slate, TextAlign::CENTER_LEFT, fit_c.c_str());
    if (writable_cnt > 1) {
      frame_hits().add_rect(x0, ym - row_h / 2, card_w, row_h,
                            WidgetId::CAL_ADD_CAL_BTN, kHitTap | kHitFocusSlot);
    }
  }

  // Row 3: Repeat
  {
    const int ym = ly.slot5_y(3);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
    const int card_w = std::min(safe_w, ly.list_card_w);
    const int x0 = cx - card_w / 2;
    const int r_idx = std::clamp<int>(rs.cal.draft_rrule_idx, 0, 6);
    char rep_buf[48];
    std::snprintf(rep_buf, sizeof(rep_buf), "Repeat: %s", kCalRepeatLabels[r_idx]);
    render_card_box(it, x0, ym - row_h / 2, card_w, row_h, kChromeGlassTint, kChromeCardBrdOn, scale_s(12));
    const std::string fit_r = fit_text_to_width_font(rep_buf, card_w - scale_x(24), ctx.font_small);
    fast_print(it, x0 + scale_x(12), ym, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT, fit_r.c_str());
    frame_hits().add_rect(x0, ym - row_h / 2, card_w, row_h,
                          WidgetId::CAL_ADD_REPEAT_BTN, kHitTap | kHitFocusSlot);
  }

  // Row 4: Next Step CTA
  {
    const int ym = ly.slot5_y(4);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);
    const int hw = std::min(scale_x(124), safe_w / 2);
    ui::pill_button(it, cx - hw, cx + hw, ym, row_h / 2,
                    ctx.theme_primary, kChromeGlassTint,
                    "Date & Time >", ctx.col_white, ctx.font_small,
                    WidgetId::CAL_ADD_NEXT_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Cancel");
}

void render_add_step2(esphome::display::Display &it,
                      const UiFrameContext &ctx,
                      DeviceRuntimeState &rs,
                      const PersistedDevicePrefs &prefs,
                      int32_t today_ymd) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &ly = layout();
  const int cx = ctx.cx;
  render_page_header(it, cx, "DATE & TIME", ctx.font_small, ctx.theme_primary);

  const int row_h = std::min(scale_y(44), ly.slot5_stride - 6);
  const int btn_r = row_h / 2;

  // Row 0: Date Stepper [ ‹ ] <Date> [ › ]
  {
    const int ym = ly.slot5_y(0);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
    const int row_w = std::min(safe_w, (SCREEN_W > 520) ? scale_x(340) : 316);
    const int lx = cx - row_w / 2 + btn_r;
    const int rx = cx + row_w / 2 - btn_r;

    render_glass_circle(it, lx, ym, btn_r, kChromeGlassTint, kChromeCardBrdOn);
    render_glass_circle(it, rx, ym, btn_r, kChromeGlassTint, kChromeCardBrdOn);
    const int ch = std::max(4, scale_s(5));
    fast_line(it, lx + 2, ym - ch, lx - 3, ym, ctx.col_white);
    fast_line(it, lx - 3, ym, lx + 2, ym + ch, ctx.col_white);
    fast_line(it, rx - 2, ym - ch, rx + 3, ym, ctx.col_white);
    fast_line(it, rx + 3, ym, rx - 2, ym + ch, ctx.col_white);
    frame_hits().add_circle(lx, ym, btn_r, WidgetId::CAL_ADD_DATE_PREV, kHitTap | kHitFocusCircle, -1, 4);
    frame_hits().add_circle(rx, ym, btn_r, WidgetId::CAL_ADD_DATE_NEXT, kHitTap | kHitFocusCircle, 1, 4);

    char dt_buf[36];
    format_cal_short_date(rs.cal.draft_ymd, today_ymd, dt_buf, sizeof(dt_buf));
    const std::string fit_d = fit_text_to_width_font(dt_buf, std::max(40, rx - lx - 2 * btn_r - 12), ctx.font_small);
    draw_shadow_text(it, cx, ym, ctx.font_small, ctx.col_white, TextAlign::CENTER, fit_d.c_str());
  }

  // Row 1: All-Day Toggle
  {
    const int ym = ly.slot5_y(1);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
    const int card_w = std::min(safe_w, ly.list_card_w);
    ui::toggle_card_row(it, cx, ym, card_w, row_h, "All-Day Event", rs.cal.draft_all_day,
                        ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate,
                        WidgetId::CAL_ADD_ALLDAY_BTN, 0);
  }

  if (!rs.cal.draft_all_day) {
    // Row 2: Start | End Time Focus Pills
    const int ym = ly.slot5_y(2);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
    const int tot_w = std::min(safe_w, (SCREEN_W > 520) ? scale_x(348) : 320);
    const int gap = scale_x(8);
    const int box_w = (tot_w - gap) / 2;
    const int s_x0 = cx - gap / 2 - box_w;
    const int e_x0 = cx + gap / 2;

    char s_tm[16], e_tm[16], s_lbl[28], e_lbl[28];
    format_cal_clock_min(rs.cal.draft_start_min, prefs.is_24h_format, s_tm, sizeof(s_tm), true);
    format_cal_clock_min(rs.cal.draft_end_min, prefs.is_24h_format, e_tm, sizeof(e_tm), true);
    std::snprintf(s_lbl, sizeof(s_lbl), "Start %s", s_tm);
    std::snprintf(e_lbl, sizeof(e_lbl), "End %s", e_tm);

    const bool focus_start = (rs.cal.draft_time_field == 0U);
    render_card_box(it, s_x0, ym - row_h / 2, box_w, row_h, kChromeGlassTint,
                    focus_start ? ctx.theme_primary : kChromeCardBorder, scale_s(12));
    render_card_box(it, e_x0, ym - row_h / 2, box_w, row_h, kChromeGlassTint,
                    !focus_start ? ctx.theme_primary : kChromeCardBorder, scale_s(12));

    const std::string fit_s = fit_text_to_width_font(s_lbl, box_w - 12, ctx.font_small);
    const std::string fit_e = fit_text_to_width_font(e_lbl, box_w - 12, ctx.font_small);
    fast_print(it, s_x0 + box_w / 2, ym, ctx.font_small,
               focus_start ? ctx.col_white : ctx.col_slate, TextAlign::CENTER, fit_s.c_str());
    fast_print(it, e_x0 + box_w / 2, ym, ctx.font_small,
               !focus_start ? ctx.col_white : ctx.col_slate, TextAlign::CENTER, fit_e.c_str());

    frame_hits().add_rect(s_x0, ym - row_h / 2, box_w, row_h,
                          WidgetId::CAL_ADD_START_BTN, kHitTap | kHitFocusSlot);
    frame_hits().add_rect(e_x0, ym - row_h / 2, box_w, row_h,
                          WidgetId::CAL_ADD_END_BTN, kHitTap | kHitFocusSlot);

    // Row 3: Canonical 4-column time stepper
    static constexpr const char *kTimeSteps[4] = {"-1h", "-15m", "+15m", "+1h"};
    const int step_h = std::min(scale_y(40), row_h);
    ui::stepper_row(it, cx, ly.slot5_y(3) - step_h / 2, step_h,
                    kTimeSteps, ctx.font_small, ctx.col_white, WidgetId::STEPPER_BASE);
  }

  // Row 4: Create Event CTA
  {
    const int ym = ly.slot5_y(4);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);
    const int hw = std::min(scale_x(122), safe_w / 2);
    ui::pill_button(it, cx - hw, cx + hw, ym, row_h / 2,
                    ctx.theme_primary, kChromeGlassTint,
                    "Create Event", ctx.col_white, ctx.font_small,
                    WidgetId::CAL_ADD_SAVE_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}

void render_picker_drilldown(esphome::display::Display &it,
                             const UiFrameContext &ctx,
                             DeviceRuntimeState &rs,
                             const CalendarSourceMeta *writable_sources,
                             int writable_cnt) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const auto &ly = layout();
  const int cx = ctx.cx;

  const char *hdr = "SELECT";
  std::string options[16]{};
  int opt_cnt = 0;
  int active_idx = -1;

  if (rs.cal.drilldown == CalDrilldown::PICK_TITLE) {
    hdr = "EVENT TITLE";
    opt_cnt = 10;
    for (int i = 0; i < 10; ++i) {
      options[i] = kCalTitlePresets[i];
      if (rs.cal.draft_title == kCalTitlePresets[i]) active_idx = i;
    }
  } else if (rs.cal.drilldown == CalDrilldown::PICK_LOCATION) {
    hdr = "LOCATION";
    options[opt_cnt++] = "None (No Location)";
    if (rs.cal.draft_location.empty()) active_idx = 0;
    std::string_view locs[kMaxCalendarLocations]{};
    const int n_locs = parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
    for (int i = 0; i < n_locs && opt_cnt < 16; ++i) {
      options[opt_cnt] = std::string(locs[i]);
      if (rs.cal.draft_location == options[opt_cnt]) active_idx = opt_cnt;
      opt_cnt++;
    }
  } else if (rs.cal.drilldown == CalDrilldown::PICK_CALENDAR) {
    hdr = "CALENDAR";
    opt_cnt = std::min(writable_cnt, 16);
    active_idx = std::clamp<int>(rs.cal.draft_cal_idx, 0, std::max(0, opt_cnt - 1));
    for (int i = 0; i < opt_cnt; ++i) {
      options[i] = writable_sources[i].label;
    }
  } else if (rs.cal.drilldown == CalDrilldown::PICK_REPEAT) {
    hdr = "REPEAT";
    opt_cnt = 7;
    active_idx = std::clamp<int>(rs.cal.draft_rrule_idx, 0, 6);
    for (int i = 0; i < 7; ++i) {
      options[i] = kCalRepeatLabels[i];
    }
  }

  render_page_header(it, cx, hdr, ctx.font_small, ctx.theme_primary);

  if (opt_cnt <= 0) {
    render_empty_state(it, cx, ctx.cy, "No options available", ctx.font_small, ctx.col_slate);
    render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
    return;
  }

  const int total_pages = std::max(1, (opt_cnt + 4) / 5);
  rs.cal.picker_page = std::clamp(rs.cal.picker_page, 0, total_pages - 1);
  const int base_i = rs.cal.picker_page * 5;
  const int vis_cnt = std::min(5, opt_cnt - base_i);
  const int row_h = std::min(scale_y(44), ly.slot5_stride - 6);
  const int bounce_y = edge_bounce().offset_px(
      PageId::CALENDAR, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);

  for (int r = 0; r < vis_cnt; ++r) {
    const int idx = base_i + r;
    const int ym = ly.slot5_y(r) + bounce_y;
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0,
        (total_pages > 1) ? 36 : 16);
    const int card_w = std::min(safe_w, ly.list_card_w);
    const int x0 = cx - card_w / 2;
    const bool sel = (idx == active_idx);
    render_card_box(it, x0, ym - row_h / 2, card_w, row_h,
                    kChromeGlassTint, sel ? ctx.theme_primary : kChromeCardBrdOn, scale_s(12));
    const std::string fit_o = fit_text_to_width_font(options[idx], card_w - scale_x(24), ctx.font_small);
    fast_print(it, x0 + scale_x(12), ym, ctx.font_small,
               sel ? ctx.theme_primary : ctx.col_white, TextAlign::CENTER_LEFT, fit_o.c_str());
    frame_hits().add_rect(x0, ym - row_h / 2, card_w, row_h,
                          static_cast<uint16_t>(WidgetId::CAL_PICKER_SLOT_BASE + r),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, rs.cal.picker_page > 0, (rs.cal.picker_page + 1) < total_pages,
                              ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}

}  // namespace

void render_calendar_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &store = calendar_window_store();

  const int32_t today_ymd = resolve_today_ymd(ctx);
  if (rs.cal.selected_ymd <= 0) {
    rs.cal.selected_ymd = today_ymd;
  }
  if (rs.cal.draft_ymd <= 0) {
    rs.cal.draft_ymd = rs.cal.selected_ymd;
  }
  const int now_min = ctx.now.is_valid()
      ? (static_cast<int>(ctx.now.hour) * 60 + static_cast<int>(ctx.now.minute))
      : -1;

  CalendarSourceMeta sources[kMaxCalendarSources]{};
  const int source_cnt = parse_calendar_sources_meta(calendar_state().calendars_meta, sources);
  CalendarSourceMeta writable_sources[kMaxCalendarSources]{};
  const int writable_cnt = collect_writable_calendar_sources(
      calendar_state().calendars_meta, writable_sources, nullptr);

  if (rs.cal.drilldown == CalDrilldown::MAP) {
    render_map_drilldown(it, ctx, rs);
    return;
  }
  if (rs.cal.drilldown == CalDrilldown::DETAIL) {
    render_detail_drilldown(it, ctx, rs, prefs, today_ymd, store, sources, source_cnt);
    return;
  }
  if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
    render_add_step1(it, ctx, rs, writable_sources, writable_cnt);
    return;
  }
  if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
    render_add_step2(it, ctx, rs, prefs, today_ymd);
    return;
  }
  if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE && rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) {
    render_picker_drilldown(it, ctx, rs, writable_sources, writable_cnt);
    return;
  }

  const int sub = std::clamp(rs.cal.subpage, CalendarSubpage::OVERVIEW, CalendarSubpage::MONTH);
  rs.cal.subpage = sub;

  if (sub == CalendarSubpage::DAY) {
    render_day_tab(it, ctx, rs, prefs, today_ymd, now_min, store, sources, source_cnt);
  } else if (sub == CalendarSubpage::WEEK) {
    render_week_tab(it, ctx, rs, prefs, today_ymd, now_min, store, sources, source_cnt);
  } else if (sub == CalendarSubpage::MONTH) {
    render_month_tab(it, ctx, rs, prefs, today_ymd, store, sources, source_cnt);
  } else {
    render_overview_tab(it, ctx, rs, prefs, today_ymd, now_min, store, sources, source_cnt);
  }

  render_calendar_mode_bar(it, ctx, sub);
}

}  // namespace latter_ui
