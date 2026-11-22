#include "ui_pages.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace latter_ui {

void render_quick_actions_overlay(Rgb565Surface &it, const UiFrameContext &ctx,
                                  const SystemTakeoverState &st) {
  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const auto &ly = layout();
  const int cx = st.cx;
  const int cy = st.cy;

  // Single backdrop paint respecting user-configured Day/Night Sky, Custom Wallpaper, or Solid Black.
  if (is_day_night_sky_wallpaper_url(ac.wallpaper_url)) {
    render_static_day_night_sky_base(
        it, cx, cy,
        resolve_day_night_sky_is_night(weather_state().sun_state, ctx.now.is_valid(), ctx.now.hour));
  } else {
    const bool has_wall = (!ac.wallpaper_url.empty() &&
                           ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                           ctx.home_wallpaper_art.valid());
    render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_wall, cx, st.col_bg);
  }

  render_page_header(it, cx, "QUICK ACTIONS", ctx.font_small, st.col_white);

  const auto bri = resolve_active_brightness_target(
      prefs, weather_state().sun_state, ctx.now.is_valid(),
      ctx.now.is_valid() ? ctx.now.hour : 12,
      ac.area_ambient_pct, rs.nav.ambient_bri_override_pct);

  char bri_ctx_buf[32];
  const char *bri_ctx_lbl = bri.context_label;
  if (bri.kind == ActiveBrightnessTargetKind::AMBIENT && !ac.area_lux_display.empty()) {
    std::snprintf(bri_ctx_buf, sizeof(bri_ctx_buf), "Ambient · %s lx", ac.area_lux_display.c_str());
    bri_ctx_lbl = bri_ctx_buf;
  }

  char sub_buf[64];
#if LATTER_HAS_BATTERY
  if (std::isnan(rs.hw_batt_level)) {
    std::snprintf(sub_buf, sizeof(sub_buf), "Battery · %s · %s", kUnknownGlyph, bri_ctx_lbl);
  } else {
    const int batt_pct = std::clamp(static_cast<int>(std::lround(rs.hw_batt_level)), 0, 100);
    const char *pwr_lbl = rs.hw_batt_charging ? "Charging" : (rs.hw_usb_connected ? "USB" : "Battery");
    std::snprintf(sub_buf, sizeof(sub_buf), "%s · %d%% · %s", pwr_lbl, batt_pct, bri_ctx_lbl);
  }
#else
  std::snprintf(sub_buf, sizeof(sub_buf), "Brightness: %s", bri_ctx_lbl);
#endif
  const int sub_max_w = std::min(kMaxContentColW, std::max(120, ly.safe_span(ly.sub_hdr_y, 16)));
  const std::string fit_sub = fit_text_to_width_font(std::string(sub_buf), sub_max_w, ctx.font_small);
  render_sub_header(it, cx, fit_sub, ctx.font_small, st.col_slate);

  const int row_w = ly.list_card_w;
  const int row_h = ly.slot5_h;
  char buf[48];

  // Row 0: Speaker Volume slider (10..100%)
  const int vol_pct = std::clamp(ctx.spk_vol_pct, 10, 100);
  std::snprintf(buf, sizeof(buf), "Volume: %d%%", vol_pct);
  ui::slider_card_row(it, cx, ly.slot5_y(0), row_w, row_h, buf, vol_pct, 10, 100, true,
                      st.theme_primary, ui::chrome_card_brd_on(), ctx.font_small, st.col_white,
                      WidgetId::QUICK_VOL_SLIDER, 0);

  // Row 1: Context-Aware Brightness slider (1..100%)
  if (bri.kind == ActiveBrightnessTargetKind::BEDTIME) {
    std::snprintf(buf, sizeof(buf), "Bedtime Bri: %d%%", static_cast<int>(bri.pct));
  } else {
    std::snprintf(buf, sizeof(buf), "Brightness: %d%%", static_cast<int>(bri.pct));
  }
  ui::slider_card_row(it, cx, ly.slot5_y(1), row_w, row_h, buf, static_cast<int>(bri.pct), 1, 100, true,
                      st.theme_primary, ui::chrome_card_brd_on(), ctx.font_small, st.col_white,
                      WidgetId::QUICK_BRI_SLIDER, static_cast<int16_t>(bri.kind));

  // Row 2: Bedtime Mode toggle
  ui::toggle_card_row(it, cx, ly.slot5_y(2), row_w, row_h, "Bedtime Mode", prefs.bedtime_active,
                      ctx.font_small, st.theme_primary, st.col_white, st.col_slate,
                      WidgetId::QUICK_BEDTIME_TOGGLE, 0);

  // Row 3: Do Not Disturb toggle (D-074)
  ui::toggle_card_row(it, cx, ly.slot5_y(3), row_w, row_h, "Do Not Disturb", prefs.do_not_disturb,
                      ctx.font_small, st.theme_primary, st.col_white, st.col_slate,
                      WidgetId::QUICK_DND_TOGGLE, 0);

  // Row 4: Power Action CTA (full stadium pill)
  const int cta_y = ly.slot5_y(4);
#if LATTER_HAS_BATTERY
  ui::action_pill_centered(it, cx, std::min(row_w, scale_x(220)), cta_y, row_h,
                           st.col_red, ui::chrome_danger_bg(), "Hibernate", st.col_white,
                           ctx.font_small, WidgetId::QUICK_HIBERNATE_BTN, kHitTap | kHitFocusSlot);
#else
  if (prefs.screensaver_mode != static_cast<uint8_t>(ScreensaverMode::Never)) {
    const ui::ActionPillSpec pwr_pills[2] = {
        {"Screensaver", WidgetId::QUICK_SCREENSAVER_BTN, ui::chrome_card_brd_on(),
         ui::chrome_card_bg_off(), st.col_white, 0, nullptr},
        {"Screen Off", WidgetId::QUICK_SCREEN_OFF_BTN, st.theme_primary,
         ui::chrome_card_bg_on(), st.col_white, 0, nullptr},
    };
    ui::action_pill_row(it, cta_y, row_h, row_w, row_w / 2, scale_x(10),
                        pwr_pills, 2, ctx.font_small, 14);
  } else {
    ui::action_pill_centered(it, cx, std::min(row_w, scale_x(220)), cta_y, row_h,
                             st.theme_primary, ui::chrome_card_bg_on(), "Screen Off", st.col_white,
                             ctx.font_small, WidgetId::QUICK_SCREEN_OFF_BTN, kHitTap | kHitFocusSlot);
  }
#endif

  render_back_button(it, cx, ctx.font_small, ly.back_y, "< Back", kChromeBackLabel);
}

}  // namespace latter_ui
