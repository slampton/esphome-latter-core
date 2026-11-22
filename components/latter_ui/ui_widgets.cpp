#include "ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

namespace ui {

namespace {

PinPadModalState g_pin_pad{};

struct HsvUnitRgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

const HsvUnitRgb *hsv_unit_lut_72() {
  static HsvUnitRgb lut[72]{};
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 72; i++) {
      const UiColor c = hsv_to_rgb(static_cast<float>(i * 5) / 360.0f, 1.0f, 1.0f);
      lut[i] = {c.r, c.g, c.b};
    }
    ready = true;
  }
  return lut;
}

}  // namespace

// ==============================================================================
// 1. Functional Action Pills, Toggles, Sliders, Lists, Grids & Keypads (D-070)
// ==============================================================================
void toggle_switch(Rgb565Surface &it, int cx_sw, int cy_sw, bool is_on, UiColor active_col) {
  const int sw_w = scale_x(40);
  const int sw_h = scale_y(22);
  const int x0 = cx_sw - sw_w / 2;
  const int y0 = cy_sw - sw_h / 2;
  const float rad = 0.5f * static_cast<float>(sw_h);

  GlassVariant eff_var = GlassVariant::Clear;
  UiColor norm_tint, rim_col;
  normalize_glass_chrome(is_on ? active_col : kChromeGlassTint,
                         is_on ? active_col : kChromeTrackBorder,
                         eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark) ? sample_glass_backdrop(x0, y0, sw_w, sw_h) : GlassBackdrop{};
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, kChromeGlassSheen, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, x0, y0, sw_w, sw_h, rad,
                        rim_col, is_on ? std::min(256, rim_top + 24) : rim_top, rim_bot, 1);
  if (sw_w > 2 && sw_h > 2) {
    fill_round_rect_glass(it, x0 + 1, y0 + 1, sw_w - 2, sw_h - 2,
                          std::max(0.5f, rad - 1.0f), eff_tint, a_top, a_bot);
  }
  const int thumb_r = std::max(5, sw_h / 2 - 3);
  const int thumb_cx = is_on ? (cx_sw + sw_w / 2 - thumb_r - 3) : (cx_sw - sw_w / 2 + thumb_r + 3);
  fast_filled_circle_aa(it, thumb_cx + 1, cy_sw + 1, thumb_r, kChromeShadow);
  fast_filled_circle_aa(it, thumb_cx, cy_sw, thumb_r, is_on ? kChromeGlassSheen : kChromeHomePill);
}

void action_pill(Rgb565Surface &it, int x0, int w, int y_mid, int h,
                 UiColor border_c, UiColor bg_c,
                 const char *label, UiColor text_c, UiFontView font,
                 uint16_t widget_id, uint8_t flags, int16_t param,
                 int hit_h_min, const char *leading_mdi) {
  const int eff_w = std::max(24, w);
  const int eff_h = std::max(24, h);
  const int r = std::min(eff_h / 2, eff_w / 2);
  const int y0 = y_mid - eff_h / 2;

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, border_c, r, GlassVariant::Clear, text_c);

  const int cx = x0 + eff_w / 2;
  const bool has_lbl = (label != nullptr && label[0] != '\0' && font != nullptr);
  const bool has_mdi = (leading_mdi != nullptr && leading_mdi[0] != '\0');
  if (has_mdi && has_lbl) {
    const int icon_px = std::clamp(eff_h - scale_y(14), 14, scale_s(20));
    const int icon_gap = scale_x(6);
    const int max_txt_w = std::max(24, eff_w - icon_px - icon_gap - scale_x(18));
    const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
    int tw = 0, x_off = 0, base = 0, th = 0;
    font->measure(fit.c_str(), &tw, &x_off, &base, &th);
    const int total_w = icon_px + icon_gap + tw;
    const int start_x = cx - total_w / 2;
    render_mdi_icon(it, leading_mdi, start_x + icon_px / 2, y_mid, icon_px, text_c, true);
    shadow_print(it, start_x + icon_px + icon_gap, y_mid, font, text_c, UiTextAlign::CENTER_LEFT, fit.c_str());
  } else if (has_mdi) {
    const int icon_px = std::clamp(eff_h - scale_y(12), 14, scale_s(22));
    render_mdi_icon(it, leading_mdi, cx, y_mid, icon_px, text_c, true);
  } else if (has_lbl) {
    const int max_txt_w = std::max(24, eff_w - scale_x(14));
    const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
    shadow_print(it, cx, y_mid, font, text_c, UiTextAlign::CENTER, fit.c_str());
  }

  if (widget_id != WidgetId::NONE) {
    const int min_h = (hit_h_min >= 0) ? hit_h_min : layout().min_hit_px();
    const int hit_h = std::max(eff_h, min_h);
    const int hit_w = std::max(eff_w, std::min(min_h, eff_w + scale_x(12)));
    frame_hits().add_centered_rect(cx, y_mid, hit_w, hit_h, widget_id, flags, param);
  }
}

void action_pill_centered(Rgb565Surface &it, int cx, int w, int y_mid, int h,
                          UiColor border_c, UiColor bg_c,
                          const char *label, UiColor text_c, UiFontView font,
                          uint16_t widget_id, uint8_t flags, int16_t param,
                          int hit_h_min, const char *leading_mdi) {
  action_pill(it, cx - w / 2, w, y_mid, h, border_c, bg_c, label, text_c, font,
              widget_id, flags, param, hit_h_min, leading_mdi);
}

void action_pill_row(Rgb565Surface &it, int cy, int h,
                     int max_row_w, int max_btn_w, int gap,
                     const ActionPillSpec *items, int count,
                     UiFontView font, int inset) {
  if (items == nullptr || count <= 0) return;
  const int n = std::min(count, 6);
  HomeHubPillGeom geoms[6]{};
  const int gn = compute_action_pill_row(
      n, cy, h, max_row_w, max_btn_w, gap,
      SCREEN_W, SCREEN_H, LATTER_ROUND_DISPLAY != 0, LATTER_IS_WEARABLE != 0,
      LATTER_PANEL_PPI, geoms, inset);
  for (int i = 0; i < gn; ++i) {
    action_pill(it, geoms[i].cx - geoms[i].w / 2, geoms[i].w, geoms[i].cy, geoms[i].h,
                items[i].border_c, items[i].bg_c, items[i].label, items[i].text_c, font,
                items[i].widget_id, kHitTap | kHitFocusSlot, items[i].param,
                geoms[i].hit_h, items[i].leading_mdi);
  }
}

void segmented_toggle(Rgb565Surface &it, int cx, int cy, int total_w, int h,
                      const char *left_label, const char *right_label, bool right_active,
                      UiFontView font,
                      UiColor theme_primary, UiColor col_white, UiColor col_slate,
                      uint16_t left_wid, uint16_t right_wid) {
  const int gap = std::max(4, scale_x(6));
  const int eff_total = std::min(kMaxContentColW, total_w);
  const int seg_w = std::max(56, (eff_total - gap) / 2);
  const int hit_h = std::max(h, std::min(layout().min_hit_px(), h + scale_y(12)));
  action_pill_centered(it, cx - (seg_w + gap) / 2, seg_w, cy, h,
                       !right_active ? theme_primary : chrome_card_brd_off(),
                       !right_active ? chrome_card_bg_on() : chrome_card_bg_off(),
                       left_label, !right_active ? col_white : col_slate, font,
                       left_wid, kHitTap | kHitFocusSlot, 0, hit_h);
  action_pill_centered(it, cx + (seg_w + gap) / 2, seg_w, cy, h,
                       right_active ? theme_primary : chrome_card_brd_off(),
                       right_active ? chrome_card_bg_on() : chrome_card_bg_off(),
                       right_label, right_active ? col_white : col_slate, font,
                       right_wid, kHitTap | kHitFocusSlot, 1, hit_h);
}

