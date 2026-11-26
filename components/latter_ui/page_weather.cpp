#include "weather_sky_internal.h"
#include "ui_pages.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace latter_ui {
using namespace latter_core;

namespace {

void format_iso_time_to(const std::string &iso_str, bool is_24h, char *out, size_t out_sz);
const char *wind_bearing_to_cardinal(float deg);
std::string format_forecast_short_name(const std::string &c);
void render_weather_sun_subpage(Rgb565Surface &it, const UiFrameContext &ctx, UiColor col_warm_gold);
void render_weather_moon_subpage(Rgb565Surface &it, const UiFrameContext &ctx);

void portable_localtime(const time_t *t, struct tm *out) {
#if defined(_WIN32)
  localtime_s(out, t);
#else
  localtime_r(t, out);
#endif
}

void render_weather_sun_subpage(Rgb565Surface &it, const UiFrameContext &ctx, UiColor col_warm_gold) {
  const auto &ws = weather_state();
  const auto &prefs = persisted_prefs();
  const auto ly = layout();
  const int cx = ctx.cx;
  const int sun_top_y = ly.hdr_y + dp_px(36);
  const int bot_y = ly.mode_bar_zone_top() - dp_px(10);
  const int below_need = dp_px(88) + (kAutoFonts.date / 2);
  const int max_ry = std::max(52, bot_y - below_need - sun_top_y);
  const int safe_w = ly.safe_span(sun_top_y + max_ry, dp_px(24));
  const int max_rx = std::max(52, (safe_w / 2) - dp_px(18));
  const int arc_rx = std::min({dp_px(136), max_rx, max_ry});
  const int arc_ry = arc_rx;
  const int arc_cy = sun_top_y + arc_ry;
  const bool has_bd_shadow = backdrop_active();

  for (int hx = cx - arc_rx; hx <= cx + arc_rx - 4; hx += 8) {
    fast_line(it, hx, arc_cy, hx + 4, arc_cy, ctx.col_gray);
  }
  const int ew_off = dp_px(14);
  if (has_bd_shadow) {
    fast_print(it, cx - arc_rx - ew_off + 1, arc_cy + 1, ctx.font_micro, kChromeShadow, UiTextAlign::CENTER, "E");
    fast_print(it, cx + arc_rx + ew_off + 1, arc_cy + 1, ctx.font_micro, kChromeShadow, UiTextAlign::CENTER, "W");
  }
  fast_print(it, cx - arc_rx - ew_off, arc_cy, ctx.font_micro, ctx.col_slate, UiTextAlign::CENTER, "E");
  fast_print(it, cx + arc_rx + ew_off, arc_cy, ctx.font_micro, ctx.col_slate, UiTextAlign::CENTER, "W");

  for (int a = 0; a <= 180; a += 2) {
    const float rad = static_cast<float>(a) * (PI_F / 180.0f);
    const int ax = cx - static_cast<int>(static_cast<float>(arc_rx) * fast_cos(rad));
    const int ay = arc_cy - static_cast<int>(static_cast<float>(arc_ry) * fast_sin(rad));
    fast_pixel(it, ax, ay, UiColor(215, 155, 35));
    fast_pixel(it, ax, ay + 1, UiColor(130, 90, 20));
  }

  if (!std::isnan(ws.sun_elevation)) {
    const char *sol_st = (ws.sun_elevation > 0.0f) ? "Above Horizon" : "Below Horizon";
    ui::shadow_print(it, cx, arc_cy + dp_px(18), ctx.font_small,
                     ws.sun_elevation > 0.0f ? col_warm_gold : ctx.col_slate, UiTextAlign::CENTER, sol_st);
  }

  if (!std::isnan(ws.sun_elevation) && ws.sun_elevation >= 0.0f) {
    float day_prog = 0.5f;
    if (!std::isnan(ws.sun_azimuth)) {
      if (ws.sun_azimuth >= 60.0f && ws.sun_azimuth <= 300.0f) day_prog = (ws.sun_azimuth - 60.0f) / 240.0f;
      else if (ws.sun_azimuth < 60.0f) day_prog = 0.05f;
      else day_prog = 0.95f;
    }
    float s_th = 0.0f, c_th = 1.0f;
    fast_sincos(day_prog * PI_F, s_th, c_th);
    const int sx = cx - static_cast<int>(static_cast<float>(arc_rx) * c_th);
    const int sy = arc_cy - static_cast<int>(static_cast<float>(arc_ry) * s_th);

    fast_filled_circle(it, sx, sy, 9, UiColor(255, 160, 20));
    fast_filled_circle(it, sx, sy, 5, UiColor(255, 230, 110));
    fast_filled_circle(it, sx, sy, 2, ctx.col_white);
    fast_line(it, sx - 13, sy, sx + 13, sy, UiColor(255, 185, 35));
    fast_line(it, sx, sy - 13, sx, sy + 13, UiColor(255, 185, 35));
  }

  char s_rise[24], s_set[24];
  format_iso_time_to(ws.sun_next_rising, prefs.is_24h_format, s_rise, sizeof(s_rise));
  format_iso_time_to(ws.sun_next_setting, prefs.is_24h_format, s_set, sizeof(s_set));

  const int col_dx = (arc_rx * 60) / 100;
  const int lbl_y  = std::min(bot_y - std::max(dp_px(32), kAutoFonts.date + dp_px(10)), arc_cy + dp_px(60));
  const int val_y  = std::min(bot_y - dp_px(8), lbl_y + std::max(dp_px(26), kAutoFonts.date + dp_px(4)));

  if (has_bd_shadow) {
    fast_print(it, cx - col_dx + 1, lbl_y + 1, ctx.font_small, kChromeShadow, UiTextAlign::CENTER, "SUNRISE");
    fast_print(it, cx - col_dx + 1, val_y + 1, ctx.font_date,  kChromeShadow, UiTextAlign::CENTER, s_rise);
    fast_print(it, cx + col_dx + 1, lbl_y + 1, ctx.font_small, kChromeShadow, UiTextAlign::CENTER, "SUNSET");
    fast_print(it, cx + col_dx + 1, val_y + 1, ctx.font_date,  kChromeShadow, UiTextAlign::CENTER, s_set);
  }
  fast_print(it, cx - col_dx, lbl_y, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "SUNRISE");
  fast_print(it, cx - col_dx, val_y, ctx.font_date,  col_warm_gold, UiTextAlign::CENTER, s_rise);
  fast_print(it, cx + col_dx, lbl_y, ctx.font_small, ctx.col_slate, UiTextAlign::CENTER, "SUNSET");
  fast_print(it, cx + col_dx, val_y, ctx.font_date,  col_warm_gold, UiTextAlign::CENTER, s_set);
}

void render_weather_moon_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &ws = weather_state();
  const auto ly = layout();
  const int cx = ctx.cx;
  double cycle = 0.5;
  int illum_pct = 0;
  std::string mp_label;
  const bool moon_known = compute_synodic_moon_phase(
      ctx.now.is_valid(), ctx.now.is_valid() ? ctx.now.timestamp : 0LL,
      ws.moon_phase, cycle, &illum_pct, &mp_label);
  if (!moon_known) {
    render_empty_state(it, cx, SCREEN_CY, "Waiting for the time or moon phase...", ctx.font_small, ctx.col_slate);
    return;
  }

