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

int band_safe_w(int cy_band, int h_band, int inset = 14) {
  const int half_h = std::max(1, h_band / 2);
  const int y_top = std::clamp(cy_band - half_h, 0, SCREEN_H - 1);
  const int y_bot = std::clamp(cy_band + half_h, 0, SCREEN_H - 1);
  return std::min(safe_span(y_top, inset), safe_span(y_bot, inset));
}

void draw_shadowed_text(esphome::display::Display &it, int x, int y,
                        esphome::display::BaseFont *font, esphome::Color col,
                        esphome::display::TextAlign align, const char *text) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  fast_print(it, x + 1, y + 1, font, kChromeShadow, align, text);
  fast_print(it, x, y, font, col, align, text);
}

void render_passive_empty_label(esphome::display::Display &it, int cx, int cy,
                                const char *text, esphome::display::BaseFont *font,
                                esphome::Color text_c) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  const int max_w = std::max(64, band_safe_w(cy, 28, 16));
  const std::string fit = fit_text_to_width_font(std::string(text), max_w, font);
  draw_shadowed_text(it, cx, cy, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
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

void draw_frosted_pill(esphome::display::Display &it, int cx_pill, int cy_pill,
                       int w_pill, int h_pill, int r_pill,
                       esphome::Color border_c, esphome::Color bg_c,
                       const char *label, esphome::Color text_c,
                       esphome::display::BaseFont *font,
                       uint16_t widget_id = WidgetId::NONE,
                       int16_t param = 0,
                       int hit_w = 0, int hit_h = 0) {
  const int eff_w = std::max(16, w_pill);
  const int eff_h = std::max(16, h_pill);
  const int eff_r = std::clamp(r_pill, 4, std::min(eff_w, eff_h) / 2);
  const int x0 = cx_pill - eff_w / 2;
  const int y0 = cy_pill - eff_h / 2;
  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, border_c, eff_r, GlassVariant::Clear, text_c);
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const std::string fit = fit_text_to_width_font(std::string(label), std::max(24, eff_w - 10), font);
    draw_shadowed_text(it, cx_pill, cy_pill, font, text_c,
                       esphome::display::TextAlign::CENTER, fit.c_str());
  }
  if (widget_id != WidgetId::NONE) {
    const int hw = std::max(eff_w, hit_w);
    const int hh = std::max(eff_h, hit_h);
    frame_hits().add_centered_rect(cx_pill, cy_pill, hw, hh, widget_id,
                                   kHitTap | kHitFocusSlot, param);
  }
}

void render_framed_camera_preview(esphome::display::Display &it, ArtImageView img,
                                  int x0, int y0, int w, int h, int corner_r) {
  if (!img.valid() || w <= 8 || h <= 8) return;
  const int src_w = img.width;
  const int src_h = img.height;
  const int step_x = (src_w << 12) / w;
  const int step_y = (src_h << 12) / h;
  const int step_fp = std::max(1, std::min(step_x, step_y));
  const int vis_w_fp = w * step_fp;
  const int vis_h_fp = h * step_fp;
  const int src_x0_fp = std::max(0, ((src_w << 12) - vis_w_fp) / 2);
  const int src_y0_fp = std::max(0, ((src_h << 12) - vis_h_fp) / 2);

  uint16_t sx_lut[SCREEN_W];
  uint16_t row_px[SCREEN_W];
  const int cl_w = std::min(w, SCREEN_W);
  int cur_x = src_x0_fp;
  for (int i = 0; i < cl_w; ++i) {
    sx_lut[i] = static_cast<uint16_t>(std::clamp(cur_x >> 12, 0, src_w - 1));
    cur_x += step_fp;
  }

  const int eff_r = std::clamp(corner_r, 0, std::min(cl_w, h) / 2);
  int cur_y = src_y0_fp;
  for (int ry = 0; ry < h; ++ry) {
    const int py = y0 + ry;
    const int sy = std::clamp(cur_y >> 12, 0, src_h - 1);
    cur_y += step_fp;
    if (!row_in_clip_band(py)) continue;

    int inset = 0;
    if (eff_r > 0) {
      int dy = 0;
      if (ry < eff_r) {
        dy = eff_r - ry;
      } else if (ry >= h - eff_r) {
        dy = ry - (h - 1 - eff_r);
      }
      if (dy > 0) {
        const int chord = chord_half_span_constexpr(eff_r, dy, false);
        inset = std::max(0, eff_r - chord);
      }
    }
    const int span_w = cl_w - 2 * inset;
    if (span_w <= 0) continue;
    const uint16_t *src_row = img.data + sy * src_w;
    for (int i = 0; i < span_w; ++i) {
      row_px[i] = src_row[sx_lut[inset + i]];
    }
    blit_rgb565_scanline(it, x0 + inset, py, span_w, row_px, false);
  }
}