void setting_cycle_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                       const char *label, bool enabled, UiFontView font,
                       UiColor col_white, UiColor col_slate, UiColor chevron_col,
                       uint16_t widget_id, uint8_t flags, int16_t param) {
  const int eff_w = std::max(80, w);
  const int eff_h = std::max(28, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const UiColor bg_c = enabled ? chrome_card_bg_on() : chrome_card_bg_off();
  const UiColor brd_c = enabled ? chrome_card_brd_on() : chrome_card_brd_off();
  const UiColor txt_c = enabled ? col_white : col_slate;

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, txt_c);

  const int chv_inset = scale_x(16);
  const int max_txt_w = std::max(40, eff_w - (enabled ? (2 * chv_inset + scale_x(24)) : scale_x(20)));
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
    shadow_print(it, cx, cy, font, txt_c, UiTextAlign::CENTER, fit.c_str());
  }

  if (enabled) {
    const int chv_r = std::max(5, scale_s(7));
    draw_icon_chevron(it, x0 + chv_inset, cy, chv_r, false, chevron_col, true);
    draw_icon_chevron(it, x0 + eff_w - chv_inset, cy, chv_r, true, chevron_col, true);
    if (widget_id != WidgetId::NONE) {
      frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, flags, param);
    }
  }
}

void slider_card_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                     const char *label, int pct, int min_pct, int max_pct, bool enabled,
                     UiColor fill_c, UiColor brd_c, UiFontView font, UiColor text_c,
                     uint16_t widget_id, int16_t param) {
  const int eff_w = std::max(80, w);
  const int eff_h = std::max(28, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const int card_r = kRadiusCardPx();
  const UiColor bg_c = enabled ? chrome_card_bg_on() : chrome_card_bg_off();
  const UiColor eff_brd = enabled ? brd_c : chrome_card_brd_off();

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, eff_brd, card_r, GlassVariant::Clear, text_c);

  if (enabled && max_pct > min_pct) {
    const int inner_x0 = x0 + 2;
    const int inner_y0 = y0 + 2;
    const int inner_w = eff_w - 4;
    const int inner_h = eff_h - 4;
    const float inner_r = static_cast<float>(std::max(2, card_r - 2));
    const int cl_pct = std::clamp(pct, min_pct, max_pct);
    const float frac = static_cast<float>(cl_pct - min_pct) / static_cast<float>(max_pct - min_pct);
    const int min_fill_w = std::min(inner_w, static_cast<int>(inner_r * 2.0f));
    const int fill_w = std::clamp(
        static_cast<int>(std::lround(frac * static_cast<float>(inner_w))),
        (cl_pct > min_pct) ? min_fill_w : 0, inner_w);
    if (fill_w > 0) {
      fill_round_rect_glass(it, inner_x0, inner_y0, fill_w, inner_h, inner_r, fill_c, 96, 68);
    }
    if (widget_id != WidgetId::NONE) {
      frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id,
                                     kHitTap | kHitHorizSlider | kHitFocusSlot, param);
    }
  }

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const std::string fit = fit_text_to_width_font(std::string(label), std::max(40, eff_w - scale_x(20)), font);
    shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
  }
}

void list_row(Rgb565Surface &it, int cx, int cy, int w, int h,
              const char *label, const char *sub_label,
              UiColor bg_c, UiColor brd_c,
              UiColor text_c, UiColor sub_c, UiFontView font,
              uint16_t widget_id, uint8_t flags, int16_t param,
              const char *leading_mdi, const char *right_badge,
              UiColor right_badge_c) {
  const int eff_w = std::max(80, w);
  const int eff_h = std::max(28, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;

  if (widget_id == WidgetId::NONE) {
    if (label != nullptr && label[0] != '\0' && font != nullptr) {
      const std::string fit = fit_text_to_width_font(std::string(label), std::max(40, eff_w - scale_x(16)), font);
      shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
    }
    return;
  }

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, text_c);

  const bool has_mdi = (leading_mdi != nullptr && leading_mdi[0] != '\0');
  const bool has_sub = (sub_label != nullptr && sub_label[0] != '\0');
  const bool has_badge = (right_badge != nullptr && right_badge[0] != '\0');
  int pad_l = scale_x(14);
  int pad_r = scale_x(14);

  if (has_mdi) {
    const int icon_px = std::clamp(eff_h - scale_y(16), 16, scale_s(22));
    render_mdi_icon(it, leading_mdi, x0 + pad_l + icon_px / 2, cy, icon_px, text_c, true);
    pad_l += icon_px + scale_x(10);
  }
  if (has_badge && font != nullptr) {
    int bw = 0, bx_off = 0, bbase = 0, bh = 0;
    font->measure(right_badge, &bw, &bx_off, &bbase, &bh);
    shadow_print(it, x0 + eff_w - pad_r, cy, font, right_badge_c, UiTextAlign::CENTER_RIGHT, right_badge);
    pad_r += bw + scale_x(10);
  }

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const int max_txt_w = std::max(36, eff_w - pad_l - pad_r);
    if (has_sub) {
      int tw = 0, x_off = 0, base = 0, th = 0;
      font->measure("Ag", &tw, &x_off, &base, &th);
      const int half_step = std::min(eff_h / 4, std::max(scale_y(10), (th + 2) / 2));
      const std::string fit1 = fit_text_to_width_font(std::string(label), max_txt_w, font);
      const std::string fit2 = fit_text_to_width_font(std::string(sub_label), max_txt_w, font);
      shadow_print(it, x0 + pad_l, cy - half_step, font, text_c, UiTextAlign::CENTER_LEFT, fit1.c_str());
      shadow_print(it, x0 + pad_l, cy + half_step, font, sub_c, UiTextAlign::CENTER_LEFT, fit2.c_str());
    } else if (has_mdi || has_badge) {
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      shadow_print(it, x0 + pad_l, cy, font, text_c, UiTextAlign::CENTER_LEFT, fit.c_str());
    } else {
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
    }
  }

  frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, flags, param);
}

