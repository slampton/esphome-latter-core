#include "home_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {
namespace {

#if LATTER_HAS_BATTERY
void render_battery_indicator(
    Rgb565Surface &it, int cx, int by, float batt, bool is_charging,
    UiFontView font_batt, const UiColor &theme_primary,
    const UiColor &col_gold, const UiColor &col_green) {
  if (std::isnan(batt) || font_batt == nullptr) return;
  char batt_str[16];
  std::snprintf(batt_str, sizeof(batt_str), "%.0f%%", batt);
  const bool compact = (SCREEN_W < 380);
  const int text_x = cx + (compact ? 6 : 8);
  ui::shadow_print(it, text_x, by, font_batt, theme_primary, UiTextAlign::CENTER, batt_str);
  if (!compact) fast_print(it, text_x + 1, by, font_batt, theme_primary, UiTextAlign::CENTER, batt_str);
  const int bx = text_x - (compact ? (batt >= 100.0f ? 24 : 20) : (batt >= 100.0f ? 36 : 28));
  if (is_charging) {
    const int dx = compact ? 2 : 3, dy = compact ? 6 : 9;
    fast_filled_triangle(it, bx + dx, by - dy, bx - dx, by, bx + 1, by, col_gold);
    fast_filled_triangle(it, bx - 1, by, bx + 2, by, bx - dx, by + dy, col_gold);
  } else if (compact) {
    fill_rect_row_major(it, bx - 1, by - 6, 3, 1, col_green);
    fast_rect_outline(it, bx - 4, by - 5, 8, 11, col_green);
    const int fill_h = static_cast<int>(std::clamp((batt / 100.0f) * 7.0f, 1.0f, 7.0f));
    fill_rect_row_major(it, bx - 2, (by + 4) - fill_h, 4, fill_h, col_green);
  } else {
    fill_rect_row_major(it, bx - 2, by - 9, 4, 2, col_green);
    fast_rect_outline(it, bx - 5, by - 7, 10, 16, col_green);
    const int fill_h = static_cast<int>(std::clamp((batt / 100.0f) * 12.0f, 1.0f, 12.0f));
    fill_rect_row_major(it, bx - 3, (by + 7) - fill_h, 6, fill_h, col_green);
  }
}
#endif

void draw_home_activity_row_contents(
    Rgb565Surface &it, int row_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, bool is_list_row, UiFontView font_small,
    UiColor card_bg, UiColor btn_bg, UiColor theme_secondary, UiColor col_white,
    float alpha = 1.0f, bool is_two_row_card = false) {
  const UiColor fg_w   = (alpha >= 0.99f) ? col_white       : lerp_color(card_bg, col_white, alpha);
  const UiColor fg_sh  = (alpha >= 0.99f) ? kChromeShadow   : lerp_color(card_bg, kChromeShadow, alpha);
  const UiColor fg_sec = (alpha >= 0.99f) ? theme_secondary : lerp_color(card_bg, theme_secondary, alpha);
  const UiColor fg_btn = (alpha >= 0.99f) ? btn_bg          : lerp_color(card_bg, btn_bg, alpha);

  const int card_w     = is_list_row ? layout().list_card_w : layout().home_activity_w();
  const int card_left  = SCREEN_CX - (card_w / 2), card_right = SCREEN_CX + (card_w / 2);
  const int gx         = is_list_row ? (card_left + scale_x(18)) : (card_left + scale_x(28));
  const int txt_x      = is_list_row ? (card_left + scale_x(33)) : (card_left + scale_x(48));
  const int div1_x     = is_list_row ? (card_right - scale_x(44))
                                     : (has_dropdown_ext ? (card_right - scale_x(108)) : (card_right - scale_x(56)));
  const int act_bx     = is_list_row ? (card_right - scale_x(22))
                                     : (has_dropdown_ext ? (card_right - scale_x(80))  : (card_right - scale_x(28)));
  const int max_txt    = std::max(64, div1_x - txt_x - scale_x(8));
  const int btn_r      = is_list_row ? scale_s(13)
                                     : (is_two_row_card ? ((SCREEN_W < 380) ? 15 : scale_s(17))
                                                        : ((SCREEN_W < 380) ? 16 : scale_s(18)));
  const int icon_r = is_list_row ? scale_s(8) : scale_s(10);
  const bool icon_sh = (alpha >= 0.85f);
  if (cur_act.kind == 1) ui::draw_icon_hourglass(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 2) ui::draw_icon_stopwatch(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 3) ui::draw_icon_eighth_notes(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 5) ui::draw_icon_calendar(it, gx, row_y, icon_r, fg_w, icon_sh);
  else if (cur_act.kind == 6) ui::draw_icon_intercom_handset(it, gx, row_y, icon_r, fg_w, icon_sh);
  else ui::draw_icon_alarm_clock(it, gx, row_y, icon_r, fg_w, icon_sh);

  struct FitActMemo { std::string raw{}, fitted{}; int max_w{-1}; const void *font{nullptr}; };
  static FitActMemo fit_memos[4];
  static uint8_t fit_memo_rr = 0;
  const std::string *fit_ptr = nullptr;
  for (auto &m : fit_memos) {
    if (m.max_w == max_txt && m.font == font_small.handle && m.raw == cur_act.label) {
      fit_ptr = &m.fitted;
      break;
    }
  }
  if (fit_ptr == nullptr) {
    auto &slot = fit_memos[fit_memo_rr++ & 3U];
    slot = {cur_act.label, fit_text_to_width_font(cur_act.label, max_txt, font_small), max_txt, font_small.handle};
    fit_ptr = &slot.fitted;
  }
  fast_print(it, txt_x + 1, row_y + 1, font_small, fg_sh, UiTextAlign::CENTER_LEFT, fit_ptr->c_str());
  fast_print(it, txt_x, row_y, font_small, fg_w, UiTextAlign::CENTER_LEFT, fit_ptr->c_str());

  if (alpha >= 0.99f) {
    render_glass_circle(it, act_bx, row_y, btn_r, fg_btn, fg_sec);
  } else if (alpha > 0.01f && btn_r > 1) {
    const int d = 2 * btn_r + 1;
    UiColor btn_eff;
    int raw_at = 0, raw_ab = 0, raw_rt = 0, raw_rb = 0;
    resolve_glass_fill(GlassBackdrop{}, fg_btn, fg_sec, btn_eff, raw_at, raw_ab, raw_rt, raw_rb, -1, GlassVariant::Clear);
    const int at = static_cast<int>(std::lround(static_cast<float>(raw_at) * alpha));
    const int ab = static_cast<int>(std::lround(static_cast<float>(raw_ab) * alpha));
    const int rt_a = static_cast<int>(std::lround(static_cast<float>(raw_rt) * alpha));
    const int rb_a = static_cast<int>(std::lround(static_cast<float>(raw_rb) * alpha));
    if (rt_a > 0 || rb_a > 0) {
      fill_round_rect_glass(it, act_bx - btn_r, row_y - btn_r, d, d, static_cast<float>(btn_r) + 0.5f, kChromeGlassSheen, rt_a, rb_a, 1);
    }
    if ((at > 0 || ab > 0) && d > 2) {
      fill_round_rect_glass(it, act_bx - btn_r + 1, row_y - btn_r + 1, d - 2, d - 2,
                            std::max(0.5f, static_cast<float>(btn_r) - 0.5f), btn_eff, at, ab);
    }
  }

  const char *act_slug = (cur_act.kind == 5) ? "mdi:chevron-right"
                       : (cur_act.kind == 4) ? "mdi:close"
                       : (cur_act.kind == 6) ? "mdi:stop"
                       : (cur_act.is_active ? "mdi:pause" : "mdi:play");
  render_mdi_icon(it, act_slug, act_bx, row_y, std::max(14, (btn_r * 13) / 10), fg_w, icon_sh);
}

void render_home_activity_card_row(
    Rgb565Surface &it, int cx, int pill_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, UiFontView font_small,
    UiColor theme_secondary, UiColor col_white, bool is_list_row,
    UiColor card_bg, UiColor card_brd, UiColor btn_bg) {
  const int card_w = is_list_row ? layout().list_card_w : layout().home_activity_w();
  const int card_h = is_list_row ? layout().slot5_h : scale_y(68);
  const int card_right = cx + (card_w / 2);
  const int div1_x = is_list_row ? (card_right - scale_x(44))
                                 : (has_dropdown_ext ? (card_right - scale_x(108)) : (card_right - scale_x(56)));
  const int div_h = is_list_row ? scale_y(14) : scale_y(23);
  const UiColor surf_bg = render_card_box(it, cx - card_w / 2, pill_y - card_h / 2, card_w, card_h,
                                          card_bg, card_brd, kRadiusCardPx(), GlassVariant::Clear, col_white, true);
  draw_home_activity_row_contents(it, pill_y, cur_act, has_dropdown_ext, is_list_row, font_small,
                                  surf_bg, btn_bg, theme_secondary, col_white, 1.0f);
  fast_line(it, div1_x, pill_y - div_h, div1_x, pill_y + div_h, card_brd);
  if (has_dropdown_ext) {
    const int div2_x = card_right - scale_x(52), drop_bx = card_right - scale_x(26);
    fast_line(it, div2_x, pill_y - div_h, div2_x, pill_y + div_h, card_brd);
    const int ch_w = scale_x(8), ch_h = scale_y(4);
    for (int w = -1; w <= 1; w++) {
      fast_line(it, drop_bx - ch_w, pill_y - ch_h + w, drop_bx, pill_y + ch_h + w, col_white);
      fast_line(it, drop_bx + ch_w, pill_y - ch_h + w, drop_bx, pill_y + ch_h + w, col_white);
    }
  }
}

void draw_activity_card_chrome(
    Rgb565Surface &it, int cx, int card_x0, int card_w, int card_h, int cur_cy,
    int y_top_slot, int y_bot_slot, int rows, bool has_drop, bool show_bot_div,
    float surf_alpha, float row_sep_a, UiColor card_bg, UiColor col_white) {
  const UiColor div_col = (surf_alpha >= 0.99f) ? kChromeCardBrdOn : lerp_color(card_bg, kChromeCardBrdOn, surf_alpha);
  const int card_right = cx + card_w / 2;
  const int div2_x = card_right - scale_x(52);
  const int div1_x = has_drop ? (card_right - scale_x(108)) : (card_right - scale_x(56));

  if (row_sep_a > 0.05f) {
    const int sep_x0 = card_x0 + scale_x(18), sep_x1 = (has_drop ? div2_x : card_right) - scale_x(12);
    if (sep_x1 > sep_x0) {
      const UiColor sep_col = (row_sep_a >= 0.99f) ? kChromeCardBrdOn : lerp_color(card_bg, kChromeCardBrdOn, row_sep_a);
      fast_hline(it, sep_x0, cur_cy, sep_x1 - sep_x0 + 1, sep_col);
    }
  }
  if (rows == 2) {
    const int div_h = scale_y(16);
    fast_line(it, div1_x, y_top_slot - div_h, div1_x, y_top_slot + div_h, div_col);
    if (show_bot_div) fast_line(it, div1_x, y_bot_slot - div_h, div1_x, y_bot_slot + div_h, div_col);
  } else {
    const int div_h = scale_y(18);
    fast_line(it, div1_x, cur_cy - div_h, div1_x, cur_cy + div_h, div_col);
  }
  if (has_drop) {
    const int col_h = std::max(scale_y(12), card_h / 2 - scale_y(10));
    fast_line(it, div2_x, cur_cy - col_h, div2_x, cur_cy + col_h, div_col);
    const UiColor chev_col = (surf_alpha >= 0.99f) ? col_white : lerp_color(card_bg, col_white, surf_alpha);
    const int d_bx = card_right - scale_x(26), ch_w = scale_x(8), ch_h = scale_y(4);
    for (int w = -1; w <= 1; w++) {
      fast_line(it, d_bx - ch_w, cur_cy - ch_h + w, d_bx, cur_cy + ch_h + w, chev_col);
      fast_line(it, d_bx + ch_w, cur_cy - ch_h + w, d_bx, cur_cy + ch_h + w, chev_col);
    }
  }
}

bool draw_glass_activity_surface(
    Rgb565Surface &it, int card_x0, int card_y0, int card_w, int box_h, float surf_alpha,
    bool sample_bd, UiColor col_bg, UiColor col_white, UiColor &out_card_bg) {
  if (box_h <= 4 || surf_alpha <= 0.01f) return false;
  if ((card_y0 + box_h) <= clip_y0() || card_y0 > clip_y1()) return false;
  const UiColor tint = kChromeGlassTint;
  const int card_r = std::min(box_h / 2, (SCREEN_W > 520) ? scale_s(16) : 16);
  if (surf_alpha >= 0.995f) {
    const GlassBackdrop anim_bd = sample_bd ? sample_glass_backdrop(card_x0, card_y0, card_w, box_h) : GlassBackdrop{};
    render_glass_card(it, card_x0, card_y0, card_w, box_h, card_r, tint, -1, GlassVariant::Clear, col_white);
    out_card_bg = tint;
    if (sample_bd) {
      UiColor eff_tint;
      int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
      resolve_glass_fill(GlassBackdrop{}, tint, col_white, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, GlassVariant::Clear);
      out_card_bg = anim_bd.valid ? mix_color_q8(anim_bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
    }
    return true;
  }

  const int prev_clip_y0 = clip_y0(), prev_clip_y1 = clip_y1();
  const int sc_y0 = std::max(prev_clip_y0, card_y0), sc_y1 = std::min(prev_clip_y1, card_y0 + box_h - 1);
  if (sc_y0 > sc_y1) return false;
  set_clip_y_band(sc_y0, sc_y1);
  const GlassBackdrop backdrop = sample_glass_backdrop(card_x0, card_y0, card_w, box_h);
  UiColor eff_tint;
  int raw_a_top = 0, raw_a_bot = 0, raw_rim_top = 0, raw_rim_bot = 0;
  resolve_glass_fill(GlassBackdrop{}, tint, col_white, eff_tint, raw_a_top, raw_a_bot, raw_rim_top, raw_rim_bot, -1, GlassVariant::Clear);
  const int a_top = static_cast<int>(std::lround(static_cast<float>(raw_a_top) * surf_alpha));
  const int a_bot = static_cast<int>(std::lround(static_cast<float>(raw_a_bot) * surf_alpha));
  const int rim_top = static_cast<int>(std::lround(static_cast<float>(raw_rim_top) * surf_alpha));
  const int rim_bot = static_cast<int>(std::lround(static_cast<float>(raw_rim_bot) * surf_alpha));
  if (rim_top > 0 || rim_bot > 0) {
    fill_round_rect_glass(it, card_x0, card_y0, card_w, box_h, static_cast<float>(card_r), kChromeGlassSheen, rim_top, rim_bot, 1);
  }
  if ((a_top > 0 || a_bot > 0) && card_w > 2 && box_h > 2) {
    fill_round_rect_glass(it, card_x0 + 1, card_y0 + 1, card_w - 2, box_h - 2,
                          std::max(0.5f, static_cast<float>(card_r) - 1.0f), eff_tint, a_top, a_bot);
  }
  out_card_bg = mix_color_q8(backdrop.valid ? backdrop.mean : col_bg, eff_tint, (a_top + a_bot) / 2);
  set_clip_y_band(prev_clip_y0, prev_clip_y1);
  return true;
}

}  // namespace

void render_home_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  sync_home_activity_away_sentinel();

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const auto &ws = weather_state();
  const auto &ms = media_state();