void render_home_hub_mode_bar(esphome::display::Display &it, const UiFrameContext &ctx,
                              const int *tabs, int tab_count, int active_slot) {
  using esphome::Color;
  const int cx = ctx.cx;
  home_hub_mode_bar_page() = get_mode_bar_start_index(tab_count, home_hub_mode_bar_page());
  const int start_idx = home_hub_mode_bar_page();
  const int vis_count = std::min(4, tab_count - start_idx);
  if (vis_count <= 0) return;

  int win_xs[4] = {0, 0, 0, 0};
  bool has_l = false;
  bool has_r = false;
  get_mode_bar_window_xs(tab_count, start_idx, cx, win_xs, has_l, has_r);

  const int active_vis_idx =
      (active_slot >= start_idx && active_slot < start_idx + vis_count)
          ? (active_slot - start_idx)
          : -1;
  render_in_app_mode_bar_track(it, cx, win_xs, vis_count, active_vis_idx,
                               has_l, has_r, tab_count, active_slot,
                               ctx.theme_primary, ctx.col_white, start_idx);

  const int by = layout().app_dock_y;
  const int ir = ui::dock_icon_r(layout().dock_r);
  const int left_cx = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride = get_dock_slot_stride(win_xs, vis_count);
  const float anim_start = mode_bar_anim().cur_start;

  for (int g_idx = 0; g_idx < tab_count; ++g_idx) {
    const float rel_slot = static_cast<float>(g_idx) - anim_start;
    if (rel_slot < -0.55f || rel_slot > static_cast<float>(vis_count - 1) + 0.55f) continue;
    const int bx = left_cx + static_cast<int>(std::roundf(rel_slot * static_cast<float>(stride)));
    if (bx < left_cx - scale_x(16) || bx > right_cx + scale_x(16)) continue;

    const bool is_act = (g_idx == active_slot);
    const Color raw_c = is_act ? ctx.theme_primary : ctx.col_white;
    float alpha = 1.0f;
    if (rel_slot < 0.0f) {
      alpha = std::max(0.25f, 1.0f + rel_slot * 1.4f);
    } else if (rel_slot > static_cast<float>(vis_count - 1)) {
      alpha = std::max(0.25f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.4f);
    }
    const Color fg_c(static_cast<uint8_t>(16 + (raw_c.r - 16) * alpha),
                     static_cast<uint8_t>(20 + (raw_c.g - 20) * alpha),
                     static_cast<uint8_t>(27 + (raw_c.b - 27) * alpha));
    const bool sh = (alpha > 0.65f);

    switch (tabs[g_idx]) {
      case HomeHubTab::ALARM:
        ui::draw_icon_shield_home(it, bx, by, ir, fg_c, sh);
        break;
      case HomeHubTab::CAMERAS:
        ui::draw_icon_camera(it, bx, by, ir, fg_c, sh);
        break;
      case HomeHubTab::INTERCOM:
        ui::draw_icon_speaker(it, bx, by, ir, fg_c, true, sh);
        break;
      case HomeHubTab::THERMOSTAT:
        ui::draw_icon_thermometer(it, bx, by, ir, fg_c, sh);
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
}

void render_alarm_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                      const DeviceRuntimeState &rs, const AreaContextState &ac,
                      int content_top, int content_bot) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  render_page_header(it, cx, "SECURITY ALARM", ctx.font_small, ctx.theme_primary);
  if (ac.alarm_entity.empty()) {
    render_passive_empty_label(it, cx, (content_top + content_bot) / 2,
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
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  const int avail_h = std::max(140, content_bot - content_top);
  const int hero_h = use_two_rows
      ? std::clamp((avail_h * 44) / 100, 66, scale_y(104))
      : std::clamp((avail_h * 56) / 100, 82, scale_y(132));
  const int hero_y0 = content_top;

  // D-048: Status readout rendered directly over wallpaper with 1px kChromeShadow (never inside a glass card).
  const int icon_cy = hero_y0 + (is_fb ? (hero_h * 25) / 100 : (hero_h * 32) / 100);
  const int st_y = hero_y0 + (is_fb ? (hero_h * 60) / 100 : (hero_h * 72) / 100);
  ui::draw_icon_shield_home(it, cx, icon_cy, ui::hero_icon_r(hero_h), badge_col, true);

  auto *st_font = (SCREEN_H < 400) ? ctx.font_btn_bold : ctx.font_date;
  const int st_max_w = std::max(80, band_safe_w(st_y, 32, 16));
  const std::string fit_badge = fit_text_to_width_font(std::string(badge_txt), st_max_w, st_font);
  draw_shadowed_text(it, cx, st_y, st_font, badge_col, TextAlign::CENTER, fit_badge.c_str());

  if (is_fb) {
    auto *fb_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    const int fb_y = hero_y0 + (hero_h * 88) / 100;
    const int fb_max_w = std::max(80, band_safe_w(fb_y, 24, 16));
    const std::string fit_fb = fit_text_to_width_font(rs.area.action_feedback, fb_max_w, fb_font);
    draw_shadowed_text(it, cx, fb_y, fb_font, ctx.theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  }

  const int deck_top = hero_y0 + hero_h + std::max(6, scale_y(10));
  HomeHubPillGeom geoms[4]{};
  const int geom_cnt = compute_home_hub_pill_deck(
      act_cnt, deck_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      use_two_rows, geoms);

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
    const bool use_short = (!use_two_rows && geom_cnt >= 3 && geoms[i].w < scale_x(92));
    draw_frosted_pill(
        it, geoms[i].cx, geoms[i].cy, geoms[i].w, geoms[i].h,
        is_disarm_cta ? (geoms[i].h / 2) : std::min(16, geoms[i].h / 2),
        acc,
        is_disarm_cta ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
        use_short ? specs[i].short_label : specs[i].label,
        ctx.col_white,
        (geom_cnt == 1 && SCREEN_W >= 360) ? ctx.font_btn_bold : ctx.font_small);
  }
  register_home_hub_pill_deck_hits(frame_hits(), WidgetId::ALARM_MODE_BASE, codes, geoms, geom_cnt);
}

void render_camera_action_pills(esphome::display::Display &it, const UiFrameContext &ctx,
                                int row_cy, int max_row_h, bool has_lock_or_garage) {
  using esphome::Color;

  struct PillSpec {
    const char *label;
    Color border;
    Color bg;
    Color text;
    uint16_t wid;
  };

  const bool in_call = cam_call_active();
  const bool is_listening = cam_listen_active();
  PillSpec specs[4]{};
  int count = 0;

  if (in_call) {
    specs[count++] = {"End", ctx.col_red, ui::chrome_danger_bg(), ctx.col_white, WidgetId::CAM_LISTEN_BTN};
  } else {
    specs[count++] = {
        is_listening ? "Mute" : "Listen",
        is_listening ? ctx.theme_primary : ui::chrome_card_brd_off(),
        is_listening ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
        is_listening ? ctx.col_white : ctx.theme_secondary,
        WidgetId::CAM_LISTEN_BTN};
  }

#if LATTER_HAS_MIC && LATTER_HAS_SPEAKER
  {
    const bool speaking = cam_intercom_ptt_active();
    const bool pulse = ((ctx.now_ms / 300U) % 2U) != 0U;
    specs[count++] = {
        speaking ? "Stop" : "Talk",
        speaking ? (pulse ? ctx.col_white : ctx.col_red) : ctx.col_green,
        speaking ? ui::chrome_danger_bg() : ui::chrome_card_bg_on(),
        ctx.col_white,
        WidgetId::CAM_PTT_BTN};
  }
#endif

  specs[count++] = {
      "Reply",
      ui::chrome_card_brd_off(),
      ui::chrome_card_bg_off(),
      ctx.theme_secondary,
      WidgetId::CAM_REPLY_MENU_BTN};

  if (has_lock_or_garage) {
    specs[count++] = {
        "Unlock",
        ctx.col_warm_gold,
        ui::chrome_card_bg_on(),
        ctx.col_white,
        WidgetId::CAM_UNLOCK_BTN};
  }

  const int min_hit = UiLayout::mm_to_px(7.0f);
  const int pill_h = std::clamp(max_row_h, 30, scale_y(40));
  const int hit_h = std::max(pill_h, std::min(min_hit, max_row_h + 10));
  const int gap = std::max(5, scale_x(6));
  const int row_w = std::min(scale_x(336), band_safe_w(row_cy, pill_h, 12));
  const int pill_w = std::max(44, (row_w - (count - 1) * gap) / count);
  const int total_w = count * pill_w + (count - 1) * gap;
  const int x_left = ctx.cx - total_w / 2;
  auto *p_font = (count >= 4 && pill_w < 64) ? ctx.font_micro : ctx.font_small;

  for (int i = 0; i < count; ++i) {
    const int pcx = x_left + i * (pill_w + gap) + pill_w / 2;
    draw_frosted_pill(it, pcx, row_cy, pill_w, pill_h, pill_h / 2,
                      specs[i].border, specs[i].bg,
                      specs[i].label, specs[i].text, p_font,
                      specs[i].wid, 0, pill_w, hit_h);
  }
}

void render_camera_selector_pill(esphome::display::Display &it, const UiFrameContext &ctx,
                                 const std::string *names, int cam_count, int hdr_cy,
                                 bool show_orient_btn) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  const int min_hit = UiLayout::mm_to_px(7.0f);
  const int pill_h = (SCREEN_H < 400) ? 28 : scale_y(34);
  const int safe_w = band_safe_w(hdr_cy, pill_h, 12);

  int orient_reserve = 0;
#if !LATTER_ROUND_DISPLAY
  if (show_orient_btn && SCREEN_W != SCREEN_H) {
    orient_reserve = scale_x(56);
  }
#else
  (void)show_orient_btn;
#endif

  const int hdr_w = std::max(120, std::min(scale_x(264), safe_w - orient_reserve));
  const int hdr_cx = cx - orient_reserve / 2;

  std::string title;
  if (!cam_intercom_status().empty()) {
    title = cam_intercom_status();
  } else if (cam_call_active()) {
    title = "Doorbell Live";
  } else if (cam_count > 1) {
    title = "Cam " + std::to_string(cam_carousel_idx() + 1) + "/" +
            std::to_string(cam_count) + " \xC2\xB7 " + names[cam_carousel_idx()];
  } else if (cam_count == 1 && !names[0].empty()) {
    title = names[0];
  } else {
    title = "Camera";
  }

  const Color hdr_brd = cam_intercom_ptt_active()
      ? ctx.col_red
      : (cam_call_active() ? ctx.col_green : ctx.theme_primary);

  if (cam_count > 1) {
    render_card_box(it, hdr_cx - hdr_w / 2, hdr_cy - pill_h / 2, hdr_w, pill_h,
                    ui::chrome_card_bg_on(), hdr_brd, pill_h / 2, GlassVariant::Clear, ctx.col_white);

    const int arrow_pad = scale_x(22);
    const std::string fit_t = fit_text_to_width_font(title, std::max(48, hdr_w - 2 * arrow_pad), ctx.font_small);
    draw_shadowed_text(it, hdr_cx, hdr_cy, ctx.font_small, ctx.col_white, TextAlign::CENTER, fit_t.c_str());

    const int lx = hdr_cx - hdr_w / 2 + scale_x(12);
    const int rx = hdr_cx + hdr_w / 2 - scale_x(12);
    draw_shadowed_text(it, lx, hdr_cy, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, "<");
    draw_shadowed_text(it, rx, hdr_cy, ctx.font_small, ctx.theme_secondary, TextAlign::CENTER, ">");
    const int half_hit_w = std::max(min_hit, hdr_w / 2);
    const int hit_h = std::max(pill_h + 8, std::min(min_hit, scale_y(48)));
    frame_hits().add_rect(hdr_cx - half_hit_w, hdr_cy - hit_h / 2, half_hit_w, hit_h,
                          WidgetId::CAM_PREV_BTN, kHitTap | kHitFocusSlot, -1);
    frame_hits().add_rect(hdr_cx, hdr_cy - hit_h / 2, half_hit_w, hit_h,
                          WidgetId::CAM_NEXT_BTN, kHitTap | kHitFocusSlot, 1);
  } else {
    // Single camera (no </> switching action): shadowed title directly over canvas, zero fake glass pill.
    const std::string fit_t = fit_text_to_width_font(title, std::max(64, hdr_w - scale_x(12)), ctx.font_small);
    draw_shadowed_text(it, hdr_cx, hdr_cy, ctx.font_small,
                       cam_intercom_ptt_active() ? ctx.col_white : hdr_brd,
                       TextAlign::CENTER, fit_t.c_str());
  }

#if !LATTER_ROUND_DISPLAY
  if (show_orient_btn && SCREEN_W != SCREEN_H) {
    const int ob_w = scale_x(48);
    const int ob_cx = hdr_cx + hdr_w / 2 + scale_x(6) + ob_w / 2;
    draw_frosted_pill(it, ob_cx, hdr_cy, ob_w, pill_h, pill_h / 2,
                      cam_landscape_mode() ? ctx.theme_primary : ui::chrome_card_brd_off(),
                      cam_landscape_mode() ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                      "90\xC2\xB0",
                      cam_landscape_mode() ? ctx.col_white : ctx.theme_secondary,
                      ctx.font_small, WidgetId::CAM_ORIENT_BTN, 0,
                      std::max(ob_w, min_hit), std::max(pill_h, std::min(min_hit, scale_y(46))));
  }
#endif
}

bool render_cameras_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                        const DeviceRuntimeState &rs, const AreaContextState &ac,
                        int content_top, int content_bot) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  std::string ids[MAX_CAMERA_SLOTS], names[MAX_CAMERA_SLOTS];
  const int cam_count = parse_cameras_summary_list(ac.cameras_summary, ids, names);
  if (cam_count > 0 && (cam_carousel_idx() < 0 || cam_carousel_idx() >= cam_count)) {
    cam_carousel_idx() = 0;
  }

  const bool has_lock_or_garage = !ac.lock_entity.empty() || !ac.garage_entity.empty();
  const bool has_url = !ac.camera_snapshot_url.empty() || (cam_count > 0);
  const bool has_frame = has_url && ctx.camera_snapshot_art.valid() &&
                         ctx.camera_snapshot_art.width > 16 &&
                         ctx.camera_snapshot_art.height > 16;

  // 1. Quick-Reply Sheet (full-takeover sub-sheet with < Back, no mode bar)
  if (cam_quick_reply_open()) {
    render_page_header(it, cx, "QUICK REPLY", ctx.font_small, ctx.theme_primary);
    static constexpr const char *kReplyLabels[3] = {
        "On My Way",
        "Leave at Door",
        "One Moment",
    };
    const int row_cnt = has_lock_or_garage ? 4 : 3;
    const int sheet_top = layout().hdr_y + ((SCREEN_H < 400) ? 22 : scale_y(30));
    const int sheet_bot = layout().back_y - ((SCREEN_H < 400) ? 20 : scale_y(26));
    const int stride = std::max(38, (sheet_bot - sheet_top) / row_cnt);
    const int card_h = std::clamp(stride - 6, 34, scale_y(48));
    const int min_hit = UiLayout::mm_to_px(7.0f);
    const int hit_h = std::max(card_h, std::min(min_hit, stride));

    for (int i = 0; i < 3; ++i) {
      const int cy_r = sheet_top + i * stride + stride / 2;
      const int w_r = std::min(scale_x(304), band_safe_w(cy_r, card_h, 14));
      draw_frosted_pill(it, cx, cy_r, w_r, card_h, std::min(14, card_h / 2),
                        ctx.theme_primary, ui::chrome_card_bg_on(),
                        kReplyLabels[i], ctx.col_white, ctx.font_small,
                        static_cast<uint16_t>(WidgetId::CAM_REPLY_BASE + i),
                        static_cast<int16_t>(i), w_r, hit_h);
    }
    if (has_lock_or_garage) {
      const int cy_u = sheet_top + 3 * stride + stride / 2;
      const int w_u = std::min(scale_x(304), band_safe_w(cy_u, card_h, 14));
      const char *u_lbl = !ac.lock_entity.empty() ? "Unlock Entry" : "Open Garage";
      draw_frosted_pill(it, cx, cy_u, w_u, card_h, std::min(14, card_h / 2),
                        ctx.col_warm_gold, ui::chrome_card_bg_on(),
                        u_lbl, ctx.col_white, ctx.font_small,
                        WidgetId::CAM_UNLOCK_BTN, 0, w_u, hit_h);
    }
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back", ctx.col_white);
    return false;
  }

  // 2. Expanded Full-Bleed Camera View (< Back collapses back to normal CAMERAS tab)
  if (cam_expanded_fullscreen()) {
    if (has_frame) {
      render_album_art_canvas(it, ctx.camera_snapshot_art, cx, false, false, false, false);
    } else {
      if (!frame_base_state().painted) {
        frame_base(it, ctx.col_bg);
      }
      render_passive_empty_label(
          it, cx, ctx.cy,
          has_url ? "Loading camera snapshot..." : "No Camera Configured",
          ctx.font_small, has_url ? ctx.theme_secondary : ctx.col_slate);
    }

    const int hdr_cy = (SCREEN_H < 400) ? 34 : scale_y(42);
    render_camera_selector_pill(it, ctx, names, cam_count, hdr_cy, has_url);

    const int deck_cy = layout().back_y - ((SCREEN_H < 400) ? 36 : scale_y(48));
    if (has_url) {
      render_camera_action_pills(it, ctx, deck_cy, (SCREEN_H < 400) ? 32 : scale_y(38), has_lock_or_garage);
    }

    // Center canvas tap collapses expanded full-bleed mode back to framed tab mode
    const int canvas_y0 = hdr_cy + scale_y(28);
    const int canvas_y1 = deck_cy - scale_y(28);
    if (canvas_y1 > canvas_y0 + 24) {
      const int cw = band_safe_w((canvas_y0 + canvas_y1) / 2, canvas_y1 - canvas_y0, 16);
      frame_hits().add_rect(cx - cw / 2, canvas_y0, cw, canvas_y1 - canvas_y0,
                            WidgetId::CAM_SNAPSHOT_CARD, kHitTap, 0);
    }

    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back", ctx.col_white);
    return false;
  }

  // 3. Normal Framed Tab Mode (with bottom 4-slot Home Hub mode bar)
  if (!has_url) {
    render_page_header(it, cx, "CAMERAS", ctx.font_small, ctx.theme_primary);
    const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());
    render_passive_empty_label(
        it, cx, (content_top + content_bot) / 2,
        is_fb ? rs.area.action_feedback.c_str() : "No cameras configured",
        ctx.font_small, is_fb ? ctx.theme_secondary : ctx.col_slate);
    return true;
  }

  const int hdr_cy = layout().hdr_y + ((SCREEN_H < 400) ? 4 : scale_y(4));
  if (cam_count > 1) {
    render_camera_selector_pill(it, ctx, names, cam_count, hdr_cy, false);
  } else {
    std::string hdr_txt = "CAMERAS";
    Color hdr_col = ctx.theme_primary;
    if (!cam_intercom_status().empty()) {
      hdr_txt = to_ascii_upper_str(cam_intercom_status());
      hdr_col = ctx.theme_secondary;
    } else if (cam_call_active()) {
      hdr_txt = "DOORBELL LIVE";
      hdr_col = ctx.theme_secondary;
    } else if (cam_count == 1 && !names[0].empty()) {
      hdr_txt = to_ascii_upper_str(names[0]);
    }
    render_page_header(it, cx, hdr_txt, ctx.font_small, hdr_col, true);
  }

  const int top_y = (cam_count > 1)
      ? (hdr_cy + ((SCREEN_H < 400) ? 20 : scale_y(24)))
      : content_top;
  const int pill_row_h = (SCREEN_H < 400) ? 32 : scale_y(38);
  const int pill_cy = content_bot - pill_row_h / 2;
  const int frame_bot = pill_cy - pill_row_h / 2 - std::max(6, scale_y(8));
  const int frame_h = std::max(64, frame_bot - top_y);
  const int frame_cy = top_y + frame_h / 2;
  const int frame_w = std::min(scale_x(328), band_safe_w(frame_cy, frame_h, 14));
  const int frame_x0 = cx - frame_w / 2;
  const int frame_y0 = frame_cy - frame_h / 2;
  const int frame_r = std::max(12, scale_s(16));

  render_card_box(it, frame_x0, frame_y0, frame_w, frame_h,
                  ui::chrome_card_bg_on(),
                  cam_call_active() ? ctx.col_green : ui::chrome_card_brd_on(),
                  frame_r, GlassVariant::Clear, ctx.col_white);

  if (has_frame) {
    render_framed_camera_preview(it, ctx.camera_snapshot_art,
                                 frame_x0 + 2, frame_y0 + 2,
                                 frame_w - 4, frame_h - 4,
                                 std::max(4, frame_r - 2));
    const int tag_w = scale_x(68);
    const int tag_h = (SCREEN_H < 400) ? 20 : scale_y(22);
    const int tag_x0 = frame_x0 + frame_w - tag_w - 8;
    const int tag_y0 = frame_y0 + frame_h - tag_h - 6;
    if (tag_x0 > frame_x0 + 8 && tag_y0 > frame_y0 + 8) {
      render_card_box(it, tag_x0, tag_y0, tag_w, tag_h,
                      kChromeGlassTint, ui::chrome_card_brd_off(),
                      tag_h / 2, GlassVariant::Clear, ctx.col_white);
      draw_shadowed_text(it, tag_x0 + tag_w / 2, tag_y0 + tag_h / 2,
                         ctx.font_micro, ctx.col_white, TextAlign::CENTER, "Expand");
    }
  } else {
    const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());
    const char *msg = is_fb ? rs.area.action_feedback.c_str() : "Loading live snapshot...";
    const std::string fit_m = fit_text_to_width_font(std::string(msg), std::max(60, frame_w - 20), ctx.font_small);
    draw_shadowed_text(it, cx, frame_cy, ctx.font_small, ctx.theme_secondary,
                       TextAlign::CENTER, fit_m.c_str());
  }

  frame_hits().add_rect(frame_x0, frame_y0, frame_w, frame_h,
                        WidgetId::CAM_SNAPSHOT_CARD, kHitTap | kHitFocusSlot, 0);
  render_camera_action_pills(it, ctx, pill_cy, pill_row_h, has_lock_or_garage);
  return true;
}

