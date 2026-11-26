#include "ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace latter_ui {
using namespace latter_core;

namespace ui {

// ==============================================================================
// 3. Universal Material Design Icon Dispatch (D-051 / D-071)
// ==============================================================================
namespace {

inline void draw_mdi_r(Rgb565Surface &it, const char *slug, int cx, int cy, int r,
                       UiColor col, bool shadow, const char *fallback = "mdi:apps") {
  if (r <= 1) return;
  render_mdi_icon(it, slug, cx, cy, std::max(12, 2 * r), col, shadow, fallback);
}

}  // namespace

void draw_icon_power(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:power", cx, cy, r, col, shadow, "mdi:power");
}
void draw_icon_clock_solid(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:clock", cx, cy, r, col, shadow, "mdi:clock");
}
void draw_icon_hourglass(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:timer-sand", cx, cy, r, col, shadow, "mdi:clock");
}
void draw_icon_alarm_clock(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:alarm", cx, cy, r, col, shadow, "mdi:clock");
}
void draw_icon_stopwatch(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:timer", cx, cy, r, col, shadow, "mdi:clock");
}
void draw_icon_shield_home(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow, bool is_armed) {
  draw_mdi_r(it, is_armed ? "mdi:shield-home" : "mdi:shield-home-outline", cx, cy, r, col, shadow, "mdi:home");
}
void draw_icon_camera(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:video", cx, cy, r, col, shadow, "mdi:cctv");
}
void draw_icon_speaker(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool waves, bool shadow, bool muted) {
  const char *slug = muted ? "mdi:volume-off" : (waves ? "mdi:volume-high" : "mdi:volume-medium");
  draw_mdi_r(it, slug, cx, cy, r, col, shadow, "mdi:volume-high");
}
void draw_icon_thermometer(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:thermometer", cx, cy, r, col, shadow, "mdi:thermostat");
}
void draw_icon_lock(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_locked, bool shadow) {
  draw_mdi_r(it, is_locked ? "mdi:lock" : "mdi:lock-open-variant", cx, cy, r, col, shadow, "mdi:lock");
}
void draw_icon_garage_door(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_open, bool shadow) {
  draw_mdi_r(it, is_open ? "mdi:garage-open" : "mdi:garage", cx, cy, r, col, shadow, "mdi:garage");
}
void draw_icon_robot_vacuum(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:robot-vacuum", cx, cy, r, col, shadow, "mdi:home");
}
void draw_icon_eighth_notes(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:music", cx, cy, r, col, shadow, "mdi:music");
}
void draw_icon_calendar(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar", cx, cy, r, col, shadow, "mdi:calendar");
}
void draw_icon_cal_agenda(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-text", cx, cy, r, col, shadow, "mdi:calendar");
}
void draw_icon_cal_day(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-today", cx, cy, r, col, shadow, "mdi:calendar");
}
void draw_icon_cal_week(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-week", cx, cy, r, col, shadow, "mdi:calendar");
}
void draw_icon_mic(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:microphone", cx, cy, r, col, shadow, "mdi:microphone");
}
void draw_icon_lightbulb(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow, bool is_on) {
  draw_mdi_r(it, is_on ? "mdi:lightbulb" : "mdi:lightbulb-outline", cx, cy, r, col, shadow, "mdi:lightbulb");
}
void draw_icon_ceiling_fan(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:fan", cx, cy, r, col, shadow, "mdi:fan");
}
void draw_icon_blinds_slats(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_open, bool shadow) {
  draw_mdi_r(it, is_open ? "mdi:blinds-horizontal" : "mdi:blinds-horizontal-closed",
             cx, cy, r, col, shadow, "mdi:blinds-horizontal");
}
void draw_icon_switch_pill(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool is_on, bool shadow) {
  draw_mdi_r(it, is_on ? "mdi:toggle-switch" : "mdi:toggle-switch-off-outline",
             cx, cy, r, col, shadow, "mdi:toggle-switch");
}
void draw_icon_sparkle(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:shimmer", cx, cy, r, col, shadow, "mdi:shimmer");
}
void draw_icon_bento_grid(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:view-grid", cx, cy, r, col, shadow, "mdi:apps");
}
void draw_icon_droplet(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:water", cx, cy, r, col, shadow, "mdi:water");
}
void draw_icon_intercom_handset(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:bullhorn", cx, cy, r, col, shadow, "mdi:bullhorn");
}
void draw_icon_bell(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:bell-ring", cx, cy, r, col, shadow, "mdi:bell-ring");
}
void draw_icon_play_pause(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:play-pause", cx, cy, r, col, shadow, "mdi:play-pause");
}
void draw_icon_hamburger(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:menu", cx, cy, r, col, shadow, "mdi:menu");
}
void draw_icon_grid_2x2(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:view-grid", cx, cy, r, col, shadow, "mdi:view-grid");
}
void draw_icon_carousel(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:view-carousel", cx, cy, r, col, shadow, "mdi:view-carousel");
}
void draw_icon_gear(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:cog", cx, cy, r, col, shadow, "mdi:cog");
}
void draw_icon_chevron(Rgb565Surface &it, int cx, int cy, int r, bool is_right, UiColor col, bool shadow) {
  draw_mdi_r(it, is_right ? "mdi:chevron-right" : "mdi:chevron-left", cx, cy, r, col, shadow, "mdi:chevron-right");
}
void draw_icon_plus(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:plus", cx, cy, r, col, shadow, "mdi:plus");
}
void draw_icon_minus(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:minus", cx, cy, r, col, shadow, "mdi:minus");
}
void draw_icon_home(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:home", cx, cy, r, col, shadow, "mdi:home");
}
void draw_icon_forecast_chart(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-week", cx, cy, r, col, shadow, "mdi:calendar-week");
}
void draw_icon_sun(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:white-balance-sunny", cx, cy, r, col, shadow, "mdi:white-balance-sunny");
}
void draw_icon_crescent_moon(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:moon-waning-crescent", cx, cy, r, col, shadow, "mdi:moon-waning-crescent");
}
void draw_icon_wind(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:weather-windy", cx, cy, r, col, shadow, "mdi:weather-windy");
}
void draw_icon_arrow_v(Rgb565Surface &it, int cx, int cy, int r, bool is_up, UiColor col, bool shadow) {
  draw_mdi_r(it, is_up ? "mdi:arrow-up" : "mdi:arrow-down", cx, cy, r, col, shadow, "mdi:arrow-up");
}

const char *route_icon_slug(RouteId route) noexcept {
  switch (route) {
    case RouteId::APP_AREA:            return "mdi:sofa";
    case RouteId::APP_PINNED:          return "mdi:pin";
    case RouteId::APP_MUSIC:           return "mdi:music";
    case RouteId::MUSIC_LIBRARY:       return "mdi:music-box";
    case RouteId::MUSIC_SPEAKERS:      return "mdi:speaker-multiple";
    case RouteId::MUSIC_FAVORITES:     return "mdi:heart";
    case RouteId::MUSIC_PLAYLISTS:     return "mdi:playlist-music";
    case RouteId::MUSIC_QUEUE:         return "mdi:playlist-play";
    case RouteId::MUSIC_ALBUM_TRACKS:  return "mdi:album";
    case RouteId::MUSIC_LYRICS:        return "mdi:script-text";
    case RouteId::MUSIC_INFO:          return "mdi:information";
    case RouteId::APP_HOME_HUB:        return "mdi:home";
    case RouteId::HOME_HUB_ALARM:      return "mdi:shield-home";
    case RouteId::APP_CAMERAS:         return "mdi:cctv";
    case RouteId::CAMERAS_GRID:        return "mdi:view-grid";
    case RouteId::CAMERAS_SINGLE:      return "mdi:video";
    case RouteId::CAMERAS_CAROUSEL:    return "mdi:view-carousel";
    case RouteId::CAMERAS_SETTINGS:    return "mdi:cog";
    case RouteId::HOME_HUB_INTERCOM:   return "mdi:bullhorn";
    case RouteId::HOME_HUB_THERMOSTAT: return "mdi:thermostat";
    case RouteId::AREA_CLIMATE:        return "mdi:thermometer";
    case RouteId::HOME_HUB_ENTRY:      return "mdi:lock";
    case RouteId::HOME_HUB_VACUUM:     return "mdi:robot-vacuum";
    case RouteId::AREA_PICKER:         return "mdi:floor-plan";
    case RouteId::AREA_SCENE:          return "mdi:shimmer";
    case RouteId::AREA_LIGHT:          return "mdi:lightbulb";
    case RouteId::AREA_COVER:          return "mdi:blinds-horizontal";
    case RouteId::AREA_FAN:            return "mdi:fan";
    case RouteId::AREA_SWITCH:         return "mdi:toggle-switch";
    case RouteId::AREA_MEDIA:          return "mdi:television-speaker";
    case RouteId::AREA_ALL:            return "mdi:view-grid";
    case RouteId::APP_REMOTE:          return "mdi:remote-tv";
    case RouteId::REMOTE_DEVICES:      return "mdi:remote";
    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:     return "mdi:weather-partly-cloudy";
    case RouteId::WEATHER_FORECAST:    return "mdi:calendar-week";
    case RouteId::WEATHER_SUN:         return "mdi:white-balance-sunny";
    case RouteId::WEATHER_MOON:        return "mdi:moon-waning-crescent";
    case RouteId::APP_TIME:
    case RouteId::APP_CLOCK:
    case RouteId::TIME_CLOCK:          return "mdi:clock";
    case RouteId::TIME_TIMERS:         return "mdi:timer-sand";
    case RouteId::TIME_ALARMS:         return "mdi:alarm";
    case RouteId::TIME_STOPWATCH:
    case RouteId::TOOLS_STOPWATCH:     return "mdi:timer";
    case RouteId::APP_TOOLS:           return "mdi:view-grid";
    case RouteId::TOOLS_LEVEL:         return "mdi:spirit-level";
    case RouteId::TOOLS_FLASHLIGHT:    return "mdi:flashlight";
    case RouteId::TOOLS_CALCULATOR:    return "mdi:calculator-variant";
    case RouteId::APP_CALENDAR:        return "mdi:calendar";
    case RouteId::CALENDAR_OVERVIEW:   return "mdi:calendar-text";
    case RouteId::CALENDAR_DAY:        return "mdi:calendar-today";
    case RouteId::CALENDAR_WEEK:       return "mdi:calendar-week";
    case RouteId::CALENDAR_MONTH:      return "mdi:calendar-month";
    case RouteId::TOOLS_DICE:          return "mdi:dice-5";
    case RouteId::TOOLS_COIN_FLIP:     return "mdi:alpha-c-circle";
    case RouteId::APP_GAMES:           return "mdi:controller";
    case RouteId::GAMES_MARBLES:       return "mdi:billiards";
    case RouteId::GAMES_ARCHERY:       return "mdi:bullseye-arrow";
    case RouteId::GAMES_BRICKS:        return "mdi:wall";
    case RouteId::GAMES_CHOMP:         return "mdi:pac-man";
    case RouteId::GAMES_BLOCKS:        return "mdi:toy-brick";
    case RouteId::GAMES_SNAKE:         return "mdi:snake";
    case RouteId::GAMES_DROP_FOUR:     return "mdi:dots-grid";
    case RouteId::SETTINGS_DISPLAY:    return "mdi:brightness-6";
    case RouteId::SETTINGS_SLEEP:      return "mdi:power-sleep";
    case RouteId::SETTINGS_SOUND:      return "mdi:volume-high";
    case RouteId::SETTINGS_VOICE:      return "mdi:microphone";
    case RouteId::SETTINGS_MUSIC:      return "mdi:music-box";
    case RouteId::SETTINGS_HOME:       return "mdi:home";
    case RouteId::SETTINGS_ACTIONS:    return "mdi:gesture-tap-button";
    case RouteId::SETTINGS_UPDATE:     return "mdi:cellphone-arrow-down";
    case RouteId::SETTINGS_INFO:       return "mdi:information";
    case RouteId::SETTINGS_ROOT:
    case RouteId::NONE:
    default:                           return "mdi:cog";
  }
}

void draw_route_icon(Rgb565Surface &it, RouteId route, int cx, int cy, int r,
                     UiColor col, UiColor cut_c, bool shadow) {
  (void)cut_c;
  const int mdi_px = std::max(14, 2 * r);
  if (route == RouteId::APP_AREA) {
    render_mdi_icon(it, resolve_area_dock_icon(area_ctx_state()), cx, cy, mdi_px, col, shadow, "mdi:sofa");
    return;
  }
  const char *slug = route_icon_slug(route);
  const char *fb = "mdi:cog";
  switch (route) {
    case RouteId::APP_PINNED:          fb = "mdi:pin"; break;
    case RouteId::APP_MUSIC:
    case RouteId::MUSIC_LIBRARY:
    case RouteId::MUSIC_SPEAKERS:
    case RouteId::MUSIC_FAVORITES:
    case RouteId::MUSIC_PLAYLISTS:
    case RouteId::MUSIC_QUEUE:
    case RouteId::MUSIC_ALBUM_TRACKS:
    case RouteId::MUSIC_LYRICS:
    case RouteId::MUSIC_INFO:          fb = "mdi:music"; break;
    case RouteId::APP_HOME_HUB:
    case RouteId::HOME_HUB_ALARM:
    case RouteId::HOME_HUB_INTERCOM:
    case RouteId::HOME_HUB_ENTRY:
    case RouteId::HOME_HUB_VACUUM:     fb = "mdi:home"; break;
    case RouteId::APP_CAMERAS:         fb = "mdi:video"; break;
    case RouteId::CAMERAS_GRID:
    case RouteId::CAMERAS_SINGLE:
    case RouteId::CAMERAS_CAROUSEL:    fb = "mdi:cctv"; break;
    case RouteId::CAMERAS_SETTINGS:    fb = "mdi:cog"; break;
    case RouteId::HOME_HUB_THERMOSTAT: fb = "mdi:thermometer"; break;
    case RouteId::AREA_CLIMATE:        fb = "mdi:thermostat"; break;
    case RouteId::AREA_PICKER:         fb = "mdi:map-marker"; break;
    case RouteId::AREA_SCENE:
    case RouteId::AREA_LIGHT:
    case RouteId::AREA_COVER:
    case RouteId::AREA_FAN:
    case RouteId::AREA_SWITCH:         fb = "mdi:sofa"; break;
    case RouteId::AREA_MEDIA:          fb = "mdi:speaker"; break;
    case RouteId::AREA_ALL:
    case RouteId::APP_TOOLS:           fb = "mdi:apps"; break;
    case RouteId::APP_REMOTE:          fb = "mdi:remote"; break;
    case RouteId::REMOTE_DEVICES:      fb = "mdi:remote-tv"; break;
    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:
    case RouteId::WEATHER_FORECAST:    fb = "mdi:cloud"; break;
    case RouteId::WEATHER_SUN:         fb = "mdi:weather-sunny"; break;
    case RouteId::WEATHER_MOON:        fb = "mdi:weather-night"; break;
    case RouteId::APP_TIME:
    case RouteId::APP_CLOCK:
    case RouteId::TIME_CLOCK:
    case RouteId::TIME_TIMERS:
    case RouteId::TIME_ALARMS:
    case RouteId::TIME_STOPWATCH:
    case RouteId::TOOLS_STOPWATCH:     fb = "mdi:clock"; break;
    case RouteId::TOOLS_LEVEL:         fb = "mdi:compass"; break;
    case RouteId::TOOLS_FLASHLIGHT:    fb = "mdi:lightbulb"; break;
    case RouteId::TOOLS_CALCULATOR:    fb = "mdi:calculator"; break;
    case RouteId::APP_CALENDAR:
    case RouteId::CALENDAR_OVERVIEW:
    case RouteId::CALENDAR_DAY:
    case RouteId::CALENDAR_WEEK:
    case RouteId::CALENDAR_MONTH:      fb = "mdi:calendar"; break;
    case RouteId::TOOLS_DICE:          fb = "mdi:dice-multiple"; break;
    case RouteId::TOOLS_COIN_FLIP:     fb = "mdi:circle-double"; break;
    case RouteId::APP_GAMES:           fb = "mdi:gamepad-variant"; break;
    case RouteId::GAMES_MARBLES:       fb = "mdi:circle-multiple"; break;
    case RouteId::GAMES_ARCHERY:       fb = "mdi:target"; break;
    case RouteId::GAMES_BRICKS:        fb = "mdi:view-grid"; break;
    case RouteId::GAMES_CHOMP:         fb = "mdi:ghost"; break;
    case RouteId::GAMES_BLOCKS:        fb = "mdi:view-grid"; break;
    case RouteId::GAMES_SNAKE:         fb = "mdi:gamepad-variant"; break;
    case RouteId::GAMES_DROP_FOUR:     fb = "mdi:circle-multiple"; break;
    default:                           fb = "mdi:cog"; break;
  }
  render_mdi_icon(it, slug, cx, cy, mdi_px, col, shadow, fb);
}

}  // namespace ui

