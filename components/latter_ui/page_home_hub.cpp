#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace latter_ui {
using namespace latter_core;

namespace {

int band_safe_w(int cy_band, int h_band, int inset = 14) {
  const int half_h = std::max(1, h_band / 2);
  const int y_top = std::clamp(cy_band - half_h, 0, SCREEN_H - 1);
  const int y_bot = std::clamp(cy_band + half_h, 0, SCREEN_H - 1);
  return std::min(safe_span(y_top, inset), safe_span(y_bot, inset));
}

std::string to_ascii_upper_str(std::string_view sv) {
  std::string out(sv);
  for (char &c : out) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'a' && uc <= 'z') {
      c = static_cast<char>(uc - ('a' - 'A'));
    }
  }
  return out;
}

struct HomeHubModeIconCtx {
  const int *tabs{nullptr};
  int count{0};
};

void draw_home_hub_mode_icon(Rgb565Surface &it, int g_idx,
                             int bx, int by, int ir,
                             UiColor fg_c, bool sh, void *user_ctx) {
  const auto *mc = static_cast<const HomeHubModeIconCtx *>(user_ctx);
  if (mc == nullptr || mc->tabs == nullptr || g_idx < 0 || g_idx >= mc->count) return;
  switch (mc->tabs[g_idx]) {
    case HomeHubTab::ALARM:
      ui::draw_icon_shield_home(it, bx, by, ir, fg_c, sh);
      break;
    case HomeHubTab::INTERCOM:
      ui::draw_icon_intercom_handset(it, bx, by, ir, fg_c, sh);
      break;
    case HomeHubTab::THERMOSTAT:
      render_mdi_icon(it, "mdi:thermostat", bx, by, std::max(16, 2 * ir), fg_c, sh, "mdi:thermometer");
      break;
    case HomeHubTab::ENTRY:
      ui::draw_icon_lock(it, bx, by, ir, fg_c, true, sh);
      break;
    case HomeHubTab::VACUUM:
    default:
      ui::draw_icon_robot_vacuum(it, bx, by, ir, fg_c, sh);
      break;
  }
}

void draw_hub_minus_icon(Rgb565Surface &it, int cx, int cy, int r,
                         UiColor col, bool shadow, void *) {
  ui::draw_icon_minus(it, cx, cy, r, col, shadow);
}

void draw_hub_plus_icon(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor col, bool shadow, void *) {
  ui::draw_icon_plus(it, cx, cy, r, col, shadow);
}

void render_home_hub_mode_bar(Rgb565Surface &it, const UiFrameContext &ctx,
                              const int *tabs, int tab_count, int active_slot) {
  home_hub_mode_bar_page() = get_mode_bar_start_index(tab_count, home_hub_mode_bar_page(), layout().dock_slot_count);
  HomeHubModeIconCtx ic{tabs, tab_count};
  ui::mode_bar(it, ctx.cx, tab_count, active_slot, home_hub_mode_bar_page(),
               ctx.theme_primary, ctx.col_white, draw_home_hub_mode_icon, &ic);
}