void labeled_action_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                        const char *title, const char *status,
                        UiColor title_c, UiColor status_c,
                        UiFontView title_font, UiFontView status_font,
                        const char *cta_label, int cta_w, int cta_h,
                        UiColor cta_border_c, UiColor cta_bg_c, UiColor cta_text_c,
                        UiFontView cta_font,
                        uint16_t cta_wid, uint8_t cta_flags,
                        int16_t cta_param, int cta_hit_h_min,
                        TileDrawIconFn draw_icon_fn, void *icon_ctx, UiColor icon_c) {
  const int eff_w = std::max(120, w);
  const int eff_h = std::max(36, h);
  const int x0 = cx - eff_w / 2;
  const int x1 = x0 + eff_w;

  const int eff_cta_h = std::clamp(cta_h, 30, eff_h);
  const int eff_cta_w = std::clamp(cta_w, 64, std::max(64, (eff_w * 42) / 100));
  const int cta_x0 = x1 - eff_cta_w;

  int txt_x0 = x0 + scale_x(4);
  if (draw_icon_fn != nullptr) {
    const int ir = std::min(card_icon_r(eff_h), scale_s(20));
    const int icon_cx = x0 + ir + scale_x(4);
    draw_icon_fn(it, icon_cx, cy, ir, icon_c, true, icon_ctx);
    txt_x0 = icon_cx + ir + std::max(10, scale_x(12));
  }

  const int gap = std::max(8, scale_x(12));
  const int txt_max_w = std::max(44, cta_x0 - txt_x0 - gap);
  const bool has_status = (status != nullptr && status[0] != '\0' && status_font != nullptr);

  if (title != nullptr && title[0] != '\0' && title_font != nullptr) {
    const std::string fit_title = fit_text_to_width_font(std::string(title), txt_max_w, title_font);
    if (has_status) {
      const std::string fit_status = fit_text_to_width_font(std::string(status), txt_max_w, status_font);
      shadow_print(it, txt_x0, cy - std::max(8, eff_h / 6), title_font, title_c,
                   UiTextAlign::CENTER_LEFT, fit_title.c_str());
      shadow_print(it, txt_x0, cy + std::max(9, eff_h / 5), status_font, status_c,
                   UiTextAlign::CENTER_LEFT, fit_status.c_str());
    } else {
      shadow_print(it, txt_x0, cy, title_font, title_c, UiTextAlign::CENTER_LEFT, fit_title.c_str());
    }
  }

  action_pill(it, cta_x0, eff_cta_w, cy, eff_cta_h, cta_border_c, cta_bg_c,
              cta_label, cta_text_c, cta_font, cta_wid, cta_flags, cta_param, cta_hit_h_min);
}

void grid_tile(Rgb565Surface &it, int cx, int cy, int w, int h,
               const char *label, bool active, bool disabled,
               UiColor accent_c, UiColor text_c, UiFontView font,
               TileDrawIconFn draw_icon_fn, void *user_ctx,
               uint16_t widget_id, int16_t param) {
  const int eff_w = std::max(48, w);
  const int eff_h = std::max(48, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const UiColor bg_c = active ? chrome_card_bg_on() : chrome_card_bg_off();
  const UiColor brd_c = active ? accent_c : (disabled ? chrome_card_brd_off() : chrome_card_brd_on());

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, text_c);

  const int icon_r = std::min(card_icon_r(eff_h), std::max(10, eff_h / 4));
  const int icon_cy = cy - eff_h / 6;
  if (draw_icon_fn != nullptr) {
    draw_icon_fn(it, cx, icon_cy, icon_r, disabled ? text_c : (active ? accent_c : text_c), true, user_ctx);
  }
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const int lbl_y = cy + (eff_h * 27) / 100;
    const std::string fit = fit_text_to_width_font(std::string(label), std::max(32, eff_w - scale_x(12)), font);
    shadow_print(it, cx, lbl_y, font, text_c, UiTextAlign::CENTER, fit.c_str());
  }
  if (widget_id != WidgetId::NONE && !disabled) {
    frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

void icon_circle_button(Rgb565Surface &it, int cx, int cy, int r,
                        UiColor bg_c, UiColor brd_c, UiColor icon_c,
                        TileDrawIconFn draw_icon_fn, void *user_ctx,
                        uint16_t widget_id, uint8_t flags, int16_t param, int hit_pad) {
  const int eff_r = std::max(10, r);
  render_glass_circle(it, cx, cy, eff_r, bg_c, brd_c);
  if (draw_icon_fn != nullptr) {
    draw_icon_fn(it, cx, cy, dock_icon_r(eff_r), icon_c, true, user_ctx);
  }
  if (widget_id != WidgetId::NONE) {
    const int min_r = (layout().min_hit_px() + 1) / 2;
    const int req_pad = std::max(0, min_r - eff_r);
    const int pad = (hit_pad >= 0) ? std::max(hit_pad, req_pad) : std::max(scale_s(6), req_pad);
    frame_hits().add_circle(cx, cy, eff_r, widget_id, flags, param, pad);
  }
}

void keypad_button(Rgb565Surface &it, int cx, int cy, int w, int h,
                   const char *label, UiColor bg_c, UiColor brd_c,
                   UiColor text_c, UiFontView font,
                   uint16_t widget_id, int16_t param) {
  const int eff_w = std::max(24, w);
  const int eff_h = std::max(24, h);
  render_card_box(it, cx - eff_w / 2, cy - eff_h / 2, eff_w, eff_h,
                  bg_c, brd_c, kRadiusKeyPx(), GlassVariant::Clear, text_c);
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, label);
  }
  if (widget_id != WidgetId::NONE) {
    const int min_px = layout().min_hit_px();
    const int req_pad = std::max(0, (std::max(min_px - eff_w, min_px - eff_h) + 1) / 2);
    frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param, req_pad);
  }
}

// ==============================================================================
// 2. Toggle Rows, Steppers, Color Wheel, Confirmation & PIN Pad Modals
// ==============================================================================
void toggle_card_row(Rgb565Surface &it, int cx, int y_mid, int w, int h,
                     const char *label, bool is_on, UiFontView font,
                     UiColor active_col, UiColor text_on, UiColor text_off,
                     uint16_t widget_id, int16_t param) {
  const int eff_w = std::min(SCREEN_W - 16, std::max(80, w));
  const int eff_h = std::max(28, h);
  const UiColor bg_c = is_on ? chrome_card_bg_on() : chrome_card_bg_off();
  const UiColor brd_c = is_on ? active_col : chrome_card_brd_off();
  const UiColor fg = is_on ? text_on : text_off;
  render_card_box(it, cx - eff_w / 2, y_mid - eff_h / 2, eff_w, eff_h,
                  bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, fg);

  const int text_x = cx - eff_w / 2 + scale_x(14);
  const int sw_cx = cx + eff_w / 2 - scale_x(28);
  const int max_lbl_px = std::max(60, (sw_cx - scale_x(26)) - text_x);
  const std::string fit_lbl = fit_text_to_width_font(
      label != nullptr ? std::string(label) : std::string(), max_lbl_px, font);
  shadow_print(it, text_x, y_mid, font, fg, UiTextAlign::CENTER_LEFT, fit_lbl.c_str());
  toggle_switch(it, sw_cx, y_mid, is_on, active_col);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx, y_mid, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

void pill_button(Rgb565Surface &it, int x1, int x2, int y_mid, int r,
                 UiColor border_c, UiColor bg_c,
                 const char *label, UiColor text_c, UiFontView font,
                 uint16_t widget_id, uint8_t flags, int16_t param, GlassVariant variant) {
  render_pill_button(it, x1, x2, y_mid, r, border_c, bg_c, label, text_c, font, variant);
  if (widget_id != WidgetId::NONE) {
    const int px0 = x1 - r;
    const int py0 = y_mid - r;
    const int pw = (x2 - x1) + 2 * r + 1;
    const int ph = 2 * r + 1;
    frame_hits().add_rect(px0, py0, pw, ph, widget_id, flags, param);
  }
}

void pill_button_box(Rgb565Surface &it, int x0, int w, int y_mid, int r,
                     UiColor border_c, UiColor bg_c,
                     const char *label, UiColor text_c, UiFontView font,
                     uint16_t widget_id, uint8_t flags, int16_t param, GlassVariant variant) {
  const int safe_w = std::max(2, w);
  const int eff_r = std::min(std::max(1, r), safe_w / 2);
  const int x1 = x0 + eff_r;
  const int x2 = std::max(x1, x0 + safe_w - 1 - eff_r);
  pill_button(it, x1, x2, y_mid, eff_r, border_c, bg_c, label, text_c, font,
              widget_id, flags, param, variant);
}

void stepper_row(Rgb565Surface &it, int cx, int y_top, int h,
                 const char *const labels[4], UiFontView font,
                 UiColor col_text, uint16_t base_widget_id) {
  const int btn_w = (SCREEN_W < 380) ? 50 : scale_x(60);
  const int step = (SCREEN_W < 380) ? 66 : scale_x(80);
  const int eff_h = std::max(28, h);
  const int y_mid = y_top + eff_h / 2;
  const int rad = kRadiusKeyPx();
  const UiColor bg_c = chrome_card_bg_on();
  const UiColor brd_c = chrome_card_brd_on();
  for (int i = 0; i < 4; i++) {
    const int bx = cx + static_cast<int>((static_cast<float>(i) - 1.5f) * static_cast<float>(step));
    render_card_box(it, bx - btn_w / 2, y_top, btn_w, eff_h, bg_c, brd_c, rad,
                    GlassVariant::Clear, col_text);
    if (labels[i] != nullptr && labels[i][0] != '\0' && font != nullptr) {
      shadow_print(it, bx, y_mid, font, col_text, UiTextAlign::CENTER, labels[i]);
    }
    if (base_widget_id != WidgetId::NONE) {
      frame_hits().add_centered_rect(
          bx, y_mid, btn_w, eff_h,
          static_cast<uint16_t>(base_widget_id + i),
          kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
  }
}

UiColor hsv_wheel_sector_color(int sector_idx_72, float sat) {
  int idx = sector_idx_72 % 72;
  if (idx < 0) idx += 72;
  const HsvUnitRgb u = hsv_unit_lut_72()[idx];
  const float s = std::max(0.0f, std::min(1.0f, sat));
  const float inv = 1.0f - s;
  return UiColor{
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.r) * s),
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.g) * s),
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.b) * s),
  };
}

