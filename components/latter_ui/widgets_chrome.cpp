#include "ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace latter_ui {

// ==============================================================================
// 1. Shared Frosted-Glass Chrome & Navigation Primitives
// ==============================================================================
void render_pill_button(Rgb565Surface &it, int x1, int x2, int y_mid, int r,
                        UiColor border_c, UiColor bg_c,
                        const char *label, UiColor text_c,
                        UiFontView font,
                        GlassVariant variant) {
  const int px0 = x1 - r;
  const int py0 = y_mid - r;
  const int pw = (x2 - x1) + 2 * r + 1;
  const int ph = 2 * r + 1;
  if (pw <= 4 || ph <= 4) return;
  GlassVariant eff_var = variant;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark) ? sample_glass_backdrop(px0, py0, pw, ph) : GlassBackdrop{};
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, px0, py0, pw, ph, static_cast<float>(r) + 0.5f,
                        rim_col, rim_top, rim_bot, 1);
  if (r > 1 && pw > 2 && ph > 2) {
    fill_round_rect_glass(it, px0 + 1, py0 + 1, pw - 2, ph - 2, static_cast<float>(r) - 0.5f,
                          eff_tint, a_top, a_bot);
  }

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const int btn_cx = (x1 + x2) / 2;
    fast_print(it, btn_cx + 1, y_mid + 1, font, kChromeShadow, UiTextAlign::CENTER, label);
    fast_print(it, btn_cx, y_mid, font, text_c, UiTextAlign::CENTER, label);
  }
}

UiColor render_modern_card(Rgb565Surface &it, int cx_card, int cy_card,
                           int w, int h, int r,
                           UiColor bg_c, UiColor border_c,
                           const char *label, UiColor text_c,
                           UiFontView font,
                           GlassVariant variant,
                           bool sample_surface) {
  if constexpr (SCREEN_W > 520) {
    if (w <= 340) w = scale_x(w);
    if (h <= 96) h = scale_y(h);
    if (r <= 24) r = scale_s(r);
  } else if (std::abs(cx_card - SCREEN_CX) <= 4) {
    if constexpr (SCREEN_W < 380) {
      if (h <= 48 && w >= 240 && w <= 330) {
        w = SCREEN_W - 88;
      } else if (w > SCREEN_W - 20) {
        w = SCREEN_W - 20;
      }
    } else if (w > SCREEN_W - 20) {
      w = SCREEN_W - 20;
    }
  }
  if (w <= 4 || h <= 4) return bg_c;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;
  const int x0 = cx_card - w / 2;
  const int y0 = cy_card - h / 2;

  GlassVariant eff_var = variant;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark)
          ? sample_glass_backdrop(x0, y0, w, h)
          : (sample_surface ? sample_glass_mean(x0, y0, w, h) : GlassBackdrop{});
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), rim_col, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, std::max(0.5f, static_cast<float>(r) - 1.0f),
                        eff_tint, a_top, a_bot);
  const UiColor surf_bg = bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    fast_print(it, cx_card + 1, cy_card + 1, font, kChromeShadow, UiTextAlign::CENTER, label);
    fast_print(it, cx_card, cy_card, font, text_c, UiTextAlign::CENTER, label);
  }
  return surf_bg;
}

UiColor render_card_box(Rgb565Surface &it, int x0, int y0, int w, int h,
                        UiColor bg_c, UiColor border_c, int r,
                        GlassVariant variant, UiColor ref_text,
                        bool sample_surface) {
  if (w <= 4 || h <= 4) return bg_c;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;

  GlassVariant eff_var = variant;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark)
          ? sample_glass_backdrop(x0, y0, w, h)
          : (sample_surface ? sample_glass_mean(x0, y0, w, h) : GlassBackdrop{});
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, ref_text, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), rim_col, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, std::max(0.5f, static_cast<float>(r) - 1.0f),
                        eff_tint, a_top, a_bot);
  return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
}