void render_alarm_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                      const DeviceRuntimeState &rs, const AreaContextState &ac,
                      int content_top, int content_bot) {
  const int cx = ctx.cx;
  render_page_header(it, cx, "SECURITY ALARM", ctx.font_small, ctx.col_white);
  if (ac.alarm_entity.empty()) {
    render_empty_state(it, cx, (content_top + content_bot) / 2,
                       "No Security Alarm configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const std::string &alarm_state =
      !ac.alarm_state.empty() ? ac.alarm_state : rs.area.alarm_state;
  const uint16_t caps = effective_alarm_caps(ac);
  const bool pulse = ((ctx.now_ms / 400U) % 2U) != 0U;
  const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());

  const char *badge_txt = kUnknownGlyph;
  Color badge_col = ctx.col_slate;
  if (!alarm_state.empty()) {
    if (eq_ascii_ci_sv(alarm_state, "disarmed")) {
      badge_txt = "Disarmed";
      badge_col = ctx.col_green;
    } else if (eq_ascii_ci_sv(alarm_state, "armed_home")) {
      badge_txt = "Armed Home";
      badge_col = ctx.col_warm_gold;
    } else if (eq_ascii_ci_sv(alarm_state, "armed_away")) {
      badge_txt = "Armed Away";
      badge_col = ctx.col_red;
    } else if (eq_ascii_ci_sv(alarm_state, "armed_night")) {
      badge_txt = "Armed Night";
      badge_col = ctx.col_accent_cyan;
    } else if (eq_ascii_ci_sv(alarm_state, "armed_vacation")) {
      badge_txt = "Armed Vacation";
      badge_col = ctx.theme_primary;
    } else if (eq_ascii_ci_sv(alarm_state, "arming") || eq_ascii_ci_sv(alarm_state, "pending")) {
      badge_txt = eq_ascii_ci_sv(alarm_state, "pending") ? "Entry Pending" : "Arming...";
      badge_col = pulse ? ctx.col_white : ctx.col_warm_gold;
    } else if (eq_ascii_ci_sv(alarm_state, "triggered")) {
      badge_txt = "Triggered!";
      badge_col = pulse ? ctx.col_white : ctx.col_red;
    } else {
      badge_txt = alarm_state.c_str();
      badge_col = ctx.theme_secondary;
    }
  }

  HomeHubActionSpec specs[4]{};
  const int act_cnt = resolve_alarm_actions(alarm_state, caps, specs);
  const bool use_two_rows = home_hub_uses_two_rows(
      act_cnt, content_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);

  const int avail_h = std::max(140, content_bot - content_top);
  const int hero_h = use_two_rows
      ? std::clamp((avail_h * 44) / 100, 66, dp_px(104))
      : std::clamp((avail_h * 56) / 100, 82, dp_px(132));
  const int hero_y0 = content_top;

  // D-048: Status readout rendered directly over wallpaper with 1px kChromeShadow (never inside a glass card).
  const int icon_cy = hero_y0 + (is_fb ? (hero_h * 25) / 100 : (hero_h * 32) / 100);
  const int st_y = hero_y0 + (is_fb ? (hero_h * 60) / 100 : (hero_h * 72) / 100);
  const bool is_disarmed = eq_ascii_ci_sv(alarm_state, "disarmed");
  ui::draw_icon_shield_home(it, cx, icon_cy, ui::hero_icon_r(hero_h), badge_col, true, !is_disarmed);

  const UiFontView st_font = (SCREEN_H < 400) ? ctx.font_btn_bold : ctx.font_date;
  const int st_max_w = std::max(80, band_safe_w(st_y, 32, 16));
  const std::string fit_badge = fit_text_to_width_font(std::string(badge_txt), st_max_w, st_font);
  ui::shadow_print(it, cx, st_y, st_font, badge_col, TextAlign::CENTER, fit_badge.c_str());

  if (is_fb) {
    const UiFontView fb_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    const int fb_y = hero_y0 + (hero_h * 88) / 100;
    const int fb_max_w = std::max(80, band_safe_w(fb_y, 24, 16));
    const std::string fit_fb = fit_text_to_width_font(rs.area.action_feedback, fb_max_w, fb_font);
    ui::shadow_print(it, cx, fb_y, fb_font, ctx.theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  }

  const int deck_top = hero_y0 + hero_h + std::max(6, dp_px(10));
  HomeHubPillGeom geoms[4]{};
  const int geom_cnt = compute_home_hub_pill_deck(
      act_cnt, deck_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      use_two_rows, geoms, LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);

  auto color_for_alarm_code = [&](int code) -> Color {
    switch (code) {
      case 0: return ctx.col_green;
      case 1: return ctx.col_warm_gold;
      case 2: return ctx.col_red;
      case 3: return ctx.col_accent_cyan;
      case 4:
      default: return ctx.theme_primary;
    }
  };

  int codes[4]{};
  for (int i = 0; i < geom_cnt; ++i) {
    codes[i] = specs[i].code;
    const Color acc = color_for_alarm_code(specs[i].code);
    const bool is_disarm_cta = (specs[i].code == 0);
    const bool use_short = (!use_two_rows && geom_cnt >= 3 && geoms[i].w < dp_px(92));
    ui::action_pill_centered(
        it, geoms[i].cx, geoms[i].w, geoms[i].cy, geoms[i].h,
        acc, is_disarm_cta ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
        use_short ? specs[i].short_label : specs[i].label,
        ctx.col_white,
        (geom_cnt == 1 && SCREEN_W >= 360) ? ctx.font_btn_bold : ctx.font_small,
        WidgetId::NONE);
  }
  register_home_hub_pill_deck_hits(frame_hits(), WidgetId::ALARM_MODE_BASE, codes, geoms, geom_cnt);
}

void render_hub_selector_pill(Rgb565Surface &it, const UiFrameContext &ctx,
                              const char *prefix_lbl, int active_idx, int item_count,
                              const std::string &item_name, const char *fallback_hdr,
                              int hdr_cy, uint16_t prev_wid, uint16_t next_wid) {
  const int cx = ctx.cx;
  if (item_count <= 1) {
    const std::string hdr_txt = !item_name.empty() ? to_ascii_upper_str(item_name) : std::string(fallback_hdr);
    render_page_header(it, cx, hdr_txt, ctx.font_small, ctx.col_white);
    return;
  }

  // D-048 / D-070: Zero glass behind multi-item selector title — [‹] glass circle + bare title + [›] glass circle.
  std::string title = std::string(prefix_lbl) + " " + std::to_string(active_idx + 1) + "/" +
                      std::to_string(item_count);
  if (!item_name.empty()) {
    title += " \xC2\xB7 " + to_ascii_upper_str(item_name);
  }
  ui::header_pager_bar(it, cx, hdr_cy, title, ctx.font_small, ctx.col_white, true,
                       prev_wid, next_wid, -1, 1);
}

void render_intercom_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                         const DeviceRuntimeState &rs, const AreaContextState &ac,
                         int content_top, int content_bot) {
  const int cx = ctx.cx;
  const bool dictating = broadcast_dictation_active(rs);
  const bool show_fb = dictating ||
                       (!rs.intercom.broadcast_feedback.empty() &&
                        (ctx.now_ms - rs.intercom.broadcast_feedback_ms < 2200U));
  if (show_fb && !rs.intercom.broadcast_feedback.empty()) {
    const std::string fit_fb = fit_text_to_width_font(
        rs.intercom.broadcast_feedback, safe_span(layout().hdr_y, 16), ctx.font_small);
    render_page_header(it, cx, fit_fb, ctx.font_small, ctx.col_white);
  } else {
    render_page_header(it, cx, "BROADCAST", ctx.font_small, ctx.col_white);
  }

  const auto &ly = layout();
  const int min_hit = UiLayout::mm_to_px(7.0f);
  std::vector<std::string> phrases;
  const int total_cnt = parse_broadcast_phrases(ac.broadcast_phrases, phrases);

  const bool has_mic = (LATTER_HAS_MIC != 0);
  const int speak_h = (SCREEN_H < 400) ? 36 : dp_px(44);
  const int speak_hit_h = std::max(speak_h, min_hit);
  const char *speak_lbl = dictating
                              ? (rs.voice.vad_active ? "Listening..." : "Cancel")
                              : "Speak";
  const Color speak_brd = dictating ? ctx.col_red : ctx.theme_primary;
  const Color speak_bg = dictating ? ui::chrome_danger_bg() : ui::chrome_card_bg_on();
  const char *speak_mdi = dictating ? "mdi:microphone-off" : "mdi:microphone";

  if (total_cnt <= 0) {
    home_hub_broadcast_page() = 0;
    const int mid_cy = (content_top + content_bot) / 2;
    if (has_mic) {
      const int speak_w = std::min({ly.list_card_w, band_safe_w(mid_cy, speak_h, 16)});
      ui::action_pill_centered(it, cx, speak_w, mid_cy, speak_h,
                               speak_brd, speak_bg, speak_lbl, ctx.col_white,
                               ctx.font_small, WidgetId::INTERCOM_SPEAK_BTN,
                               kHitTap | kHitFocusSlot, 0, speak_hit_h, speak_mdi);
    } else {
      render_empty_state(it, cx, mid_cy,
                         "No Quick Phrases configured", ctx.font_small, ctx.col_slate);
    }
    return;
  }

  int rows_top = content_top;
  if (has_mic) {
    const int speak_cy = content_top + speak_h / 2 + std::max(2, dp_px(4));
    const int speak_w = std::min({ly.list_card_w, band_safe_w(speak_cy, speak_h, 14)});
    ui::action_pill_centered(it, cx, speak_w, speak_cy, speak_h,
                             speak_brd, speak_bg, speak_lbl, ctx.col_white,
                             ctx.font_small, WidgetId::INTERCOM_SPEAK_BTN,
                             kHitTap | kHitFocusSlot, 0, speak_hit_h, speak_mdi);
    rows_top = speak_cy + speak_h / 2 + std::max(6, dp_px(10));
  }

  const int rows_avail = std::max(90, content_bot - rows_top);
  const int card_h = ly.slot5_h;
  const int std_gap = std::max(4, ly.slot5_stride - card_h);
  const int total_pages = std::max(1, (total_cnt + 2) / 3);
  int &cur_pg = home_hub_broadcast_page();
  cur_pg = std::clamp(cur_pg, 0, total_pages - 1);
  const int base_idx = cur_pg * 3;
  const int vis_cnt = std::min(3, total_cnt - base_idx);

  const int eff_gap = std::clamp((rows_avail - 3 * card_h) / 2, 4, std_gap);
  const int block_h = vis_cnt * card_h + std::max(0, vis_cnt - 1) * eff_gap;
  const int start_y0 = rows_top + std::max(0, (rows_avail - block_h) / 2);
  const int top_cy = start_y0 + card_h / 2;
  const int bot_cy = start_y0 + std::max(0, vis_cnt - 1) * (card_h + eff_gap) + card_h / 2;
  const int pips_inset = (total_pages > 1) ? 24 : 14;
  const int stack_w = std::min({ly.list_card_w,
                                band_safe_w(top_cy, card_h, pips_inset),
                                band_safe_w(bot_cy, card_h, pips_inset)});
  const int bounce_y = edge_bounce().offset_px(
      PageId::HOME_HUB, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);

  for (int i = 0; i < vis_cnt; ++i) {
    const int g_idx = base_idx + i;
    const int cy_r = start_y0 + i * (card_h + eff_gap) + card_h / 2 + bounce_y;
    ui::list_row(it, cx, cy_r, stack_w, card_h,
                 phrases[static_cast<size_t>(g_idx)].c_str(), nullptr,
                 ui::chrome_card_bg_off(), ui::chrome_card_brd_on(),
                 ctx.col_white, ctx.col_slate, ctx.font_small,
                 static_cast<uint16_t>(WidgetId::INTERCOM_ROW_BASE + i),
                 kHitTap | kHitFocusSlot, static_cast<int16_t>(g_idx),
                 "mdi:bullhorn");
  }
  if (total_pages > 1) {
    render_vertical_page_pips(it, cur_pg > 0, cur_pg + 1 < total_pages,
                              ctx.col_bg, ctx.theme_primary, ctx.col_dark_gray,
                              ly.mode_bar_zone_top());
  }
}

void render_thermostat_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs, const AreaContextState &ac,
                           int content_top, int content_bot) {
  (void)ac;
  const int cx = ctx.cx;
  const auto &hub = home_hub_state();
  if (hub.thermostat_count <= 0) {
    render_page_header(it, cx, "THERMOSTAT", ctx.font_small, ctx.col_white);
    render_empty_state(it, cx, (content_top + content_bot) / 2,
                       "No Thermostat configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const int idx = std::clamp(hub.thermostat_idx, 0, hub.thermostat_count - 1);
  const auto &z = hub.thermostats[idx];
  const int hdr_cy = layout().hdr_y + dp_px(4);
  render_hub_selector_pill(it, ctx, "Zone", idx, hub.thermostat_count, z.name,
                           "THERMOSTAT", hdr_cy,
                           WidgetId::THERMOSTAT_PREV_BTN, WidgetId::THERMOSTAT_NEXT_BTN);
  const int top_y = (hub.thermostat_count > 1)
      ? (hdr_cy + dp_px(24))
      : content_top;
  render_climate_controls_deck(it, ctx, rs, z, top_y, content_bot);
}

struct EntryIconCtx {
  bool is_lock{true};
  bool active_state{false};
};

void draw_entry_item_icon(Rgb565Surface &it, int cx, int cy, int r,
                          UiColor col, bool shadow, void *user_ctx) {
  const auto *ec = static_cast<const EntryIconCtx *>(user_ctx);
  if (ec == nullptr) return;
  if (ec->is_lock) {
    ui::draw_icon_lock(it, cx, cy, r, col, ec->active_state, shadow);
  } else {
    ui::draw_icon_garage_door(it, cx, cy, r, col, ec->active_state, shadow);
  }
}

void render_entry_card(Rgb565Surface &it, const UiFrameContext &ctx,
                       int cx, int cy, int card_w, int card_h, int hit_h,
                       bool is_lock, const char *title, const char *state_txt,
                       UiColor state_col, const char *cta_txt,
                       UiColor cta_col, uint16_t btn_wid,
                       int16_t hit_param = 0) {
  // D-048 / D-070 / D-073: Strict button-only glass — bare shadowed icon/label/status on the left,
  // frosted-glass action pill on the right as the sole interactive control.
  EntryIconCtx icon_ctx{
      is_lock,
      is_lock ? (std::strcmp(state_txt, "Unlocked") != 0) : (std::strcmp(state_txt, "Open") == 0),
  };
  const int min_hit = UiLayout::mm_to_px(kHitFloorPrimaryMm);
  const int min_cta_w = std::max(84, min_hit);
  const int cta_w = safe_clamp(card_w / 3, min_cta_w, std::max(min_cta_w, dp_px(122)));
  const int min_cta_h = std::max(36, min_row_hit_px_for(LATTER_PANEL_PPI));
  const int max_cta_h = std::max(min_cta_h, std::min(card_h - dp_px(8), dp_px(64)));
  const int cta_h = safe_clamp(card_h - dp_px(16), min_cta_h, max_cta_h);
  const UiFontView pri_font = (SCREEN_H < 400) ? ctx.font_small : ctx.font_btn_bold;
  const UiFontView sec_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;

  ui::labeled_action_row(
      it, cx, cy, card_w, card_h,
      title, state_txt, ctx.col_white, state_col, pri_font, sec_font,
      cta_txt, cta_w, cta_h, cta_col, ui::chrome_card_bg_on(), ctx.col_white,
      ctx.font_small, btn_wid, kHitTap | kHitFocusSlot, hit_param,
      std::max(cta_h, std::min(hit_h, min_hit + 8)),
      draw_entry_item_icon, &icon_ctx, state_col);
}

void render_entry_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                      const DeviceRuntimeState &rs, const AreaContextState &ac,
                      int content_top, int content_bot) {
  (void)ac;
  const int cx = ctx.cx;
  const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());
  if (is_fb) {
    const std::string fit_fb = fit_text_to_width_font(
        rs.area.action_feedback, safe_span(layout().hdr_y, 16), ctx.font_small);
    render_page_header(it, cx, fit_fb, ctx.font_small, ctx.col_white);
  } else {
    render_page_header(it, cx, "ENTRY & GARAGE", ctx.font_small, ctx.col_white);
  }

  const auto &hub = home_hub_state();
  if (hub.entry_count <= 0) {
    render_empty_state(it, cx, (content_top + content_bot) / 2,
                       "No Entry Controls configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const int avail_h = std::max(120, content_bot - content_top);
  const int min_hit = UiLayout::mm_to_px(7.0f);
  const int total_pages = (hub.entry_count + 1) / 2;
  const int page = std::clamp(hub.entry_page, 0, total_pages - 1);
  const int start_i = page * 2;
  const int vis_cnt = std::min(2, hub.entry_count - start_i);
  const int bounce_y = edge_bounce().offset_px(
      PageId::HOME_HUB, GestureAxisLock::Vertical, ctx.now_ms, LATTER_PANEL_PPI);

  auto draw_item_slot = [&](int slot, int cy_card, int w_card, int card_h, int hit_h) {
    const int global_i = start_i + slot;
    const auto &item = hub.entries[global_i];
    const char *title = !item.name.empty()
        ? item.name.c_str()
        : (item.is_lock ? "Door Lock" : "Garage Door");
    if (item.is_lock) {
      const LockState ls = item.lock_state;
      const char *st_txt = (ls == LockState::LOCKED) ? "Locked"
                         : ((ls == LockState::UNLOCKED) ? "Unlocked" : kUnknownGlyph);
      const Color st_col = (ls == LockState::LOCKED) ? ctx.col_green
                         : ((ls == LockState::UNLOCKED) ? ctx.col_red : ctx.col_slate);
      const char *cta_txt = (ls == LockState::UNLOCKED) ? "Lock" : "Unlock";
      const Color cta_col = (ls == LockState::UNLOCKED) ? ctx.col_green : ctx.col_warm_gold;
      render_entry_card(it, ctx, cx, cy_card, w_card, card_h, hit_h,
                        true, title, st_txt, st_col, cta_txt, cta_col,
                        static_cast<uint16_t>(WidgetId::ENTRY_ACTION_BASE + slot),
                        static_cast<int16_t>(global_i));
    } else {
      const GarageState gs = item.garage_state;
      const char *st_txt = (gs == GarageState::CLOSED) ? "Closed"
                         : ((gs == GarageState::OPEN) ? "Open" : kUnknownGlyph);
      const Color st_col = (gs == GarageState::CLOSED) ? ctx.col_green
                         : ((gs == GarageState::OPEN) ? ctx.col_warm_gold : ctx.col_slate);
      const char *cta_txt = (gs == GarageState::OPEN) ? "Close" : "Open";
      const Color cta_col = (gs == GarageState::OPEN) ? ctx.col_green : ctx.col_warm_gold;
      render_entry_card(it, ctx, cx, cy_card, w_card, card_h, hit_h,
                        false, title, st_txt, st_col, cta_txt, cta_col,
                        static_cast<uint16_t>(WidgetId::ENTRY_ACTION_BASE + slot),
                        static_cast<int16_t>(global_i));
    }
  };

  const int max_entry_w = std::min(layout().max_content_w(360), layout().area_card3_w);
  const int pips_inset = (total_pages > 1) ? 24 : 14;
  if (vis_cnt >= 2) {
    const int stride = avail_h / 2;
    const int card_h = std::clamp(stride - 10, 56, dp_px(84));
    const int hit_h = std::max(card_h, std::min(min_hit, stride));
    const int cy0 = content_top + stride / 2 + bounce_y;
    const int cy1 = content_top + stride + stride / 2 + bounce_y;
    const int stack_w = std::min({max_entry_w,
                                  band_safe_w(cy0, card_h, pips_inset),
                                  band_safe_w(cy1, card_h, pips_inset)});
    draw_item_slot(0, cy0, stack_w, card_h, hit_h);
    draw_item_slot(1, cy1, stack_w, card_h, hit_h);
  } else {
    const int cy_only = content_top + avail_h / 2 + bounce_y;
    const int card_h = std::clamp((avail_h * 56) / 100, 68, dp_px(104));
    const int hit_h = std::max(card_h, min_hit);
    const int w_only = std::min(max_entry_w, band_safe_w(cy_only, card_h, pips_inset));
    draw_item_slot(0, cy_only, w_only, card_h, hit_h);
  }

  if (total_pages > 1) {
    render_vertical_page_pips(it, page > 0, page + 1 < total_pages,
                              ctx.col_bg, ctx.col_white, ctx.col_slate,
                              layout().mode_bar_zone_top());
  }
}

void render_vacuum_tab(Rgb565Surface &it, const UiFrameContext &ctx,
                       const DeviceRuntimeState &rs, const AreaContextState &ac,
                       int content_top, int content_bot) {
  (void)ac;
  const int cx = ctx.cx;
  const auto &hub = home_hub_state();
  if (hub.vacuum_count <= 0) {
    render_page_header(it, cx, "ROBOT VACUUM", ctx.font_small, ctx.col_white);
    render_empty_state(it, cx, (content_top + content_bot) / 2,
                       "No Robot Vacuum configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const int idx = std::clamp(hub.vacuum_idx, 0, hub.vacuum_count - 1);
  const auto &v = hub.vacuums[idx];
  const int hdr_cy = layout().hdr_y + dp_px(4);
  render_hub_selector_pill(it, ctx, "Vac", idx, hub.vacuum_count, v.name,
                           "ROBOT VACUUM", hdr_cy,
                           WidgetId::VACUUM_PREV_BTN, WidgetId::VACUUM_NEXT_BTN);
  const int top_y = (hub.vacuum_count > 1)
      ? (hdr_cy + dp_px(24))
      : content_top;

  const std::string &vst = v.state;
  const bool is_cln = eq_ascii_ci_sv(vst, "cleaning");
  const bool is_psd = eq_ascii_ci_sv(vst, "paused");
  const bool is_idl = eq_ascii_ci_sv(vst, "idle");
  const bool is_ret = eq_ascii_ci_sv(vst, "returning");
  const bool is_dck = eq_ascii_ci_sv(vst, "docked");
  const bool is_err = eq_ascii_ci_sv(vst, "error");

  const char *st_lbl = vst.empty() ? kUnknownGlyph
                     : (is_cln ? "Cleaning"
                     : (is_psd ? "Paused"
                     : (is_idl ? "Idle"
                     : (is_ret ? "Returning"
                     : (is_dck ? "Docked"
                     : (is_err ? "Error" : vst.c_str()))))));
  const Color st_col = is_cln ? ctx.col_green
                     : ((is_psd || is_idl) ? ctx.col_warm_gold
                     : (is_ret ? ctx.theme_secondary
                     : (is_dck ? ctx.col_accent_cyan
                     : (is_err ? ctx.col_red : ctx.col_slate))));
  const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());

  const int avail_h = std::max(120, content_bot - top_y);
  const int hero_h = std::clamp((avail_h * 56) / 100, 74, dp_px(132));
  const int hero_y0 = top_y;

  // D-048: Robot vacuum icon + status text rendered directly over wallpaper with kChromeShadow
  const int icon_cy = hero_y0 + (is_fb ? (hero_h * 25) / 100 : (hero_h * 32) / 100);
  const int st_y = hero_y0 + (is_fb ? (hero_h * 60) / 100 : (hero_h * 72) / 100);
  ui::draw_icon_robot_vacuum(it, cx, icon_cy, ui::hero_icon_r(hero_h), st_col, true);

  const UiFontView st_font = (SCREEN_H < 400) ? ctx.font_btn_bold : ctx.font_date;
  const int st_max_w = std::max(80, band_safe_w(st_y, 32, 16));
  const std::string fit_st = fit_text_to_width_font(std::string(st_lbl), st_max_w, st_font);
  ui::shadow_print(it, cx, st_y, st_font, st_col, TextAlign::CENTER, fit_st.c_str());

  if (is_fb) {
    const UiFontView fb_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    const int fb_y = hero_y0 + (hero_h * 88) / 100;
    const int fb_max_w = std::max(80, band_safe_w(fb_y, 24, 16));
    const std::string fit_fb = fit_text_to_width_font(rs.area.action_feedback, fb_max_w, fb_font);
    ui::shadow_print(it, cx, fb_y, fb_font, ctx.theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  }

  HomeHubActionSpec specs[4]{};
  const uint16_t eff_vcaps = (v.caps != 0U) ? v.caps : kVacuumCapDefault;
  const int act_cnt = resolve_vacuum_actions(vst, eff_vcaps, specs);
  const int deck_top = hero_y0 + hero_h + std::max(6, dp_px(10));
  HomeHubPillGeom geoms[4]{};
  const int geom_cnt = compute_home_hub_pill_deck(
      act_cnt, deck_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      false, geoms, LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);

  auto color_for_vac_code = [&](int code) -> Color {
    switch (code) {
      case 0: return ctx.col_green;
      case 1:
      case 3: return ctx.col_warm_gold;
      case 2: return ctx.col_accent_cyan;
      case 4:
      case 5:
      default: return ctx.theme_primary;
    }
  };

  int codes[4]{};
  for (int i = 0; i < geom_cnt; ++i) {
    codes[i] = specs[i].code;
    const Color vc = color_for_vac_code(specs[i].code);
    const bool primary_only = (geom_cnt == 1);
    ui::action_pill_centered(
        it, geoms[i].cx, geoms[i].w, geoms[i].cy, geoms[i].h,
        vc, primary_only ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
        specs[i].label, ctx.col_white,
        (primary_only && SCREEN_W >= 360) ? ctx.font_btn_bold : ctx.font_small,
        WidgetId::NONE);
  }
  register_home_hub_pill_deck_hits(frame_hits(), WidgetId::VACUUM_ACTION_BASE, codes, geoms, geom_cnt);
}

}  // namespace

void render_home_hub_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  int tabs[kMaxHomeHubTabs]{};
  const int tab_count = collect_home_hub_tabs(tabs, ac);
  if (tab_count <= 0) {
    render_page_header(it, ctx.cx, "HOME HUB", ctx.font_small, ctx.col_white);
    render_empty_state(it, ctx.cx, layout().area_card3_y(1),
                       "No Home Controls configured", ctx.font_small, ctx.col_slate);
    return;
  }

  int active_slot = -1;
  for (int i = 0; i < tab_count; ++i) {
    if (tabs[i] == home_hub_tab()) {
      active_slot = i;
      break;
    }
  }
  if (active_slot < 0) {
    home_hub_tab() = tabs[0];
    active_slot = 0;
  }
  const int tab = tabs[active_slot];

  const int content_top = layout().hdr_y + dp_px(24);
  const int content_bot = layout().mode_bar_zone_top() - dp_px(6);

  switch (tab) {
    case HomeHubTab::ALARM:
      render_alarm_tab(it, ctx, rs, ac, content_top, content_bot);
      break;
    case HomeHubTab::INTERCOM:
      render_intercom_tab(it, ctx, rs, ac, content_top, content_bot);
      break;
    case HomeHubTab::THERMOSTAT:
      render_thermostat_tab(it, ctx, rs, ac, content_top, content_bot);
      break;
    case HomeHubTab::ENTRY:
      render_entry_tab(it, ctx, rs, ac, content_top, content_bot);
      break;
    case HomeHubTab::VACUUM:
    default:
      render_vacuum_tab(it, ctx, rs, ac, content_top, content_bot);
      break;
  }

  render_home_hub_mode_bar(it, ctx, tabs, tab_count, active_slot);
}

void render_climate_controls_deck(Rgb565Surface &it,
                                  const UiFrameContext &ctx,
                                  const DeviceRuntimeState &rs,
                                  const ThermostatZoneItem &clim,
                                  int content_top,
                                  int content_bot) {
  const int cx = ctx.cx;
  const std::string &m = clim.mode;
  const uint16_t eff_caps = (clim.caps != 0U) ? clim.caps : kClimateCapDefault;
  const bool is_off = eq_ascii_ci_sv(m, "off");
  const bool is_heat = eq_ascii_ci_sv(m, "heat");
  const bool is_cool = eq_ascii_ci_sv(m, "cool");
  const bool is_auto = eq_ascii_ci_sv(m, "auto") || eq_ascii_ci_sv(m, "heat_cool");
  const bool is_fan = eq_ascii_ci_sv(m, "fan_only");
  const bool is_dry = eq_ascii_ci_sv(m, "dry");
  const bool show_setpoint_btns = thermostat_shows_setpoint_buttons(m, eff_caps);

  const Color sub_tint = is_heat ? ctx.col_orange
                       : (is_cool ? ctx.col_accent_cyan
                       : (is_auto ? ctx.theme_secondary
                       : (is_fan ? ctx.theme_primary
                       : (is_dry ? ctx.col_warm_gold : ctx.col_slate))));
  const char *mode_lbl = m.empty() ? kUnknownGlyph
                       : (is_heat ? "Heating"
                       : (is_cool ? "Cooling"
                       : (is_auto ? "Auto"
                       : (is_fan ? "Fan"
                       : (is_dry ? "Dry"
                       : (is_off ? "Off" : m.c_str()))))));

  ThermostatModeSpec mode_specs[6]{};
  const int mode_cnt = resolve_thermostat_modes(m, eff_caps, mode_specs);
  const bool use_two_rows = home_hub_uses_two_rows(
      mode_cnt, content_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);

  const int avail_h = std::max(120, content_bot - content_top);
  const int hero_h = use_two_rows
      ? std::clamp((avail_h * 46) / 100, 64, dp_px(108))
      : std::clamp((avail_h * 58) / 100, 78, dp_px(136));
  const int hero_cy = content_top + hero_h / 2 + std::max(2, dp_px(4));
  const int hero_w = std::min({layout().max_content_w(340), dp_px(328), band_safe_w(hero_cy, hero_h, 14)});
  const int hero_x0 = cx - hero_w / 2;
  const int hero_y0 = hero_cy - hero_h / 2;

  const int min_hit = UiLayout::mm_to_px(7.0f);
  const int btn_r = std::clamp(hero_h / 3, 18, dp_px(26));
  const int btn_hit_r = std::max(btn_r, std::min(min_hit / 2, hero_h / 2));
  const int minus_cx = hero_x0 + btn_r + std::max(8, dp_px(12));
  const int plus_cx = hero_x0 + hero_w - btn_r - std::max(8, dp_px(12));

  // D-048: Glass reserved strictly for interactive -/+ circles and HVAC mode pills;
  // temperature and current-reading text render directly over wallpaper with kChromeShadow.
  if (show_setpoint_btns) {
    ui::icon_circle_button(it, minus_cx, hero_cy, btn_r, ui::chrome_card_bg_off(), ui::chrome_card_brd_on(),
                           ctx.col_white, draw_hub_minus_icon, nullptr, WidgetId::NONE);
    ui::icon_circle_button(it, plus_cx, hero_cy, btn_r, ui::chrome_card_bg_off(), ui::chrome_card_brd_on(),
                           ctx.col_white, draw_hub_plus_icon, nullptr, WidgetId::NONE);
    register_thermostat_stepper_hits(frame_hits(), true, minus_cx, plus_cx, hero_cy, btn_r, btn_hit_r);
  }

  char tgt_buf[24];
  if (is_off) {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "Off");
  } else if (clim.target_temp > 0 && show_setpoint_btns) {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "%d\xC2\xB0", clim.target_temp);
  } else if (!show_setpoint_btns && !m.empty()) {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "%s", mode_lbl);
  } else {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "%s\xC2\xB0", kUnknownGlyph);
  }

  const int inner_span = show_setpoint_btns
      ? std::max(64, (plus_cx - btn_r) - (minus_cx + btn_r) - 12)
      : std::max(80, hero_w - 16);
  const UiFontView temp_font = (SCREEN_H < 400 || inner_span < 120) ? ctx.font_date : ctx.font_chrono_time;
  const int temp_y = hero_cy - std::max(8, hero_h / 7);
  ui::shadow_print(it, cx, temp_y, temp_font, ctx.col_white, TextAlign::CENTER, tgt_buf);

  char sub_buf[48];
  const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());
  const float cur_t = clim.current_temp;
  const bool has_cur = !std::isnan(cur_t) && cur_t > -40.0f && cur_t < 150.0f;
  if (is_fb) {
    std::snprintf(sub_buf, sizeof(sub_buf), "%s", rs.area.action_feedback.c_str());
  } else if (is_off || (!show_setpoint_btns && !m.empty())) {
    if (has_cur) {
      std::snprintf(sub_buf, sizeof(sub_buf), "Current %.0f\xC2\xB0", cur_t);
    } else {
      sub_buf[0] = '\0';
    }
  } else if (has_cur) {
    std::snprintf(sub_buf, sizeof(sub_buf), "Current %.0f\xC2\xB0 \xC2\xB7 %s", cur_t, mode_lbl);
  } else {
    std::snprintf(sub_buf, sizeof(sub_buf), "%s", mode_lbl);
  }

  if (sub_buf[0] != '\0') {
    const UiFontView sub_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    const std::string fit_sub = fit_text_to_width_font(std::string(sub_buf), inner_span, sub_font);
    const int sub_y = hero_cy + std::max(14, hero_h / 4);
    ui::shadow_print(it, cx, sub_y, sub_font, is_fb ? ctx.theme_secondary : sub_tint,
                     TextAlign::CENTER, fit_sub.c_str());
  }

  const int mode_top = hero_y0 + hero_h + std::max(6, dp_px(10));
  HomeHubPillGeom geoms[6]{};
  const int geom_cnt = compute_home_hub_pill_deck(
      mode_cnt, mode_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      use_two_rows, geoms, LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);

  auto color_for_clim_code = [&](int code) -> Color {
    switch (code) {
      case 0: return ctx.col_orange;
      case 1: return ctx.col_accent_cyan;
      case 2: return ctx.col_green;
      case 4: return ctx.theme_primary;
      case 5: return ctx.col_warm_gold;
      case 3:
      default: return ctx.col_slate;
    }
  };

  int codes[6]{};
  for (int i = 0; i < geom_cnt; ++i) {
    codes[i] = mode_specs[i].code;
    const Color mc = color_for_clim_code(mode_specs[i].code);
    const bool act = mode_specs[i].active;
    ui::action_pill_centered(
        it, geoms[i].cx, geoms[i].w, geoms[i].cy, geoms[i].h,
        act ? mc : ui::chrome_card_brd_off(),
        act ? mc : ui::chrome_card_bg_off(),
        mode_specs[i].label,
        act ? ctx.col_white : ctx.col_slate,
        ctx.font_small, WidgetId::NONE);
  }
  register_home_hub_pill_deck_hits(frame_hits(), WidgetId::CLIMATE_MODE_BASE, codes, geoms, geom_cnt);
}

}  // namespace latter_ui
