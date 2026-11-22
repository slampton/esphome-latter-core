#include "calendar_internal.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace latter_ui {

namespace {

static constexpr const char *kFullMonths[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};
static constexpr const char *kUpperWeekdaysSun0[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static constexpr const char *kSingleWeekdaysSun0[7] = {"S", "M", "T", "W", "T", "F", "S"};

int32_t resolve_today_ymd(const UiFrameContext &ctx) {
  return (ctx.now.is_valid() && ctx.now.year >= 2020)
      ? make_ymd(ctx.now.year, ctx.now.month, ctx.now.day_of_month) : 20260101;
}

void format_cal_badge_date(int32_t ymd, int32_t today_ymd, char *out, size_t cap) {
  if (ymd == today_ymd) { std::snprintf(out, cap, "TODAY"); return; }
  if (ymd == add_days_ymd(today_ymd, 1)) { std::snprintf(out, cap, "TMRW"); return; }
  int y = 2026, m = 1, d = 1;
  (void)split_ymd(ymd, y, m, d);
  const int diff = days_between_ymd(today_ymd, ymd);
  if (diff >= 0 && diff < 7) {
    std::snprintf(out, cap, "%s %d", kUpperWeekdaysSun0[weekday_sun0_from_ymd(ymd)], d);
  } else {
    std::snprintf(out, cap, "%s %d", kCalShortMonths[std::clamp(m - 1, 0, 11)], d);
  }
}

bool is_event_live_now(const CalendarWindowEvent &ev, int32_t today_ymd, int now_min) {
  if (!calendar_event_overlaps_ymd(ev, today_ymd) || now_min < 0 || ev.all_day != 0U || ev.start_min < 0) return false;
  const int eff_end = (ev.end_min > ev.start_min) ? ev.end_min : (ev.start_min + 60);
  return now_min >= ev.start_min && now_min < eff_end;
}

void draw_calendar_mode_icon(Rgb565Surface &it, int global_idx, int bx, int by, int ir, UiColor fg_c, bool shadow, void *) {
  if (global_idx == CalendarSubpage::OVERVIEW) ui::draw_icon_cal_agenda(it, bx, by, ir, fg_c, shadow);
  else if (global_idx == CalendarSubpage::DAY) ui::draw_icon_cal_day(it, bx, by, ir, fg_c, shadow);
  else if (global_idx == CalendarSubpage::WEEK) ui::draw_icon_cal_week(it, bx, by, ir, fg_c, shadow);
  else render_mdi_icon(it, "mdi:calendar-month", bx, by, std::max(16, 2 * ir), fg_c, shadow, "mdi:calendar");
}

void draw_cal_plus(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow, void *) {
  ui::draw_icon_plus(it, cx, cy, r, col, shadow);
}

void render_calendar_mode_bar(Rgb565Surface &it, const UiFrameContext &ctx, int active_sub) {
  ui::mode_bar(it, ctx.cx, 4, active_sub, 0, ctx.theme_primary, ctx.col_white, draw_calendar_mode_icon, nullptr);
}

struct CalSubheaderGeom { int sub_y{0}; int content_top{0}; int content_bot{0}; };

CalSubheaderGeom render_calendar_subheader(Rgb565Surface &it, const UiFrameContext &ctx,
                                           const char *page_title, const char *period_label,
                                           bool period_is_away_from_today) {
  const auto &rs = runtime_state();
  const auto &ly = layout();
  const int cx = ctx.cx;

  const bool show_fb = !rs.cal.feedback_text.empty() &&
                       (ctx.now_ms >= rs.cal.feedback_ms) &&
                       ((ctx.now_ms - rs.cal.feedback_ms) < 2400U);
  if (show_fb) {
    const std::string fit_fb = fit_text_to_width_font(rs.cal.feedback_text, safe_span(ly.hdr_y, 24), ctx.font_small);
    render_page_header(it, cx, fit_fb, ctx.font_small, ctx.col_white);
  } else {
    render_page_header(it, cx, page_title, ctx.font_small, ctx.col_white);
  }

  const int sub_y = ly.sub_hdr_y + (LATTER_ROUND_DISPLAY ? scale_y(8) : scale_y(10));
  const int btn_r = (SCREEN_W < 360) ? 15 : scale_s(17);
  const int btn_d = 2 * btn_r + 1;
  const int safe_w = band_safe_span_for(
      sub_y, btn_d, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14);
  const int bar_w = std::min(safe_w, (SCREEN_W > 520) ? ly.area_card3_w : 348);
  const int bar_x0 = cx - bar_w / 2;
  const int bar_x1 = bar_x0 + bar_w;

  const int prev_cx = bar_x0 + btn_r;
  const int add_cx  = bar_x1 - btn_r;
  const int next_cx = add_cx - (2 * btn_r + std::max(6, scale_x(8)));
  const int hit_pad = std::max(4, (mm_to_px(7.0f) - btn_d) / 2);

  ui::icon_circle_button(it, prev_cx, sub_y, btn_r, kChromeGlassTint, kChromeCardBrdOn,
                         ctx.col_white, draw_cal_chevron_left, nullptr,
                         WidgetId::CAL_NAV_PREV, kHitTap | kHitFocusSlot | kHitFocusCircle, -1, hit_pad);
  ui::icon_circle_button(it, next_cx, sub_y, btn_r, kChromeGlassTint, kChromeCardBrdOn,
                         ctx.col_white, draw_cal_chevron_right, nullptr,
                         WidgetId::CAL_NAV_NEXT, kHitTap | kHitFocusSlot | kHitFocusCircle, 1, hit_pad);
  ui::icon_circle_button(it, add_cx, sub_y, btn_r, kChromeGlassTint, ctx.theme_primary,
                         ctx.col_white, draw_cal_plus, nullptr,
                         WidgetId::CAL_ADD_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, hit_pad);

  const int mid_x0 = prev_cx + btn_r + std::max(6, scale_x(8));
  const int mid_x1 = next_cx - btn_r - std::max(6, scale_x(8));
  const int mid_cx = (mid_x0 + mid_x1) / 2;
  const int mid_w = std::max(40, mid_x1 - mid_x0);

  if (period_is_away_from_today) {
    const int pill_h = std::max(28, btn_d - 2);
    ui::action_pill_centered(it, mid_cx, mid_w, sub_y, pill_h,
                             ctx.theme_primary, kChromeGlassTint,
                             period_label, ctx.col_white, ctx.font_small,
                             WidgetId::CAL_TODAY_BTN, kHitTap | kHitFocusSlot, 0);
  } else {
    const UiFontView mid_font = (SCREEN_W < 360 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
    const std::string fit_p = fit_text_to_width_font(period_label, mid_w - 4, mid_font);
    ui::shadow_print(it, mid_cx, sub_y, mid_font, ctx.col_white, TextAlign::CENTER, fit_p.c_str());
  }

  CalSubheaderGeom g{};
  g.sub_y = sub_y;
  g.content_top = sub_y + btn_r + std::max(8, scale_y(10));
  g.content_bot = ly.mode_bar_zone_top() - std::max(6, scale_y(8));
  return g;
}

void render_event_list_cards(Rgb565Surface &it, const UiFrameContext &ctx,
                             const CalSubheaderGeom &geom, const int *ev_indices,
                             int total_events, int &io_page, int32_t today_ymd,
                             int now_min, bool is_24h, bool show_date_badge,
                             const CalendarWindowEvent *events, int event_cap,
                             bool syncing_empty, const CalendarSourceMeta *sources,
                             int source_cnt, const char *empty_msg) {
  const int avail_h = std::max(80, geom.content_bot - geom.content_top);
  const int cy_mid = (geom.content_top + geom.content_bot) / 2;
  if (total_events <= 0 || events == nullptr || event_cap <= 0) {
    io_page = 0;
    render_empty_state(it, ctx.cx, cy_mid,
                       syncing_empty ? "Syncing calendar..." : empty_msg,
                       ctx.font_small, syncing_empty ? ctx.theme_secondary : ctx.col_slate);
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

  const UiFontView sub_font = (ctx.font_micro != nullptr && card_h < scale_y(54)) ? ctx.font_micro : ctx.font_small;
  const int pip_reserve = (total_pages > 1) ? scale_x(30) : 0;
  const int first_cy = start_y0 + card_h / 2;
  const int last_cy = start_y0 + (per_page - 1) * stride + card_h / 2;
  const int stack_safe_w = std::min(
      band_safe_span_for(first_cy, card_h, SCREEN_W, SCREEN_H,
                         LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14 + pip_reserve),
      band_safe_span_for(last_cy, card_h, SCREEN_W, SCREEN_H,
                         LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 14 + pip_reserve));
  const int card_w = std::min(stack_safe_w, (SCREEN_W > 520) ? layout().area_card3_w : 344);
  const int card_x = ctx.cx - (card_w / 2) - (pip_reserve / 3);

  for (int r = 0; r < vis_cnt; ++r) {
    const int ev_idx = ev_indices[io_page * per_page + r];
    if (ev_idx < 0 || ev_idx >= event_cap) continue;
    const auto &ev = events[ev_idx];

    const int cy_row = start_y0 + r * stride + card_h / 2 + bounce_y;
    const int card_y = cy_row - card_h / 2;

    const bool live_now = is_event_live_now(ev, today_ymd, now_min);
    const UiColor accent = event_accent_color(ctx, ev.cal_idx, sources, source_cnt);
    const UiColor brd_c = live_now ? ctx.theme_primary : kChromeCardBrdOn;

    render_card_box(it, card_x, card_y, card_w, card_h, kChromeGlassTint, brd_c, kRadiusCardPx());

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
    ui::shadow_print(it, text_x, title_y, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT, fit_title.c_str());

    if (badge_buf[0] != '\0') {
      const UiColor b_col = (live_now || ev.start_ymd == today_ymd) ? ctx.theme_primary : ctx.theme_secondary;
      ui::shadow_print(it, right_x, title_y, sub_font, b_col, TextAlign::CENTER_RIGHT, badge_buf);
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
    ui::shadow_print(it, text_x, meta_y, sub_font, ctx.col_slate, TextAlign::CENTER_LEFT, fit_meta.c_str());

    frame_hits().add_rect(card_x, card_y, card_w, card_h,
                          static_cast<uint16_t>(WidgetId::CAL_EVENT_BASE + r),
                          kHitTap | kHitFocusSlot, static_cast<int16_t>(ev_idx));
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, io_page > 0, (io_page + 1) < total_pages,
                              ctx.col_bg, ctx.theme_secondary, ctx.col_slate,
                              layout().mode_bar_zone_top());
  }
}

void render_overview_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                         DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                         int32_t today_ymd, int now_min, const CalendarWindowStore &store,
                         const CalendarSourceMeta *sources, int source_cnt) {
  const int lookahead = static_cast<int>(snap_calendar_lookahead_days(prefs.calendar_lookahead_days));
  char period_buf[32];
  std::snprintf(period_buf, sizeof(period_buf), "Next %d Days", lookahead);
  const auto geom = render_calendar_subheader(it, ctx, "UPCOMING", period_buf, false);

  const bool syncing_empty = store.loading && store.window_start_ymd == 0;

  if (store.count == 0 && store.window_start_ymd == 0 && !calendar_state().summary.empty()) {
    CalendarWindowEvent *preview_evs = calendar_summary_preview_scratch();
    const int preview_cnt = populate_calendar_summary_preview(
        calendar_state().summary, today_ymd, preview_evs, kMaxCalendarSummaryPreviewEvents);
    if (preview_cnt > 0) {
      static constexpr int kPreviewIdxs[kMaxCalendarSummaryPreviewEvents] = {0, 1, 2, 3, 4};
      render_event_list_cards(it, ctx, geom, kPreviewIdxs, preview_cnt, rs.cal.overview_page,
                              today_ymd, now_min, prefs.is_24h_format, true,
                              preview_evs, preview_cnt, false,
                              sources, source_cnt, "No upcoming events — tap +");
      return;
    }
  }

  int idxs[kMaxCalendarWindowEvents]{};
  const int cnt = collect_calendar_overview_events(
      today_ymd, now_min, lookahead, idxs, kMaxCalendarWindowEvents, store);
  render_event_list_cards(it, ctx, geom, idxs, cnt, rs.cal.overview_page,
                          today_ymd, now_min, prefs.is_24h_format, true,
                          store.events, store.count, syncing_empty,
                          sources, source_cnt, "No upcoming events — tap +");
}

void render_day_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                    DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                    int32_t today_ymd, int now_min, const CalendarWindowStore &store,
                    const CalendarSourceMeta *sources, int source_cnt) {
  char period_buf[36];
  format_cal_short_date(rs.cal.selected_ymd, today_ymd, period_buf, sizeof(period_buf));
  const auto geom = render_calendar_subheader(it, ctx, "DAY VIEW", period_buf, rs.cal.selected_ymd != today_ymd);

  int idxs[kMaxCalendarWindowEvents]{};
  const int cnt = collect_calendar_day_events(rs.cal.selected_ymd, idxs, kMaxCalendarWindowEvents, store);
  render_event_list_cards(it, ctx, geom, idxs, cnt, rs.cal.day_page,
                          today_ymd, now_min, prefs.is_24h_format, false,
                          store.events, store.count, store.loading && store.window_start_ymd == 0,
                          sources, source_cnt, "No events on this day — tap +");
}

void render_week_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                     DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                     int32_t today_ymd, int now_min, const CalendarWindowStore &store,
                     const CalendarSourceMeta *sources, int source_cnt) {
  const bool monday_first = (prefs.calendar_week_start == 1U);
  const int32_t week_start = start_of_week_ymd(rs.cal.selected_ymd, monday_first);
  const int32_t week_end   = add_days_ymd(week_start, 6);
  const int32_t today_week = start_of_week_ymd(today_ymd, monday_first);

  int sy = 2026, sm = 1, sd = 1, ey = 2026, em = 1, ed = 1;
  (void)split_ymd(week_start, sy, sm, sd);
  (void)split_ymd(week_end, ey, em, ed);
  char period_buf[36];
  if (sm == em) {
    std::snprintf(period_buf, sizeof(period_buf), "%s %d – %d",
                  kCalShortMonths[std::clamp(sm - 1, 0, 11)], sd, ed);
  } else {
    std::snprintf(period_buf, sizeof(period_buf), "%s %d – %s %d",
                  kCalShortMonths[std::clamp(sm - 1, 0, 11)], sd,
                  kCalShortMonths[std::clamp(em - 1, 0, 11)], ed);
  }

  const auto geom = render_calendar_subheader(it, ctx, "WEEK VIEW", period_buf, week_start != today_week);

  if constexpr (SCREEN_W > 520) {
    (void)now_min;
    const int board_h = std::max(180, geom.content_bot - geom.content_top);
    const int safe_w = band_safe_span_for(
        (geom.content_top + geom.content_bot) / 2, board_h, SCREEN_W, SCREEN_H,
        LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);
    const int gap = scale_x(6);
    const int col_w = std::max(64, (safe_w - 6 * gap) / 7);
    const int total_w = 7 * col_w + 6 * gap;
    const int x0_board = ctx.cx - total_w / 2;
    const UiFontView chip_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

    for (int d = 0; d < 7; ++d) {
      const int32_t d_ymd = add_days_ymd(week_start, d);
      int yy = 2026, mm = 1, dd = 1;
      (void)split_ymd(d_ymd, yy, mm, dd);
      const int wd = weekday_sun0_from_ymd(d_ymd);
      const bool is_today = (d_ymd == today_ymd);
      const bool is_sel = (d_ymd == rs.cal.selected_ymd);

      const int col_x = x0_board + d * (col_w + gap);
      const int col_cx = col_x + col_w / 2;
      const UiColor brd_c = is_today ? ctx.theme_primary : (is_sel ? ctx.theme_secondary : kChromeCardBorder);

      render_card_box(it, col_x, geom.content_top, col_w, board_h, kChromeGlassTint, brd_c, kRadiusCardPx());

      const int wd_y = geom.content_top + scale_y(16);
      const int num_y = geom.content_top + scale_y(40);
      ui::shadow_print(it, col_cx, wd_y, chip_font,
                       is_today ? ctx.theme_primary : ctx.col_slate,
                       TextAlign::CENTER, kUpperWeekdaysSun0[wd]);

      if (is_today) {
        ui::draw_aa_disc(it, static_cast<float>(col_cx), static_cast<float>(num_y),
                         static_cast<float>(scale_s(13)), ctx.theme_primary);
      }
      char num_buf[8];
      std::snprintf(num_buf, sizeof(num_buf), "%d", dd);
      if (is_today) {
        fast_print(it, col_cx, num_y, ctx.font_small, ctx.col_black, TextAlign::CENTER, num_buf);
      } else {
        ui::shadow_print(it, col_cx, num_y, ctx.font_small, ctx.col_white, TextAlign::CENTER, num_buf);
      }

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
        const UiColor acc = event_accent_color(ctx, ev.cal_idx, sources, source_cnt);
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
        ui::shadow_print(it, col_x + 12, cy_top + chip_h / 3, chip_font, acc, TextAlign::CENTER_LEFT, fit_t.c_str());
        ui::shadow_print(it, col_x + 12, cy_top + (chip_h * 3) / 4 - 2, chip_font, ctx.col_white,
                         TextAlign::CENTER_LEFT, fit_n.c_str());
      }
      if (day_cnt > max_chips) {
        char more_buf[24];
        std::snprintf(more_buf, sizeof(more_buf), "+%d more", day_cnt - max_chips);
        ui::shadow_print(it, col_cx, geom.content_bot - scale_y(12), chip_font, ctx.theme_secondary,
                         TextAlign::CENTER, more_buf);
      }

      frame_hits().add_rect(col_x, geom.content_top, col_w, board_h,
                            static_cast<uint16_t>(WidgetId::CAL_WEEK_DAY_BASE + d),
                            kHitTap | kHitFocusSlot, static_cast<int16_t>(d));
    }
  } else {
    const int strip_h = std::max(mm_to_px(7.0f), scale_y(50));
    const int strip_cy = geom.content_top + strip_h / 2;
    const int strip_safe = band_safe_span_for(
        strip_cy, strip_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 12);
    const int strip_w = std::min(strip_safe, 356);
    const int gap = (SCREEN_W < 360) ? 3 : 4;
    const int cap_w = std::max(34, (strip_w - 6 * gap) / 7);
    const int tot_strip_w = 7 * cap_w + 6 * gap;
    const int x0_strip = ctx.cx - tot_strip_w / 2;
    const UiFontView lbl_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

    for (int d = 0; d < 7; ++d) {
      const int32_t d_ymd = add_days_ymd(week_start, d);
      int yy = 2026, mm = 1, dd = 1;
      (void)split_ymd(d_ymd, yy, mm, dd);
      const int wd = weekday_sun0_from_ymd(d_ymd);
      const bool is_today = (d_ymd == today_ymd);
      const bool is_sel = (d_ymd == rs.cal.selected_ymd);

      const int cx_d = x0_strip + d * (cap_w + gap) + cap_w / 2;
      const UiColor brd_c = is_today ? ctx.theme_primary : (is_sel ? ctx.theme_secondary : kChromeCardBorder);
      render_card_box(it, cx_d - cap_w / 2, strip_cy - strip_h / 2, cap_w, strip_h,
                      kChromeGlassTint, brd_c, kRadiusKeyPx());

      ui::shadow_print(it, cx_d, strip_cy - strip_h / 4 - 1, lbl_font,
                       is_today ? ctx.theme_primary : ctx.col_slate,
                       TextAlign::CENTER, kSingleWeekdaysSun0[wd]);

      char dd_buf[8];
      std::snprintf(dd_buf, sizeof(dd_buf), "%d", dd);
      ui::shadow_print(it, cx_d, strip_cy + 3, ctx.font_small,
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
                            store.events, store.count, store.loading && store.window_start_ymd == 0,
                            sources, source_cnt, "No events this week — tap +");
  }
}

void render_month_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                      DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                      int32_t today_ymd, const CalendarWindowStore &store,
                      const CalendarSourceMeta *sources, int source_cnt) {
  int sel_y = 2026, sel_m = 1, sel_d = 1, tod_y = 2026, tod_m = 1, tod_d = 1;
  (void)split_ymd(rs.cal.selected_ymd, sel_y, sel_m, sel_d);
  (void)split_ymd(today_ymd, tod_y, tod_m, tod_d);

  char period_buf[36];
  std::snprintf(period_buf, sizeof(period_buf), "%s %d",
                kFullMonths[std::clamp(sel_m - 1, 0, 11)], sel_y);
  const auto geom = render_calendar_subheader(it, ctx, "MONTH VIEW", period_buf, (sel_y != tod_y) || (sel_m != tod_m));

  const bool monday_first = (prefs.calendar_week_start == 1U);
  const int32_t grid_start = start_of_week_ymd(make_ymd(sel_y, sel_m, 1), monday_first);

  const UiFontView hdr_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
  const UiFontView cell_font = (SCREEN_W <= 520 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

  const int wd_y = geom.content_top + std::max(8, scale_y(9));
  const int grid_top = wd_y + std::max(12, scale_y(14));
  const int grid_h = std::max(120, geom.content_bot - grid_top);
  const int row_h = grid_h / 6;

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
    ui::shadow_print(it, cx_c, wd_y, hdr_font, ctx.theme_secondary, TextAlign::CENTER, lbl);
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
      const UiColor fg = is_today ? ctx.theme_primary : (in_month ? ctx.col_white : kChromeSubtle);
      ui::shadow_print(it, cell_cx, num_cy, cell_font, fg, TextAlign::CENTER, d_buf);

      uint8_t dot_cals[3]{};
      const int n_dots = collect_calendar_day_dots(cell_ymd, dot_cals, store);
      if (n_dots > 0) {
        const int dot_y = row_y0 + row_h - (SCREEN_W > 520 ? 7 : 3);
        const int dx0 = cell_cx - ((n_dots - 1) * dot_sp) / 2;
        for (int di = 0; di < n_dots; ++di) {
          const UiColor dc = event_accent_color(ctx, dot_cals[di], sources, source_cnt);
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

}  // namespace

void render_calendar_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &store = calendar_window_store();

  const bool time_ok = ctx.now.is_valid() && ctx.now.year >= 2020;
  const int32_t today_ymd = resolve_today_ymd(ctx);
  static bool s_anchored_valid_time = false;
  if (!time_ok) s_anchored_valid_time = false;
  if (rs.cal.selected_ymd <= 0 ||
      (time_ok && !s_anchored_valid_time && rs.cal.selected_ymd == 20260101)) {
    rs.cal.selected_ymd = today_ymd;
  }
  if (rs.cal.draft_ymd <= 0 ||
      (time_ok && !s_anchored_valid_time && rs.cal.draft_ymd == 20260101)) {
    rs.cal.draft_ymd = today_ymd;
  }
  if (time_ok) s_anchored_valid_time = true;

  const int now_min = ctx.now.is_valid()
      ? (static_cast<int>(ctx.now.hour) * 60 + static_cast<int>(ctx.now.minute))
      : -1;

  static std::vector<CalendarSourceMeta> s_sources(static_cast<size_t>(kMaxCalendarSources));
  static std::vector<CalendarSourceMeta> s_writable(static_cast<size_t>(kMaxCalendarSources));
  CalendarSourceMeta *sources = s_sources.data();
  const int source_cnt = parse_calendar_sources_meta(calendar_state().calendars_meta, sources);
  CalendarSourceMeta *writable_sources = s_writable.data();
  const int writable_cnt = collect_writable_calendar_sources(
      calendar_state().calendars_meta, writable_sources, nullptr);

  if (rs.cal.drilldown == CalDrilldown::DETAIL) {
    render_calendar_detail_drilldown(it, ctx, rs, prefs, today_ymd, store, sources, source_cnt);
    return;
  }
  if (rs.cal.drilldown == CalDrilldown::ADD_STEP1) {
    render_calendar_add_step1(it, ctx, rs, writable_sources, writable_cnt);
    return;
  }
  if (rs.cal.drilldown == CalDrilldown::ADD_STEP2) {
    render_calendar_add_step2(it, ctx, rs, prefs, today_ymd);
    return;
  }
  if (rs.cal.drilldown >= CalDrilldown::PICK_TITLE && rs.cal.drilldown <= CalDrilldown::PICK_REPEAT) {
    render_calendar_picker_drilldown(it, ctx, rs, writable_sources, writable_cnt);
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