  const int cx = ctx.cx, cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  const int home_subpage = rs.nav.home_subpage;
  const bool is_24h_format = prefs.is_24h_format;
  const bool show_weather_bg = ctx.home_weather_bg;
  const bool show_wake_greeting = ctx.home_wake_greeting;

  const bool is_rem_media = !ms.is_local;
  const bool music_playing = is_music_playing_effective(is_rem_media, ctx.local_media_playing, ms.state, now_ms);
  const std::string music_title = (is_rem_media || ctx.track_title.empty()) ? ms.title : ctx.track_title.str();
  const std::string music_artist = (is_rem_media || ctx.track_artist.empty()) ? ms.artist : ctx.track_artist.str();

  const bool sim_override_on = (!ctx.weather_sim_opt.empty() && ctx.weather_sim_opt != "Live (Home Assistant)");
  const bool has_day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
  const bool has_custom_wallpaper = (!ac.wallpaper_url.empty() && !has_day_night_sky &&
                                     ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                                     ctx.home_wallpaper_art.get_width() > 0);
  const bool dns_night = resolve_day_night_sky_is_night(ws.sun_state, ctx.now.is_valid(), ctx.now.hour);
  const BackdropMode bd_mode = resolve_backdrop_mode(
      PageId::HOME, home_subpage, show_weather_bg, prefs.weather_app_background,
      sim_override_on, has_custom_wallpaper, has_day_night_sky);