  const bool large_moon_txt = (!LATTER_ROUND_DISPLAY && ly.frame.w_dp >= 360);
  const int moon_top_y = ly.hdr_y + dp_px(30);
  const int illum_y    = ly.mode_bar_zone_top() - dp_px(22);
  const int phase_y    = illum_y - std::max(dp_px(26), kAutoFonts.date + dp_px(4));
  const int text_gap   = std::max(dp_px(18), (kAutoFonts.date / 2) + dp_px(12));
  const int disc_span  = std::max(64, (phase_y - text_gap) - moon_top_y);
  const int moon_cy    = moon_top_y + disc_span / 2;
  const int moon_r     = std::clamp(
      std::min({dp_px(110), ly.safe_span(moon_cy, dp_px(24)) / 2, disc_span / 2}),
      34, dp_px(110));

  render_lunar_disc(it, cx, moon_cy, moon_r, cycle);

  char illum_buf[32];
  std::snprintf(illum_buf, sizeof(illum_buf), "%d%% Illumination", illum_pct);
  const UiFontView illum_font = large_moon_txt ? ctx.font_date : ctx.font_small;
  ui::shadow_print(it, cx, phase_y, ctx.font_date, ctx.col_slate, UiTextAlign::CENTER, mp_label.c_str());
  ui::shadow_print(it, cx, illum_y, illum_font, ctx.col_white, UiTextAlign::CENTER, illum_buf);
}

// ==============================================================================
// Weather Suite Formatters & Simulation Override
// ==============================================================================