UiColor render_glass_circle(Rgb565Surface &it, int cx_btn, int cy_btn, int r,
                            UiColor bg_c, UiColor border_c,
                            int inner_hole_r, GlassVariant variant,
                            UiColor ref_text,
                            bool sample_surface) {
  if (r <= 1) return bg_c;
  GlassVariant eff_var = variant;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);

  const int d = 2 * r + 1;
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark)
          ? sample_glass_backdrop(cx_btn - r, cy_btn - r, d, d)
          : (sample_surface ? sample_glass_mean(cx_btn - r, cy_btn - r, d, d) : GlassBackdrop{});
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, ref_text, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, cx_btn - r, cy_btn - r, d, d, static_cast<float>(r) + 0.5f,
                        rim_col, rim_top, rim_bot, 1);
  if (inner_hole_r <= 0) {
    fill_round_rect_glass(it, cx_btn - r + 1, cy_btn - r + 1, d - 2, d - 2, static_cast<float>(r) - 0.5f,
                          eff_tint, a_top, a_bot);
  } else {
    const int hr = std::min(inner_hole_r, r - 2);
    if (hr > 0) {
      const int fill_r = r - 1;
      const int ir = hr + 1;
      const int ring_thick = fill_r - ir;
      if (ring_thick > 0) {
        fill_round_rect_glass(it, cx_btn - fill_r, cy_btn - fill_r, 2 * fill_r + 1, 2 * fill_r + 1,
                              static_cast<float>(fill_r) + 0.5f, eff_tint, a_top, a_bot, ring_thick);
      }
      fill_round_rect_glass(it, cx_btn - ir, cy_btn - ir, 2 * ir + 1, 2 * ir + 1,
                            static_cast<float>(ir) + 0.5f, rim_col, rim_bot, rim_top, 1);
    }
  }
  return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
}

void render_scroll_chevron(Rgb565Surface &it, int x, int y, bool is_up,
                           UiColor c, UiColor shadow_c, bool shadow) {
  const int dx = scale_s(7);
  const int dy_hi = scale_s(5);
  const int dy_lo = scale_s(4);
  const int thick = (SCREEN_W > 520) ? 3 : 2;
  if (shadow) {
    for (int t = 0; t < thick; t++) {
      if (is_up) {
        fast_line(it, x - dx, y + dy_lo + 1 + t, x, y - dy_hi + 1 + t, shadow_c);
        fast_line(it, x + dx, y + dy_lo + 1 + t, x, y - dy_hi + 1 + t, shadow_c);
      } else {
        fast_line(it, x - dx, y - dy_lo + 1 + t, x, y + dy_hi + 1 + t, shadow_c);
        fast_line(it, x + dx, y - dy_lo + 1 + t, x, y + dy_hi + 1 + t, shadow_c);
      }
    }
  }
  for (int t = 0; t < thick; t++) {
    if (is_up) {
      fast_line(it, x - dx, y + dy_lo + t, x, y - dy_hi + t, c);
      fast_line(it, x + dx, y + dy_lo + t, x, y - dy_hi + t, c);
    } else {
      fast_line(it, x - dx, y - dy_hi + t, x, y + dy_lo + t, c);
      fast_line(it, x + dx, y - dy_hi + t, x, y + dy_lo + t, c);
    }
  }
}

void render_bezel_alert_border(Rgb565Surface &it, int cx, int cy, UiColor ring_c) {
#if LATTER_ROUND_DISPLAY
  for (int r = SCREEN_R - 10; r <= SCREEN_R - 7; r++) {
    fast_circle(it, cx, cy, r, ring_c);
  }
#else
  (void)cx;
  (void)cy;
  const int pad = LATTER_IS_WEARABLE ? 5 : 3;
  const int thick = 5;
  const int cr_out = std::max(8, LATTER_CORNER_R - pad);
  stroke_round_rect_aa(it, pad, pad, SCREEN_W - 2 * pad, SCREEN_H - 2 * pad,
                       static_cast<float>(cr_out), ring_c, ring_c, thick);
#endif
}