void render_intercom_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                         const DeviceRuntimeState &rs, const AreaContextState &ac,
                         int content_top, int content_bot) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  const bool show_fb = !rs.intercom.broadcast_feedback.empty() &&
                       (ctx.now_ms - rs.intercom.broadcast_feedback_ms < 2200U);
  if (show_fb) {
    const std::string fit_fb = fit_text_to_width_font(
        rs.intercom.broadcast_feedback, safe_span(layout().hdr_y, 16), ctx.font_small);
    render_page_header(it, cx, fit_fb, ctx.font_small, ctx.theme_secondary);
  } else {
    render_page_header(it, cx, "INTERCOM", ctx.font_small, ctx.theme_primary);
  }

  const int min_hit = UiLayout::mm_to_px(7.0f);
  const bool live_mode = intercom_live_call_mode();
  const bool has_lock_or_garage = !ac.lock_entity.empty() || !ac.garage_entity.empty();

  // Top Segmented Toggle Pill: Broadcast | Call Display
  const int seg_h = (SCREEN_H < 400) ? 28 : scale_y(34);
  const int seg_cy = content_top + seg_h / 2;
  const int seg_gap = std::max(4, scale_x(6));
  const int seg_total_w = std::min(scale_x(296), band_safe_w(seg_cy, seg_h, 14));
  const int seg_w = std::max(56, (seg_total_w - seg_gap) / 2);
  const int seg_hit_h = std::max(seg_h, std::min(min_hit, seg_h + 10));

  draw_frosted_pill(it, cx - (seg_w + seg_gap) / 2, seg_cy, seg_w, seg_h, seg_h / 2,
                    !live_mode ? ctx.theme_primary : ui::chrome_card_brd_off(),
                    !live_mode ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                    "Broadcast", !live_mode ? ctx.col_white : ctx.col_slate,
                    ctx.font_small, WidgetId::INTERCOM_SEG_BROADCAST, 0, seg_w, seg_hit_h);
  draw_frosted_pill(it, cx + (seg_w + seg_gap) / 2, seg_cy, seg_w, seg_h, seg_h / 2,
                    live_mode ? ctx.theme_primary : ui::chrome_card_brd_off(),
                    live_mode ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
                    "Call Display", live_mode ? ctx.col_white : ctx.col_slate,
                    ctx.font_small, WidgetId::INTERCOM_SEG_CALL, 1, seg_w, seg_hit_h);

  const int unlock_h = has_lock_or_garage ? ((SCREEN_H < 400) ? 28 : scale_y(34)) : 0;
  const int unlock_cy = content_bot - unlock_h / 2;
  const int rows_top = seg_cy + seg_h / 2 + std::max(6, scale_y(8));
  const int rows_bot = has_lock_or_garage ? (unlock_cy - unlock_h / 2 - std::max(5, scale_y(6))) : content_bot;
  const int rows_avail = std::max(90, rows_bot - rows_top);

  if (!live_mode) {
    static constexpr const char *kBroadcastLabels[3] = {
        "Dinner Is Ready",
        "Come Upstairs",
        "Leaving Soon",
    };
    const int stride = rows_avail / 3;
    const int card_h = std::clamp(stride - 5, 30, scale_y(46));
    const int hit_h = std::max(card_h, std::min(min_hit, stride));
    for (int i = 0; i < 3; ++i) {
      const int cy_r = rows_top + i * stride + stride / 2;
      const int w_r = std::min(scale_x(312), band_safe_w(cy_r, card_h, 14));
      const int x0 = cx - w_r / 2;
      render_card_box(it, x0, cy_r - card_h / 2, w_r, card_h,
                      ui::chrome_card_bg_off(), ui::chrome_card_brd_on(),
                      std::min(12, card_h / 2), GlassVariant::Clear, ctx.col_white);
      const int spk_r = std::clamp(card_h / 3, 9, scale_s(13));
      ui::draw_icon_speaker(it, x0 + scale_x(22), cy_r, spk_r, ctx.theme_primary, true, true);
      const std::string fit_l = fit_text_to_width_font(
          std::string(kBroadcastLabels[i]), std::max(60, w_r - scale_x(54)), ctx.font_small);
      draw_shadowed_text(it, x0 + scale_x(42), cy_r, ctx.font_small, ctx.col_white,
                         TextAlign::CENTER_LEFT, fit_l.c_str());
      frame_hits().add_centered_rect(cx, cy_r, w_r, hit_h,
                                     static_cast<uint16_t>(WidgetId::INTERCOM_ROW_BASE + i),
                                     kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
  } else if (cam_call_active()) {
    // D-048: Print passive "Intercom Call Active" directly over wallpaper with kChromeShadow;
    // glass is reserved exclusively for the interactive "End Call" button.
    const int mid_cy = rows_top + rows_avail / 2;
    const int st_y = mid_cy - std::max(16, rows_avail / 5);
    auto *st_font = (SCREEN_H < 400) ? ctx.font_btn_bold : ctx.font_date;
    const std::string st_txt = cam_intercom_status().empty()
        ? std::string("Intercom Call Active")
        : cam_intercom_status();
    const std::string fit_st = fit_text_to_width_font(
        st_txt, std::max(80, band_safe_w(st_y, 28, 16)), st_font);
    draw_shadowed_text(it, cx, st_y, st_font, ctx.col_green, TextAlign::CENTER, fit_st.c_str());

    const int btn_cy = mid_cy + std::max(18, rows_avail / 4);
    const int end_h = std::clamp(rows_avail / 3 + 4, 34, scale_y(46));
    const int end_w = std::min(scale_x(164), band_safe_w(btn_cy, end_h, 16));
    draw_frosted_pill(it, cx, btn_cy, end_w, end_h, end_h / 2,
                      ctx.col_red, ui::chrome_danger_bg(), "End Call", ctx.col_white,
                      ctx.font_small, WidgetId::CAM_LISTEN_BTN, 0,
                      std::max(end_w, min_hit), std::max(end_h, std::min(min_hit, rows_avail / 2)));
  } else {
    std::string t_ids[5], t_lbls[5];
    bool t_all[5] = {false, false, false, false, false};
    const int cnt = std::min(3, parse_intercom_targets_list(ac.intercom_targets_summary, t_ids, t_lbls, t_all));
    if (cnt <= 0) {
      render_passive_empty_label(it, cx, rows_top + rows_avail / 2,
                                 "No Peer Displays", ctx.font_small, ctx.col_slate);
    } else {
      const int stride = rows_avail / 3;
      const int card_h = std::clamp(stride - 5, 30, scale_y(46));
      const int hit_h = std::max(card_h, std::min(min_hit, stride));
      for (int i = 0; i < cnt; ++i) {
        const int cy_r = rows_top + i * stride + stride / 2;
        const int w_r = std::min(scale_x(312), band_safe_w(cy_r, card_h, 14));
        const int x0 = cx - w_r / 2;
        const Color acc = t_all[i] ? ctx.theme_secondary : ctx.col_green;
        render_card_box(it, x0, cy_r - card_h / 2, w_r, card_h,
                        ui::chrome_card_bg_off(), acc,
                        std::min(12, card_h / 2), GlassVariant::Clear, ctx.col_white);
        fast_filled_circle_aa(it, x0 + scale_x(18), cy_r, std::max(4, scale_s(5)), acc);
        const std::string fit_l = fit_text_to_width_font(
            t_lbls[i], std::max(60, w_r - scale_x(64)), ctx.font_small);
        draw_shadowed_text(it, x0 + scale_x(34), cy_r, ctx.font_small, ctx.col_white,
                           TextAlign::CENTER_LEFT, fit_l.c_str());
        draw_shadowed_text(it, x0 + w_r - scale_x(14), cy_r, ctx.font_micro,
                           t_all[i] ? ctx.theme_secondary : ctx.col_white,
                           TextAlign::CENTER_RIGHT, "Call");
        frame_hits().add_centered_rect(cx, cy_r, w_r, hit_h,
                                       static_cast<uint16_t>(WidgetId::INTERCOM_ROW_BASE + i),
                                       kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
      }
    }
  }

  if (has_lock_or_garage) {
    const int uw = std::min(scale_x(164), band_safe_w(unlock_cy, unlock_h, 18));
    const char *u_lbl = !ac.lock_entity.empty() ? "Unlock Entry" : "Open Garage";
    draw_frosted_pill(it, cx, unlock_cy, uw, unlock_h, unlock_h / 2,
                      ctx.col_warm_gold, ui::chrome_card_bg_on(),
                      u_lbl, ctx.col_white, ctx.font_small,
                      WidgetId::INTERCOM_UNLOCK_BTN, 0,
                      std::max(uw, min_hit), std::max(unlock_h, std::min(min_hit, unlock_h + 8)));
  }
}

void render_thermostat_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                           const DeviceRuntimeState &rs, const AreaContextState &ac,
                           int content_top, int content_bot) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  render_page_header(it, cx, "THERMOSTAT", ctx.font_small, ctx.theme_primary);
  if (ac.thermostat_entity.empty()) {
    render_passive_empty_label(it, cx, (content_top + content_bot) / 2,
                               "No Thermostat configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const std::string &m = home_hub_thermostat_mode();
  const uint16_t eff_caps = effective_climate_caps(ac);
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
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI);

  const int avail_h = std::max(140, content_bot - content_top);
  const int hero_h = use_two_rows
      ? std::clamp((avail_h * 46) / 100, 68, scale_y(108))
      : std::clamp((avail_h * 58) / 100, 84, scale_y(136));
  const int hero_cy = content_top + hero_h / 2 + std::max(2, scale_y(4));
  const int hero_w = std::min(scale_x(328), band_safe_w(hero_cy, hero_h, 14));
  const int hero_x0 = cx - hero_w / 2;
  const int hero_y0 = hero_cy - hero_h / 2;

  const int min_hit = UiLayout::mm_to_px(7.0f);
  const int btn_r = std::clamp(hero_h / 3, 18, scale_s(26));
  const int btn_hit_r = std::max(btn_r, std::min(min_hit / 2, hero_h / 2));
  const int minus_cx = hero_x0 + btn_r + std::max(8, scale_x(12));
  const int plus_cx = hero_x0 + hero_w - btn_r - std::max(8, scale_x(12));

  // D-048: Glass reserved strictly for interactive -/+ circles and HVAC mode pills;
  // temperature and current-reading text render directly over wallpaper with kChromeShadow.
  if (show_setpoint_btns) {
    const int gl_s = std::max(6, btn_r / 3);
    render_glass_circle(it, minus_cx, hero_cy, btn_r, ui::chrome_card_bg_off(), ui::chrome_card_brd_on());
    fast_hline(it, minus_cx - gl_s + 1, hero_cy + 1, 2 * gl_s + 1, kChromeShadow);
    fast_hline(it, minus_cx - gl_s, hero_cy, 2 * gl_s + 1, ctx.col_white);
    fast_hline(it, minus_cx - gl_s, hero_cy + 1, 2 * gl_s + 1, ctx.col_white);

    render_glass_circle(it, plus_cx, hero_cy, btn_r, ui::chrome_card_bg_off(), ui::chrome_card_brd_on());
    fast_hline(it, plus_cx - gl_s + 1, hero_cy + 1, 2 * gl_s + 1, kChromeShadow);
    fast_vline(it, plus_cx + 1, hero_cy - gl_s + 1, 2 * gl_s + 1, kChromeShadow);
    fast_hline(it, plus_cx - gl_s, hero_cy, 2 * gl_s + 1, ctx.col_white);
    fast_hline(it, plus_cx - gl_s, hero_cy + 1, 2 * gl_s + 1, ctx.col_white);
    fast_vline(it, plus_cx, hero_cy - gl_s, 2 * gl_s + 1, ctx.col_white);
    fast_vline(it, plus_cx + 1, hero_cy - gl_s, 2 * gl_s + 1, ctx.col_white);

    register_thermostat_stepper_hits(frame_hits(), true, minus_cx, plus_cx, hero_cy, btn_r, btn_hit_r);
  }

  char tgt_buf[24];
  if (is_off) {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "Off");
  } else if (home_hub_thermostat_target_f() > 0 && show_setpoint_btns) {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "%d\xC2\xB0", home_hub_thermostat_target_f());
  } else if (!show_setpoint_btns && !m.empty()) {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "%s", mode_lbl);
  } else {
    std::snprintf(tgt_buf, sizeof(tgt_buf), "%s\xC2\xB0", kUnknownGlyph);
  }

  const int inner_span = show_setpoint_btns
      ? std::max(64, (plus_cx - btn_r) - (minus_cx + btn_r) - 12)
      : std::max(80, hero_w - 16);
  auto *temp_font = (SCREEN_H < 400 || inner_span < 120) ? ctx.font_date : ctx.font_chrono_time;
  const int temp_y = hero_cy - std::max(8, hero_h / 7);
  draw_shadowed_text(it, cx, temp_y, temp_font, ctx.col_white, TextAlign::CENTER, tgt_buf);

  char sub_buf[48];
  const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());
  const float cur_t = home_hub_state().thermostat_current_f;
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
    auto *sub_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    const std::string fit_sub = fit_text_to_width_font(std::string(sub_buf), inner_span, sub_font);
    const int sub_y = hero_cy + std::max(14, hero_h / 4);
    draw_shadowed_text(it, cx, sub_y, sub_font, is_fb ? ctx.theme_secondary : sub_tint,
                       TextAlign::CENTER, fit_sub.c_str());
  }

  const int mode_top = hero_y0 + hero_h + std::max(6, scale_y(10));
  HomeHubPillGeom geoms[6]{};
  const int geom_cnt = compute_home_hub_pill_deck(
      mode_cnt, mode_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      use_two_rows, geoms);

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
    draw_frosted_pill(
        it, geoms[i].cx, geoms[i].cy, geoms[i].w, geoms[i].h,
        std::min(14, geoms[i].h / 2),
        act ? mc : ui::chrome_card_brd_off(),
        act ? mc : ui::chrome_card_bg_off(),
        mode_specs[i].label,
        act ? ctx.col_white : ctx.col_slate,
        ctx.font_small);
  }
  register_home_hub_pill_deck_hits(frame_hits(), WidgetId::CLIMATE_MODE_BASE, codes, geoms, geom_cnt);
}