// ==============================================================================
// 1. Scrollbars, Page Pips, Mode Bar Track & Progress Arc
// ==============================================================================
void render_viewport_scrollbar(Rgb565Surface &it, int cx, int y_pos, int total_items,
                               float cur_start_idx, UiColor col_thumb, bool is_dim) {
  const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
  if (total_items <= vis_cap) return;
  const int track_w = std::min(layout().safe_span(y_pos, dp_px(20)), std::max(84, dp_px(112)));
  const int track_h = std::max(4, dp_px(4));
  const int r_cap = track_h / 2;
  const int track_x0 = cx - (track_w / 2);
  const int track_y0 = y_pos - r_cap;

  const float cap = static_cast<float>(r_cap);
  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_styled_glass(track_x0, track_y0, track_w, track_h,
                       kChromeGlassTint, kChromeCardBorder,
                       GlassVariant::Clear, kChromeGlassSheen, is_dim ? 72 : -1, false,
                       bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
  if (is_dim) {
    rim_top = (rim_top * 2) / 3;
    rim_bot = (rim_bot * 2) / 3;
  }
  fill_round_rect_glass(it, track_x0, track_y0, track_w, track_h, cap,
                        rim_col, rim_top, rim_bot, 1);
  if (track_h > 2) {
    fill_round_rect_glass(it, track_x0 + 1, track_y0 + 1, track_w - 2, track_h - 2,
                          std::max(0.5f, cap - 1.0f), eff_tint, a_top, a_bot);
  }

  const int min_thumb_w = std::max(20, dp_px(20));
  const int thumb_w = std::max(min_thumb_w, std::min(track_w, (track_w * vis_cap) / total_items));
  const float max_start = static_cast<float>(std::max(1, total_items - vis_cap));
  const float clamped_start = std::max(0.0f, std::min(max_start, cur_start_idx));
  const float frac = clamped_start / max_start;
  const int thumb_x0 = track_x0 + static_cast<int>(std::round(frac * static_cast<float>(track_w - thumb_w)));

  const UiColor thumb_tint =
      mix_color_q8(kChromeGlassSheen, col_thumb, is_semantic_glass_color(col_thumb) ? 96 : 48);
  fill_round_rect_glass(it, thumb_x0, track_y0, thumb_w, track_h, cap,
                        thumb_tint, is_dim ? 136 : 216, is_dim ? 120 : 196);
}

void render_dock_page_pips(Rgb565Surface &it, int cx, int y_pos, int total_pages,
                           int active_page, UiColor col_active, UiColor col_inactive) {
  if (total_pages <= 1) return;
  const int dot_r = std::max(2, dp_px(2));
  const int max_span = std::max(40, layout().safe_span(y_pos, dp_px(24)));
  const int base_spacing = std::max(10, dp_px(14));
  int vis_count = total_pages;
  int win_start = 0;
  int spacing = base_spacing;
  if ((vis_count - 1) * spacing > max_span) {
    spacing = std::max(8, max_span / std::max(1, vis_count - 1));
    if ((vis_count - 1) * spacing > max_span) {
      vis_count = std::max(3, (max_span / spacing) + 1);
      win_start = std::clamp(active_page - vis_count / 2, 0, total_pages - vis_count);
    }
  }
  const int total_w = (vis_count - 1) * spacing;
  const int start_x = cx - (total_w / 2);
  for (int i = 0; i < vis_count; i++) {
    const int p = win_start + i;
    const int px = start_x + i * spacing;
    if (p == active_page) {
      const int pw = std::min(dp_px(7), std::max(3, spacing - 2 * dot_r - 2));
      const int ph = dot_r * 2 + 1;
      const int sx0 = px - pw / 2 - dot_r;
      const int sw = pw + 2 * dot_r + 1;
      const float cap = static_cast<float>(dot_r) + 0.5f;
      fill_round_rect_aa(it, sx0 + 1, y_pos - dot_r + 1, sw, ph, cap, kChromeShadow);
      fill_round_rect_aa(it, sx0, y_pos - dot_r, sw, ph, cap, col_active);
    } else {
      fast_filled_circle_aa(it, px + 1, y_pos + 1, dot_r, kChromeShadow);
      fast_filled_circle_aa(it, px, y_pos, dot_r, col_inactive);
    }
  }
}

void render_vertical_page_pips(Rgb565Surface &it, bool show_up, bool show_down,
                               UiColor col_bg, UiColor col_active,
                               UiColor col_inactive, int bottom_limit_y) {
  (void)col_inactive;
  if (!show_up && !show_down) return;
  const auto ly = layout();
  int mask_x = 0, chev_x = 0, hit_x0 = 0, hit_w = 0, min_hit_h = 0;
  compute_vertical_page_pips_x(SCREEN_W, LATTER_ROUND_DISPLAY != 0, LATTER_PANEL_PPI,
                               mask_x, chev_x, hit_x0, hit_w, min_hit_h);
  const bool above_bar = (bottom_limit_y < SCREEN_H - dp_px(40));
  const int vp_top = ly.hdr_y + dp_px(30);
  const int vp_bot = std::min(bottom_limit_y, above_bar ? bottom_limit_y : (ly.back_y - dp_px(20)));
  const int mid_y = (vp_top + vp_bot) / 2;
  const int sep = std::min(dp_px(56), std::max(dp_px(32), (vp_bot - vp_top) / 4));
  const int up_y = mid_y - sep;
  const int dn_y = mid_y + sep;
  const int split_y = mid_y;
  const int max_dn_y1 = std::min(vp_bot, bottom_limit_y);
  if (!backdrop_active() && mask_x < SCREEN_W) {
    const int mask_y0 = std::max(0, up_y - dp_px(22));
    const int mask_y1 = std::min(bottom_limit_y, dn_y + dp_px(22));
    if (mask_y1 > mask_y0) {
      fill_rect_row_major(it, mask_x, mask_y0, SCREEN_W - mask_x, mask_y1 - mask_y0, col_bg);
    }
  }
  if (show_up) {
    render_scroll_chevron(it, chev_x, up_y, true, col_active, kChromeShadow, backdrop_active());
    const int up_y0 = std::min(vp_top, std::max(0, split_y - min_hit_h));
    const int up_h = std::max(min_hit_h, split_y - up_y0);
    frame_hits().add_rect(hit_x0, up_y0, hit_w, std::min(bottom_limit_y - up_y0, up_h),
                          WidgetId::CHEVRON_UP, kHitTap | kHitFocusSlot);
  }
  if (show_down) {
    render_scroll_chevron(it, chev_x, dn_y, false, col_active, kChromeShadow, backdrop_active());
    const int dn_avail = std::max(0, max_dn_y1 - split_y);
    if (dn_avail > 0) {
      frame_hits().add_rect(hit_x0, split_y, hit_w, dn_avail,
                            WidgetId::CHEVRON_DOWN, kHitTap | kHitFocusSlot);
    }
  }
}

void render_in_app_mode_bar_track(Rgb565Surface &it, int cx, const int *win_xs,
                                  int vis_count, int active_vis_idx, bool has_left_arrow,
                                  bool has_right_arrow, int total_count, int active_global_idx,
                                  UiColor theme_primary, UiColor col_white,
                                  int explicit_win_start) {
  if (vis_count <= 0 || win_xs == nullptr) return;

  const uint32_t now_ms = hal_millis_now();
  const int win_start_idx = (explicit_win_start >= 0)
                                ? explicit_win_start
                                : ((active_vis_idx >= 0) ? (active_global_idx - active_vis_idx) : 0);
  const float anim_start = mode_bar_anim().step_window(win_start_idx, now_ms);
  const float anim_active = mode_bar_anim().step_active(active_global_idx, now_ms);

  const int by = layout().app_dock_y;
  const int dr = layout().dock_r;
  const int r_track = dr + std::max(2, dp_px(3));
  const int left_cx = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride = get_dock_slot_stride(win_xs, vis_count);

  const int tx0 = left_cx - r_track;
  const int tw = (right_cx - left_cx) + 2 * r_track + 1;
  const int th = 2 * r_track + 1;
  render_glass_card(it, tx0, by - r_track, tw, th, r_track, kChromeGlassTint, -1, GlassVariant::Clear);

  const float rel_active = anim_active - anim_start;
  if (rel_active >= -0.55f && rel_active <= static_cast<float>(vis_count - 1) + 0.55f) {
    const int abx = left_cx + static_cast<int>(std::round(rel_active * static_cast<float>(stride)));
    if (abx >= left_cx - dp_px(16) && abx <= right_cx + dp_px(16)) {
      float ring_alpha = 1.0f;
      if (rel_active < 0.0f) {
        ring_alpha = std::max(0.20f, 1.0f + rel_active * 1.6f);
      } else if (rel_active > static_cast<float>(vis_count - 1)) {
        ring_alpha = std::max(0.20f, 1.0f - (rel_active - static_cast<float>(vis_count - 1)) * 1.6f);
      }
      const UiColor slot_rim = mix_color_q8(kChromeGlassSheen, theme_primary, kGlassSemanticRimMaxQ8);
      const int slot_rim_top = static_cast<int>(std::round(132.0f * ring_alpha));
      const int slot_rim_bot = static_cast<int>(std::round(64.0f * ring_alpha));
      const int slot_wash_top = static_cast<int>(std::round(static_cast<float>(kGlassSemanticWashMaxQ8) * ring_alpha));
      const int slot_wash_bot = static_cast<int>(std::round(24.0f * ring_alpha));
      fill_round_rect_glass(it, abx - dr, by - dr, 2 * dr + 1, 2 * dr + 1,
                            static_cast<float>(dr) + 0.5f, slot_rim, slot_rim_top, slot_rim_bot, 1);
      fill_round_rect_glass(it, abx - dr + 1, by - dr + 1, 2 * dr - 1, 2 * dr - 1,
                            static_cast<float>(dr) - 0.5f, theme_primary, slot_wash_top, slot_wash_bot);
    }
  }

  const int mode_pad = std::max(dp_px(8), layout().dock_hit_r() - dr);
  for (int i = 0; i < vis_count && i < kMaxDockSlots; i++) {
    frame_hits().add_circle(win_xs[i], by, dr,
                            static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                            kHitTap | kHitFocusSlot | kHitFocusCircle,
                            static_cast<int16_t>(win_start_idx + i),
                            mode_pad);
  }

  const int chev_off = dr + std::max(9, dp_px(12));
  const int ch_s = std::max(4, dp_px(5));
  if (has_left_arrow) {
    const int lx = left_cx - chev_off;
    fast_line(it, lx + ch_s / 2, by - ch_s, lx - ch_s / 2, by, col_white);
    fast_line(it, lx - ch_s / 2, by, lx + ch_s / 2, by + ch_s, col_white);
    fast_line(it, lx + ch_s / 2 + 1, by - ch_s, lx - ch_s / 2 + 1, by, theme_primary);
    fast_line(it, lx - ch_s / 2 + 1, by, lx + ch_s / 2 + 1, by + ch_s, theme_primary);
    frame_hits().add_rect(0, by - dr - dp_px(12), std::max(8, left_cx - dr),
                          2 * (dr + dp_px(12)), WidgetId::MODE_BAR_LEFT, kHitTap | kHitFocusSlot);
  }
  if (has_right_arrow) {
    const int rx = right_cx + chev_off;
    fast_line(it, rx - ch_s / 2, by - ch_s, rx + ch_s / 2, by, col_white);
    fast_line(it, rx + ch_s / 2, by, rx - ch_s / 2, by + ch_s, col_white);
    fast_line(it, rx - ch_s / 2 - 1, by - ch_s, rx + ch_s / 2 - 1, by, theme_primary);
    fast_line(it, rx + ch_s / 2 - 1, by, rx - ch_s / 2 - 1, by + ch_s, theme_primary);
    frame_hits().add_rect(right_cx + dr, by - dr - dp_px(12),
                          std::max(8, SCREEN_W - (right_cx + dr)),
                          2 * (dr + dp_px(12)), WidgetId::MODE_BAR_RIGHT, kHitTap | kHitFocusSlot);
  }

  if (total_count > std::clamp(layout().dock_slot_count, 1, kMaxDockSlots)) {
    render_viewport_scrollbar(it, cx, layout().app_dock_pips_y, total_count, anim_start,
                              theme_primary, false);
  }
}

void render_progress_arc_r224(Rgb565Surface &it, int cx, int cy, float prog, UiColor col) {
#if !LATTER_ROUND_DISPLAY
  if (prog <= 0.001f) return;
  if (prog > 1.0f) prog = 1.0f;
  (void)cx;
  (void)cy;
  const int bar_y = 6;
  const int bar_x0 = std::max(36, bezel_x_inset_for(bar_y, SCREEN_W, SCREEN_H, LATTER_CORNER_R, false, 10));
  const int bar_w = std::max(32, SCREEN_W - 2 * bar_x0);
  const int fill_w = static_cast<int>(prog * static_cast<float>(bar_w));
  fill_rect_row_major(it, bar_x0, bar_y, bar_w, 4, kChromeTrack);
  if (fill_w > 0) {
    fill_rect_row_major(it, bar_x0, bar_y, fill_w, 4, col);
  }
  return;
#else
  if (prog <= 0.003f) return;
  if (prog > 1.0f) prog = 1.0f;

  static constexpr int R_BORDER = SCREEN_MIN_R - 7;
  static constexpr int R_IN = SCREEN_MIN_R - 6;
  static constexpr int R_OUT = SCREEN_MIN_R - 1;
  static constexpr int R_MID = SCREEN_MIN_R - 4;
  static constexpr int R_BORDER_SQ = R_BORDER * R_BORDER;
  static constexpr int R_IN_SQ = R_IN * R_IN;

  const float theta = prog * (2.0f * PI_F);
  float sin_t = 0.0f, cos_t = 1.0f;
  fast_sincos(theta, sin_t, cos_t);
  const float ux = sin_t;
  const float uy = -cos_t;
  const bool past_half = (prog >= 0.5f);

  const UiColor col_hi{
      static_cast<uint8_t>(col.r + ((255 - col.r) >> 2)),
      static_cast<uint8_t>(col.g + ((255 - col.g) >> 2)),
      static_cast<uint8_t>(col.b + ((255 - col.b) >> 2)),
  };

  auto in_sweep = [&](int dx, int dy) -> bool {
    if (dx >= 0) {
      if (past_half) return true;
      return (static_cast<float>(dx) * uy - static_cast<float>(dy) * ux) >= 0.0f;
    }
    if (!past_half) return false;
    return (static_cast<float>(dx) * uy - static_cast<float>(dy) * ux) >= 0.0f;
  };

  int cur_dx_in = 0;
  int cur_dx_brd = 0;
  for (int dy = -R_OUT; dy <= R_OUT; dy++) {
    const int y = cy + dy;
    if (y < 0 || y >= SCREEN_H) continue;
    const int dy2 = dy * dy;
    const int dx_out = circle_half_span(y, 0);
    if (dy2 < R_IN_SQ) {
      while ((cur_dx_in + 1) * (cur_dx_in + 1) + dy2 <= R_IN_SQ) cur_dx_in++;
      while (cur_dx_in * cur_dx_in + dy2 > R_IN_SQ) cur_dx_in--;
    } else {
      cur_dx_in = 0;
    }
    if (dy2 < R_BORDER_SQ) {
      while ((cur_dx_brd + 1) * (cur_dx_brd + 1) + dy2 <= R_BORDER_SQ) cur_dx_brd++;
      while (cur_dx_brd * cur_dx_brd + dy2 > R_BORDER_SQ) cur_dx_brd--;
    } else {
      cur_dx_brd = 0;
    }
    const int dx_in = cur_dx_in;
    const int dx_brd = cur_dx_brd;

    if (past_half) {
      const int span = dx_out - dx_in + 1;
      if (dx_brd > 0 && dx_in > dx_brd) {
        fast_hline(it, cx + dx_brd, y, dx_in - dx_brd, kChromeShadow);
      }
      if (span > 0) {
        fast_hline(it, cx + dx_in, y, span, col);
        if (span >= 3) {
          fast_pixel(it, cx + dx_in + (span >> 1), y, col_hi);
        }
      }
    } else if (in_sweep(dx_in, dy) || in_sweep(dx_out, dy)) {
      int x0 = -1, x1 = -1;
      for (int dx = dx_in; dx <= dx_out; dx++) {
        if (in_sweep(dx, dy)) {
          if (x0 < 0) x0 = dx;
          x1 = dx;
        }
      }
      if (x0 >= 0) {
        if (dx_brd > 0 && x0 == dx_in && dx_in > dx_brd) {
          fast_hline(it, cx + dx_brd, y, dx_in - dx_brd, kChromeShadow);
        }
        const int span = x1 - x0 + 1;
        fast_hline(it, cx + x0, y, span, col);
        if (span >= 3) {
          fast_pixel(it, cx + x0 + (span >> 1), y, col_hi);
        }
      }
    }

    if (past_half && dx_out > 0) {
      const int left_max_dx = (dx_in > 0) ? -dx_in : -1;
      if (in_sweep(-dx_out, dy) && in_sweep(left_max_dx, dy)) {
        const int span = left_max_dx - (-dx_out) + 1;
        if (span > 0) {
          fast_hline(it, cx - dx_out, y, span, col);
          if (span >= 3) {
            fast_pixel(it, cx - dx_out + (span >> 1), y, col_hi);
          }
        }
        if (dx_brd > 0 && dx_in > dx_brd) {
          fast_hline(it, cx - dx_in + 1, y, dx_in - dx_brd, kChromeShadow);
        }
      } else if (in_sweep(-dx_out, dy) || in_sweep(left_max_dx, dy)) {
        int x0 = 1, x1 = 0;
        for (int dx = -dx_out; dx <= left_max_dx; dx++) {
          if (in_sweep(dx, dy)) {
            if (x0 > 0) x0 = dx;
            x1 = dx;
          }
        }
        if (x0 <= 0) {
          const int span = x1 - x0 + 1;
          fast_hline(it, cx + x0, y, span, col);
          if (span >= 3) {
            fast_pixel(it, cx + x0 + (span >> 1), y, col_hi);
          }
        }
      }
    }
  }

  fast_filled_circle(it, cx, cy - R_MID, 2, col);
  const int tip_x = cx + static_cast<int>(std::round(static_cast<float>(R_MID) * ux));
  const int tip_y = cy + static_cast<int>(std::round(static_cast<float>(R_MID) * uy));
  fast_filled_circle(it, tip_x, tip_y, 2, col);
#endif
}

namespace ui {

// ==============================================================================
// 2. Canonical Mode Bar & Header Pager Bar (D-070)
// ==============================================================================
void mode_bar(Rgb565Surface &it, int cx,
              int total_count, int active_global_idx, int win_start,
              UiColor theme_primary, UiColor col_white,
              ModeBarDrawIconFn draw_icon_fn, void *user_ctx) {
  if (total_count <= 0) return;
  const int vis_cap = std::clamp(layout().dock_slot_count, 1, kMaxDockSlots);
  const int start_idx = get_mode_bar_start_index(total_count, win_start, vis_cap);
  const int vis_count = std::min(vis_cap, total_count - start_idx);
  if (vis_count <= 0) return;

  int win_xs[kMaxDockSlots] = {};
  bool has_l = false;
  bool has_r = false;
  get_mode_bar_window_xs(total_count, start_idx, cx, win_xs, has_l, has_r);

  const int active_vis_idx =
      (active_global_idx >= start_idx && active_global_idx < start_idx + vis_count)
          ? (active_global_idx - start_idx)
          : -1;
  render_in_app_mode_bar_track(it, cx, win_xs, vis_count, active_vis_idx,
                               has_l, has_r, total_count, active_global_idx,
                               theme_primary, col_white, start_idx);

  if (draw_icon_fn == nullptr) return;
  const int by = layout().app_dock_y;
  const int ir = dock_icon_r(layout().dock_r);
  const int left_cx = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride = get_dock_slot_stride(win_xs, vis_count);
  const float anim_start = mode_bar_anim().cur_start;

  for (int g_idx = 0; g_idx < total_count; ++g_idx) {
    const float rel_slot = static_cast<float>(g_idx) - anim_start;
    if (rel_slot < -0.55f || rel_slot > static_cast<float>(vis_count - 1) + 0.55f) continue;
    const int bx = left_cx + static_cast<int>(std::lround(rel_slot * static_cast<float>(stride)));
    if (bx < left_cx - dp_px(16) || bx > right_cx + dp_px(16)) continue;

    const bool is_act = (g_idx == active_global_idx);
    const UiColor raw_c = is_act ? theme_primary : col_white;
    float alpha = 1.0f;
    if (rel_slot < 0.0f) {
      alpha = std::max(0.25f, 1.0f + rel_slot * 1.4f);
    } else if (rel_slot > static_cast<float>(vis_count - 1)) {
      alpha = std::max(0.25f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.4f);
    }
    const int alpha_q8 = std::clamp(static_cast<int>(std::lround(alpha * 256.0f)), 0, 256);
    const UiColor fg_c = mix_color_q8(kChromeBarTrack, raw_c, alpha_q8);
    draw_icon_fn(it, g_idx, bx, by, ir, fg_c, (alpha > 0.65f), user_ctx);
  }
}

void header_pager_bar(Rgb565Surface &it, int cx, int cy,
                      const std::string &title, UiFontView font,
                      UiColor col_white, bool show_arrows,
                      uint16_t prev_wid, uint16_t next_wid,
                      int16_t prev_param, int16_t next_param,
                      bool prev_enabled, bool next_enabled) {
  if (font == nullptr) return;
  const auto &ly = layout();
  if (!show_arrows) {
    const int max_w = std::min(ly.max_content_w(), std::max(80, ly.safe_span(cy, dp_px(16))));
    const std::string fit = fit_text_to_width_font(title, max_w, font);
    shadow_print(it, cx, cy, font, col_white, UiTextAlign::CENTER, fit.c_str());
    return;
  }

  const int btn_r = std::max(14, dp_px(17));
  const int safe_w = std::min(ly.max_content_w(), ly.band_safe_span(cy, 2 * btn_r, dp_px(14)));
  const int span_w = std::min(safe_w, dp_px(300));
  const int lx = cx - span_w / 2 + btn_r;
  const int rx = cx + span_w / 2 - btn_r;
  const int gap = dp_px(10);
  const int title_max_w = std::max(48, (rx - btn_r) - (lx + btn_r) - 2 * gap);

  const int min_r = (ly.min_hit_px() + 1) / 2;
  const int hit_pad = std::max(dp_px(6), min_r - btn_r);
  const int ir = dock_icon_r(btn_r);

  render_glass_circle(it, lx, cy, btn_r, chrome_card_bg_off(),
                      prev_enabled ? chrome_card_brd_on() : chrome_card_brd_off());
  draw_icon_chevron(it, lx, cy, ir, false,
                    prev_enabled ? col_white : kChromePipInactive, prev_enabled);
  if (prev_enabled && prev_wid != WidgetId::NONE) {
    frame_hits().add_circle(lx, cy, btn_r, prev_wid,
                            kHitTap | kHitFocusSlot | kHitFocusCircle, prev_param, hit_pad);
  }

  render_glass_circle(it, rx, cy, btn_r, chrome_card_bg_off(),
                      next_enabled ? chrome_card_brd_on() : chrome_card_brd_off());
  draw_icon_chevron(it, rx, cy, ir, true,
                    next_enabled ? col_white : kChromePipInactive, next_enabled);
  if (next_enabled && next_wid != WidgetId::NONE) {
    frame_hits().add_circle(rx, cy, btn_r, next_wid,
                            kHitTap | kHitFocusSlot | kHitFocusCircle, next_param, hit_pad);
  }

  const std::string fit = fit_text_to_width_font(title, title_max_w, font);
  shadow_print(it, cx, cy, font, col_white, UiTextAlign::CENTER, fit.c_str());
}

// ==============================================================================
// 3. Shared Sub-Pixel SDF Horology Primitives
// ==============================================================================
void draw_aa_line(Rgb565Surface &it, float x0, float y0, float x1, float y1,
                  float width, UiColor col, int alpha_max) {
  if (width <= 0.05f || alpha_max <= 0) return;
  const float r = 0.5f * width;
  const float pad = r + 1.25f;
  const int min_y = std::max(clip_y0(), static_cast<int>(std::floor(std::min(y0, y1) - pad)));
  const int max_y = std::min(clip_y1(), static_cast<int>(std::ceil(std::max(y0, y1) + pad)));
  if (min_y > max_y) return;

  const int bbox_x0 = std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - pad)));
  const int bbox_x1 = std::min(SCREEN_W - 1, static_cast<int>(std::ceil(std::max(x0, x1) + pad)));
  if (bbox_x0 > bbox_x1) return;

  const float vx = x1 - x0;
  const float vy = y1 - y0;
  const float len2 = vx * vx + vy * vy;
  const float inv_len2 = (len2 > 1e-4f) ? (1.0f / len2) : 0.0f;
  const bool steep_enough = (std::fabs(vy) > 0.75f);
  const float slope_xy = steep_enough ? (vx / vy) : 0.0f;
  const float row_half_span = steep_enough
                                  ? (pad * std::sqrt(1.0f + slope_xy * slope_xy) + 1.5f)
                                  : 0.0f;

  for (int py = min_y; py <= max_y; ++py) {
    const float yc = static_cast<float>(py) + 0.5f;
    int rx0 = bbox_x0;
    int rx1 = bbox_x1;
    if (steep_enough) {
      const float x_cross = x0 + (yc - y0) * slope_xy;
      rx0 = std::max(bbox_x0, static_cast<int>(std::floor(x_cross - row_half_span)));
      rx1 = std::min(bbox_x1, static_cast<int>(std::ceil(x_cross + row_half_span)));
    }
    for (int px = rx0; px <= rx1; ++px) {
      const float xc = static_cast<float>(px) + 0.5f;
      const float t = (inv_len2 > 0.0f)
                          ? std::clamp(((xc - x0) * vx + (yc - y0) * vy) * inv_len2, 0.0f, 1.0f)
                          : 0.0f;
      const float dx = xc - (x0 + t * vx);
      const float dy = yc - (y0 + t * vy);
      const float dist = std::sqrt(dx * dx + dy * dy);
      const float cov = r + 0.5f - dist;
      if (cov > 0.0f) {
        const int a = (cov >= 1.0f) ? alpha_max
                                    : ((static_cast<int>(cov * 255.0f + 0.5f) * alpha_max) / 255);
        fast_pixel_blend(it, px, py, col, a);
      }
    }
  }
}