bool hsv_wheel_pick(int tx, int ty, int cx, int cy, int outer_r_466, int inner_r_466,
                    float &out_hue_deg, float &out_sat) {
  const float ws = (SCREEN_MIN_DIM > 520)
                       ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f)
                       : ((SCREEN_MIN_DIM < 380) ? (static_cast<float>(SCREEN_MIN_DIM) / 420.0f) : 1.0f);
  const float dx = static_cast<float>(tx - cx);
  const float dy = static_cast<float>(ty - cy);
  const float dist_466 = std::sqrt(dx * dx + dy * dy) / ws;
  const float min_r = static_cast<float>(inner_r_466) - 12.0f;
  const float max_r = static_cast<float>(outer_r_466) + 14.0f;
  if (dist_466 < min_r || dist_466 > max_r) return false;

  float hue = std::atan2(dy, dx) * (180.0f / PI_F);
  if (hue < 0.0f) hue += 360.0f;
  const float span = std::max(1.0f, static_cast<float>(outer_r_466 - inner_r_466));
  const float norm = (dist_466 - static_cast<float>(inner_r_466)) / span;
  out_hue_deg = hue;
  out_sat = std::max(0.35f, std::min(1.0f, 0.35f + norm * 0.65f));
  return true;
}

void hsv_color_wheel(Rgb565Surface &it, int cx, int cy, int outer_r_466, int inner_r_466,
                     float cur_hue_deg, float cur_sat,
                     UiColor col_bg, UiColor col_white, uint16_t widget_id) {
  const float ws = (SCREEN_MIN_DIM > 520)
                       ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f)
                       : ((SCREEN_MIN_DIM < 380) ? (static_cast<float>(SCREEN_MIN_DIM) / 420.0f) : 1.0f);
  const int r_in = static_cast<int>(static_cast<float>(inner_r_466) * ws);
  const int r_out = static_cast<int>(static_cast<float>(outer_r_466) * ws);
  const int r_span = std::max(1, r_out - r_in);

  for (int a_idx = 0; a_idx < 72; a_idx++) {
    const float rad = static_cast<float>(a_idx * 5) * (PI_F / 180.0f);
    float s_v = 0.0f, c_v = 1.0f;
    fast_sincos(rad, s_v, c_v);
    for (int band = 0; band < 4; band++) {
      const int r0 = r_in + (band * r_span) / 4;
      const int r1 = r_in + ((band + 1) * r_span) / 4;
      const float sat_b = 0.40f + 0.20f * static_cast<float>(band);
      const UiColor seg_c = hsv_wheel_sector_color(a_idx, sat_b);
      const int x0 = cx + static_cast<int>(static_cast<float>(r0) * c_v);
      const int y0 = cy + static_cast<int>(static_cast<float>(r0) * s_v);
      const int x1 = cx + static_cast<int>(static_cast<float>(r1) * c_v);
      const int y1 = cy + static_cast<int>(static_cast<float>(r1) * s_v);
      fast_line(it, x0, y0, x1, y1, seg_c);
      fast_line(it, x0 + 1, y0, x1 + 1, y1, seg_c);
      fast_line(it, x0, y0 + 1, x1, y1 + 1, seg_c);
    }
  }

  const UiColor cur_rgb = hsv_to_rgb(cur_hue_deg / 360.0f, cur_sat, 1.0f);
  fast_filled_circle_aa(it, cx, cy, std::max(8, r_in - static_cast<int>(10.0f * ws)), cur_rgb);
  fast_circle_aa(it, cx, cy, static_cast<float>(std::max(9, r_in - static_cast<int>(9.0f * ws))),
                 chrome_wheel_inner_ring(), 1.5f);
  fast_circle_aa(it, cx, cy, static_cast<float>(r_out + 2), chrome_wheel_outer_ring(), 1.5f);

  const float sel_rad = cur_hue_deg * (PI_F / 180.0f);
  const float norm_s = std::max(0.0f, std::min(1.0f, (cur_sat - 0.35f) / 0.65f));
  const float sel_r = static_cast<float>(r_in) + norm_s * static_cast<float>(r_span);
  float ss = 0.0f, sc = 1.0f;
  fast_sincos(sel_rad, ss, sc);
  const int sx = cx + static_cast<int>(sel_r * sc);
  const int sy = cy + static_cast<int>(sel_r * ss);
  const int thumb_r = std::max(7, static_cast<int>(9.0f * ws));
  fast_filled_circle_aa(it, sx, sy, thumb_r + 2, col_bg);
  fast_filled_circle_aa(it, sx, sy, thumb_r, col_white);
  fast_filled_circle_aa(it, sx, sy, std::max(3, thumb_r - 3), cur_rgb);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_circle(cx, cy, r_out + 8, widget_id, kHitTap);
  }
}

void confirm_modal(Rgb565Surface &it, int cx, const char *title,
                   const char *line1, const char *line2,
                   const char *cancel_lbl, const char *confirm_lbl,
                   UiColor confirm_border, UiColor confirm_bg,
                   UiFontView font_hdr, UiFontView font_body,
                   UiColor theme_primary, UiColor col_white, UiColor col_slate,
                   uint16_t cancel_wid, uint16_t confirm_wid) {
  (void)theme_primary;
  render_page_header(it, cx, title, font_hdr, col_white, true);
  if (line1 != nullptr && line1[0] != '\0') {
    const int w1 = std::min(kMaxContentColW, layout().safe_span(scale_y(132), 18));
    const std::string f1 = fit_text_to_width_font(std::string(line1), w1, font_body);
    shadow_print(it, cx, scale_y(132), font_body, col_white, UiTextAlign::CENTER, f1.c_str());
  }
  if (line2 != nullptr && line2[0] != '\0') {
    const int w2 = std::min(kMaxContentColW, layout().safe_span(scale_y(168), 18));
    const std::string f2 = fit_text_to_width_font(std::string(line2), w2, font_body);
    shadow_print(it, cx, scale_y(168), font_body, col_slate, UiTextAlign::CENTER, f2.c_str());
  }
  const int btn_y = scale_y(248);
  const int btn_h = std::max(44, scale_y(50));
  const ActionPillSpec specs[2] = {
      {cancel_lbl, cancel_wid, chrome_card_brd_on(), chrome_card_bg_off(), col_white, 0, nullptr},
      {confirm_lbl, confirm_wid, confirm_border, confirm_bg, col_white, 1, nullptr},
  };
  action_pill_row(it, btn_y, btn_h, std::min(kMaxContentColW, scale_x(300)),
                  scale_x(138), scale_x(14), specs, 2, font_body, 16);
}