void render_home_indicator_bar(Rgb565Surface &it, int cx, int y_pos, UiColor bar_col) {
  const int half_w = scale_x(54);
  const int bar_h = (SCREEN_H > 520) ? 7 : 5;
  const int cap_r = bar_h / 2;
  const float cap = 0.5f * static_cast<float>(bar_h);
  fill_round_rect_aa(it, cx - half_w, y_pos - cap_r + 1, 2 * half_w + 1, bar_h, cap, kChromeShadow);
  fill_round_rect_aa(it, cx - half_w, y_pos - cap_r, 2 * half_w + 1, bar_h, cap, bar_col);
}

void render_back_button(Rgb565Surface &it, int cx, UiFontView font,
                        int y_pos, const char *label, UiColor text_col) {
  const auto ly = layout();
  const int back_lbl_y = (y_pos > 0) ? std::clamp(y_pos, 0, ly.h - 1) : ly.back_y;
  if (label == nullptr) return;
  if (std::strcmp(label, "Home") == 0) {
    render_home_indicator_bar(it, cx, ly.home_bar_y);
    return;
  }
  fast_print(it, cx + 1, back_lbl_y + 1, font, kChromeShadow, UiTextAlign::CENTER, label);
  fast_print(it, cx, back_lbl_y, font, text_col, UiTextAlign::CENTER, label);
  const int hit_y0 = std::clamp(back_lbl_y - scale_y(22), 0, ly.h - 1);
  const int hit_h = std::max(1, ly.h - hit_y0);
  const int max_span = std::max(ly.min_hit_px(), ly.safe_span(back_lbl_y, 8));
  const int hit_w = std::min(max_span, std::max(scale_x(235), ly.min_hit_px() * 2));
  const int hit_pad = std::max(0, (ly.min_hit_px() - hit_h + 1) / 2);
  frame_hits().add_rect(cx - hit_w / 2, hit_y0, hit_w, hit_h,
                        WidgetId::BACK_BUTTON, kHitTap, 0, hit_pad);
}

void render_offline_pill(Rgb565Surface &it, int cx, uint32_t now_ms,
                         UiFontView font, UiColor text_c,
                         bool ha_connected, uint32_t ha_disconnect_ms) {
  if (font == nullptr || !offline_pill_due(ha_connected, ha_disconnect_ms, now_ms)) return;
  static const char *const kLabel = "Offline";
  int text_w = 0, x_off = 0, base = 0, text_h = 0;
  font->measure(kLabel, &text_w, &x_off, &base, &text_h);
  const int em = (text_h * 2048 + 1200) / 2400;
  const int glyph_h = (em * 3 + 2) / 4;
  const int pad_y = std::max(2, layout().mm_to_px(0.2f));
  const int pad_x = layout().mm_to_px(1.4f);
  const int pill_h = glyph_h + 2 * pad_y;
  const int pill_w = text_w + 2 * pad_x;
  const int r = pill_h / 2;
  const int x0 = cx - pill_w / 2;
#if LATTER_ROUND_DISPLAY
  const int margin = layout().mm_to_px(0.6f);
  const int y0 = (SCREEN_R - chord_half_span_int(SCREEN_R, pill_w / 2 + margin)) + margin;
  const bool round_top = true;
#else
  const int y0 = 0;
  const bool round_top = false;
#endif
  const float rad = 0.5f * static_cast<float>(pill_h);
  const int sy0 = round_top ? y0 : (y0 - r);
  const int sh = round_top ? pill_h : (pill_h + r);
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(GlassBackdrop{}, kChromeGlassTint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot,
                     -1, GlassVariant::Clear);
  fill_round_rect_glass(it, x0 - 1, sy0 - 1, pill_w + 2, sh + 2, rad + 1.0f, kChromeGlassSheen, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0, sy0, pill_w, sh, rad, eff_tint, a_top, a_bot);
  const int text_top = y0 + pad_y - (base - glyph_h);
  fast_print(it, cx + 1, text_top + 1, font, kChromeShadow, UiTextAlign::TOP_CENTER, kLabel);
  fast_print(it, cx, text_top, font, text_c, UiTextAlign::TOP_CENTER, kLabel);
}