void draw_aa_disc(Rgb565Surface &it, float cx, float cy, float r, UiColor col, int alpha_max) {
  if (r <= 0.05f || alpha_max <= 0) return;
  const float pad = r + 1.0f;
  const int min_y = std::max(clip_y0(), static_cast<int>(std::floor(cy - pad)));
  const int max_y = std::min(clip_y1(), static_cast<int>(std::ceil(cy + pad)));
  if (min_y > max_y) return;
  const int min_x = std::max(0, static_cast<int>(std::floor(cx - pad)));
  const int max_x = std::min(SCREEN_W - 1, static_cast<int>(std::ceil(cx + pad)));
  if (min_x > max_x) return;

  for (int py = min_y; py <= max_y; ++py) {
    const float dy = (static_cast<float>(py) + 0.5f) - cy;
    const float dy2 = dy * dy;
    for (int px = min_x; px <= max_x; ++px) {
      const float dx = (static_cast<float>(px) + 0.5f) - cx;
      const float dist = std::sqrt(dx * dx + dy2);
      const float cov = r + 0.5f - dist;
      if (cov > 0.0f) {
        const int a = (cov >= 1.0f) ? alpha_max
                                    : ((static_cast<int>(cov * 255.0f + 0.5f) * alpha_max) / 255);
        fast_pixel_blend(it, px, py, col, a);
      }
    }
  }
}