void game_over_sheet(Rgb565Surface &it, int cx, int cy,
                     const char *title, UiColor title_col,
                     const char *score_line, const char *best_line, bool is_new_best,
                     const char *cta_lbl,
                     UiFontView font_hdr, UiFontView font_body,
                     UiColor theme_primary, UiColor col_white,
                     UiColor col_gold, UiColor col_slate, uint16_t cta_wid) {
  const int scrim_w = std::min(kMaxContentColW, std::min(SCREEN_W - 24, scale_x(312)));
  const int scrim_h = scale_y(206);
  fill_round_rect_glass(it, cx - scrim_w / 2, cy - scale_y(102), scrim_w, scrim_h,
                        static_cast<float>(kRadiusCardPx()), kChromeShadow, 176, 176);
  shadow_print(it, cx, cy - scale_y(60), font_hdr, title_col, UiTextAlign::CENTER, title, true);
  if (score_line != nullptr && score_line[0] != '\0') {
    shadow_print(it, cx, cy - scale_y(18), font_body, col_white, UiTextAlign::CENTER, score_line);
  }
  if (best_line != nullptr && best_line[0] != '\0') {
    shadow_print(it, cx, cy + scale_y(14), font_body, is_new_best ? col_gold : col_slate,
                 UiTextAlign::CENTER, best_line);
  }
  const int pill_h = std::max(40, scale_y(44));
  const int pill_w = std::min(scrim_w - scale_x(36), scale_x(204));
  action_pill_centered(it, cx, pill_w, cy + scale_y(62), pill_h,
                       theme_primary, chrome_card_bg_on(), cta_lbl, col_white, font_body,
                       cta_wid, kHitTap | kHitFocusSlot, 0);
}

PinPadModalState &pin_pad_state() { return g_pin_pad; }

void pin_pad_scrub() {
  volatile char *p = g_pin_pad.digits;
  for (size_t i = 0; i < sizeof(g_pin_pad.digits); i++) {
    p[i] = '\0';
  }
  g_pin_pad.len = 0;
  g_pin_pad.open = false;
  g_pin_pad.target = PinPadTargetKind::NONE;
  g_pin_pad.error_msg[0] = '\0';
  g_pin_pad.error_ms = 0;
  g_pin_pad.last_activity_ms = 0;
}

void pin_pad_open(PinPadTargetKind kind, const char *prompt_label, uint32_t now_ms) {
  pin_pad_scrub();
  g_pin_pad.open = true;
  g_pin_pad.target = kind;
  g_pin_pad.last_activity_ms = (now_ms != 0U) ? now_ms : hal_millis_now();
  if (prompt_label != nullptr) {
    std::snprintf(g_pin_pad.prompt, sizeof(g_pin_pad.prompt), "%s", prompt_label);
  } else {
    g_pin_pad.prompt[0] = '\0';
  }
}

bool pin_pad_press_digit(uint8_t digit, uint32_t now_ms) {
  if (digit > 9 || g_pin_pad.len >= PinPadModalState::kMaxDigits) return false;
  g_pin_pad.digits[g_pin_pad.len++] = static_cast<char>('0' + digit);
  g_pin_pad.digits[g_pin_pad.len] = '\0';
  g_pin_pad.error_msg[0] = '\0';
  g_pin_pad.last_activity_ms = (now_ms != 0U) ? now_ms : hal_millis_now();
  return true;
}

void pin_pad_backspace(uint32_t now_ms) {
  if (g_pin_pad.len > 0) {
    volatile char *p = g_pin_pad.digits;
    --g_pin_pad.len;
    p[g_pin_pad.len] = '\0';
  }
  g_pin_pad.error_msg[0] = '\0';
  g_pin_pad.last_activity_ms = (now_ms != 0U) ? now_ms : hal_millis_now();
}

bool is_perimeter_action_blocked(const std::string &policy, const std::string &domain_or_entity,
                                 const std::string &action, int level) {
  return evaluate_perimeter_action_blocked(policy, domain_or_entity, action, level);
}

bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level, uint16_t alarm_caps) {
  return evaluate_perimeter_pin_prompt(
      policy, domain_or_entity, action, level, alarm_caps, LATTER_IS_STATIONARY != 0);
}

bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level) {
  return requires_perimeter_pin_prompt(
      policy, domain_or_entity, action, level, effective_alarm_caps());
}

void pin_pad_modal(Rgb565Surface &it, int cx, int cy, uint32_t now_ms,
                   UiFontView font_hdr, UiFontView font_key, UiFontView font_small,
                   UiColor theme_primary, UiColor col_white, UiColor col_slate, UiColor col_red) {
  (void)cy;
  (void)font_hdr;
  auto &st = pin_pad_state();
  const char *hdr = (st.prompt[0] != '\0') ? st.prompt : "ENTER SECURITY PIN";
  render_page_header(it, cx, hdr, font_small, col_white, true);

  const int disp_y = scale_y(84);
  const int disp_w = std::min(kMaxContentColW, std::min(SCREEN_W - 44, scale_x(236)));
  const bool show_err = (st.error_msg[0] != '\0' && (now_ms - st.error_ms) < 2200U);
  if (show_err) {
    shadow_print(it, cx, disp_y, font_small, col_white, UiTextAlign::CENTER, st.error_msg);
  } else if (st.len == 0) {
    shadow_print(it, cx, disp_y, font_small, col_slate, UiTextAlign::CENTER, "4-12 Digits");
  } else {
    const int max_step = scale_x(18);
    const int dot_step = (st.len > 1)
                             ? std::min(max_step, std::max(scale_x(12), (disp_w - scale_x(20)) / (static_cast<int>(st.len) - 1)))
                             : max_step;
    const int dot_r = (st.len > 9) ? std::max(3, scale_s(4)) : scale_s(5);
    const int start_x = cx - ((static_cast<int>(st.len) - 1) * dot_step) / 2;
    for (uint8_t i = 0; i < st.len; i++) {
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step + 1, disp_y + 1, dot_r, kChromeShadow);
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step, disp_y, dot_r, col_white);
    }
  }
  fast_hline(it, cx - disp_w / 2, disp_y + scale_y(16), disp_w, show_err ? col_red : chrome_card_brd_on());

  const int key_w = (SCREEN_W < 380) ? 68 : scale_x(78);
  const int key_h = scale_y(48);
  const int col_step = (SCREEN_W < 380) ? 78 : scale_x(90);
  const int row_y0 = scale_y(136);
  const int row_step = scale_y(56);

  static constexpr const char *kLabels[12] = {
      "1", "2", "3",
      "4", "5", "6",
      "7", "8", "9",
      "DEL", "0", "OK",
  };
  static constexpr uint16_t kWids[12] = {
      WidgetId::PIN_PAD_KEY_BASE + 1, WidgetId::PIN_PAD_KEY_BASE + 2, WidgetId::PIN_PAD_KEY_BASE + 3,
      WidgetId::PIN_PAD_KEY_BASE + 4, WidgetId::PIN_PAD_KEY_BASE + 5, WidgetId::PIN_PAD_KEY_BASE + 6,
      WidgetId::PIN_PAD_KEY_BASE + 7, WidgetId::PIN_PAD_KEY_BASE + 8, WidgetId::PIN_PAD_KEY_BASE + 9,
      WidgetId::PIN_PAD_BACKSPACE,    WidgetId::PIN_PAD_KEY_BASE + 0, WidgetId::PIN_PAD_SUBMIT,
  };

  for (int idx = 0; idx < 12; idx++) {
    const int r = idx / 3;
    const int c = idx % 3;
    const int kx = cx + (c - 1) * col_step;
    const int ky = row_y0 + r * row_step;
    const bool is_ok = (idx == 11);
    const bool is_del = (idx == 9);
    const bool ok_ready = (st.len >= 4);
    const UiColor brd = is_ok ? (ok_ready ? theme_primary : chrome_key_brd()) : chrome_key_brd();
    const UiColor bg = is_ok && ok_ready ? chrome_card_bg_on() : chrome_key_bg();
    const UiColor fg = (is_ok && !ok_ready) ? col_slate : col_white;
    keypad_button(it, kx, ky, key_w, key_h, kLabels[idx], bg, brd, fg,
                  (is_ok || is_del) ? font_small : font_key, kWids[idx], static_cast<int16_t>(idx));
  }

  const auto ly = layout();
  const int cancel_y = ly.back_y;
  fast_print(it, cx + 1, cancel_y + 1, font_small, kChromeShadow, UiTextAlign::CENTER, "< Cancel");
  fast_print(it, cx, cancel_y, font_small, kChromeBackLabel, UiTextAlign::CENTER, "< Cancel");
  const int cancel_y0 = std::clamp(cancel_y - scale_y(22), 0, ly.h - 1);
  const int cancel_h = std::max(1, ly.h - cancel_y0);
  const int cancel_max_w = std::max(ly.min_hit_px(), ly.safe_span(cancel_y, 8));
  const int cancel_w = std::min(cancel_max_w, std::max(scale_x(200), ly.min_hit_px() * 2));
  const int cancel_pad = std::max(0, (ly.min_hit_px() - cancel_h + 1) / 2);
  frame_hits().add_rect(cx - cancel_w / 2, cancel_y0, cancel_w, cancel_h,
                        WidgetId::PIN_PAD_CANCEL, kHitTap | kHitFocusSlot, 0, cancel_pad);
}

}  // namespace ui