void format_iso_time_to(const std::string &iso_str, bool is_24h, char *out, size_t out_sz) {
  if (out == nullptr || out_sz == 0U) return;
  ParsedIsoTimestamp ts{};
  if (!parse_iso_timestamp_components(iso_str, ts)) {
    std::snprintf(out, out_sz, "%s", kUnknownGlyph);
    return;
  }
  if (!ts.has_tz) {
    format_hhmm_clock_to(ts.hour, ts.minute, is_24h, out, out_sz);
    return;
  }
  const LocalTzInfo tz = parse_local_tz_info(area_ctx_state().local_tz_info);
  if (tz.valid) {
    (void)format_iso_time_with_offset(iso_str, is_24h, tz.utc_offset_min, out, out_sz);
    return;
  }
  const time_t t = static_cast<time_t>(ts.utc_epoch_sec);
  struct tm tm_loc = {};
  portable_localtime(&t, &tm_loc);
  format_hhmm_clock_to(tm_loc.tm_hour, tm_loc.tm_min, is_24h, out, out_sz);
}

const char *wind_bearing_to_cardinal(float deg) {
  if (std::isnan(deg)) return "";
  static constexpr const char *DIRS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const float norm = std::fmod(deg + 360.0f, 360.0f);
  const int idx = static_cast<int>(std::floor((norm + 22.5f) / 45.0f)) & 7;
  return DIRS[idx];
}

std::string format_forecast_short_name(const std::string &c) {
  if (c == "partlycloudy" || c == "partly-cloudy") return "P.Cloudy";
  if (c == "mostlycloudy" || c == "mostly-cloudy") return "M.Cloudy";
  if (c == "mostlysunny" || c == "mostly-sunny") return "M.Sunny";
  if (c == "partlysunny" || c == "partly-sunny") return "P.Sunny";
  if (c == "clear-night" || c == "clearnight") return "Clear";
  if (c == "sunny" || c == "clear-day" || c == "clearday" || c == "clear") return "Sunny";
  if (c == "rainy" || c == "pouring" || c == "heavy-rain" || c == "drizzle") return "Rain";
  if (c == "cloudy" || c == "overcast") return "Cloudy";
  if (c == "snowy") return "Snow";
  if (c == "lightning" || c == "lightning-rainy" || c == "thunderstorm") return "Storm";
  if (c == "snowy-rainy" || c == "sleet") return "Sleet";
  if (c == "windy" || c == "windy-variant" || c == "breezy") return "Windy";
  if (c == "fog" || c == "foggy") return "Fog";
  if (c == "mist" || c == "misty" || c == "haze") return "Mist";
  if (c == "hail") return "Hail";
  if (c.empty()) return kUnknownGlyph;
  std::string s = c;
  for (char &ch : s) {
    if (ch == '-' || ch == '_') ch = ' ';
  }
  s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

}  // namespace