void render_entry_card(esphome::display::Display &it, const UiFrameContext &ctx,
                       int cx, int cy, int card_w, int card_h, int hit_h,
                       bool is_lock, const char *title, const char *state_txt,
                       esphome::Color state_col, const char *cta_txt,
                       esphome::Color cta_col, uint16_t card_wid, uint16_t btn_wid) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int x0 = cx - card_w / 2;
  const int y0 = cy - card_h / 2;
  (void) render_card_box(it, x0, y0, card_w, card_h,
                         ui::chrome_card_bg_on(), state_col,
                         std::max(12, scale_s(16)), GlassVariant::Clear, ctx.col_white);

  const int ir = std::min(ui::card_icon_r(card_h), scale_s(20));
  const int icon_cx = x0 + ir + std::max(12, scale_x(16));
  if (is_lock) {
    const bool locked = (std::strcmp(state_txt, "Unlocked") != 0);
    ui::draw_icon_lock(it, icon_cx, cy, ir, state_col, locked, true);
  } else {
    const bool open = (std::strcmp(state_txt, "Open") == 0);
    ui::draw_icon_garage_door(it, icon_cx, cy, ir, state_col, open, true);
  }

  const int min_hit = UiLayout::mm_to_px(7.0f);
  const int pill_w = std::clamp(card_w / 3, std::max(64, std::min(min_hit, 76)), scale_x(98));
  const int pill_h = std::clamp(card_h - 16, 32, scale_y(44));
  const int pill_cx = x0 + card_w - pill_w / 2 - std::max(8, scale_x(10));

  const int txt_x0 = icon_cx + ir + std::max(10, scale_x(14));
  const int txt_max_w = std::max(54, (pill_cx - pill_w / 2 - 8) - txt_x0);
  auto *pri_font = (SCREEN_H < 400) ? ctx.font_small : ctx.font_btn_bold;
  auto *sec_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
  const std::string fit_pri = fit_text_to_width_font(std::string(title), txt_max_w, pri_font);
  const std::string fit_sec = fit_text_to_width_font(std::string(state_txt), txt_max_w, sec_font);
  draw_shadowed_text(it, txt_x0, cy - std::max(8, card_h / 6), pri_font,
                     ctx.col_white, TextAlign::CENTER_LEFT, fit_pri.c_str());
  draw_shadowed_text(it, txt_x0, cy + std::max(9, card_h / 5), sec_font,
                     state_col, TextAlign::CENTER_LEFT, fit_sec.c_str());

  const int split_x = pill_cx - pill_w / 2 - 4;
  const int left_w = std::max(40, split_x - x0);
  const int right_w = std::max(pill_w, (x0 + card_w) - split_x);
  frame_hits().add_rect(x0, cy - hit_h / 2, left_w, hit_h, card_wid, kHitTap | kHitFocusSlot, 0);
  draw_frosted_pill(it, pill_cx, cy, pill_w, pill_h, pill_h / 2,
                    cta_col, ui::chrome_card_bg_off(),
                    cta_txt, ctx.col_white, ctx.font_small,
                    btn_wid, 0, right_w, hit_h);
}