// ==============================================================================
// 1. Scrollbars, Page Pips, Mode Bar Track & Progress Arc
// ==============================================================================
void render_viewport_scrollbar(Rgb565Surface &it, int cx, int y_pos, int total_items,
                               float cur_start_idx, UiColor col_thumb, bool is_dim) {
  if (total_items <= 4) return;
  const int track_w = (SCREEN_W < 380) ? 84 : ((SCREEN_W > 520) ? 168 : 112);
  const int track_h = (SCREEN_W > 520) ? 6 : 4;
  const int r_cap = track_h / 2;
  const int track_x0 = cx - (track_w / 2);
  const int track_y0 = y_pos - r_cap;

  const float cap = static_cast<float>(r_cap);
  UiColor eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(GlassBackdrop{}, kChromeGlassTint, kChromeGlassSheen, eff_tint, a_top, a_bot,
                     rim_top, rim_bot, is_dim ? 72 : -1, GlassVariant::Clear);
  if (is_dim) {
    rim_top = (rim_top * 2) / 3;
    rim_bot = (rim_bot * 2) / 3;
  }
  fill_round_rect_glass(it, track_x0, track_y0, track_w, track_h, cap,
                        kChromeGlassSheen, rim_top, rim_bot, 1);
  if (track_h > 2) {
    fill_round_rect_glass(it, track_x0 + 1, track_y0 + 1, track_w - 2, track_h - 2,
                          std::max(0.5f, cap - 1.0f), eff_tint, a_top, a_bot);
  }

  const int min_thumb_w = (SCREEN_W > 520) ? 28 : 20;
  const int thumb_w = std::max(min_thumb_w, std::min(track_w, (track_w * 4) / total_items));
  const float max_start = static_cast<float>(std::max(1, total_items - 4));
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
  const int spacing = scale_x(14);
  const int dot_r = (SCREEN_W > 520) ? 3 : 2;
  const int total_w = (total_pages - 1) * spacing;
  const int start_x = cx - (total_w / 2);
  for (int p = 0; p < total_pages; p++) {
    const int px = start_x + p * spacing;
    if (p == active_page) {
      const int pw = scale_x(7);
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
  int mask_x = 0, chev_x = 0, hit_x0 = 0, hit_w = 0, min_hit_h = 0;
  compute_vertical_page_pips_x(SCREEN_W, LATTER_ROUND_DISPLAY != 0, LATTER_PANEL_PPI,
                               mask_x, chev_x, hit_x0, hit_w, min_hit_h);
  const bool above_bar = (bottom_limit_y < SCREEN_H - scale_y(40));
  const int up_y = above_bar ? scale_y(134) : scale_y(162);
  const int dn_y = above_bar ? scale_y(236) : scale_y(278);
  const int split_y = above_bar ? ((up_y + dn_y) / 2) : SCREEN_CY;
  const int max_dn_y1 = std::min(above_bar ? bottom_limit_y : (SCREEN_H - scale_y(70)), bottom_limit_y);
  if (!backdrop_active() && mask_x < SCREEN_W) {
    const int mask_y0 = std::max(0, up_y - scale_y(22));
    const int mask_y1 = std::min(bottom_limit_y, dn_y + scale_y(22));
    if (mask_y1 > mask_y0) {
      fill_rect_row_major(it, mask_x, mask_y0, SCREEN_W - mask_x, mask_y1 - mask_y0, col_bg);
    }
  }
  if (show_up) {
    render_scroll_chevron(it, chev_x, up_y, true, col_active, kChromeShadow, backdrop_active());
    const int up_y0 = std::min(scale_y(70), std::max(0, split_y - min_hit_h));
    const int up_h = std::max(min_hit_h, split_y - up_y0);
    frame_hits().add_rect(hit_x0, up_y0, hit_w, std::min(bottom_limit_y - up_y0, up_h),
                          WidgetId::CHEVRON_UP, kHitTap);
  }
  if (show_down) {
    render_scroll_chevron(it, chev_x, dn_y, false, col_active, kChromeShadow, backdrop_active());
    const int dn_avail = std::max(0, max_dn_y1 - split_y);
    if (dn_avail > 0) {
      frame_hits().add_rect(hit_x0, split_y, hit_w, dn_avail, WidgetId::CHEVRON_DOWN, kHitTap);
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
  const int r_track = dr + scale_s(3);
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
    if (abx >= left_cx - scale_x(16) && abx <= right_cx + scale_x(16)) {
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

  const int mode_pad = std::max(scale_s(8), layout().dock_hit_r() - dr);
  for (int i = 0; i < vis_count && i < 4; i++) {
    frame_hits().add_circle(win_xs[i], by, dr,
                            static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                            kHitTap | kHitFocusSlot | kHitFocusCircle,
                            static_cast<int16_t>(win_start_idx + i),
                            mode_pad);
  }

  const int chev_off = dr + ((SCREEN_W < 380) ? 10 : scale_x(13));
  const int ch_s = scale_s(5);
  if (has_left_arrow) {
    const int lx = left_cx - chev_off;
    fast_line(it, lx + ch_s / 2, by - ch_s, lx - ch_s / 2, by, col_white);
    fast_line(it, lx - ch_s / 2, by, lx + ch_s / 2, by + ch_s, col_white);
    fast_line(it, lx + ch_s / 2 + 1, by - ch_s, lx - ch_s / 2 + 1, by, theme_primary);
    fast_line(it, lx - ch_s / 2 + 1, by, lx + ch_s / 2 + 1, by + ch_s, theme_primary);
    frame_hits().add_rect(0, by - dr - scale_y(12), std::max(8, left_cx - dr),
                          2 * (dr + scale_y(12)), WidgetId::MODE_BAR_LEFT, kHitTap);
  }
  if (has_right_arrow) {
    const int rx = right_cx + chev_off;
    fast_line(it, rx - ch_s / 2, by - ch_s, rx + ch_s / 2, by, col_white);
    fast_line(it, rx + ch_s / 2, by, rx - ch_s / 2, by + ch_s, col_white);
    fast_line(it, rx - ch_s / 2 - 1, by - ch_s, rx + ch_s / 2 - 1, by, theme_primary);
    fast_line(it, rx + ch_s / 2 - 1, by, rx - ch_s / 2 - 1, by + ch_s, theme_primary);
    frame_hits().add_rect(right_cx + dr, by - dr - scale_y(12),
                          std::max(8, SCREEN_W - (right_cx + dr)),
                          2 * (dr + scale_y(12)), WidgetId::MODE_BAR_RIGHT, kHitTap);
  }

  if (total_count > 4) {
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
  const int bar_x0 = 36;
  const int bar_w = SCREEN_W - 72;
  const int fill_w = static_cast<int>(prog * static_cast<float>(bar_w));
  fill_rect_row_major(it, bar_x0, 6, bar_w, 4, kChromeTrack);
  if (fill_w > 0) {
    fill_rect_row_major(it, bar_x0, 6, fill_w, 4, col);
  }
  return;
#else
  if (prog <= 0.003f) return;
  if (prog > 1.0f) prog = 1.0f;

  static constexpr int R_BORDER = scale_min(226);
  static constexpr int R_IN = scale_min(227);
  static constexpr int R_OUT = scale_min(232);
  static constexpr int R_MID = scale_min(229);
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
  const int start_idx = get_mode_bar_start_index(total_count, win_start);
  const int vis_count = std::min(4, total_count - start_idx);
  if (vis_count <= 0) return;

  int win_xs[4] = {0, 0, 0, 0};
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
    if (bx < left_cx - scale_x(16) || bx > right_cx + scale_x(16)) continue;

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
                      int16_t prev_param, int16_t next_param) {
  if (font == nullptr) return;
  const auto &ly = layout();
  if (!show_arrows) {
    const int max_w = std::min(kMaxContentColW, std::max(80, ly.safe_span(cy, 16)));
    const std::string fit = fit_text_to_width_font(title, max_w, font);
    shadow_print(it, cx, cy, font, col_white, UiTextAlign::CENTER, fit.c_str());
    return;
  }

  const int btn_r = (SCREEN_W < 380) ? 16 : scale_s(17);
  const int safe_w = std::min(kMaxContentColW, ly.band_safe_span(cy, 2 * btn_r, 14));
  const int span_w = std::min(safe_w, scale_x(300));
  const int lx = cx - span_w / 2 + btn_r;
  const int rx = cx + span_w / 2 - btn_r;
  const int gap = scale_x(10);
  const int title_max_w = std::max(48, (rx - btn_r) - (lx + btn_r) - 2 * gap);

  const int min_r = (ly.min_hit_px() + 1) / 2;
  const int hit_pad = std::max(scale_s(6), min_r - btn_r);
  const int ir = dock_icon_r(btn_r);

  render_glass_circle(it, lx, cy, btn_r, chrome_card_bg_off(), chrome_card_brd_on());
  draw_icon_chevron(it, lx, cy, ir, false, col_white, true);
  if (prev_wid != WidgetId::NONE) {
    frame_hits().add_circle(lx, cy, btn_r, prev_wid,
                            kHitTap | kHitFocusSlot | kHitFocusCircle, prev_param, hit_pad);
  }

  render_glass_circle(it, rx, cy, btn_r, chrome_card_bg_off(), chrome_card_brd_on());
  draw_icon_chevron(it, rx, cy, ir, true, col_white, true);
  if (next_wid != WidgetId::NONE) {
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
    const float sh_off = static_cast<float>(std::max(1, scale_min(2)));
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
  const float w_card = std::max(2.6f, scale_min_f(3.6f));
  const float w_maj = std::max(2.0f, scale_min_f(2.6f));
  const float w_min = std::max(1.1f, scale_min_f(1.4f));
  const float len_card = scale_min_f(22.0f);
  const float len_maj = scale_min_f(16.0f);
  const float len_min = scale_min_f(8.5f);

  for (int i = 0; i < 60; ++i) {
    const float rad = (static_cast<float>(i) / 60.0f) * 2.0f * PI_F - HALF_PI_F;
    const float s = std::sin(rad);
    const float c = std::cos(rad);
    if (i == 0 && double_twelve) {
      const float sep = scale_min_f(3.4f);
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
  const int r_outer = std::max(6, scale_min(8));
  const int r_cap = std::max(4, scale_min(5));
  const int r_jewel = std::max(2, scale_min(2));
  fast_filled_circle_aa(it, cx + 1, cy + 1, r_outer + 1, kChromeShadow);
  fast_filled_circle_aa(it, cx, cy, r_outer, outer_col);
  fast_filled_circle_aa(it, cx, cy, r_cap, cap_col);
  fast_filled_circle_aa(it, cx, cy, r_jewel, jewel_col);
}

}  // namespace ui


// ==============================================================================
// 1. Page Headers, Sub-Headers, Empty States & Time Readout (D-060 / D-070)
// ==============================================================================
void render_page_header(Rgb565Surface &it, int cx, const char *title,
                        UiFontView font, UiColor color, bool shadow) {
  (void)color;
  (void)shadow;
  if (title == nullptr || title[0] == '\0' || font == nullptr) return;
  const int y = layout().hdr_y;
  fast_print(it, cx + 1, y + 1, font, kChromeShadow, UiTextAlign::CENTER, title);
  fast_print(it, cx, y, font, UiColor::WHITE, UiTextAlign::CENTER, title);
}

void render_page_header(Rgb565Surface &it, int cx, const std::string &title,
                        UiFontView font, UiColor color, bool shadow) {
  if (title.empty() || font == nullptr) return;
  const std::string fitted = fit_text_to_width_font(title, scale_x(280), font);
  render_page_header(it, cx, fitted.c_str(), font, color, shadow);
}

void render_sub_header(Rgb565Surface &it, int cx, const char *text,
                       UiFontView font, UiColor color) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  if (backdrop_active()) {
    fast_print(it, cx + 1, layout().sub_hdr_y + 1, font, kChromeShadow, UiTextAlign::CENTER, text);
  }
  fast_print(it, cx, layout().sub_hdr_y, font, color, UiTextAlign::CENTER, text);
}

void render_sub_header(Rgb565Surface &it, int cx, const std::string &text,
                       UiFontView font, UiColor color) {
  render_sub_header(it, cx, text.c_str(), font, color);
}

void render_empty_state(Rgb565Surface &it, int cx, int cy, const char *text,
                        UiFontView font, UiColor text_c) {
  // D-043 / D-048 / D-070: Zero frosted glass behind passive empty-state text.
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  const int max_w = std::min(kMaxContentColW, std::max(80, layout().band_safe_span(cy, scale_y(48), 18)));
  std::string lines[2];
  const int n = ui::wrap_text_lines(std::string(text), max_w, font, lines, 2);
  if (n <= 1) {
    const std::string fit = fit_text_to_width_font(std::string(text), max_w, font);
    ui::shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
  } else {
    int tw = 0, x_off = 0, base = 0, th = 0;
    font->measure("Ag", &tw, &x_off, &base, &th);
    const int half_step = std::max(scale_y(13), (th + 6) / 2);
    ui::shadow_print(it, cx, cy - half_step, font, text_c, UiTextAlign::CENTER, lines[0].c_str());
    ui::shadow_print(it, cx, cy + half_step, font, text_c, UiTextAlign::CENTER, lines[1].c_str());
  }
}

void render_time_readout(Rgb565Surface &it, int cx, const char *text,
                         UiFontView font_time, UiColor color, int y_pos) {
  if (text == nullptr || font_time == nullptr) return;
  if (backdrop_active()) {
    fast_print(it, cx + 1, y_pos + 1, font_time, kChromeShadow, UiTextAlign::CENTER, text);
  }
  fast_print(it, cx, y_pos, font_time, color, UiTextAlign::CENTER, text);
}

namespace ui {

// ==============================================================================
// 2. Shadowed Text, Multi-Line Wrapping & Split Action Card Row
// ==============================================================================
void shadow_print(Rgb565Surface &it, int x, int y,
                  UiFontView font, UiColor col,
                  UiTextAlign align, const char *text,
                  bool double_shadow) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  if (double_shadow) {
    fast_print(it, x + 2, y + 2, font, kChromeShadow, align, text);
  }
  fast_print(it, x + 1, y + 1, font, kChromeShadow, align, text);
  fast_print(it, x, y, font, col, align, text);
}

int wrap_text_lines(const std::string &text, int max_w, UiFontView font,
                    std::string *out_lines, int max_lines) {
  if (text.empty() || font == nullptr || out_lines == nullptr || max_lines <= 0 || max_w <= 12) {
    return 0;
  }
  const std::string clean = sanitize_utf8_for_font(text);
  int count = 0;
  size_t pos = 0;
  const size_t n = clean.size();
  while (pos < n && count < max_lines) {
    while (pos < n && (clean[pos] == ' ' || clean[pos] == '\t' || clean[pos] == '\r')) ++pos;
    if (pos >= n) break;
    if (clean[pos] == '\n') {
      ++pos;
      continue;
    }
    std::string line;
    while (pos < n && clean[pos] != '\n') {
      size_t ws_end = pos;
      while (ws_end < n && clean[ws_end] == ' ') ++ws_end;
      size_t word_end = ws_end;
      while (word_end < n && clean[word_end] != ' ' && clean[word_end] != '\n' && clean[word_end] != '\r') {
        ++word_end;
      }
      if (word_end == ws_end) {
        pos = ws_end;
        break;
      }
      const std::string word = clean.substr(ws_end, word_end - ws_end);
      const std::string cand = line.empty() ? word : (line + " " + word);
      int tw = 0, x_off = 0, base = 0, th = 0;
      font->measure(cand.c_str(), &tw, &x_off, &base, &th);
      if (tw <= max_w) {
        line = cand;
        pos = word_end;
      } else if (line.empty()) {
        line = fit_text_to_width_font(word, max_w, font);
        pos = word_end;
        break;
      } else {
        break;
      }
    }
    if (count + 1 == max_lines && pos < n) {
      size_t rem = pos;
      while (rem < n && (clean[rem] == ' ' || clean[rem] == '\n' || clean[rem] == '\r')) ++rem;
      if (rem < n) {
        line = fit_text_to_width_font(line + "...", max_w, font);
      }
    }
    out_lines[count++] = line;
    if (pos < n && clean[pos] == '\n') ++pos;
  }
  return count;
}

void split_action_card_row(Rgb565Surface &it, int cx, int cy, int w, int h,
                           const char *primary_txt, UiFontView font_pri, UiColor pri_col,
                           const char *secondary_txt, UiFontView font_sec, UiColor sec_col,
                           bool card_active, UiColor accent_col,
                           bool action_is_toggle, bool action_on,
                           uint16_t left_wid, uint16_t right_wid, int16_t param) {
  const int x0 = cx - w / 2;
  const int y0 = cy - h / 2;
  const int rad = std::min(h / 2, kRadiusCardPx());
  render_card_box(it, x0, y0, w, h,
                  card_active ? chrome_card_bg_on() : chrome_card_bg_off(),
                  card_active ? accent_col : chrome_card_brd_off(),
                  rad, GlassVariant::Clear, pri_col);

  const int split_w = (w * 70) / 100;
  const int div_x = x0 + split_w;
  const int div_pad = std::max(6, h / 5);
  fast_vline(it, div_x, y0 + div_pad, h - 2 * div_pad, kChromeCardBorder);

  const int text_x = x0 + scale_x(16);
  const int max_text_w = std::max(scale_x(40), split_w - scale_x(24));
  const char *safe_pri = (primary_txt != nullptr) ? primary_txt : "";
  const char *safe_sec = (secondary_txt != nullptr) ? secondary_txt : "";
  if (safe_sec[0] != '\0') {
    const std::string pri_clamped = fit_text_to_width_font(safe_pri, max_text_w, font_pri);
    const std::string sec_clamped = fit_text_to_width_font(safe_sec, max_text_w, font_sec);
    fast_print(it, text_x + 1, cy - scale_y(9) + 1, font_pri, kChromeShadow, UiTextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x, cy - scale_y(9), font_pri, pri_col, UiTextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x + 1, cy + scale_y(13) + 1, font_sec, kChromeShadow, UiTextAlign::CENTER_LEFT, sec_clamped.c_str());
    fast_print(it, text_x, cy + scale_y(13), font_sec, sec_col, UiTextAlign::CENTER_LEFT, sec_clamped.c_str());
  } else {
    const std::string pri_clamped = fit_text_to_width_font(safe_pri, max_text_w, font_pri);
    fast_print(it, text_x + 1, cy + 1, font_pri, kChromeShadow, UiTextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x, cy, font_pri, pri_col, UiTextAlign::CENTER_LEFT, pri_clamped.c_str());
  }

  const int right_w = w - split_w;
  const int right_cx = div_x + right_w / 2;
  if (action_is_toggle) {
    toggle_switch(it, right_cx, cy, action_on, accent_col);
  } else {
    const int btn_r = std::max(12, std::min(h / 2 - 6, scale_s(17)));
    fast_filled_circle_aa(it, right_cx, cy, btn_r, action_on ? accent_col : kChromeTrack);
    fast_circle_aa(it, right_cx, cy, static_cast<float>(btn_r),
                   action_on ? accent_col : kChromeTrackBorder, 1.5f);
    const int ic_px = std::max(14, (btn_r * 13) / 10);
    render_mdi_icon(it, action_on ? "mdi:pause" : "mdi:play", right_cx, cy, ic_px,
                    action_on ? kChromePauseGlyph : kChromePlayGlyph, false);
  }

  const auto ly = layout();
  const int max_gap_pad = std::max(0, (ly.tool_card3_stride - h - 1) / 2);
  const int card_pad = std::min(max_gap_pad, std::max(0, (ly.min_hit_px() - h + 1) / 2));
  frame_hits().add_rect(x0, y0, split_w, h, left_wid, kHitTap | kHitFocusSlot, param, card_pad);
  frame_hits().add_rect(div_x, y0, right_w, h, right_wid, kHitTap | kHitFocusSlot, param, card_pad);
}

}  // namespace ui
}  // namespace latter_ui