  if (home_subpage == 1) {
    set_home_activity_deck_rotates(false);
    home_activity_card_shown() = false;
    home_act_state().was_visible = false;
    home_act_state().fade_out_start_ms = 0U;
    if (bd_mode == BackdropMode::DayNightSky) {
      render_static_day_night_sky_base(it, cx, cy, dns_night);
    } else {
      render_page_wallpaper_base(it, ctx.home_wallpaper_art, bd_mode == BackdropMode::CustomWallpaper, cx, ctx.col_bg);
    }
    render_page_header(it, cx, "HOME SETTINGS", ctx.font_small, ctx.col_white);

    const int hs_pg = std::clamp(rs.nav.home_settings_page, 1, 2);
    rs.nav.home_settings_page = hs_pg;
    const int hs_w = layout().list_card_w, hs_h = layout().slot5_h;
    if (hs_pg == 1) {
      char tap_buf[48], dtap_buf[48];
      std::snprintf(tap_buf, sizeof(tap_buf), "Time Tap: %s", (prefs.home_clock_tap_action == 0U) ? "Time App" : "Clock Face");
      ui::setting_cycle_row(it, cx, layout().slot5_y(0), hs_w, hs_h, tap_buf, true,
                            ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                            WidgetId::LIST_SLOT_BASE, kHitTap | kHitFocusSlot, 0);
      std::snprintf(dtap_buf, sizeof(dtap_buf), "Date Tap: %s", pref_home_date_tap_cstr(prefs));
      ui::setting_cycle_row(it, cx, layout().slot5_y(1), hs_w, hs_h, dtap_buf, true,
                            ctx.font_small, ctx.col_white, ctx.col_slate, ctx.theme_secondary,
                            WidgetId::LIST_SLOT_BASE + 1, kHitTap | kHitFocusSlot, 1);

      struct HomeBoolRow { const char *label; bool value; int16_t param; };
      const HomeBoolRow bool_rows[3] = {
          {"Weather Sky", prefs.weather_background, 2},
          {"Wake Greeting", prefs.wake_greeting, 3},
          {"Sun Indicator", prefs.home_sun, 4},
      };
      for (int i = 0; i < 3; ++i) {
        const int row_idx = i + 2;
        ui::toggle_card_row(it, cx, layout().slot5_y(row_idx), hs_w, hs_h, bool_rows[i].label, bool_rows[i].value,
                            ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate,
                            WidgetId::LIST_SLOT_BASE + static_cast<uint16_t>(row_idx), bool_rows[i].param);
      }
    } else {
      ui::toggle_card_row(it, cx, layout().slot5_y(0), hs_w, hs_h, "Moon Indicator", prefs.home_moon,
                          ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, WidgetId::LIST_SLOT_BASE, 5);
      ui::toggle_card_row(it, cx, layout().slot5_y(1), hs_w, hs_h, "Weather App Sky", prefs.weather_app_background,
                          ctx.font_small, ctx.theme_primary, ctx.col_white, ctx.col_slate, WidgetId::LIST_SLOT_BASE + 1, 6);
    }
    render_vertical_page_pips(it, hs_pg == 2, hs_pg == 1, ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  int act_count = 0;
  const HomeActivityItem *act_items = collect_home_activities_cached(ctx, music_playing, music_title, music_artist, act_count);

  std::string cond = ws.condition;
  bool is_night = (ws.sun_state == "below_horizon" || cond == "clear-night" ||
                   cond == "clearnight" || (cond.find("night") != std::string::npos));
  apply_weather_sim_override(ctx.weather_sim_opt, cond, is_night);

  static std::string s_memo_cond_in, s_memo_cond_title = "Unavailable";
  if (cond != s_memo_cond_in) {
    s_memo_cond_in = cond;
    s_memo_cond_title = format_weather_title(cond);
  }
  const std::string &cond_title = s_memo_cond_title;
  const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
  const bool weather_ready = ((!std::isnan(ws.temperature) && sun_ready) || sim_override_on) && (cond_title != "Unavailable");
  const bool live_sky_drawn = (bd_mode == BackdropMode::LiveWeatherSky && weather_ready);
  const int cel_x = layout().celestial_x, cel_y = layout().celestial_y;
  const bool show_celestial = is_night ? ctx.home_moon_phase : ctx.home_sun_arc;

  if (home_subpage == 2) {
    set_home_activity_deck_rotates(false);
    home_activity_card_shown() = false;
    home_activity_rendered_rows() = 0;
    reset_home_activity_anim_state(false);
    if (bd_mode == BackdropMode::DayNightSky) {
      render_static_day_night_sky_base(it, cx, cy, dns_night);
    } else if (bd_mode == BackdropMode::CustomWallpaper) {
      render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false, 2);
    } else if ((show_weather_bg || sim_override_on) && weather_ready) {
      backdrop_active() = true;
      frame_base(it, ctx.col_bg);
      if (!is_night) {
#if LATTER_ROUND_DISPLAY
        fast_filled_circle(it, cx, cy, 228, UiColor(7, 13, 22));
#else
        fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, UiColor(7, 13, 22));
#endif
      }
      if (show_celestial) {
        fast_filled_circle(it, cel_x, cel_y, LATTER_ROUND_DISPLAY ? 23 : scale_min(27),
                           is_night ? UiColor(22, 26, 34) : UiColor(42, 36, 12));
      }
    } else {
      frame_base(it, ctx.col_bg);
    }
  } else if (live_sky_drawn) {
    backdrop_active() = true;
    double moon_cycle = 0.5;
    if (is_night) {
      compute_synodic_moon_phase(ctx.now.is_valid(), ctx.now.is_valid() ? ctx.now.timestamp : 0LL,
                                 ws.moon_phase, moon_cycle, nullptr, nullptr);
    }
    render_weather_motion_wallpaper(it, cx, cy, now_ms, cond, is_night, moon_cycle, cel_x, cel_y, true,
                                    rs.hw_accel_x, rs.hw_accel_y, show_celestial);
  } else if (has_day_night_sky) {
    render_static_day_night_sky_base(it, cx, cy, dns_night);
  } else if (has_custom_wallpaper) {
    render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false, 0);
  } else {
    frame_base(it, ctx.col_bg);
  }
  frame_base(it, ctx.col_bg);

  if (home_subpage == 2) {
    char hdr_buf[36] = "ACTIVE ITEMS";
    if (ctx.now.is_valid()) {
      char t_sub[16];
      format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, t_sub, sizeof(t_sub));
      std::snprintf(hdr_buf, sizeof(hdr_buf), "ACTIVE  •  %s", t_sub);
    }
    render_page_header(it, cx, hdr_buf, ctx.font_small, ctx.col_white, true);

    if (act_count <= 0) {
      render_empty_state(it, cx, cy, "No active activities", ctx.font_small, ctx.col_slate);
    } else {
      const int total_pages = std::max(1, (act_count + 4) / 5);
      if (home_activity_overlay_page() >= total_pages || home_activity_overlay_page() < 0) home_activity_overlay_page() = 0;
      const int start_i = home_activity_overlay_page() * 5;
      const int vis_cnt = std::min(5, act_count - start_i);
      for (int row = 0; row < vis_cnt; row++) {
        const int y_mid = get_activity_overlay_card_y(vis_cnt, row);
        render_home_activity_card_row(it, cx, y_mid, act_items[start_i + row], false, ctx.font_small,
                                      ctx.theme_secondary, ctx.col_white, true,
                                      kChromeGlassTint, kChromeCardBorder, kChromeGlassTint);
        frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                       WidgetId::LIST_SLOT_BASE + static_cast<uint16_t>(row),
                                       kHitTap | kHitFocusSlot, static_cast<int16_t>(start_i + row));
      }
      if (total_pages > 1) {
        render_vertical_page_pips(it, home_activity_overlay_page() > 0, home_activity_overlay_page() + 1 < total_pages,
                                  ctx.col_bg, ctx.theme_secondary, ctx.col_slate);
      }
    }
    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  if (live_sky_drawn && show_celestial) {
    frame_hits().add_circle(cel_x, cel_y, LATTER_ROUND_DISPLAY ? scale_min(30) : scale_min(34),
                            WidgetId::HOME_QUICK_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle, 0, scale_s(10));
  }

  const UiColor fg_home_cond = (!live_sky_drawn || is_night) ? ctx.theme_secondary : kChromeDayGold;
  const int z_date_y = layout().home_date_y, z_clock_y = layout().home_clock_y, z_weath_y = layout().home_weather_y;

  ProfLap home_lap;
  if (ctx.now.is_valid()) {
    char date_bar[32];
    ctx.now.strftime(date_bar, sizeof(date_bar), "%a, %b %d");
    ui::shadow_print(it, cx, z_date_y, ctx.font_date, ctx.col_white, UiTextAlign::CENTER, date_bar);
    const int date_hit_w = std::min(scale_x(230), safe_span(z_date_y, 16));
    const int date_hit_h = std::min(scale_y(34), std::max(24, (z_clock_y - scale_y(38) - z_date_y) * 2));
    frame_hits().add_centered_rect(cx, z_date_y, date_hit_w, date_hit_h, WidgetId::HOME_DATE_HERO, kHitTap | kHitFocusSlot);
  }

  char time_str[16];
  std::snprintf(time_str, sizeof(time_str), "%s", kUnknownGlyph);
  if (ctx.now.is_valid()) format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, time_str, sizeof(time_str));
  ui::shadow_print(it, cx, z_clock_y, ctx.font_time, ctx.col_white, UiTextAlign::CENTER, time_str);
  frame_hits().add_centered_rect(cx, z_clock_y, scale_x(260), scale_y(72), WidgetId::HOME_TIME_READOUT, kHitTap | kHitFocusSlot);

  if (weather_ready) {
    char w_line[64];
    if (!std::isnan(ws.temperature)) std::snprintf(w_line, sizeof(w_line), "%.0f° • %s", ws.temperature, cond_title.c_str());
    else std::snprintf(w_line, sizeof(w_line), "%s", cond_title.c_str());
    const int max_w_span = std::min(scale_x(340), safe_span(z_weath_y));
    const std::string fit_w = fit_text_to_width_font(std::string(w_line), max_w_span, ctx.font_date);
    fast_print(it, cx + 1, z_weath_y + 1, ctx.font_date, ctx.col_black, UiTextAlign::CENTER, fit_w.c_str());
    fast_print(it, cx, z_weath_y, ctx.font_date, fg_home_cond, UiTextAlign::CENTER, fit_w.c_str());
    frame_hits().add_centered_rect(cx, z_weath_y, std::min(scale_x(300), max_w_span), scale_y(36),
                                   WidgetId::HOME_WEATHER_HERO, kHitTap | kHitFocusSlot);
  }

  const auto &ly = layout();
  const HomeActivityView view = resolve_home_activity_view(act_items, act_count, now_ms, ly.home_activity_two_line());

  if (rs.nav.wake_greeting_pending) {
    rs.nav.wake_greeting_pending = false;
    if (show_wake_greeting && ctx.now.is_valid() && !view.pinned) {
      wake_greeting_latched() = true;
      wake_greeting_start_ms() = (now_ms == 0U) ? 1U : now_ms;
    }
  }
  if (wake_greeting_latched()) {
    const uint32_t el_chk = (now_ms >= wake_greeting_start_ms()) ? (now_ms - wake_greeting_start_ms()) : 0U;
    if (!show_wake_greeting || view.pinned || !ctx.now.is_valid() || el_chk >= kWakeGreetingTotalMs) {
      wake_greeting_latched() = false;
    }
  }
  const uint32_t greet_el = (wake_greeting_latched() && now_ms >= wake_greeting_start_ms())
                                ? (now_ms - wake_greeting_start_ms()) : 999999U;
  const bool greeting_active = wake_greeting_latched() && (greet_el < kWakeGreetingTotalMs);

  if (ctx.font_date != nullptr) {
    static bool weather_h_measured = false;
    if (!weather_h_measured) {
      int m_w = 0, m_xo = 0, m_bl = 0, m_h = 0;
      ctx.font_date->measure("Ag", &m_w, &m_xo, &m_bl, &m_h);
      if (m_h > 0) home_weather_text_h() = m_h;
      weather_h_measured = true;
    }
  }
  const int weather_h = home_weather_text_h();
  const int act_card_cy = ly.home_activity_cy(weather_h);
  const int h1 = ly.home_activity_h_for_rows(1), card_w = ly.home_activity_w(), pitch = ly.home_activity_pitch();

  const HomeActivityStepResult step = step_home_activity_card(
      now_ms, home_subpage, greeting_active, weather_h, view, act_items, act_count, rs.nav.home_activity_idx);
  const auto &st = home_act_state();

  if (step.draw_active) {
    const int card_h = step.card_h;
    const int card_y1 = ly.is_round ? (ly.home_activity_cy_for_h(weather_h, card_h) + card_h / 2) : ly.home_activity_bottom();
    const int card_y0 = card_y1 - card_h, cur_cy = card_y0 + card_h / 2, card_x0 = cx - card_w / 2;
    const float e = step.e, surf_alpha = step.surf_alpha;

    UiColor card_bg = kChromeGlassTint;
    if (draw_glass_activity_surface(it, card_x0, card_y0, card_w, card_h, surf_alpha,
                                    step.animating, ctx.col_bg, ctx.col_white, card_bg)) {
      const int prev_clip_y0 = clip_y0(), prev_clip_y1 = clip_y1();
      const int inner_y0 = std::max(prev_clip_y0, card_y0 + 1), inner_y1 = std::min(prev_clip_y1, card_y0 + card_h - 2);
      const int y_top_slot = card_y0 + h1 / 2, y_bot_slot = card_y1 - h1 / 2;
      const float shrink_out_a = std::clamp(1.0f - e * 1.65f, 0.0f, 1.0f);

      if (inner_y0 <= inner_y1) {
        set_clip_y_band(inner_y0, inner_y1);
        const int rise = scale_y(16);
        auto draw_item = [&](const HomeActivityItem &it_act, int y, float alpha, bool two_row_style) {
          if (it_act.kind == 0 || alpha <= 0.04f) return;
          draw_home_activity_row_contents(it, y, it_act, step.has_drop, false, ctx.font_small,
                                          card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white,
                                          std::min(1.0f, alpha), two_row_style);
        };
        auto change_in_place = [&](const HomeActivityItem &from_it, const HomeActivityItem &to_it, int y, bool two_row_style) {
          if (!step.animating || from_it.kind == 0 || same_activity_id(from_it, to_it)) {
            draw_item(to_it, y, surf_alpha, two_row_style);
            return;
          }
          draw_item(from_it, y - static_cast<int>(static_cast<float>(rise) * e), (1.0f - e) * surf_alpha, two_row_style);
          draw_item(to_it, y + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e * surf_alpha, two_row_style);
        };

        if (step.animating && st.from_rows == 0) {
          const int rise_in = static_cast<int>(static_cast<float>(scale_y(10)) * (1.0f - e));
          if (step.target_rows == 1) {
            draw_item(st.cur_top_item, cur_cy + rise_in, surf_alpha, false);
          } else {
            draw_item(st.cur_top_item, y_top_slot + rise_in, surf_alpha, true);
            draw_item(st.cur_bot_item, y_bot_slot + rise_in, surf_alpha, true);
          }
        } else if (step.animating && st.from_rows == 1 && step.target_rows == 2) {
          if (same_activity_id(st.cur_top_item, st.from_top_item)) {
            draw_item(st.cur_top_item, y_top_slot, 1.0f, true);
            draw_item(st.cur_bot_item, y_bot_slot + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e, true);
          } else if (same_activity_id(st.cur_bot_item, st.from_top_item)) {
            draw_item(st.cur_top_item, y_top_slot - static_cast<int>(static_cast<float>(scale_y(10)) * (1.0f - e)), e, true);
            draw_item(st.cur_bot_item, y_bot_slot, 1.0f, true);
          } else {
            draw_item(st.from_top_item, y_bot_slot - static_cast<int>(static_cast<float>(rise) * e), 1.0f - e, true);
            draw_item(st.cur_top_item, y_top_slot, e, true);
            draw_item(st.cur_bot_item, y_bot_slot, e, true);
          }
        } else if (step.animating && st.from_rows == 2 && step.target_rows == 1) {
          if (same_activity_id(st.cur_top_item, st.from_bot_item)) {
            draw_item(st.from_top_item, y_top_slot, shrink_out_a, true);
            draw_item(st.cur_top_item, y_bot_slot, 1.0f, false);
          } else if (same_activity_id(st.cur_top_item, st.from_top_item)) {
            draw_item(st.from_bot_item, y_bot_slot, shrink_out_a, true);
            draw_item(st.cur_top_item, y_top_slot, 1.0f, false);
          } else {
            draw_item(st.from_top_item, y_top_slot, shrink_out_a, true);
            draw_item(st.cur_top_item, y_bot_slot, e, false);
          }
        } else if (step.target_rows == 1) {
          change_in_place(st.from_top_item, st.cur_top_item, cur_cy, false);
        } else if (step.animating && st.from_rows == 2 && !view.pinned && same_activity_id(st.cur_top_item, st.from_bot_item)) {
          draw_item(st.from_top_item, y_top_slot - static_cast<int>(static_cast<float>(rise) * e), 1.0f - e, true);
          draw_item(st.cur_top_item, y_bot_slot - static_cast<int>(static_cast<float>(pitch) * e), 1.0f, true);
          draw_item(st.cur_bot_item, y_bot_slot + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e, true);
        } else if (step.animating) {
          change_in_place(st.from_top_item, st.cur_top_item, y_top_slot, true);
          change_in_place(st.from_bot_item, st.cur_bot_item, y_bot_slot, true);
        } else {
          draw_item(st.cur_top_item, y_top_slot, 1.0f, true);
          draw_item(st.cur_bot_item, y_bot_slot, 1.0f, true);
        }

        float row_sep_a = 0.0f;
        if (step.target_rows == 2 && (st.from_rows == 2 || st.from_rows == 0)) row_sep_a = surf_alpha;
        else if (step.target_rows == 2 && st.from_rows == 1) row_sep_a = step.animating ? e : 1.0f;
        else if (step.target_rows == 1 && st.from_rows == 2 && step.animating) row_sep_a = shrink_out_a;

        draw_activity_card_chrome(it, cx, card_x0, card_w, card_h, cur_cy, y_top_slot, y_bot_slot,
                                  step.target_rows, step.has_drop, (!step.animating || st.from_rows != 1 || e > 0.25f),
                                  surf_alpha, row_sep_a, card_bg, ctx.col_white);
        set_clip_y_band(prev_clip_y0, prev_clip_y1);
      }
    }
    register_home_activity_hit_regions(frame_hits(), cx, step.target_cy, card_w, step.target_h,
                                       (step.target_rows >= 2) && (view.bottom >= 0), step.has_drop,
                                       view.top, view.bottom, scale_x(56), scale_x(52));
  } else if (step.draw_fade_out) {
    const int fade_h = step.card_h;
    const float fade_alpha = step.surf_alpha;
    const int card_y1 = ly.is_round ? (ly.home_activity_cy_for_h(weather_h, fade_h) + fade_h / 2) : ly.home_activity_bottom();
    const int card_y0 = card_y1 - fade_h, cur_cy = card_y0 + fade_h / 2, card_x0 = cx - card_w / 2;

    UiColor card_bg = kChromeGlassTint;
    if (draw_glass_activity_surface(it, card_x0, card_y0, card_w, fade_h, fade_alpha, true, ctx.col_bg, ctx.col_white, card_bg)) {
      const int prev_clip_y0 = clip_y0(), prev_clip_y1 = clip_y1();
      const int inner_y0 = std::max(prev_clip_y0, card_y0 + 1), inner_y1 = std::min(prev_clip_y1, card_y1 - 2);
      if (inner_y0 <= inner_y1) {
        set_clip_y_band(inner_y0, inner_y1);
        const int y_top_slot = card_y0 + h1 / 2, y_bot_slot = card_y1 - h1 / 2;
        if (st.fade_out_rows >= 2) {
          if (st.fade_out_top_item.kind != 0) {
            draw_home_activity_row_contents(it, y_top_slot, st.fade_out_top_item, st.fade_out_has_drop, false, ctx.font_small,
                                            card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white, fade_alpha, true);
          }
          if (st.fade_out_bot_item.kind != 0) {
            draw_home_activity_row_contents(it, y_bot_slot, st.fade_out_bot_item, st.fade_out_has_drop, false, ctx.font_small,
                                            card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white, fade_alpha, true);
          }
        } else if (st.fade_out_top_item.kind != 0) {
          draw_home_activity_row_contents(it, cur_cy, st.fade_out_top_item, st.fade_out_has_drop, false, ctx.font_small,
                                          card_bg, kChromeGlassTint, ctx.theme_secondary, ctx.col_white, fade_alpha, false);
        }
        draw_activity_card_chrome(it, cx, card_x0, card_w, fade_h, cur_cy, y_top_slot, y_bot_slot,
                                  st.fade_out_rows >= 2 ? 2 : 1, st.fade_out_has_drop, true,
                                  fade_alpha, st.fade_out_rows >= 2 ? fade_alpha : 0.0f, card_bg, ctx.col_white);
        set_clip_y_band(prev_clip_y0, prev_clip_y1);
      }
    }
  }

  if (greeting_active) {
    const float greet_temp_f = weather_temp_to_fahrenheit(ws.temperature, ws.temp_unit);
    const char *raw_greet = resolve_wake_greeting(ctx.now.hour, ctx.now.minute, ctx.now.day_of_year, cond, greet_temp_f);
    const std::string fit_greet = fit_text_to_width_font(
        std::string(raw_greet), std::min(scale_x(340), safe_span(act_card_cy)), ctx.font_date);
    const int greet_y = act_card_cy - 2;
    float alpha = 1.0f;
    if (greet_el > kWakeGreetingHoldMs) {
      const float u = std::clamp(
          1.0f - static_cast<float>(greet_el - kWakeGreetingHoldMs) / static_cast<float>(kWakeGreetingFadeMs), 0.0f, 1.0f);
      alpha = u * u * (3.0f - 2.0f * u);
    }
    const int alpha_q8 = std::clamp(static_cast<int>(std::lround(alpha * 256.0f)), 0, 256);
    if (alpha_q8 > 0) {
      const int shadow_q8 = (alpha_q8 * 192) >> 8;
      if (shadow_q8 > 8) fast_print_alpha(it, cx + 1, greet_y + 1, ctx.font_date, kChromeShadow, UiTextAlign::CENTER, fit_greet.c_str(), shadow_q8);
      fast_print_alpha(it, cx, greet_y, ctx.font_date, ctx.col_white, UiTextAlign::CENTER, fit_greet.c_str(), alpha_q8);
    }
  }

  home_lap.mark(ProfSect::HOME_TEXT);

  ShortcutItem *dock_items = dock_items_scratch();
  const int dock_count = collect_effective_dock_items(dock_items, kMaxEffectiveDockItems);
  if (dock_count > 0) {
    const int total_dock_pages = (dock_count + 3) / 4;
    if (home_dock_page() < 0 || home_dock_page() >= total_dock_pages) home_dock_page() = 0;
    int win_xs[4] = {0, 0, 0, 0};
    bool has_left_arr = false, has_right_arr = false;
    get_home_dock_window_xs(dock_count, home_dock_page(), cx, win_xs, has_left_arr, has_right_arr);
    const int start_idx = get_home_dock_start_index(dock_count, home_dock_page());
    const int vis_count = std::min(4, dock_count - start_idx);
    const int dock_y = ly.dock_y, dock_r = ly.dock_r, dock_pad = std::max(0, ly.dock_hit_r() - dock_r);
    for (int v = 0; v < vis_count; v++) {
      frame_hits().add_circle(win_xs[v], dock_y, dock_r, WidgetId::DOCK_SLOT_BASE + static_cast<uint16_t>(v),
                              kHitTap | kHitFocusSlot | kHitFocusCircle, static_cast<int16_t>(start_idx + v), dock_pad);
    }

    const float anim_start = home_dock_anim().step_window(start_idx, now_ms);
    const int dock_bounce_x = edge_bounce().offset_px(PageId::HOME, GestureAxisLock::Horizontal, now_ms, LATTER_PANEL_PPI);
    const int stride = get_dock_slot_stride(win_xs, vis_count);
    const int left_cx = win_xs[0], right_cx = win_xs[vis_count - 1], clip_pad = stride / 2 + dock_r, icon_r = ui::dock_icon_r(dock_r);

    for (int g_idx = 0; g_idx < dock_count; g_idx++) {
      const float rel_slot = static_cast<float>(g_idx) - anim_start;
      if (rel_slot < -0.85f || rel_slot > static_cast<float>(vis_count - 1) + 0.85f) continue;
      const int bx = left_cx + static_cast<int>(std::roundf(rel_slot * static_cast<float>(stride))) + dock_bounce_x;
      if (bx < left_cx - clip_pad || bx > right_cx + clip_pad) continue;

      float alpha = 1.0f;
      if (rel_slot < 0.0f) alpha = std::max(0.15f, 1.0f + rel_slot * 1.15f);
      else if (rel_slot > static_cast<float>(vis_count - 1)) alpha = std::max(0.15f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.15f);

      const ShortcutItem &item = dock_items[g_idx];
      const bool item_active = (item.kind == ShortcutKind::ENTITY && item.state == 1);
      const UiColor base_fg = (item.kind == ShortcutKind::ENTITY && item.state == 2) ? ctx.col_gray : ctx.col_white;
      const UiColor slot_fg_c(static_cast<uint8_t>(static_cast<float>(base_fg.r) * alpha),
                              static_cast<uint8_t>(static_cast<float>(base_fg.g) * alpha),
                              static_cast<uint8_t>(static_cast<float>(base_fg.b) * alpha));
      const int d = 2 * dock_r + 1;
      const UiColor dock_tint = item_active ? ui::chrome_card_bg_on() : kChromeGlassTint;
      const UiColor dock_rim = item_active ? ctx.theme_primary : kChromeGlassSheen;
      UiColor eff_tint;
      int raw_a_top = 0, raw_a_bot = 0, raw_rim_top = 0, raw_rim_bot = 0;
      resolve_glass_fill(GlassBackdrop{}, dock_tint, dock_rim, eff_tint, raw_a_top, raw_a_bot, raw_rim_top, raw_rim_bot, -1, GlassVariant::Clear);
      const int a_top = static_cast<int>(std::roundf(static_cast<float>(raw_a_top) * alpha));
      const int a_bot = static_cast<int>(std::roundf(static_cast<float>(raw_a_bot) * alpha));
      const int sheen_top = static_cast<int>(std::roundf(static_cast<float>(item_active ? std::min(255, raw_rim_top + 48) : raw_rim_top) * alpha));
      const int sheen_bot = static_cast<int>(std::roundf(static_cast<float>(item_active ? std::min(255, raw_rim_bot + 36) : raw_rim_bot) * alpha));
      fill_round_rect_glass(it, bx - dock_r, dock_y - dock_r, d, d, static_cast<float>(dock_r) + 0.5f, dock_rim, sheen_top, sheen_bot, 1);
      fill_round_rect_glass(it, bx - dock_r + 1, dock_y - dock_r + 1, d - 2, d - 2, static_cast<float>(dock_r) - 0.5f, eff_tint, a_top, a_bot);

      if (item.kind == ShortcutKind::ROUTE && item.icon.empty()) {
        ui::draw_route_icon(it, item.route_id, bx, dock_y, icon_r, slot_fg_c, eff_tint, alpha > 0.55f);
      } else {
        render_mdi_icon(it, item.icon, bx, dock_y, std::max(16, 2 * icon_r), slot_fg_c, alpha > 0.55f,
                        (item.kind == ShortcutKind::ROUTE) ? "mdi:apps" : "mdi:gesture-tap-button");
      }
    }
    render_viewport_scrollbar(it, cx, ly.dock_pips_y, dock_count, anim_start, ctx.theme_primary, false);
  }

#if LATTER_HAS_BATTERY
  render_battery_indicator(it, ly.battery_cx, ly.battery_y, rs.hw_batt_level, rs.hw_batt_charging,
                           (SCREEN_W < 380 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small,
                           ctx.theme_secondary, ctx.col_gold, ctx.col_green);
#endif
  home_lap.mark(ProfSect::HOME_DOCK);
}

}  // namespace latter_ui