namespace ui {

// ==============================================================================
// 2. Semantic Chrome Surface Tokens
// ==============================================================================
UiColor chrome_card_bg_on() { return kChromeCardBgOn; }
UiColor chrome_card_bg_off() { return kChromeCardBgOff; }
UiColor chrome_card_brd_on() { return kChromeCardBrdOn; }
UiColor chrome_card_brd_off() { return kChromeCardBrdOff; }
UiColor chrome_danger_bg() { return kChromeDangerBg; }
UiColor chrome_modal_bg() { return kChromeModalBg; }
UiColor chrome_key_bg() { return kChromeKeyBg; }
UiColor chrome_key_brd() { return kChromeKeyBrd; }
UiColor chrome_wheel_inner_ring() { return kChromeWheelInnerRing; }
UiColor chrome_wheel_outer_ring() { return kChromeWheelOuterRing; }

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
  draw_mdi_r(it, "mdi:clock-outline", cx, cy, r, col, shadow, "mdi:clock-outline");
}
void draw_icon_hourglass(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:timer-sand", cx, cy, r, col, shadow, "mdi:clock-outline");
}
void draw_icon_alarm_clock(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:alarm", cx, cy, r, col, shadow, "mdi:clock-outline");
}
void draw_icon_stopwatch(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:timer-outline", cx, cy, r, col, shadow, "mdi:clock-outline");
}
void draw_icon_shield_home(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:shield-home", cx, cy, r, col, shadow, "mdi:home");
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
void draw_icon_lightbulb(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:lightbulb", cx, cy, r, col, shadow, "mdi:lightbulb");
}
void draw_icon_ceiling_fan(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:fan", cx, cy, r, col, shadow, "mdi:fan");
}
void draw_icon_curtains(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:curtains", cx, cy, r, col, shadow, "mdi:blinds-horizontal");
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
void draw_icon_thumbtack(Rgb565Surface &it, int cx, int cy, int r, UiColor col, bool shadow) {
  draw_mdi_r(it, "mdi:pin", cx, cy, r, col, shadow, "mdi:pin");
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
  draw_mdi_r(it, "mdi:cog-outline", cx, cy, r, col, shadow, "mdi:cog");
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

void draw_route_icon(Rgb565Surface &it, RouteId route, int cx, int cy, int r,
                     UiColor col, UiColor cut_c, bool shadow) {
  (void)cut_c;
  const int mdi_px = std::max(16, 2 * r);
  if (route == RouteId::APP_AREA) {
    render_mdi_icon(it, resolve_area_dock_icon(area_ctx_state()), cx, cy, mdi_px, col, shadow, "mdi:sofa");
    return;
  }
  const char *slug = "mdi:cog";
  const char *fb = "mdi:cog";
  switch (route) {
    case RouteId::APP_PINNED:          slug = "mdi:pin"; fb = "mdi:pin"; break;
    case RouteId::APP_MUSIC:           slug = "mdi:music"; fb = "mdi:music"; break;
    case RouteId::MUSIC_LIBRARY:       slug = "mdi:music-box-outline"; fb = "mdi:music"; break;
    case RouteId::MUSIC_SPEAKERS:      slug = "mdi:speaker-multiple"; fb = "mdi:music"; break;
    case RouteId::MUSIC_FAVORITES:     slug = "mdi:heart"; fb = "mdi:music"; break;
    case RouteId::MUSIC_PLAYLISTS:     slug = "mdi:playlist-music"; fb = "mdi:music"; break;
    case RouteId::MUSIC_QUEUE:         slug = "mdi:playlist-play"; fb = "mdi:music"; break;
    case RouteId::MUSIC_ALBUM_TRACKS:  slug = "mdi:album"; fb = "mdi:music"; break;
    case RouteId::MUSIC_LYRICS:        slug = "mdi:script-text"; fb = "mdi:music"; break;
    case RouteId::MUSIC_INFO:          slug = "mdi:information-outline"; fb = "mdi:music"; break;
    case RouteId::APP_HOME_HUB:        slug = "mdi:home"; fb = "mdi:home"; break;
    case RouteId::HOME_HUB_ALARM:      slug = "mdi:shield-home"; fb = "mdi:home"; break;
    case RouteId::APP_CAMERAS:         slug = "mdi:cctv"; fb = "mdi:video"; break;
    case RouteId::CAMERAS_GRID:        slug = "mdi:view-grid"; fb = "mdi:cctv"; break;
    case RouteId::CAMERAS_SINGLE:      slug = "mdi:video"; fb = "mdi:cctv"; break;
    case RouteId::CAMERAS_CAROUSEL:    slug = "mdi:view-carousel"; fb = "mdi:cctv"; break;
    case RouteId::CAMERAS_SETTINGS:    slug = "mdi:cog-outline"; fb = "mdi:cog"; break;
    case RouteId::HOME_HUB_INTERCOM:   slug = "mdi:bullhorn"; fb = "mdi:home"; break;
    case RouteId::HOME_HUB_THERMOSTAT: slug = "mdi:thermostat"; fb = "mdi:thermometer"; break;
    case RouteId::AREA_CLIMATE:        slug = "mdi:thermometer"; fb = "mdi:thermostat"; break;
    case RouteId::HOME_HUB_ENTRY:      slug = "mdi:lock"; fb = "mdi:home"; break;
    case RouteId::HOME_HUB_VACUUM:     slug = "mdi:robot-vacuum"; fb = "mdi:home"; break;
    case RouteId::AREA_PICKER:         slug = "mdi:floor-plan"; fb = "mdi:map-marker-outline"; break;
    case RouteId::AREA_SCENE:          slug = "mdi:shimmer"; fb = "mdi:sofa"; break;
    case RouteId::AREA_LIGHT:          slug = "mdi:lightbulb"; fb = "mdi:sofa"; break;
    case RouteId::AREA_COVER:          slug = "mdi:blinds-horizontal"; fb = "mdi:sofa"; break;
    case RouteId::AREA_FAN:            slug = "mdi:fan"; fb = "mdi:sofa"; break;
    case RouteId::AREA_SWITCH:         slug = "mdi:toggle-switch"; fb = "mdi:sofa"; break;
    case RouteId::AREA_MEDIA:          slug = "mdi:television-speaker"; fb = "mdi:speaker"; break;
    case RouteId::AREA_ALL:            slug = "mdi:view-grid"; fb = "mdi:apps"; break;
    case RouteId::APP_REMOTE:          slug = "mdi:remote-tv"; fb = "mdi:remote"; break;
    case RouteId::REMOTE_DEVICES:      slug = "mdi:remote"; fb = "mdi:remote-tv"; break;
    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:     slug = "mdi:weather-partly-cloudy"; fb = "mdi:cloud"; break;
    case RouteId::WEATHER_FORECAST:    slug = "mdi:calendar-week"; fb = "mdi:cloud"; break;
    case RouteId::WEATHER_SUN:         slug = "mdi:white-balance-sunny"; fb = "mdi:weather-sunny"; break;
    case RouteId::WEATHER_MOON:        slug = "mdi:moon-waning-crescent"; fb = "mdi:weather-night"; break;
    case RouteId::APP_TIME:
    case RouteId::APP_CLOCK:
    case RouteId::TIME_CLOCK:          slug = "mdi:clock-outline"; fb = "mdi:clock-outline"; break;
    case RouteId::TIME_TIMERS:         slug = "mdi:timer-sand"; fb = "mdi:clock-outline"; break;
    case RouteId::TIME_ALARMS:         slug = "mdi:alarm"; fb = "mdi:clock-outline"; break;
    case RouteId::TIME_STOPWATCH:
    case RouteId::TOOLS_STOPWATCH:     slug = "mdi:timer-outline"; fb = "mdi:clock-outline"; break;
    case RouteId::APP_TOOLS:           slug = "mdi:view-grid-outline"; fb = "mdi:apps"; break;
    case RouteId::TOOLS_LEVEL:         slug = "mdi:spirit-level"; fb = "mdi:compass-outline"; break;
    case RouteId::TOOLS_FLASHLIGHT:    slug = "mdi:flashlight"; fb = "mdi:lightbulb"; break;
    case RouteId::TOOLS_CALCULATOR:    slug = "mdi:calculator-variant"; fb = "mdi:calculator"; break;
    case RouteId::APP_CALENDAR:        slug = "mdi:calendar"; fb = "mdi:calendar"; break;
    case RouteId::CALENDAR_OVERVIEW:   slug = "mdi:calendar-text"; fb = "mdi:calendar"; break;
    case RouteId::CALENDAR_DAY:        slug = "mdi:calendar-today"; fb = "mdi:calendar"; break;
    case RouteId::CALENDAR_WEEK:       slug = "mdi:calendar-week"; fb = "mdi:calendar"; break;
    case RouteId::CALENDAR_MONTH:      slug = "mdi:calendar-month"; fb = "mdi:calendar"; break;
    case RouteId::TOOLS_DICE:          slug = "mdi:dice-5"; fb = "mdi:dice-multiple"; break;
    case RouteId::TOOLS_COIN_FLIP:     slug = "mdi:alpha-c-circle"; fb = "mdi:circle-double"; break;
    case RouteId::APP_GAMES:           slug = "mdi:controller"; fb = "mdi:gamepad-variant"; break;
    case RouteId::GAMES_MARBLES:       slug = "mdi:billiards"; fb = "mdi:circle-multiple"; break;
    case RouteId::GAMES_ARCHERY:       slug = "mdi:bullseye-arrow"; fb = "mdi:target"; break;
    case RouteId::GAMES_BRICKS:        slug = "mdi:wall"; fb = "mdi:view-grid"; break;
    case RouteId::GAMES_CHOMP:         slug = "mdi:pac-man"; fb = "mdi:ghost"; break;
    case RouteId::GAMES_2048:          slug = "mdi:numeric-2-box-multiple"; fb = "mdi:grid"; break;
    case RouteId::GAMES_MEMORY:        slug = "mdi:cards"; fb = "mdi:apps"; break;
    case RouteId::SETTINGS_DISPLAY:    slug = "mdi:brightness-6"; break;
    case RouteId::SETTINGS_SLEEP:      slug = "mdi:power-sleep"; break;
    case RouteId::SETTINGS_SOUND:      slug = "mdi:volume-high"; break;
    case RouteId::SETTINGS_VOICE:      slug = "mdi:microphone"; break;
    case RouteId::SETTINGS_MUSIC:      slug = "mdi:music-box-outline"; break;
    case RouteId::SETTINGS_HOME:       slug = "mdi:home-outline"; break;
    case RouteId::SETTINGS_ACTIONS:    slug = "mdi:gesture-tap-button"; break;
    case RouteId::SETTINGS_UPDATE:     slug = "mdi:cellphone-arrow-down"; break;
    case RouteId::SETTINGS_INFO:       slug = "mdi:information-outline"; break;
    case RouteId::SETTINGS_ROOT:
    case RouteId::NONE:
    default: break;
  }
  render_mdi_icon(it, slug, cx, cy, mdi_px, col, shadow, fb);
}

}  // namespace ui
}  // namespace latter_ui
