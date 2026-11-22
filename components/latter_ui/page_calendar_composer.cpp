#include "calendar_internal.h"
#include "ui_touch_recognizer.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace latter_ui {

namespace {

void draw_cal_mic(Rgb565Surface &it, int cx, int cy, int r,
                  UiColor col, bool shadow, void *) {
  ui::draw_icon_mic(it, cx, cy, r, col, shadow);
}

}  // namespace

void render_calendar_add_step1(Rgb565Surface &it, const UiFrameContext &ctx,
                               DeviceRuntimeState &rs,
                               const CalendarSourceMeta *writable_sources, int writable_cnt) {
  const auto &ly = layout();
  const int cx = ctx.cx;
  const bool dictating = cal_dictation_active(rs);
  const bool show_fb = !dictating && !rs.cal.feedback_text.empty() &&
                       (ctx.now_ms >= rs.cal.feedback_ms) &&
                       ((ctx.now_ms - rs.cal.feedback_ms) < 2400U);

  if (dictating) {
    render_page_header(it, cx,
                       (rs.cal.dictation_field == 1U) ? "SPEAK EVENT TITLE..." : "SPEAK LOCATION...",
                       ctx.font_small, ctx.col_white);
  } else if (show_fb) {
    render_page_header(it, cx, rs.cal.feedback_text, ctx.font_small, ctx.col_white);
  } else {
    render_page_header(it, cx, "NEW EVENT", ctx.font_small, ctx.col_white);
  }

  const int row_h = ly.slot5_h;
  const int mic_r = row_h / 2;
  const int mic_gap = std::max(6, scale_x(8));
  const int stack_w = ui::compute_uniform_stack_w(
      ly.slot5_y(0), ly.slot5_y(3), row_h, std::min(ly.list_card_w, kMaxContentColW), 14);
  (void)mic_r;
  (void)mic_gap;

  auto draw_field_with_mic = [&](int slot_idx, const char *prefix, const std::string &val,
                                 const char *placeholder, uint16_t field_wid, uint16_t mic_wid,
                                 uint8_t dict_id) {
    const int ym = ly.slot5_y(slot_idx);
    const int x0 = cx - stack_w / 2;
#if LATTER_HAS_MIC
    const int card_w = stack_w - (2 * mic_r + mic_gap);
    const int mic_cx = x0 + card_w + mic_gap + mic_r;
    const bool mic_active = (rs.cal.dictation_field == dict_id);
#else
    const int card_w = stack_w;
    const bool mic_active = false;
    (void)mic_wid;
    (void)dict_id;
    (void)draw_cal_mic;
#endif
    render_card_box(it, x0, ym - row_h / 2, card_w, row_h,
                    kChromeGlassTint, mic_active ? ctx.col_red : kChromeCardBrdOn, kRadiusCardPx());
    std::string full_lbl(prefix);
    full_lbl += val.empty() ? placeholder : val;
    const std::string fit_l = fit_text_to_width_font(full_lbl, card_w - scale_x(24), ctx.font_small);
    ui::shadow_print(it, x0 + scale_x(12), ym, ctx.font_small,
                     val.empty() ? ctx.col_slate : ctx.col_white, TextAlign::CENTER_LEFT, fit_l.c_str());
    frame_hits().add_rect(x0, ym - row_h / 2, card_w, row_h, field_wid, kHitTap | kHitFocusSlot);

#if LATTER_HAS_MIC
    ui::icon_circle_button(it, mic_cx, ym, mic_r,
                           mic_active ? ui::chrome_danger_bg() : kChromeGlassTint,
                           mic_active ? ctx.col_red : ctx.theme_primary,
                           mic_active ? ctx.col_red : ctx.col_white,
                           draw_cal_mic, nullptr,
                           mic_wid, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, 4);
#endif
  };

  draw_field_with_mic(0, "Title: ", rs.cal.draft_title, "Reminder",
                      WidgetId::CAL_ADD_TITLE_BTN, WidgetId::CAL_ADD_TITLE_MIC, 1U);
  draw_field_with_mic(1, "Location: ", rs.cal.draft_location, "None",
                      WidgetId::CAL_ADD_LOC_BTN, WidgetId::CAL_ADD_LOC_MIC, 2U);

  {
    const int ym = ly.slot5_y(2);
    const int card_w = stack_w;
    const int x0 = cx - card_w / 2;
    const int w_idx = (writable_cnt > 0) ? std::clamp<int>(rs.cal.draft_cal_idx, 0, writable_cnt - 1) : 0;
    const char *cal_name = (writable_cnt > 0) ? writable_sources[w_idx].label : "Default Calendar";
    char cal_buf[64];
    std::snprintf(cal_buf, sizeof(cal_buf), "Calendar: %s", cal_name);
    if (writable_cnt > 1) {
      render_card_box(it, x0, ym - row_h / 2, card_w, row_h,
                      kChromeGlassTint, kChromeCardBrdOn, kRadiusCardPx());
      ui::draw_aa_disc(it, static_cast<float>(x0 + scale_x(14)), static_cast<float>(ym), 4.0f,
                       cal_source_color(ctx, writable_sources[w_idx].color_idx));
      const int tx_x = x0 + scale_x(24);
      const std::string fit_c = fit_text_to_width_font(cal_buf, card_w - (tx_x - x0) - scale_x(12), ctx.font_small);
      ui::shadow_print(it, tx_x, ym, ctx.font_small, ctx.col_white, TextAlign::CENTER_LEFT, fit_c.c_str());
      frame_hits().add_rect(x0, ym - row_h / 2, card_w, row_h,
                            WidgetId::CAL_ADD_CAL_BTN, kHitTap | kHitFocusSlot);
    } else {
      const std::string fit_c = fit_text_to_width_font(cal_buf, card_w - scale_x(24), ctx.font_small);
      if (writable_cnt > 0) {
        int sw = 0, xo = 0, bl = 0, th = 0;
        ctx.font_small->measure(fit_c.c_str(), &sw, &xo, &bl, &th);
        const int dot_x = cx - (sw + 14) / 2 + 4;
        ui::draw_aa_disc(it, static_cast<float>(dot_x), static_cast<float>(ym), 4.0f,
                         cal_source_color(ctx, writable_sources[w_idx].color_idx));
        ui::shadow_print(it, dot_x + 10, ym, ctx.font_small, ctx.col_slate, TextAlign::CENTER_LEFT, fit_c.c_str());
      } else {
        ui::shadow_print(it, cx, ym, ctx.font_small, ctx.col_slate, TextAlign::CENTER, fit_c.c_str());
      }
    }
  }

  {
    const int ym = ly.slot5_y(3);
    const int r_idx = std::clamp<int>(rs.cal.draft_rrule_idx, 0, 6);
    char rep_buf[48];
    std::snprintf(rep_buf, sizeof(rep_buf), "Repeat: %s", kCalRepeatLabels[r_idx]);
    ui::list_row(it, cx, ym, stack_w, row_h, rep_buf, nullptr,
                 kChromeGlassTint, kChromeCardBrdOn, ctx.col_white, ctx.col_slate,
                 ctx.font_small, WidgetId::CAL_ADD_REPEAT_BTN, kHitTap | kHitFocusSlot);
  }

  {
    const int ym = ly.slot5_y(4);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);
    const int btn_w = std::min({scale_x(248), safe_w, kMaxContentColW});
    ui::action_pill_centered(it, cx, btn_w, ym, std::max(44, row_h),
                             ctx.theme_primary, kChromeGlassTint,
                             "Date & Time >", ctx.col_white, ctx.font_small,
                             WidgetId::CAL_ADD_NEXT_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Cancel");
}

void render_calendar_add_step2(Rgb565Surface &it, const UiFrameContext &ctx,
                               DeviceRuntimeState &rs, const PersistedDevicePrefs &prefs,
                               int32_t today_ymd) {
  const auto &ly = layout();
  const int cx = ctx.cx;
  render_page_header(it, cx, "DATE & TIME", ctx.font_small, ctx.col_white);

  const int row_h = ly.slot5_h;
  const int btn_r = row_h / 2;
  const int stack_w = ui::compute_uniform_stack_w(
      ly.slot5_y(0), ly.slot5_y(2), row_h, std::min(ly.list_card_w, kMaxContentColW), 14);

  {
    const int ym = ly.slot5_y(0);
    const int lx = cx - stack_w / 2 + btn_r;
    const int rx = cx + stack_w / 2 - btn_r;

    ui::icon_circle_button(it, lx, ym, btn_r, kChromeGlassTint, kChromeCardBrdOn,
                           ctx.col_white, draw_cal_chevron_left, nullptr,
                           WidgetId::CAL_ADD_DATE_PREV, kHitTap | kHitFocusSlot | kHitFocusCircle, -1, 4);
    ui::icon_circle_button(it, rx, ym, btn_r, kChromeGlassTint, kChromeCardBrdOn,
                           ctx.col_white, draw_cal_chevron_right, nullptr,
                           WidgetId::CAL_ADD_DATE_NEXT, kHitTap | kHitFocusSlot | kHitFocusCircle, 1, 4);

    char dt_buf[36];
    format_cal_short_date(rs.cal.draft_ymd, today_ymd, dt_buf, sizeof(dt_buf));
    const std::string fit_d = fit_text_to_width_font(dt_buf, std::max(40, rx - lx - 2 * btn_r - 12), ctx.font_small);
    ui::shadow_print(it, cx, ym, ctx.font_small, ctx.col_white, TextAlign::CENTER, fit_d.c_str());
  }

  {
    const int ym = ly.slot5_y(1);
    ui::toggle_card_row(it, cx, ym, stack_w, row_h, "All-Day Event", rs.cal.draft_all_day,
                        ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate,
                        WidgetId::CAL_ADD_ALLDAY_BTN, 0);
  }

  if (!rs.cal.draft_all_day) {
    const int ym = ly.slot5_y(2);
    const int gap = scale_x(8);
    const int box_w = (stack_w - gap) / 2;
    const int s_x0 = cx - gap / 2 - box_w;
    const int e_x0 = cx + gap / 2;

    char s_tm[16], e_tm[16], s_lbl[28], e_lbl[28];
    format_cal_clock_min(rs.cal.draft_start_min, prefs.is_24h_format, s_tm, sizeof(s_tm), true);
    format_cal_clock_min(rs.cal.draft_end_min, prefs.is_24h_format, e_tm, sizeof(e_tm), true);
    std::snprintf(s_lbl, sizeof(s_lbl), "Start %s", s_tm);
    std::snprintf(e_lbl, sizeof(e_lbl), "End %s", e_tm);

    const bool focus_start = (rs.cal.draft_time_field == 0U);
    render_card_box(it, s_x0, ym - row_h / 2, box_w, row_h, kChromeGlassTint,
                    focus_start ? ctx.theme_primary : kChromeCardBorder, kRadiusCardPx());
    render_card_box(it, e_x0, ym - row_h / 2, box_w, row_h, kChromeGlassTint,
                    !focus_start ? ctx.theme_primary : kChromeCardBorder, kRadiusCardPx());

    const std::string fit_s = fit_text_to_width_font(s_lbl, box_w - 12, ctx.font_small);
    const std::string fit_e = fit_text_to_width_font(e_lbl, box_w - 12, ctx.font_small);
    ui::shadow_print(it, s_x0 + box_w / 2, ym, ctx.font_small,
                     focus_start ? ctx.col_white : ctx.col_slate, TextAlign::CENTER, fit_s.c_str());
    ui::shadow_print(it, e_x0 + box_w / 2, ym, ctx.font_small,
                     !focus_start ? ctx.col_white : ctx.col_slate, TextAlign::CENTER, fit_e.c_str());

    frame_hits().add_rect(s_x0, ym - row_h / 2, box_w, row_h,
                          WidgetId::CAL_ADD_START_BTN, kHitTap | kHitFocusSlot);
    frame_hits().add_rect(e_x0, ym - row_h / 2, box_w, row_h,
                          WidgetId::CAL_ADD_END_BTN, kHitTap | kHitFocusSlot);

    static constexpr const char *kTimeSteps[4] = {"-1h", "-15m", "+15m", "+1h"};
    const int step_h = std::min(scale_y(40), row_h);
    ui::stepper_row(it, cx, ly.slot5_y(3) - step_h / 2, step_h,
                    kTimeSteps, ctx.font_small, ctx.col_white, WidgetId::STEPPER_BASE);
  }

  {
    const int ym = ly.slot5_y(4);
    const int safe_w = band_safe_span_for(
        ym, row_h, SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, 18);
    const int btn_w = std::min({scale_x(244), safe_w, kMaxContentColW});
    ui::action_pill_centered(it, cx, btn_w, ym, std::max(44, row_h),
                             ctx.theme_primary, kChromeGlassTint,
                             "Create Event", ctx.col_white, ctx.font_small,
                             WidgetId::CAL_ADD_SAVE_BTN, kHitTap | kHitFocusSlot);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}

void render_calendar_picker_drilldown(Rgb565Surface &it, const UiFrameContext &ctx,
                                      DeviceRuntimeState &rs,
                                      const CalendarSourceMeta *writable_sources, int writable_cnt) {
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

  render_page_header(it, cx, hdr, ctx.font_small, ctx.col_white);

  if (opt_cnt <= 0) {
    render_empty_state(it, cx, ctx.cy, "No options available", ctx.font_small, ctx.col_slate);
    render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
    return;
  }

  const int total_pages = std::max(1, (opt_cnt + 4) / 5);
  rs.cal.picker_page = std::clamp(rs.cal.picker_page, 0, total_pages - 1);
  const int base_i = rs.cal.picker_page * 5;
  const int vis_cnt = std::min(5, opt_cnt - base_i);
  const int row_h = ly.slot5_h;
  const int bounce_y = edge_bounce().offset_px(
      PageId::CALENDAR, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);
  const int stack_margin = (total_pages > 1) ? 36 : 16;
  const int card_w = ui::compute_uniform_stack_w(
      ly.slot5_y(0), ly.slot5_y(4), row_h, std::min(ly.list_card_w, kMaxContentColW), stack_margin);

  for (int r = 0; r < vis_cnt; ++r) {
    const int idx = base_i + r;
    const int ym = ly.slot5_y(r) + bounce_y;
    const bool sel = (idx == active_idx);
    ui::list_row(it, cx, ym, card_w, row_h, options[idx].c_str(), nullptr,
                 kChromeGlassTint, sel ? ctx.theme_primary : kChromeCardBrdOn,
                 sel ? ctx.theme_primary : ctx.col_white, ctx.col_slate,
                 ctx.font_small,
                 static_cast<uint16_t>(WidgetId::CAL_PICKER_SLOT_BASE + r),
                 kHitTap | kHitFocusSlot, static_cast<int16_t>(idx));
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, rs.cal.picker_page > 0, (rs.cal.picker_page + 1) < total_pages,
                              ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
  }

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back");
}

}  // namespace latter_ui