std::string format_weather_title(const std::string &c_in) {
  if (c_in.empty()) return "Unavailable";
  if (c_in == "partlycloudy" || c_in == "partly-cloudy") return "Partly Cloudy";
  if (c_in == "mostlycloudy" || c_in == "mostly-cloudy") return "Mostly Cloudy";
  if (c_in == "mostlysunny" || c_in == "mostly-sunny") return "Mostly Sunny";
  if (c_in == "partlysunny" || c_in == "partly-sunny") return "Partly Sunny";
  if (c_in == "clear-night" || c_in == "clearnight") return "Clear Night";
  if (c_in == "clear-day" || c_in == "clearday" || c_in == "sunny") return "Sunny";
  if (c_in == "cloudy" || c_in == "overcast") return "Cloudy";
  if (c_in == "rainy" || c_in == "drizzle") return "Rainy";
  if (c_in == "snowy") return "Snowy";
  if (c_in == "fog" || c_in == "foggy") return "Fog & Mist";
  if (c_in == "mist" || c_in == "misty" || c_in == "haze") return "Mist & Haze";
  if (c_in == "lightning-rainy" || c_in == "thunderstorm") return "Thunderstorm";
  if (c_in == "lightning") return "Lightning";
  if (c_in == "snowy-rainy" || c_in == "sleet") return "Sleet & Mix";
  if (c_in == "windy-variant" || c_in == "windy" || c_in == "breezy") return "Windy";
  if (c_in == "pouring" || c_in == "heavy-rain") return "Heavy Rain";
  if (c_in == "exceptional") return "Severe Alert";
  std::string s = c_in;
  bool cap_next = true;
  for (char &c : s) {
    if (c == '-' || c == '_') {
      c = ' ';
      cap_next = true;
    } else if (cap_next) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      cap_next = false;
    } else {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return s;
}

namespace {

void render_forecast_glyph(Rgb565Surface &it, int gx, int gy, const std::string &c_name) {
  auto S = [](int v) -> int { return dp_px(v); };
  auto SR = [](int v) -> int { return std::max(1, dp_px(v)); };
  const int thick = (kUiScaleMilli >= 1200) ? 2 : 1;

  if (c_name == "sunny" || c_name == "clear-day" || c_name == "clear" || c_name == "clearday") {
    fast_filled_circle_aa(it, gx, gy, SR(7), UiColor(250, 185, 30));
    fast_circle_aa(it, gx, gy, static_cast<float>(SR(10)), UiColor(255, 215, 80), static_cast<float>(thick));
    const UiColor ray_c(255, 225, 100);
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx + t, gy - S(12), gx + t, gy - S(10), ray_c);
      fast_line(it, gx + t, gy + S(10), gx + t, gy + S(12), ray_c);
      fast_line(it, gx - S(12), gy + t, gx - S(10), gy + t, ray_c);
      fast_line(it, gx + S(10), gy + t, gx + S(12), gy + t, ray_c);
    }
  } else if (c_name == "clear-night" || c_name == "clearnight") {
    const UiColor moon_c(225, 235, 250);
    const int ro = SR(8), rc = SR(7), ox_c = S(2), cx_c = S(1);
    for (int dy = -ro; dy <= ro; dy++) {
      for (int dx = -ro + ox_c; dx <= ro + ox_c; dx++) {
        const int ox = dx - ox_c;
        if (ox * ox + dy * dy <= ro * ro) {
          const int cx_d = dx + cx_c, cy_d = dy + cx_c;
          if (cx_d * cx_d + cy_d * cy_d > rc * rc) fast_pixel(it, gx + dx, gy + dy, moon_c);
        }
      }
    }
  } else if (c_name == "partlycloudy" || c_name == "partly-cloudy" || c_name == "mostlycloudy" ||
             c_name == "mostlysunny" || c_name == "partlysunny") {
    fast_filled_circle_aa(it, gx - S(4), gy - S(4), SR(6), UiColor(245, 175, 30));
    fast_filled_circle_aa(it, gx + S(2), gy + S(1), SR(6), UiColor(175, 190, 210));
    fast_filled_circle_aa(it, gx - S(3), gy + S(2), SR(5), UiColor(155, 170, 190));
    fast_filled_circle_aa(it, gx + S(7), gy + S(3), SR(4), UiColor(145, 160, 180));
  } else if (c_name == "rainy" || c_name == "pouring" || c_name == "drizzle" || c_name == "heavy-rain") {
    fast_filled_circle_aa(it, gx, gy - S(2), SR(6), UiColor(130, 145, 170));
    fast_filled_circle_aa(it, gx - S(5), gy - S(1), SR(5), UiColor(115, 130, 155));
    fast_filled_circle_aa(it, gx + S(5), gy, SR(4), UiColor(115, 130, 155));
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(4) + t, gy + S(6), gx - S(6) + t, gy + S(10), UiColor(70, 170, 245));
      fast_line(it, gx + S(2) + t, gy + S(6), gx + t, gy + S(10), UiColor(70, 170, 245));
    }
  } else if (c_name == "snowy" || c_name == "snowy-rainy" || c_name == "sleet" || c_name == "hail") {
    fast_filled_circle_aa(it, gx, gy - S(2), SR(6), UiColor(160, 175, 195));
    fill_rect_row_major(it, gx - S(3), gy + S(7), thick, thick, UiColor(255, 255, 255));
    fill_rect_row_major(it, gx + S(3), gy + S(7), thick, thick, UiColor(255, 255, 255));
    fill_rect_row_major(it, gx, gy + S(9), thick, thick, UiColor(255, 255, 255));
  } else if (c_name == "lightning" || c_name == "lightning-rainy" || c_name == "thunderstorm") {
    fast_filled_circle_aa(it, gx, gy - S(3), SR(6), UiColor(110, 120, 140));
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(1) + t, gy + S(3), gx - S(3) + t, gy + S(7), UiColor(255, 220, 40));
      fast_line(it, gx - S(3), gy + S(7) + t, gx + S(1), gy + S(7) + t, UiColor(255, 220, 40));
      fast_line(it, gx + S(1) + t, gy + S(7), gx - S(1) + t, gy + S(11), UiColor(255, 220, 40));
    }
  } else if (c_name == "windy" || c_name == "windy-variant" || c_name == "breezy") {
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(9), gy - S(3) + t, gx + S(5), gy - S(3) + t, UiColor(160, 190, 220));
      fast_line(it, gx - S(6), gy + S(1) + t, gx + S(8), gy + S(1) + t, UiColor(180, 210, 235));
      fast_line(it, gx - S(4), gy + S(5) + t, gx + S(4), gy + S(5) + t, UiColor(140, 170, 200));
    }
    fast_line(it, gx + S(6), gy - S(5), gx + S(9), gy - S(3), UiColor(125, 195, 95));
    fast_line(it, gx + S(6), gy - S(4), gx + S(9), gy - S(3), UiColor(230, 185, 55));
  } else if (c_name == "fog" || c_name == "foggy" || c_name == "mist" || c_name == "misty" || c_name == "haze") {
    fast_filled_circle_aa(it, gx, gy - S(4), SR(5), UiColor(135, 155, 175));
    for (int t = 0; t < thick; ++t) {
      fast_line(it, gx - S(9), gy - S(1) + t, gx + S(9), gy - S(1) + t, UiColor(185, 205, 225));
      fast_line(it, gx - S(7), gy + S(2) + t, gx + S(10), gy + S(2) + t, UiColor(155, 178, 200));
      fast_line(it, gx - S(10), gy + S(5) + t, gx + S(6), gy + S(5) + t, UiColor(130, 152, 175));
      fast_line(it, gx - S(5), gy + S(8) + t, gx + S(8), gy + S(8) + t, UiColor(105, 125, 148));
    }
  } else {
    fast_filled_circle_aa(it, gx, gy - S(1), SR(7), UiColor(165, 180, 200));
    fast_filled_circle_aa(it, gx - S(5), gy + S(1), SR(5), UiColor(145, 160, 180));
    fast_filled_circle_aa(it, gx + S(5), gy + S(1), SR(5), UiColor(145, 160, 180));
  }
}