void draw_aa_ring(Rgb565Surface &it, float cx, float cy, float r, float thick, UiColor col, int alpha_max) {
  if (r <= 0.25f || thick <= 0.05f || alpha_max <= 0) return;
  const float half_t = 0.5f * thick;
  const float pad = r + half_t + 1.0f;
  const int min_y = std::max(clip_y0(), static_cast<int>(std::floor(cy - pad)));
  const int max_y = std::min(clip_y1(), static_cast<int>(std::ceil(cy + pad)));
  if (min_y > max_y) return;
  const int min_x = std::max(0, static_cast<int>(std::floor(cx - pad)));
  const int max_x = std::min(SCREEN_W - 1, static_cast<int>(std::ceil(cx + pad)));
  if (min_x > max_x) return;

  for (int py = min_y; py <= max_y; ++py) {
    const float dy = (static_cast<float>(py) + 0.5f) - cy;
    const float dy2 = dy * dy;
    for (int px = min_x; px <= max_x; ++px) {
      const float dx = (static_cast<float>(px) + 0.5f) - cx;
      const float dist = std::sqrt(dx * dx + dy2);
      const float cov = half_t + 0.5f - std::fabs(dist - r);
      if (cov > 0.0f) {
        const int a = (cov >= 1.0f) ? alpha_max
                                    : ((static_cast<int>(cov * 255.0f + 0.5f) * alpha_max) / 255);
        fast_pixel_blend(it, px, py, col, a);
      }
    }
  }
}

