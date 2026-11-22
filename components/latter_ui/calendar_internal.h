#pragma once

#include "ui_pages.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace latter_ui {

inline constexpr const char *kCalShortMonths[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

inline constexpr const char *kCalShortWeekdaysSun0[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

inline UiColor cal_source_color(const UiFrameContext &ctx, uint8_t color_idx) {
  switch (color_idx % 6U) {
    case 0: return ctx.theme_primary;
    case 1: return ctx.col_green;
    case 2: return ctx.col_gold;
    case 3: return ctx.theme_secondary;
    case 4: return mix_color_q8(ctx.theme_primary, ctx.col_red, 148);
    default: return ctx.col_solaris_gold;
  }
}

inline UiColor event_accent_color(const UiFrameContext &ctx, uint8_t cal_idx,
                                  const CalendarSourceMeta *sources, int source_cnt) {
  if (sources != nullptr && static_cast<int>(cal_idx) < source_cnt) {
    return cal_source_color(ctx, sources[cal_idx].color_idx);
  }
  return cal_source_color(ctx, cal_idx);
}

inline void format_cal_clock_min(int mins, bool is_24h, char *out, size_t cap, bool include_ampm = true) {
  const int clamped = std::clamp(mins, 0, 1440);
  const int h24 = (clamped == 1440) ? 0 : (clamped / 60);
  const int mm = clamped % 60;
  if (is_24h) {
    std::snprintf(out, cap, (clamped == 1440) ? "24:00" : "%02d:%02d", h24, mm);
    return;
  }
  const int h12 = (h24 % 12 == 0) ? 12 : (h24 % 12);
  const char *ap = (h24 >= 12 && clamped < 1440) ? "PM" : "AM";
  if (include_ampm) {
    std::snprintf(out, cap, "%d:%02d %s", h12, mm, ap);
  } else {
    std::snprintf(out, cap, "%d:%02d", h12, mm);
  }
}

inline void format_cal_event_time_range(const CalendarWindowEvent &ev, bool is_24h, char *out, size_t cap) {
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

inline void format_cal_short_date(int32_t ymd, int32_t today_ymd, char *out, size_t cap) {
  int y = 2026, m = 1, d = 1;
  (void)split_ymd(ymd, y, m, d);
  const char *mon = kCalShortMonths[std::clamp(m - 1, 0, 11)];
  if (ymd == today_ymd) {
    std::snprintf(out, cap, "Today, %s %d", mon, d);
  } else if (ymd == add_days_ymd(today_ymd, 1)) {
    std::snprintf(out, cap, "Tomorrow, %s %d", mon, d);
  } else {
    std::snprintf(out, cap, "%s, %s %d", kCalShortWeekdaysSun0[weekday_sun0_from_ymd(ymd)], mon, d);
  }
}

inline void draw_cal_chevron_left(Rgb565Surface &it, int cx, int cy, int r,
                                  UiColor col, bool shadow, void *) {
  ui::draw_icon_chevron(it, cx, cy, r, false, col, shadow);
}

inline void draw_cal_chevron_right(Rgb565Surface &it, int cx, int cy, int r,
                                   UiColor col, bool shadow, void *) {
  ui::draw_icon_chevron(it, cx, cy, r, true, col, shadow);
}

void render_calendar_detail_drilldown(Rgb565Surface &it, const UiFrameContext &ctx,
                                      DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                                      int32_t today_ymd, const CalendarWindowStore &store,
                                      const CalendarSourceMeta *sources, int source_cnt);

void render_calendar_add_step1(Rgb565Surface &it, const UiFrameContext &ctx,
                               DeviceRuntimeState &rs,
                               const CalendarSourceMeta *writable_sources, int writable_cnt);

void render_calendar_add_step2(Rgb565Surface &it, const UiFrameContext &ctx,
                               DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                               int32_t today_ymd);

void render_calendar_picker_drilldown(Rgb565Surface &it, const UiFrameContext &ctx,
                                      DeviceRuntimeState &rs,
                                      const CalendarSourceMeta *writable_sources, int writable_cnt);

}  // namespace latter_ui