void draw_weather_mode_icon(Rgb565Surface &it, int i, int bx, int by, int ir,
                            UiColor fg_c, bool shadow, void *) {
  if (i == 0) {
    render_mdi_icon(it, "mdi:weather-partly-cloudy", bx, by, std::max(16, 2 * ir), fg_c, shadow, "mdi:cloud");
  } else if (i == 1) {
    ui::draw_icon_forecast_chart(it, bx, by, ir, fg_c, shadow);
  } else if (i == 2) {
    ui::draw_icon_sun(it, bx, by, ir, fg_c, shadow);
  } else {
    ui::draw_icon_crescent_moon(it, bx, by, ir, fg_c, shadow);
  }
}

void render_weather_subpage_icons(Rgb565Surface &it, int active_subpage,
                                  UiColor col_active, UiColor col_inactive) {
  ui::mode_bar(it, SCREEN_CX, 4, active_subpage, 0, col_active, col_inactive,
               draw_weather_mode_icon, nullptr);
}

}  // namespace

void render_weather_page(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &rs = runtime_state();
  const auto &ws = weather_state();
  const auto &prefs = persisted_prefs();

  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const int sky_weather_subpage = rs.nav.sky_weather_subpage;
  const auto &now = ctx.now;
  const UiStrView weather_sim_opt = ctx.weather_sim_opt;

  const UiFontView font_time = ctx.font_time;
  const UiFontView font_chrono_time = ctx.font_chrono_time;
  const UiFontView font_date = ctx.font_date;
  const UiFontView font_small = ctx.font_small;
  const UiFontView font_micro = ctx.font_micro;

  const UiColor theme_primary = ctx.theme_primary;
  const UiColor theme_secondary = ctx.theme_secondary;
  const UiColor col_white = ctx.col_white;
  const UiColor col_slate = ctx.col_slate;
#if LATTER_PANEL_IPS
  const UiColor col_warm_gold(255, 178, 28);
#else
  const UiColor col_warm_gold = ctx.col_warm_gold;
#endif

  // Authoritative backdrop precedence (resolve_backdrop_mode, D-067)
  std::string sky_cond = ws.condition;
  bool sky_night = (ws.sun_state == "below_horizon" || sky_cond == "clear-night" ||
                    sky_cond == "clearnight" || (sky_cond.find("night") != std::string::npos));
  apply_weather_sim_override(weather_sim_opt, sky_cond, sky_night);
  const bool sim_override_on = (!weather_sim_opt.empty() && weather_sim_opt != "Live (Home Assistant)");
  const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
  {
    const auto &ac = area_ctx_state();
    const bool has_day_night_sky = is_day_night_sky_wallpaper_url(ac.wallpaper_url);
    const bool has_custom_wallpaper =
        (!ac.wallpaper_url.empty() && !has_day_night_sky &&
         ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
         ctx.home_wallpaper_art.valid());
    const bool dns_known = has_known_day_night_phase(ws.sun_state, now.is_valid(), now.hour);
    const bool dns_night = dns_known && resolve_day_night_sky_is_night(ws.sun_state, now.is_valid(), now.hour);
    const BackdropMode bd_mode = resolve_backdrop_mode(
        PageId::WEATHER, 0, prefs.weather_background, prefs.weather_app_background,
        sim_override_on, has_custom_wallpaper, has_day_night_sky);

    if (bd_mode == BackdropMode::LiveWeatherSky) {
      if (((!std::isnan(ws.temperature) && sun_ready) || sim_override_on) &&
          format_weather_title(sky_cond) != "Unavailable") {
        double sky_moon_cycle = 0.5;
        compute_synodic_moon_phase(now.is_valid(), now.is_valid() ? now.timestamp : 0LL,
                                   ws.moon_phase, sky_moon_cycle);
        backdrop_active() = true;
        render_weather_motion_wallpaper(it, cx, cy, ctx.now_ms, sky_cond, sky_night,
                                        sky_moon_cycle, 0, 0,
                                        rs.hw_accel_x, rs.hw_accel_y, false);
      } else if (!backdrop_active()) {
        if (dns_known) {
          render_neutral_day_night_sky_base(it, cx, cy, dns_night);
        } else if (has_custom_wallpaper) {
          render_page_wallpaper_base(it, ctx.home_wallpaper_art, true, cx, ctx.col_bg);
        } else {
          frame_base(it, ctx.col_bg);
        }
      }
    } else if (bd_mode == BackdropMode::DayNightSky) {
      if (!backdrop_active()) {
        if (dns_known) render_static_day_night_sky_base(it, cx, cy, dns_night);
        else frame_base(it, ctx.col_bg);
      }
    } else if (bd_mode == BackdropMode::CustomWallpaper) {
      if (!backdrop_active()) render_page_wallpaper_base(it, ctx.home_wallpaper_art, true, cx, ctx.col_bg);
    } else if (!backdrop_active()) {
      frame_base(it, ctx.col_bg);
    }
  }

  if (sky_weather_subpage == 0) {
    // SUBPAGE 0: CURRENT CONDITIONS
    const auto ly = layout();
    const std::string cond = (!sim_override_on && !sun_ready) ? std::string{} : sky_cond;

    render_page_header(it, cx, "CURRENT", font_small, col_white);

    const int content_top = ly.hdr_y + dp_px(24);
    const int content_bot = ly.mode_bar_zone_top() - dp_px(14);
    const int avail_h = std::max(140, content_bot - content_top);
    const int temp_y = content_top + (avail_h * 22) / 100;
    const int cond_y = content_top + (avail_h * 46) / 100;
    const int hilo_y = content_top + (avail_h * 68) / 100;
    const int tele_y = content_top + (avail_h * 88) / 100;

    char temp_num[16];
    if (!std::isnan(ws.temperature)) std::snprintf(temp_num, sizeof(temp_num), "%.0f", ws.temperature);
    else std::snprintf(temp_num, sizeof(temp_num), "%s", kUnknownGlyph);
    int w_num = 0, x_off = 0, base = 0, th = 0;
    font_time->measure(temp_num, &w_num, &x_off, &base, &th);
    const int num_cx = cx - dp_px(5);
    const int deg_cx = num_cx + (w_num / 2) + dp_px(16);
    const int deg_cy = temp_y - (kAutoFonts.time * 26) / 100;

    if (backdrop_active()) {
      fast_print(it, num_cx + 2, temp_y + 2, font_time, kChromeShadow, UiTextAlign::CENTER, temp_num);
    }
    fast_print(it, num_cx, temp_y, font_time, col_white, UiTextAlign::CENTER, temp_num);

    const float deg_r = std::max(5.0f, dp_f(7.0f));
    const float deg_th = std::max(2.0f, dp_f(2.0f) + 0.35f);
    if (backdrop_active()) fast_circle_aa(it, deg_cx + 1, deg_cy + 1, deg_r, kChromeShadow, deg_th);
    fast_circle_aa(it, deg_cx, deg_cy, deg_r, kChromeDegSilver, deg_th);

    const int cond_max_w = std::min({ly.max_content_w(), ly.safe_span(cond_y, dp_px(16)), dp_px(340)});
    const bool weather_syncing = (!sim_override_on && ws.rev == 0U && ws.epoch == 0U && cond.empty());
    const std::string raw_cond_title = weather_syncing ? std::string("Syncing...") : format_weather_title(cond);
    const std::string cond_title = fit_text_to_width_font(raw_cond_title, cond_max_w, font_date);
    ui::shadow_print(it, cx, cond_y, font_date, theme_secondary, UiTextAlign::CENTER, cond_title.c_str());

    const bool large_cur = (!LATTER_ROUND_DISPLAY && ly.frame.w_dp >= 360 && ly.frame.h_dp >= 400);
    const UiColor icon_c = kChromeTeleIce;
    const bool has_bd_shadow = backdrop_active();

    if (!std::isnan(ws.high) || !std::isnan(ws.low)) {
      char hi_str[16], lo_str[16];
      if (!std::isnan(ws.high)) std::snprintf(hi_str, sizeof(hi_str), "%.0f°", ws.high);
      else std::snprintf(hi_str, sizeof(hi_str), "%s°", kUnknownGlyph);
      if (!std::isnan(ws.low)) std::snprintf(lo_str, sizeof(lo_str), "%.0f°", ws.low);
      else std::snprintf(lo_str, sizeof(lo_str), "%s°", kUnknownGlyph);

      int w_hi = 0, w_lo = 0;
      const UiFontView hilo_font = (large_cur && font_chrono_time != nullptr) ? font_chrono_time : font_date;
      hilo_font->measure(hi_str, &w_hi, &x_off, &base, &th);
      hilo_font->measure(lo_str, &w_lo, &x_off, &base, &th);
      const int arr_slot = dp_px(14), arr_pad = dp_px(10), pair_gap = dp_px(28);
      const int total_hilo_w = arr_slot + arr_pad + w_hi + pair_gap + arr_slot + arr_pad + w_lo;
      const int hilo_left = cx - (total_hilo_w / 2);
      const int up_arr_x = hilo_left + arr_slot / 2;
      const int hi_txt_x = hilo_left + arr_slot + arr_pad;
      const int dn_arr_x = hi_txt_x + w_hi + pair_gap + arr_slot / 2;
      const int lo_txt_x = dn_arr_x + (arr_slot - arr_slot / 2) + arr_pad;
      const int arr_r = std::max(7, dp_px(10));

      ui::draw_icon_arrow_v(it, up_arr_x, hilo_y, arr_r, true, icon_c, has_bd_shadow);
      ui::shadow_print(it, hi_txt_x, hilo_y, hilo_font, col_white, UiTextAlign::CENTER_LEFT, hi_str);
      ui::draw_icon_arrow_v(it, dn_arr_x, hilo_y, arr_r, false, icon_c, has_bd_shadow);
      ui::shadow_print(it, lo_txt_x, hilo_y, hilo_font, col_slate, UiTextAlign::CENTER_LEFT, lo_str);
    }

    char hum_str[20];
    if (!std::isnan(ws.humidity)) std::snprintf(hum_str, sizeof(hum_str), "%.0f%%", ws.humidity);
    else std::snprintf(hum_str, sizeof(hum_str), "%s%%", kUnknownGlyph);
    char wind_str[28];
    const char *w_dir = wind_bearing_to_cardinal(ws.wind_bearing);
    if (!std::isnan(ws.wind_speed)) {
      if (w_dir[0] != '\0') std::snprintf(wind_str, sizeof(wind_str), "%.0f mph %s", std::roundf(ws.wind_speed), w_dir);
      else std::snprintf(wind_str, sizeof(wind_str), "%.0f mph", std::roundf(ws.wind_speed));
    } else {
      std::snprintf(wind_str, sizeof(wind_str), "%s mph", kUnknownGlyph);
    }
    const UiFontView tele_font = large_cur ? font_date : font_small;
    int w_hum = 0, w_wind = 0;
    tele_font->measure(hum_str, &w_hum, &x_off, &base, &th);
    tele_font->measure(wind_str, &w_wind, &x_off, &base, &th);
    const int drop_r = std::max(7, dp_px(10));
    const int wind_r = std::max(9, dp_px(12));
    const int ic_gap = dp_px(8);
    const int mid_gap = dp_px(22);
    const int tele_w = (2 * drop_r + ic_gap + w_hum) + mid_gap + (2 * wind_r + ic_gap + w_wind);
    const int tele_left = cx - tele_w / 2;
    const int drop_x = tele_left + drop_r;
    const int hum_x = drop_x + drop_r + ic_gap;
    const int w_icon_x = hum_x + w_hum + mid_gap + wind_r;
    ui::draw_icon_droplet(it, drop_x, tele_y, drop_r, icon_c, has_bd_shadow);
    ui::shadow_print(it, hum_x, tele_y, tele_font, col_white, UiTextAlign::CENTER_LEFT, hum_str);
    ui::draw_icon_wind(it, w_icon_x, tele_y, wind_r, icon_c, has_bd_shadow);
    ui::shadow_print(it, w_icon_x + wind_r + ic_gap, tele_y, tele_font, col_white, UiTextAlign::CENTER_LEFT, wind_str);

  } else if (sky_weather_subpage == 1) {
    // SUBPAGE 1: 5-DAY FORECAST
    render_page_header(it, cx, "FORECAST", font_small, col_white);

    struct DayForecast {
      std::string day, high, low, cond;
    };
    DayForecast fc_days[5];
    int fc_count = 0;
    {
      std::string_view recs[5];
      const size_t n = v3_split_fields(ws.forecast_5d, recs, 5, kProtocolRecordSep);
      for (size_t r = 0; r < n; r++) {
        std::string_view f[4];
        if (v3_split_fields(recs[r], f, 4, '|') != 4) continue;
        DayForecast &df = fc_days[fc_count++];
        df.day = std::string(f[0]);
        df.cond = std::string(f[1]);
        df.high = f[2].empty() ? std::string(kUnknownGlyph) : std::string(f[2]);
        df.low = f[3].empty() ? std::string(kUnknownGlyph) : std::string(f[3]);
      }
    }
    if (fc_count == 0) {
      render_empty_state(it, cx, SCREEN_CY, "Forecast not available yet", font_small, col_slate);
    } else {
      bool dummy_night = false;
      apply_weather_sim_override(weather_sim_opt, fc_days[0].cond, dummy_night);
    }

    const WeatherForecastGeom fg = compute_weather_forecast_geom(
        SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_PANEL_PPI,
        LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT, LATTER_IS_STATIONARY != 0);
    const UiFontView row_font = fg.large_font ? font_date : font_small;
    const UiFontView sep_font = fg.large_font ? font_small : font_micro;

    for (int i = 0; i < fc_count && i < 5; i++) {
      const int ry = fg.fc_top_y + i * fg.fc_step_y;
      const int ry_data = ry + 1;
      const auto &d = fc_days[i];
      if (!d.cond.empty()) render_forecast_glyph(it, fg.x_glyph, ry_data, d.cond);

      const std::string hi_str = (d.high == kUnknownGlyph) ? d.high : d.high + "°";
      const std::string lo_str = (d.low == kUnknownGlyph) ? d.low : d.low + "°";
      const std::string c_txt = fit_text_to_width_font(
          format_forecast_short_name(d.cond), fg.cond_max_w, row_font);
      ui::shadow_print(it, fg.x_day, ry, row_font, (i == 0) ? theme_primary : col_white, UiTextAlign::CENTER, d.day.c_str());
      ui::shadow_print(it, fg.x_cond, ry_data, row_font, col_slate, UiTextAlign::CENTER_LEFT, c_txt.c_str());
      ui::shadow_print(it, fg.x_slash - fg.slash_gap, ry_data, row_font, col_white, UiTextAlign::CENTER_RIGHT, hi_str.c_str());
      ui::shadow_print(it, fg.x_slash, ry_data, sep_font, col_slate, UiTextAlign::CENTER, "/");
      ui::shadow_print(it, fg.x_slash + fg.slash_gap, ry_data, row_font, col_slate, UiTextAlign::CENTER_LEFT, lo_str.c_str());
    }

  } else if (sky_weather_subpage == 2) {
    // SUBPAGE 2: SUN
    render_page_header(it, cx, "SUN", font_small, col_white);
    render_weather_sun_subpage(it, ctx, col_warm_gold);

  } else if (sky_weather_subpage == 3) {
    // SUBPAGE 3: MOON
    render_page_header(it, cx, "MOON", font_small, col_white);
    render_weather_moon_subpage(it, ctx);
  }

  render_weather_subpage_icons(it, sky_weather_subpage, theme_primary, col_white);
}


}  // namespace latter_ui