void draw_aa_tapered_hand(Rgb565Surface &it, float cx, float cy, float angle_rad,
                          float tail_len, float tip_len, float root_w, float tip_w,
                          UiColor body_col, UiColor lume_col,
                          bool draw_lume, bool draw_shadow) {
  const float s_ang = std::sin(angle_rad);
  const float c_ang = std::cos(angle_rad);
  const float ux = s_ang;
  const float uy = -c_ang;

  const float x0 = cx - tail_len * ux;
  const float y0 = cy - tail_len * uy;
  const float x1 = cx + tip_len * ux;
  const float y1 = cy + tip_len * uy;
  const float total_len = tail_len + tip_len;
  if (total_len <= 0.5f) return;

  if (draw_shadow) {
    const float sh_off = std::max(1.0f, dp_f(2.0f));
    draw_aa_line(it, x0 + sh_off, y0 + sh_off + 0.5f, x1 + sh_off, y1 + sh_off + 0.5f,
                 0.55f * (root_w + tip_w), kChromeShadow, 92);
  }

  const float max_r = 0.5f * std::max(root_w, tip_w);
  const float pad = max_r + 1.5f;
  const int min_y = std::max(clip_y0(), static_cast<int>(std::floor(std::min(y0, y1) - pad)));
  const int max_y = std::min(clip_y1(), static_cast<int>(std::ceil(std::max(y0, y1) + pad)));
  const int bbox_x0 = std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - pad)));
  const int bbox_x1 = std::min(SCREEN_W - 1, static_cast<int>(std::ceil(std::max(x0, x1) + pad)));
  if (min_y <= max_y && bbox_x0 <= bbox_x1) {
    const float vx = x1 - x0;
    const float vy = y1 - y0;
    const float inv_len2 = 1.0f / (total_len * total_len);
    const bool steep_enough = (std::fabs(vy) > 0.75f);
    const float slope_xy = steep_enough ? (vx / vy) : 0.0f;
    const float row_half_span = steep_enough
                                    ? (pad * std::sqrt(1.0f + slope_xy * slope_xy) + 1.5f)
                                    : 0.0f;

    for (int py = min_y; py <= max_y; ++py) {
      const float yc = static_cast<float>(py) + 0.5f;
      int rx0 = bbox_x0;
      int rx1 = bbox_x1;
      if (steep_enough) {
        const float x_cross = x0 + (yc - y0) * slope_xy;
        rx0 = std::max(bbox_x0, static_cast<int>(std::floor(x_cross - row_half_span)));
        rx1 = std::min(bbox_x1, static_cast<int>(std::ceil(x_cross + row_half_span)));
      }
      for (int px = rx0; px <= rx1; ++px) {
        const float xc = static_cast<float>(px) + 0.5f;
        const float t = std::clamp(((xc - x0) * vx + (yc - y0) * vy) * inv_len2, 0.0f, 1.0f);
        const float hw = 0.5f * (root_w + (tip_w - root_w) * t);
        const float dx = xc - (x0 + t * vx);
        const float dy = yc - (y0 + t * vy);
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float cov = hw + 0.5f - dist;
        if (cov > 0.0f) {
          fast_pixel_blend(it, px, py, body_col, aa_coverage_8(cov));
        }
      }
    }
  }

  if (draw_lume && tip_len > 18.0f) {
    const float l0 = 0.28f * tip_len;
    const float l1 = 0.86f * tip_len;
    const float lw = std::max(1.4f, 0.34f * (root_w + tip_w));
    draw_aa_line(it, cx + l0 * ux, cy + l0 * uy, cx + l1 * ux, cy + l1 * uy, lw, lume_col, 245);
  }
}