void render_entry_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                      const DeviceRuntimeState &rs, const AreaContextState &ac,
                      int content_top, int content_bot) {
  using esphome::Color;

  const int cx = ctx.cx;
  const bool is_fb = (ctx.now_ms - rs.area.feedback_timer < 1800U && !rs.area.action_feedback.empty());
  if (is_fb) {
    const std::string fit_fb = fit_text_to_width_font(
        rs.area.action_feedback, safe_span(layout().hdr_y, 16), ctx.font_small);
    render_page_header(it, cx, fit_fb, ctx.font_small, ctx.theme_secondary);
  } else {
    render_page_header(it, cx, "ENTRY & GARAGE", ctx.font_small, ctx.theme_primary);
  }

  const bool show_lock = !ac.lock_entity.empty();
  const bool show_garage = !ac.garage_entity.empty();
  if (!show_lock && !show_garage) {
    render_passive_empty_label(it, cx, (content_top + content_bot) / 2,
                               "No Entry Controls configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const int avail_h = std::max(120, content_bot - content_top);
  const int min_hit = UiLayout::mm_to_px(7.0f);

  const LockState ls = home_hub_lock_state();
  const char *lock_st = (ls == LockState::LOCKED) ? "Locked"
                      : ((ls == LockState::UNLOCKED) ? "Unlocked" : kUnknownGlyph);
  const Color lock_col = (ls == LockState::LOCKED) ? ctx.col_green
                       : ((ls == LockState::UNLOCKED) ? ctx.col_red : ctx.col_slate);
  const char *lock_cta = (ls == LockState::UNLOCKED) ? "Lock" : "Unlock";

  const GarageState gs = home_hub_garage_state();
  const char *gar_st = (gs == GarageState::CLOSED) ? "Closed"
                     : ((gs == GarageState::OPEN) ? "Open" : kUnknownGlyph);
  const Color gar_col = (gs == GarageState::CLOSED) ? ctx.col_green
                      : ((gs == GarageState::OPEN) ? ctx.col_warm_gold : ctx.col_slate);
  const char *gar_cta = (gs == GarageState::OPEN) ? "Close" : "Open";

  if (show_lock && show_garage) {
    const int stride = avail_h / 2;
    const int card_h = std::clamp(stride - 10, 56, scale_y(84));
    const int hit_h = std::max(card_h, std::min(min_hit, stride));
    const int cy0 = content_top + stride / 2;
    const int cy1 = content_top + stride + stride / 2;
    render_entry_card(it, ctx, cx, cy0, std::min(scale_x(324), band_safe_w(cy0, card_h, 14)),
                      card_h, hit_h, true, "Entry Lock", lock_st, lock_col,
                      lock_cta, (ls == LockState::UNLOCKED) ? ctx.col_green : ctx.col_warm_gold,
                      WidgetId::ENTRY_LOCK_CARD, WidgetId::LOCK_ACTION_BTN);
    render_entry_card(it, ctx, cx, cy1, std::min(scale_x(324), band_safe_w(cy1, card_h, 14)),
                      card_h, hit_h, false, "Garage Door", gar_st, gar_col,
                      gar_cta, (gs == GarageState::OPEN) ? ctx.col_green : ctx.col_warm_gold,
                      WidgetId::ENTRY_GARAGE_CARD, WidgetId::COVER_OPEN_BTN);
  } else {
    const int cy_only = content_top + avail_h / 2;
    const int card_h = std::clamp((avail_h * 56) / 100, 68, scale_y(104));
    const int hit_h = std::max(card_h, min_hit);
    const int w_only = std::min(scale_x(324), band_safe_w(cy_only, card_h, 14));
    if (show_lock) {
      render_entry_card(it, ctx, cx, cy_only, w_only, card_h, hit_h,
                        true, "Entry Lock", lock_st, lock_col, lock_cta,
                        (ls == LockState::UNLOCKED) ? ctx.col_green : ctx.col_warm_gold,
                        WidgetId::ENTRY_LOCK_CARD, WidgetId::LOCK_ACTION_BTN);
    } else {
      render_entry_card(it, ctx, cx, cy_only, w_only, card_h, hit_h,
                        false, "Garage Door", gar_st, gar_col, gar_cta,
                        (gs == GarageState::OPEN) ? ctx.col_green : ctx.col_warm_gold,
                        WidgetId::ENTRY_GARAGE_CARD, WidgetId::COVER_OPEN_BTN);
    }
  }
}

void render_vacuum_tab(esphome::display::Display &it, const UiFrameContext &ctx,
                       const DeviceRuntimeState &rs, const AreaContextState &ac,
                       int content_top, int content_bot) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const int cx = ctx.cx;
  render_page_header(it, cx, "ROBOT VACUUM", ctx.font_small, ctx.theme_primary);
  if (ac.vacuum_entity.empty()) {
    render_passive_empty_label(it, cx, (content_top + content_bot) / 2,
                               "No Robot Vacuum configured", ctx.font_small, ctx.col_slate);
    return;
  }

  const std::string &vst = home_hub_vacuum_state();
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

  const int avail_h = std::max(140, content_bot - content_top);
  const int hero_h = std::clamp((avail_h * 56) / 100, 82, scale_y(132));
  const int hero_y0 = content_top;

  // D-048: Robot vacuum icon + status text rendered directly over wallpaper with kChromeShadow
  const int icon_cy = hero_y0 + (is_fb ? (hero_h * 25) / 100 : (hero_h * 32) / 100);
  const int st_y = hero_y0 + (is_fb ? (hero_h * 60) / 100 : (hero_h * 72) / 100);
  ui::draw_icon_robot_vacuum(it, cx, icon_cy, ui::hero_icon_r(hero_h), st_col, true);

  auto *st_font = (SCREEN_H < 400) ? ctx.font_btn_bold : ctx.font_date;
  const int st_max_w = std::max(80, band_safe_w(st_y, 32, 16));
  const std::string fit_st = fit_text_to_width_font(std::string(st_lbl), st_max_w, st_font);
  draw_shadowed_text(it, cx, st_y, st_font, st_col, TextAlign::CENTER, fit_st.c_str());

  if (is_fb) {
    auto *fb_font = (SCREEN_H < 400) ? ctx.font_micro : ctx.font_small;
    const int fb_y = hero_y0 + (hero_h * 88) / 100;
    const int fb_max_w = std::max(80, band_safe_w(fb_y, 24, 16));
    const std::string fit_fb = fit_text_to_width_font(rs.area.action_feedback, fb_max_w, fb_font);
    draw_shadowed_text(it, cx, fb_y, fb_font, ctx.theme_secondary, TextAlign::CENTER, fit_fb.c_str());
  }

  HomeHubActionSpec specs[4]{};
  const int act_cnt = resolve_vacuum_actions(vst, effective_vacuum_caps(ac), specs);
  const int deck_top = hero_y0 + hero_h + std::max(6, scale_y(10));
  HomeHubPillGeom geoms[4]{};
  const int geom_cnt = compute_home_hub_pill_deck(
      act_cnt, deck_top, content_bot, SCREEN_W, SCREEN_H,
      LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0, LATTER_PANEL_PPI,
      false, geoms);

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
    draw_frosted_pill(
        it, geoms[i].cx, geoms[i].cy, geoms[i].w, geoms[i].h,
        primary_only ? (geoms[i].h / 2) : std::min(16, geoms[i].h / 2),
        vc,
        primary_only ? ui::chrome_card_bg_on() : ui::chrome_card_bg_off(),
        specs[i].label,
        ctx.col_white,
        (primary_only && SCREEN_W >= 360) ? ctx.font_btn_bold : ctx.font_small);
  }
  register_home_hub_pill_deck_hits(frame_hits(), WidgetId::VACUUM_ACTION_BASE, codes, geoms, geom_cnt);
}

}  // namespace

void render_home_hub_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ac = area_ctx_state();

  int tabs[kMaxHomeHubTabs]{};
  const int tab_count = collect_home_hub_tabs(tabs, ac);
  if (tab_count <= 0) {
    render_page_header(it, ctx.cx, "HOME HUB", ctx.font_small, ctx.theme_primary);
    render_passive_empty_label(it, ctx.cx, layout().area_card3_y(1),
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

  const int content_top = layout().hdr_y + ((SCREEN_H < 400) ? 20 : scale_y(28));
  const int content_bot = layout().mode_bar_zone_top() - ((SCREEN_H < 400) ? 4 : scale_y(6));

  bool show_mode_bar = true;
  switch (tab) {
    case HomeHubTab::ALARM:
      render_alarm_tab(it, ctx, rs, ac, content_top, content_bot);
      break;
    case HomeHubTab::CAMERAS:
      show_mode_bar = render_cameras_tab(it, ctx, rs, ac, content_top, content_bot);
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

  if (show_mode_bar) {
    render_home_hub_mode_bar(it, ctx, tabs, tab_count, active_slot);
  }
}

}  // namespace latter_ui
