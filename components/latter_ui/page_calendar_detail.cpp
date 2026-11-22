#include "calendar_internal.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace latter_ui {

void render_calendar_detail_drilldown(Rgb565Surface &it, const UiFrameContext &ctx,
                                      DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                                      int32_t today_ymd, const CalendarWindowStore &store,
                                      const CalendarSourceMeta *sources, int source_cnt) {
  const auto &ly = layout();
  const int cx = ctx.cx;
  if (rs.cal.selected_event_idx < 0 || rs.cal.selected_event_idx >= store.count) {
    render_page_header(it, cx, "EVENT DETAILS", ctx.font_small, ctx.col_white);
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
                      ctx.col_white, ctx.col_white, ctx.col_slate,
                      WidgetId::CONFIRM_CANCEL, WidgetId::CONFIRM_ACCEPT);
    return;
  }

  render_page_header(it, cx, "EVENT DETAILS", ctx.font_small, ctx.col_white);

  const UiColor accent = event_accent_color(ctx, ev.cal_idx, sources, source_cnt);
  const int meta_y = scale_y(84);
  char dt_buf[36];
  char tm_buf[36];
  format_cal_short_date(ev.start_ymd, today_ymd, dt_buf, sizeof(dt_buf));
  format_cal_event_time_range(ev, prefs.is_24h_format, tm_buf, sizeof(tm_buf));
  char when_buf[80];
  std::snprintf(when_buf, sizeof(when_buf), "%s  •  %s", dt_buf, tm_buf);
  const std::string fit_when = fit_text_to_width_font(
      when_buf, std::min(kMaxContentColW, safe_span(meta_y, 20)), ctx.font_small);
  ui::shadow_print(it, cx, meta_y, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, fit_when.c_str());

  int cur_y = scale_y(114);
  if (static_cast<int>(ev.cal_idx) < source_cnt && sources[ev.cal_idx].label[0] != '\0') {
    const UiFontView src_font = (ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
    const std::string src_fit = fit_text_to_width_font(
        sources[ev.cal_idx].label, std::min(kMaxContentColW, safe_span(cur_y, 36)), src_font);
    int sw = 0, xo = 0, bl = 0, th = 0;
    src_font->measure(src_fit.c_str(), &sw, &xo, &bl, &th);
    const int tot_w = sw + 14;
    const int dot_x = cx - tot_w / 2 + 4;
    ui::draw_aa_disc(it, static_cast<float>(dot_x), static_cast<float>(cur_y), 3.6f, accent);
    ui::shadow_print(it, dot_x + 10, cur_y, src_font, ctx.col_slate, TextAlign::CENTER_LEFT, src_fit.c_str());
    cur_y += scale_y(28);
  } else {
    cur_y += scale_y(12);
  }

  const int title_max_w = std::min({kMaxContentColW, safe_span(cur_y + scale_y(14), 22), scale_x(336)});
  std::string title_lines[2];
  const int t_cnt = ui::wrap_text_lines(ev.title, title_max_w, ctx.font_date, title_lines, 2);
  for (int i = 0; i < t_cnt; ++i) {
    ui::shadow_print(it, cx, cur_y, ctx.font_date, ctx.col_white, TextAlign::CENTER, title_lines[i].c_str());
    cur_y += scale_y(30);
  }

  cur_y += scale_y(6);
  fast_hline(it, cx - scale_x(104), cur_y, 2 * scale_x(104), kChromeCardBorder);
  cur_y += scale_y(20);

  const bool has_loc = (ev.location[0] != '\0');
  if (has_loc) {
    const std::string loc_fit = fit_text_to_width_font(
        ev.location, std::min(kMaxContentColW, safe_span(cur_y, 22)), ctx.font_small);
    ui::shadow_print(it, cx, cur_y, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, loc_fit.c_str());
    cur_y += scale_y(26);
  }

  const int btn_y = ly.back_y - scale_y(54);
  const int desc_avail = btn_y - scale_y(30) - cur_y;
  if (ev.description[0] != '\0' && desc_avail >= scale_y(20)) {
    const UiFontView desc_font = (ctx.font_micro != nullptr && SCREEN_H <= 520) ? ctx.font_micro : ctx.font_small;
    const int max_desc_lines = (desc_avail >= scale_y(50)) ? 2 : 1;
    std::string desc_lines[2];
    const int d_cnt = ui::wrap_text_lines(
        ev.description, std::min(kMaxContentColW, safe_span(cur_y + 10, 26)), desc_font, desc_lines, max_desc_lines);
    for (int i = 0; i < d_cnt; ++i) {
      ui::shadow_print(it, cx, cur_y, desc_font, ctx.col_slate, TextAlign::CENTER, desc_lines[i].c_str());
      cur_y += scale_y(22);
    }
  } else if (!has_loc && ev.description[0] == '\0') {
    ui::shadow_print(it, cx, cur_y + scale_y(10), ctx.font_small, ctx.col_slate,
                     TextAlign::CENTER, "No additional details");
  }

  const int btn_h = std::max(44, scale_y(44));
  const int row_safe = band_safe_span_for(
      btn_y, btn_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);

  if (can_del) {
    const int btn_w = std::min({scale_x(216), row_safe, kMaxContentColW});
    ui::action_pill_centered(it, cx, btn_w, btn_y, btn_h,
                             ctx.col_red, ui::chrome_danger_bg(),
                             "Delete Event", ctx.col_white, ctx.font_small,
                             WidgetId::CAL_DELETE_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}

}  // namespace latter_ui