void render_analog_chapter_ring(Rgb565Surface &it, int cx, int cy, int r_dial,
                                UiColor cardinal_col, UiColor major_col,
                                UiColor minor_col, bool double_twelve) {
  const float fcx = static_cast<float>(cx) + 0.5f;
  const float fcy = static_cast<float>(cy) + 0.5f;
  const float r_out = static_cast<float>(r_dial);
  const float ds = r_out / 224.0f;
  const float w_card = std::max(2.6f, 3.6f * ds);
  const float w_maj = std::max(2.0f, 2.6f * ds);
  const float w_min = std::max(1.1f, 1.4f * ds);
  const float len_card = 22.0f * ds;
  const float len_maj = 16.0f * ds;
  const float len_min = 8.5f * ds;

  for (int i = 0; i < 60; ++i) {
    const float rad = (static_cast<float>(i) / 60.0f) * 2.0f * PI_F - HALF_PI_F;
    const float s = std::sin(rad);
    const float c = std::cos(rad);
    if (i == 0 && double_twelve) {
      const float sep = 3.4f * ds;
      const float nx = -s;
      const float ny = c;
      const float r_in = r_out - len_card;
      draw_aa_line(it, fcx + r_in * c - sep * nx, fcy + r_in * s - sep * ny,
                   fcx + r_out * c - sep * nx, fcy + r_out * s - sep * ny,
                   w_maj, cardinal_col, 255);
      draw_aa_line(it, fcx + r_in * c + sep * nx, fcy + r_in * s + sep * ny,
                   fcx + r_out * c + sep * nx, fcy + r_out * s + sep * ny,
                   w_maj, cardinal_col, 255);
    } else if (i % 15 == 0) {
      const float r_in = r_out - len_card;
      draw_aa_line(it, fcx + r_in * c, fcy + r_in * s, fcx + r_out * c, fcy + r_out * s,
                   w_card, cardinal_col, 255);
    } else if (i % 5 == 0) {
      const float r_in = r_out - len_maj;
      draw_aa_line(it, fcx + r_in * c, fcy + r_in * s, fcx + r_out * c, fcy + r_out * s,
                   w_maj, major_col, 235);
    } else {
      const float r_in = r_out - len_min;
      draw_aa_line(it, fcx + r_in * c, fcy + r_in * s, fcx + r_out * c, fcy + r_out * s,
                   w_min, minor_col, 175);
    }
  }
}

void render_center_arbor_aa(Rgb565Surface &it, int cx, int cy,
                            UiColor outer_col, UiColor cap_col, UiColor jewel_col) {
  const float ds = static_cast<float>(SCREEN_MIN_DIM) / 466.0f;
  const int r_outer = std::max(6, static_cast<int>(std::lround(8.0f * ds)));
  const int r_cap = std::max(4, static_cast<int>(std::lround(5.0f * ds)));
  const int r_jewel = std::max(2, static_cast<int>(std::lround(2.0f * ds)));
  fast_filled_circle_aa(it, cx + 1, cy + 1, r_outer + 1, kChromeShadow);
  fast_filled_circle_aa(it, cx, cy, r_outer, outer_col);
  fast_filled_circle_aa(it, cx, cy, r_cap, cap_col);
  fast_filled_circle_aa(it, cx, cy, r_jewel, jewel_col);
}

}  // namespace ui

}  // namespace latter_ui
