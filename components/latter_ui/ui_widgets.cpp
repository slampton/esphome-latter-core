#include "ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {
using namespace latter_core;

// ==============================================================================
// 1. Shared Frosted-Glass Chrome & Navigation Primitives
// ==============================================================================
UiColor render_card_box(Rgb565Surface &it, int x0, int y0, int w, int h,
                        UiColor bg_c, UiColor border_c, int r,
                        GlassVariant variant, UiColor ref_text,
                        bool sample_surface) {
  if (w <= 4 || h <= 4 || (y0 + h) <= clip_y0() || y0 > clip_y1()) return bg_c;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;

  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_styled_glass(x0, y0, w, h, bg_c, border_c, variant, ref_text, -1, sample_surface,
                       bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);

  fill_round_rect_glass_rimmed(it, x0, y0, w, h, static_cast<float>(r),
                               rim_col, rim_top, rim_bot, eff_tint, a_top, a_bot,
                               std::max(0.5f, static_cast<float>(r) - 1.0f));
  return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
}

UiColor render_glass_circle(Rgb565Surface &it, int cx_btn, int cy_btn, int r,
                            UiColor bg_c, UiColor border_c,
                            int inner_hole_r, GlassVariant variant,
                            UiColor ref_text,
                            bool sample_surface) {
  if (r <= 1 || (cy_btn + r) < clip_y0() || (cy_btn - r) > clip_y1()) return bg_c;
  const int d = 2 * r + 1;
  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_styled_glass(cx_btn - r, cy_btn - r, d, d, bg_c, border_c, variant, ref_text, -1, sample_surface,
                       bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);

  if (inner_hole_r <= 0) {
    fill_round_rect_glass_rimmed(it, cx_btn - r, cy_btn - r, d, d, static_cast<float>(r) + 0.5f,
                                 rim_col, rim_top, rim_bot, eff_tint, a_top, a_bot,
                                 static_cast<float>(r) - 0.5f);
  } else {
    fill_round_rect_glass(it, cx_btn - r, cy_btn - r, d, d, static_cast<float>(r) + 0.5f,
                          rim_col, rim_top, rim_bot, 1);
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
  const int dx = std::max(5, dp_px(7));
  const int dy_hi = std::max(4, dp_px(5));
  const int dy_lo = std::max(3, dp_px(4));
  const int thick = std::max(2, dp_px(2));
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
  for (int r = SCREEN_MIN_R - 10; r <= SCREEN_MIN_R - 7; r++) {
    fast_circle(it, cx, cy, r, ring_c);
  }
#else
  (void)cx;
  (void)cy;
  const int pad = (LATTER_CORNER_R >= 64) ? 5 : 3;
  const int thick = 5;
  const int cr_out = std::max(8, LATTER_CORNER_R - pad);
  stroke_round_rect_aa(it, pad, pad, SCREEN_W - 2 * pad, SCREEN_H - 2 * pad,
                       static_cast<float>(cr_out), ring_c, ring_c, thick);
#endif
}

void render_home_indicator_bar(Rgb565Surface &it, int cx, int y_pos, UiColor bar_col) {
  const int half_w = std::min(SCREEN_W / 4, std::max(32, dp_px(54)));
  const int bar_h = std::max(5, dp_px(5));
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
  const int hit_y0 = std::clamp(back_lbl_y - dp_px(22), 0, ly.h - 1);
  const int hit_h = std::max(1, ly.h - hit_y0);
  const int max_span = std::max(ly.min_hit_px(), ly.safe_span(back_lbl_y, 8));
  const int hit_w = std::min(max_span, std::max(dp_px(220), ly.min_hit_px() * 2));
  const int hit_pad = std::max(0, (ly.min_hit_px() - hit_h + 1) / 2);
  frame_hits().add_rect(cx - hit_w / 2, hit_y0, hit_w, hit_h,
                        WidgetId::BACK_BUTTON, kHitTap | kHitFocusSlot, 0, hit_pad);
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
  const int y0 = (SCREEN_MIN_R - chord_half_span_int(SCREEN_MIN_R, pill_w / 2 + margin)) + margin;
  const bool round_top = true;
#else
  const int y0 = 0;
  const bool round_top = false;
#endif
  const float rad = 0.5f * static_cast<float>(pill_h);
  const int sy0 = round_top ? y0 : (y0 - r);
  const int sh = round_top ? pill_h : (pill_h + r);
  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_styled_glass(x0, sy0, pill_w, sh, kChromeGlassTint, kChromeCardBorder,
                       GlassVariant::Clear, text_c, -1, false,
                       bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);
  fill_round_rect_glass(it, x0 - 1, sy0 - 1, pill_w + 2, sh + 2, rad + 1.0f, rim_col, rim_top, rim_bot, 1);
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
UiColor chrome_key_bg() { return kChromeKeyBg; }
UiColor chrome_key_brd() { return kChromeKeyBrd; }
UiColor chrome_wheel_inner_ring() { return kChromeWheelInnerRing; }
UiColor chrome_wheel_outer_ring() { return kChromeWheelOuterRing; }

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

float color_wheel_scale(const ColorWheelGeom &geom) noexcept {
  const auto ly = layout();
  const int top_lim = ly.sub_hdr_y + dp_px(14);
  const int bot_lim = ly.back_y - dp_px(26);
  const int max_h = std::max(44, std::min(geom.cy - top_lim, bot_lim - geom.cy) - dp_px(6));
  const int max_w = std::max(44, ly.safe_span(geom.cy, dp_px(14)) / 2 - dp_px(6));
  const int max_r = std::min(max_h, max_w);
  const float fit_s = static_cast<float>(max_r) / std::max(1.0f, static_cast<float>(geom.outer_r_466));
  return std::max(0.45f, std::min(dp_f(1.0f), fit_s));
}

}  // namespace

// ==============================================================================
// 1. Functional Action Pills, Toggles, Sliders, Lists, Grids & Keypads (D-070)
// ==============================================================================
void toggle_switch(Rgb565Surface &it, int cx_sw, int cy_sw, bool is_on, UiColor active_col) {
  const int sw_h = std::max(18, dp_px(22));
  const int sw_w = (sw_h * 40 + 11) / 22;
  const int x0 = cx_sw - sw_w / 2;
  const int y0 = cy_sw - sw_h / 2;
  const float rad = 0.5f * static_cast<float>(sw_h);

  GlassBackdrop bd{};
  UiColor eff_tint{}, rim_col{};
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_styled_glass(x0, y0, sw_w, sw_h,
                       is_on ? active_col : kChromeGlassTint,
                       is_on ? active_col : kChromeTrackBorder,
                       GlassVariant::Clear, kChromeGlassSheen, -1, false,
                       bd, eff_tint, rim_col, a_top, a_bot, rim_top, rim_bot);

  fill_round_rect_glass_rimmed(it, x0, y0, sw_w, sw_h, rad,
                               rim_col, is_on ? std::min(256, rim_top + 24) : rim_top, rim_bot,
                               eff_tint, a_top, a_bot, std::max(0.5f, rad - 1.0f));
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
  const int cx = x0 + eff_w / 2;

  if ((y0 + eff_h) > clip_y0() && y0 <= clip_y1()) {
    render_card_box(it, x0, y0, eff_w, eff_h, bg_c, border_c, r, GlassVariant::Clear, text_c);

    const bool has_lbl = (label != nullptr && label[0] != '\0' && font != nullptr);
    const bool has_mdi = (leading_mdi != nullptr && leading_mdi[0] != '\0');
    if (has_mdi && has_lbl) {
      const int icon_px = std::clamp(eff_h - dp_px(14), 14, dp_px(20));
      const int icon_gap = dp_px(6);
      const int max_txt_w = std::max(24, eff_w - icon_px - icon_gap - dp_px(18));
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      int tw = 0, x_off = 0, base = 0, th = 0;
      font->measure(fit.c_str(), &tw, &x_off, &base, &th);
      const int total_w = icon_px + icon_gap + tw;
      const int start_x = cx - total_w / 2;
      render_mdi_icon(it, leading_mdi, start_x + icon_px / 2, y_mid, icon_px, text_c, true);
      shadow_print(it, start_x + icon_px + icon_gap, y_mid, font, text_c, UiTextAlign::CENTER_LEFT, fit.c_str());
    } else if (has_mdi) {
      const int icon_px = std::clamp(eff_h - dp_px(12), 14, dp_px(22));
      render_mdi_icon(it, leading_mdi, cx, y_mid, icon_px, text_c, true);
    } else if (has_lbl) {
      const int max_txt_w = std::max(24, eff_w - dp_px(14));
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      shadow_print(it, cx, y_mid, font, text_c, UiTextAlign::CENTER, fit.c_str());
    }
  }

  if (widget_id != WidgetId::NONE) {
    const int min_h = (hit_h_min >= 0) ? hit_h_min : layout().min_hit_px();
    const int hit_h = std::max(eff_h, min_h);
    const int hit_w = std::max(eff_w, std::min(min_h, eff_w + dp_px(12)));
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
      LATTER_PANEL_PPI, geoms, inset, LATTER_CORNER_R, LATTER_VIEW_SCALE_PCT,
      LATTER_IS_STATIONARY != 0);
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
  const auto ly = layout();
  const int gap = std::max(4, dp_px(6));
  const int eff_total = std::min(ly.max_content_w(), total_w);
  const int seg_w = std::max(56, (eff_total - gap) / 2);
  const int hit_h = std::max(h, std::min(ly.min_hit_px(), std::max(ly.slot5_h, h + dp_px(14))));
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

  const int chv_inset = dp_px(16);
  const int max_txt_w = std::max(40, eff_w - (enabled ? (2 * chv_inset + dp_px(24)) : dp_px(20)));
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
    shadow_print(it, cx, cy, font, txt_c, UiTextAlign::CENTER, fit.c_str());
  }

  if (enabled) {
    const int chv_r = std::max(5, dp_px(7));
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
    const std::string fit = fit_text_to_width_font(std::string(label), std::max(40, eff_w - dp_px(20)), font);
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
      const std::string fit = fit_text_to_width_font(std::string(label), std::max(40, eff_w - dp_px(16)), font);
      shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
    }
    return;
  }

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, text_c);

  const bool has_mdi = (leading_mdi != nullptr && leading_mdi[0] != '\0');
  const bool has_sub = (sub_label != nullptr && sub_label[0] != '\0');
  const bool has_badge = (right_badge != nullptr && right_badge[0] != '\0');
  int pad_l = dp_px(14);
  int pad_r = dp_px(14);

  if (has_mdi) {
    const int icon_px = std::clamp(eff_h - dp_px(16), 16, dp_px(22));
    render_mdi_icon(it, leading_mdi, x0 + pad_l + icon_px / 2, cy, icon_px, text_c, true);
    pad_l += icon_px + dp_px(10);
  }
  if (has_badge && font != nullptr) {
    int bw = 0, bx_off = 0, bbase = 0, bh = 0;
    font->measure(right_badge, &bw, &bx_off, &bbase, &bh);
    shadow_print(it, x0 + eff_w - pad_r, cy, font, right_badge_c, UiTextAlign::CENTER_RIGHT, right_badge);
    pad_r += bw + dp_px(10);
  }

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const int max_txt_w = std::max(36, eff_w - pad_l - pad_r);
    if (has_sub) {
      int tw = 0, x_off = 0, base = 0, th = 0;
      font->measure("Ag", &tw, &x_off, &base, &th);
      const int half_step = std::min(eff_h / 4, std::max(dp_px(10), (th + 2) / 2));
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

  int txt_x0 = x0 + dp_px(4);
  if (draw_icon_fn != nullptr) {
    const int ir = std::min(card_icon_r(eff_h), dp_px(20));
    const int icon_cx = x0 + ir + dp_px(4);
    draw_icon_fn(it, icon_cx, cy, ir, icon_c, true, icon_ctx);
    txt_x0 = icon_cx + ir + std::max(10, dp_px(12));
  }

  const int gap = std::max(8, dp_px(12));
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
               uint16_t widget_id, int16_t param,
               bool inverted_badge, bool has_accent,
               uint8_t col_span, uint8_t row_span,
               int8_t entity_state, UiFontView hero_font) {
  const int eff_w = std::max(48, w);
  const int eff_h = std::max(48, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const UiColor bg_c = active ? chrome_card_bg_on() : chrome_card_bg_off();
  const UiColor brd_c = disabled
                            ? chrome_card_brd_off()
                            : ((active || has_accent) ? accent_c : chrome_card_brd_on());

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, text_c);

  const bool is_hero = (col_span >= 2U && row_span >= 2U);
  const bool is_wide = !is_hero && ((col_span >= 2U && row_span == 1U) || (eff_w * 10 >= eff_h * 16));
  const bool is_tall = !is_hero && !is_wide && ((col_span == 1U && row_span >= 2U) || (eff_h * 10 >= eff_w * 14));
  const char *state_caption = (entity_state == 1)
                                  ? "On"
                                  : ((entity_state == 0) ? "Off" : ((entity_state == 2) ? "Unavailable" : nullptr));
  const UiColor state_caption_c = active ? accent_c : kChromeBackLabel;
  const bool has_label = (label != nullptr && label[0] != '\0' && font != nullptr);

  auto draw_glyph_badge = [&](int ix, int iy, int icon_r, int max_disc_r) {
    if (inverted_badge) {
      const int clamped_max = std::max(icon_r, max_disc_r);
      const int disc_r = std::clamp(icon_r + dp_px(4), icon_r, clamped_max);
      const UiColor disc_c = disabled ? text_c : (has_accent ? accent_c : text_c);
      fast_filled_circle_aa(it, ix, iy, disc_r, disc_c);
      if (draw_icon_fn != nullptr) {
        const int inset_r = std::max(7, std::max(14, (icon_r * 16) / 10) / 2);
        draw_icon_fn(it, ix, iy, inset_r, kChromeShadow, false, user_ctx);
      }
    } else if (draw_icon_fn != nullptr) {
      const UiColor icon_c = disabled ? text_c : ((active || has_accent) ? accent_c : text_c);
      draw_icon_fn(it, ix, iy, icon_r, icon_c, true, user_ctx);
    }
  };

  auto measure_line_h = [](UiFontView f) -> int {
    if (f == nullptr) return 14;
    int tw = 0, x_off = 0, base = 0, th = 0;
    f->measure("Ag", &tw, &x_off, &base, &th);
    return std::max(12, th);
  };

  if (is_hero) {
    // Hero (2x2+): oversized icon + medium/bold title (falling back to font_small) + centered state subtitle
    const int icon_r = std::clamp(std::min(eff_w, eff_h) / 5, std::max(16, dp_px(20)), dp_px(42));
    const int icon_cy = y0 + (eff_h * 34) / 100;
    const int max_disc_r = std::min((icon_cy - y0) - std::max(4, dp_px(6)), eff_w / 2 - dp_px(10));
    draw_glyph_badge(cx, icon_cy, icon_r, max_disc_r);

    const int lbl_max_w = std::max(40, eff_w - dp_px(24));
    const int lbl_cy = (state_caption != nullptr) ? (y0 + (eff_h * 66) / 100) : (y0 + (eff_h * 72) / 100);
    if (has_label) {
      bool use_hero_font = false;
      if (hero_font != nullptr) {
        int tw = 0, x_off = 0, base = 0, th = 0;
        const std::string clean = sanitize_utf8_for_font(std::string(label), hero_font);
        hero_font->measure(clean.c_str(), &tw, &x_off, &base, &th);
        use_hero_font = (tw <= lbl_max_w);
      }
      if (use_hero_font && hero_font != nullptr) {
        const std::string fit = fit_text_to_width_font(std::string(label), lbl_max_w, hero_font);
        shadow_print(it, cx, lbl_cy, hero_font, text_c, UiTextAlign::CENTER, fit.c_str());
      } else {
        std::string lines[2];
        const int n = wrap_text_lines(std::string(label), lbl_max_w, font, lines, 2);
        if (n <= 1) {
          const std::string fit = fit_text_to_width_font(std::string(label), lbl_max_w, font);
          shadow_print(it, cx, lbl_cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
        } else {
          const int half_step = (measure_line_h(font) + 4) / 2;
          shadow_print(it, cx, lbl_cy - half_step, font, text_c, UiTextAlign::CENTER, lines[0].c_str());
          shadow_print(it, cx, lbl_cy + half_step, font, text_c, UiTextAlign::CENTER, lines[1].c_str());
        }
      }
    }
    if (state_caption != nullptr && font != nullptr) {
      const int sub_y = y0 + (eff_h * 84) / 100;
      const std::string fit_sub = fit_text_to_width_font(std::string(state_caption), lbl_max_w, font);
      shadow_print(it, cx, sub_y, font, state_caption_c, UiTextAlign::CENTER, fit_sub.c_str());
    }
  } else if (is_wide) {
    // Wide (2x1+): left-anchored icon + left-aligned 1-2 line label + right-edge state dot when stateful
    const int pad_x = std::max(12, dp_px(14));
    const int icon_r = std::clamp(std::min(card_icon_r(eff_h), (eff_h * 26) / 100), 10, dp_px(26));
    const int disc_extra = inverted_badge ? dp_px(4) : 0;
    const int icon_cx = x0 + pad_x + icon_r + disc_extra;
    const int max_disc_r = std::min((eff_h / 2) - std::max(4, dp_px(5)), icon_r + disc_extra);
    draw_glyph_badge(icon_cx, cy, icon_r, max_disc_r);

    int right_limit = x0 + eff_w - pad_x;
    if (entity_state == 0 || entity_state == 1) {
      const int dot_r = std::max(3, dp_px(4));
      const int dot_cx = right_limit - dot_r;
      const UiColor dot_c = active ? accent_c : kChromePipInactive;
      fast_filled_circle_aa(it, dot_cx + 1, cy + 1, dot_r, kChromeShadow);
      fast_filled_circle_aa(it, dot_cx, cy, dot_r, dot_c);
      right_limit = dot_cx - dot_r - std::max(8, dp_px(10));
    }

    if (has_label) {
      const int txt_x0 = icon_cx + icon_r + disc_extra + std::max(10, dp_px(12));
      const int max_txt_w = std::max(36, right_limit - txt_x0);
      const int line_h = measure_line_h(font);
      std::string lines[2];
      const int n = (eff_h >= 2 * line_h + 22)
                        ? wrap_text_lines(std::string(label), max_txt_w, font, lines, 2)
                        : 1;
      if (n <= 1) {
        const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
        shadow_print(it, txt_x0, cy, font, text_c, UiTextAlign::CENTER_LEFT, fit.c_str());
      } else {
        const int half_step = (line_h + 4) / 2;
        shadow_print(it, txt_x0, cy - half_step, font, text_c, UiTextAlign::CENTER_LEFT, lines[0].c_str());
        shadow_print(it, txt_x0, cy + half_step, font, text_c, UiTextAlign::CENTER_LEFT, lines[1].c_str());
      }
    }
  } else if (is_tall) {
    // Tall (1x2+): larger upper icon + wrapped 2-line label + optional On/Off/Unavailable caption
    const int icon_r = std::clamp(std::min(eff_w / 4, eff_h / 6), 14, dp_px(34));
    const int icon_cy = y0 + (eff_h * 30) / 100;
    const int max_disc_r = std::min((icon_cy - y0) - std::max(4, dp_px(5)), eff_w / 2 - dp_px(8));
    draw_glyph_badge(cx, icon_cy, icon_r, max_disc_r);

    const int lbl_max_w = std::max(36, eff_w - dp_px(16));
    const int lbl_cy = (state_caption != nullptr) ? (y0 + (eff_h * 64) / 100) : (y0 + (eff_h * 70) / 100);
    if (has_label) {
      std::string lines[2];
      const int n = wrap_text_lines(std::string(label), lbl_max_w, font, lines, 2);
      if (n <= 1) {
        const std::string fit = fit_text_to_width_font(std::string(label), lbl_max_w, font);
        shadow_print(it, cx, lbl_cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
      } else {
        const int half_step = (measure_line_h(font) + 4) / 2;
        shadow_print(it, cx, lbl_cy - half_step, font, text_c, UiTextAlign::CENTER, lines[0].c_str());
        shadow_print(it, cx, lbl_cy + half_step, font, text_c, UiTextAlign::CENTER, lines[1].c_str());
      }
    }
    if (state_caption != nullptr && font != nullptr) {
      const int sub_y = y0 + (eff_h * 84) / 100;
      const std::string fit_sub = fit_text_to_width_font(std::string(state_caption), lbl_max_w, font);
      shadow_print(it, cx, sub_y, font, state_caption_c, UiTextAlign::CENTER, fit_sub.c_str());
    }
  } else {
    // 1x1 standard tile: centered icon above centered 1-2 line font_small label
    const int icon_r = std::min(card_icon_r(eff_h), std::max(10, eff_h / 4));
    const int line_h = measure_line_h(font);
    const int lbl_max_w = std::max(32, eff_w - dp_px(14));
    std::string lines[2];
    int line_cnt = 0;
    if (has_label) {
      const bool allow_two_lines = (eff_h >= 2 * line_h + 2 * icon_r + 24);
      line_cnt = allow_two_lines ? wrap_text_lines(std::string(label), lbl_max_w, font, lines, 2) : 1;
      if (line_cnt <= 1) {
        lines[0] = fit_text_to_width_font(std::string(label), lbl_max_w, font);
        line_cnt = 1;
      }
    }
    const int icon_cy = (line_cnt >= 2) ? (y0 + (eff_h * 33) / 100) : (cy - eff_h / 6);
    const int lbl_y = (line_cnt >= 2) ? (y0 + (eff_h * 74) / 100) : (cy + (eff_h * 27) / 100);
    const int max_top = (icon_cy - y0) - std::max(3, dp_px(4));
    const int max_bot = (lbl_y - icon_cy) - std::max(11, eff_h / 7);
    const int max_disc_r = std::min({max_top, max_bot, eff_w / 2 - dp_px(6)});
    draw_glyph_badge(cx, icon_cy, icon_r, max_disc_r);

    if (line_cnt == 1) {
      shadow_print(it, cx, lbl_y, font, text_c, UiTextAlign::CENTER, lines[0].c_str());
    } else if (line_cnt >= 2) {
      const int half_step = (line_h + 3) / 2;
      shadow_print(it, cx, lbl_y - half_step, font, text_c, UiTextAlign::CENTER, lines[0].c_str());
      shadow_print(it, cx, lbl_y + half_step, font, text_c, UiTextAlign::CENTER, lines[1].c_str());
    }
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
    const int pad = (hit_pad >= 0) ? std::max(hit_pad, req_pad) : std::max(dp_px(6), req_pad);
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

  const int text_x = cx - eff_w / 2 + dp_px(14);
  const int sw_h = std::max(18, dp_px(22));
  const int sw_w = (sw_h * 40 + 11) / 22;
  const int sw_cx = cx + eff_w / 2 - (sw_w / 2 + dp_px(10));
  const int max_lbl_px = std::max(60, (sw_cx - sw_w / 2 - dp_px(8)) - text_x);
  const std::string fit_lbl = fit_text_to_width_font(
      label != nullptr ? std::string(label) : std::string(), max_lbl_px, font);
  shadow_print(it, text_x, y_mid, font, fg, UiTextAlign::CENTER_LEFT, fit_lbl.c_str());
  toggle_switch(it, sw_cx, y_mid, is_on, active_col);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx, y_mid, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

void stepper_row(Rgb565Surface &it, int cx, int y_top, int h,
                 const char *const labels[4], UiFontView font,
                 UiColor col_text, uint16_t base_widget_id) {
  const auto ly = layout();
  const int eff_h = std::max(28, h);
  const int y_mid = y_top + eff_h / 2;
  const int avail_w = std::min(ly.max_content_w(340), ly.band_safe_span(y_mid, eff_h, dp_px(14)));
  const int gap = std::max(8, dp_px(12));
  const int btn_w = std::clamp((avail_w - 3 * gap) / 4, 46, dp_px(76));
  const int step = btn_w + gap;
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

bool hsv_wheel_pick(int tx, int ty, const ColorWheelGeom &geom,
                    float &out_hue_deg, float &out_sat) {
  const float ws = color_wheel_scale(geom);
  const float dx = static_cast<float>(tx - geom.cx);
  const float dy = static_cast<float>(ty - geom.cy);
  const float dist_466 = std::sqrt(dx * dx + dy * dy) / ws;
  const float min_r = static_cast<float>(geom.inner_r_466) - 12.0f;
  const float max_r = static_cast<float>(geom.outer_r_466) + 12.0f;
  if (dist_466 < min_r || dist_466 > max_r) return false;

  float hue = std::atan2(dy, dx) * (180.0f / PI_F);
  if (hue < 0.0f) hue += 360.0f;
  const float span = std::max(1.0f, static_cast<float>(geom.outer_r_466 - geom.inner_r_466));
  const float norm = (dist_466 - static_cast<float>(geom.inner_r_466)) / span;
  out_hue_deg = hue;
  out_sat = std::max(0.35f, std::min(1.0f, 0.35f + norm * 0.65f));
  return true;
}

namespace {
struct ColorWheelCache {
  uint16_t *rgb{nullptr};
  uint8_t *alpha{nullptr};
  int r_in{-1};
  int r_out{-1};
  int endian{-1};
  int side{0};
};

ColorWheelCache &color_wheel_cache() {
  static ColorWheelCache s_cache{};
  return s_cache;
}

void color_wheel_cache_drop() noexcept {
  auto &c = color_wheel_cache();
  if (c.rgb != nullptr) {
    ui_psram_free(c.rgb);
    c.rgb = nullptr;
  }
  if (c.alpha != nullptr) {
    ui_psram_free(c.alpha);
    c.alpha = nullptr;
  }
  c.r_in = -1;
  c.r_out = -1;
  c.endian = -1;
  c.side = 0;
}

bool color_wheel_cache_ensure(int r_in, int r_out) noexcept {
  static const bool s_wheel_evictor_reg = register_psram_cache_evictor(&color_wheel_cache_drop);
  (void)s_wheel_evictor_reg;
  auto &c = color_wheel_cache();
  const int endian = active_frame_endian_mode();
  const int half = r_out + 1;
  const int side = 2 * half + 1;
  if (side <= 2 || r_out <= r_in) return false;
  if (c.rgb != nullptr && c.alpha != nullptr &&
      c.r_in == r_in && c.r_out == r_out && c.endian == endian && c.side == side) {
    return true;
  }
  color_wheel_cache_drop();
  const size_t px_count = static_cast<size_t>(side) * static_cast<size_t>(side);
  c.rgb = static_cast<uint16_t *>(ui_psram_malloc(px_count * sizeof(uint16_t)));
  c.alpha = static_cast<uint8_t *>(ui_psram_malloc(px_count));
  if (c.rgb == nullptr || c.alpha == nullptr) {
    color_wheel_cache_drop();
    return false;
  }
  std::memset(c.rgb, 0, px_count * sizeof(uint16_t));
  std::memset(c.alpha, 0, px_count);

  const float f_in = static_cast<float>(r_in);
  const float f_out = static_cast<float>(r_out);
  const float inv_span = 1.0f / static_cast<float>(std::max(1, r_out - r_in));
  constexpr float kInvTwoPi = 1.0f / (2.0f * PI_F);
  const int r_in_lo = std::max(0, r_in - 1);
  const int r_out_hi = std::min(half, r_out + 1);
  const int r_in_lo2 = r_in_lo * r_in_lo;
  const int r_out_hi2 = r_out_hi * r_out_hi;

  auto fast_atan2_q1_unit = [&](int dx_i, int dy_i) noexcept -> float {
    if (dy_i == 0) return 0.0f;
    if (dx_i == 0) return 0.25f;
    const int mn = (dx_i < dy_i) ? dx_i : dy_i;
    const int mx = (dx_i < dy_i) ? dy_i : dx_i;
    const float t = static_cast<float>(mn) / static_cast<float>(mx);
    const float t2 = t * t;
    float a_rad = t * (0.9998660f +
                       t2 * (-0.3302995f +
                             t2 * (0.1801410f + t2 * (-0.0851330f + t2 * 0.0208351f))));
    if (dy_i > dx_i) a_rad = 0.5f * PI_F - a_rad;
    return a_rad * kInvTwoPi;
  };

  for (int dy_i = 0; dy_i <= r_out_hi; ++dy_i) {
    const int dy2 = dy_i * dy_i;
    const size_t off_pos = static_cast<size_t>(half + dy_i) * static_cast<size_t>(side);
    const size_t off_neg = static_cast<size_t>(half - dy_i) * static_cast<size_t>(side);
    for (int dx_i = 0; dx_i <= r_out_hi; ++dx_i) {
      const int d2 = dx_i * dx_i + dy2;
      if (d2 == 0 || d2 < r_in_lo2 || d2 > r_out_hi2) continue;
      const float d = std::sqrt(static_cast<float>(d2));
      const float cov = std::min(std::clamp(d - (f_in - 0.5f), 0.0f, 1.0f),
                                 std::clamp((f_out + 0.5f) - d, 0.0f, 1.0f));
      if (cov <= 0.004f) continue;
      const uint8_t a_u8 = static_cast<uint8_t>(std::lround(cov * 255.0f));
      const float sat = std::clamp(0.35f + 0.65f * ((d - f_in) * inv_span), 0.35f, 1.0f);
      const float h_q1 = fast_atan2_q1_unit(dx_i, dy_i);

      c.rgb[off_pos + static_cast<size_t>(half + dx_i)] = encode_hw_rgb565(hsv_to_rgb(h_q1, sat, 1.0f));
      c.alpha[off_pos + static_cast<size_t>(half + dx_i)] = a_u8;
      if (dx_i > 0) {
        c.rgb[off_pos + static_cast<size_t>(half - dx_i)] = encode_hw_rgb565(hsv_to_rgb(0.5f - h_q1, sat, 1.0f));
        c.alpha[off_pos + static_cast<size_t>(half - dx_i)] = a_u8;
      }
      if (dy_i > 0) {
        c.rgb[off_neg + static_cast<size_t>(half + dx_i)] = encode_hw_rgb565(hsv_to_rgb(1.0f - h_q1, sat, 1.0f));
        c.alpha[off_neg + static_cast<size_t>(half + dx_i)] = a_u8;
        if (dx_i > 0) {
          c.rgb[off_neg + static_cast<size_t>(half - dx_i)] = encode_hw_rgb565(hsv_to_rgb(0.5f + h_q1, sat, 1.0f));
          c.alpha[off_neg + static_cast<size_t>(half - dx_i)] = a_u8;
        }
      }
    }
  }
  c.r_in = r_in;
  c.r_out = r_out;
  c.endian = endian;
  c.side = side;
  return true;
}
}  // namespace

void hsv_color_wheel(Rgb565Surface &it, const ColorWheelGeom &geom,
                     float cur_hue_deg, float cur_sat,
                     UiColor col_bg, UiColor col_white, uint16_t widget_id) {
  const int cx = geom.cx;
  const int cy = geom.cy;
  const float ws = color_wheel_scale(geom);
  const int r_in = static_cast<int>(static_cast<float>(geom.inner_r_466) * ws);
  const int r_out = static_cast<int>(static_cast<float>(geom.outer_r_466) * ws);
  const int r_span = std::max(1, r_out - r_in);
  const int half = r_out + 1;
  const int x0_box = cx - half;
  const int y0_box = cy - half;
  const int cy0 = std::max(0, clip_y0());
  const int cy1 = std::min(SCREEN_H - 1, clip_y1());

  if (color_wheel_cache_ensure(r_in, r_out)) {
    const auto &c = color_wheel_cache();
    uint16_t *const fb = active_frame_buf16();
    const int side = c.side;
    const int y_lo = std::max(0, cy0 - y0_box);
    const int y_hi = std::min(side - 1, cy1 - y0_box);
    const int x_lo = std::max(0, -x0_box);
    const int x_hi = std::min(side - 1, (SCREEN_W - 1) - x0_box);
    for (int sy = y_lo; sy <= y_hi; ++sy) {
      const int dy_scr = y0_box + sy;
      const uint16_t *const rgb_row = c.rgb + static_cast<size_t>(sy) * static_cast<size_t>(side);
      const uint8_t *const alpha_row = c.alpha + static_cast<size_t>(sy) * static_cast<size_t>(side);
      uint16_t *const dst_row =
          (fb != nullptr) ? (fb + static_cast<size_t>(dy_scr) * static_cast<size_t>(SCREEN_W)) : nullptr;
      int sx = x_lo;
      while (sx <= x_hi) {
        const uint8_t a = alpha_row[sx];
        if (a == 0U) {
          ++sx;
          continue;
        }
        if (a == 255U) {
          int run = sx + 1;
          while (run <= x_hi && alpha_row[run] == 255U) ++run;
          const int len = run - sx;
          if (dst_row != nullptr) {
            std::memcpy(dst_row + x0_box + sx, rgb_row + sx, static_cast<size_t>(len) * sizeof(uint16_t));
          } else {
            blit_rgb565_scanline(it, x0_box + sx, dy_scr, len, rgb_row + sx);
          }
          sx = run;
        } else {
          const uint16_t raw = decode_hw_rgb565_raw(rgb_row[sx]);
          const uint8_t r5 = static_cast<uint8_t>((raw >> 11) & 0x1FU);
          const uint8_t g6 = static_cast<uint8_t>((raw >> 5) & 0x3FU);
          const uint8_t b5 = static_cast<uint8_t>(raw & 0x1FU);
          const UiColor col(static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                            static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                            static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
          fast_pixel_blend(it, x0_box + sx, dy_scr, col, a);
          ++sx;
        }
      }
    }
  } else {
    // Transient PSRAM OOM fallback: direct scanline AA annulus without spoke gaps.
    const float f_in = static_cast<float>(r_in);
    const float f_out = static_cast<float>(r_out);
    const float inv_span = 1.0f / static_cast<float>(r_span);
    constexpr float kInvTwoPi = 1.0f / (2.0f * PI_F);
    for (int dy = -half; dy <= half; ++dy) {
      const int py = cy + dy;
      if (py < cy0 || py > cy1) continue;
      const float fdy = static_cast<float>(dy);
      for (int dx = -half; dx <= half; ++dx) {
        const int px = cx + dx;
        if (px < 0 || px >= SCREEN_W) continue;
        const float fdx = static_cast<float>(dx);
        const float d = std::sqrt(fdx * fdx + fdy * fdy);
        const float cov = std::min(std::clamp(d - (f_in - 0.5f), 0.0f, 1.0f),
                                   std::clamp((f_out + 0.5f) - d, 0.0f, 1.0f));
        if (cov <= 0.004f) continue;
        float ang = std::atan2(fdy, fdx);
        if (ang < 0.0f) ang += 2.0f * PI_F;
        const float sat = std::clamp(0.35f + 0.65f * ((d - f_in) * inv_span), 0.35f, 1.0f);
        fast_pixel_blend(it, px, py, hsv_to_rgb(ang * kInvTwoPi, sat, 1.0f),
                         static_cast<uint8_t>(std::lround(cov * 255.0f)));
      }
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
    const int hit_r = r_out + std::max(8, static_cast<int>(12.0f * ws));
    frame_hits().add_circle(cx, cy, hit_r, widget_id, kHitTap);
  }
}

void confirm_modal(Rgb565Surface &it, int cx, const char *title,
                   const char *line1, const char *line2,
                   const char *cancel_lbl, const char *confirm_lbl,
                   UiColor confirm_border, UiColor confirm_bg,
                   UiFontView font_hdr, UiFontView font_body,
                   UiColor col_white, UiColor col_slate,
                   uint16_t cancel_wid, uint16_t confirm_wid) {
  const auto ly = layout();
  render_page_header(it, cx, title, font_hdr);
  const int y1 = ly.hdr_y + dp_px(56);
  const int y2 = y1 + dp_px(34);
  if (line1 != nullptr && line1[0] != '\0') {
    const int w1 = std::min(ly.max_content_w(), ly.safe_span(y1, dp_px(18)));
    const std::string f1 = fit_text_to_width_font(std::string(line1), w1, font_body);
    shadow_print(it, cx, y1, font_body, col_white, UiTextAlign::CENTER, f1.c_str());
  }
  if (line2 != nullptr && line2[0] != '\0') {
    const int w2 = std::min(ly.max_content_w(), ly.safe_span(y2, dp_px(18)));
    const std::string f2 = fit_text_to_width_font(std::string(line2), w2, font_body);
    shadow_print(it, cx, y2, font_body, col_slate, UiTextAlign::CENTER, f2.c_str());
  }
  const int btn_y = std::min(ly.back_y - dp_px(52), y2 + dp_px(76));
  const int btn_h = std::max(44, dp_px(50));
  const ActionPillSpec specs[2] = {
      {cancel_lbl, cancel_wid, chrome_card_brd_on(), chrome_card_bg_off(), col_white, 0, nullptr},
      {confirm_lbl, confirm_wid, confirm_border, confirm_bg, col_white, 1, nullptr},
  };
  action_pill_row(it, btn_y, btn_h, std::min(ly.max_content_w(320), dp_px(300)),
                  dp_px(138), dp_px(14), specs, 2, font_body, dp_px(16));
}

void game_over_sheet(Rgb565Surface &it, int cx, int cy,
                     const char *title, UiColor title_col,
                     const char *score_line, const char *best_line, bool is_new_best,
                     const char *cta_lbl,
                     UiFontView font_hdr, UiFontView font_body,
                     UiColor theme_primary, UiColor col_white,
                     UiColor col_gold, UiColor col_slate, uint16_t cta_wid) {
  const auto ly = layout();
  const int scrim_h = std::min(SCREEN_H - dp_px(64), dp_px(206));
  const int scrim_w = std::min(ly.max_content_w(340), ly.band_safe_span(cy, scrim_h, dp_px(12)));
  fill_round_rect_glass(it, cx - scrim_w / 2, cy - scrim_h / 2, scrim_w, scrim_h,
                        static_cast<float>(kRadiusCardPx()), kChromeShadow, 176, 176);
  shadow_print(it, cx, cy - (scrim_h * 29) / 100, font_hdr, title_col, UiTextAlign::CENTER, title, true);
  if (score_line != nullptr && score_line[0] != '\0') {
    shadow_print(it, cx, cy - (scrim_h * 9) / 100, font_body, col_white, UiTextAlign::CENTER, score_line);
  }
  if (best_line != nullptr && best_line[0] != '\0') {
    shadow_print(it, cx, cy + (scrim_h * 7) / 100, font_body, is_new_best ? col_gold : col_slate,
                 UiTextAlign::CENTER, best_line);
  }
  const int pill_h = std::max(40, dp_px(44));
  const int pill_w = std::min(scrim_w - dp_px(32), dp_px(204));
  action_pill_centered(it, cx, pill_w, cy + (scrim_h * 30) / 100, pill_h,
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

void pin_pad_modal(Rgb565Surface &it, int cx, uint32_t now_ms,
                   UiFontView font_key, UiFontView font_small,
                   UiColor theme_primary, UiColor col_white, UiColor col_slate, UiColor col_red) {
  const auto ly = layout();
  auto &st = pin_pad_state();
  const char *hdr = (st.prompt[0] != '\0') ? st.prompt : "ENTER SECURITY PIN";
  render_page_header(it, cx, hdr, font_small);

  const int disp_y = ly.hdr_y + dp_px(32);
  const int disp_w = std::min(ly.max_content_w(260), ly.safe_span(disp_y, dp_px(22)));
  const bool show_err = (st.error_msg[0] != '\0' && (now_ms - st.error_ms) < 2200U);
  if (show_err) {
    shadow_print(it, cx, disp_y, font_small, col_white, UiTextAlign::CENTER, st.error_msg);
  } else if (st.len == 0) {
    shadow_print(it, cx, disp_y, font_small, col_slate, UiTextAlign::CENTER, "4-12 Digits");
  } else {
    const int max_step = dp_px(18);
    const int dot_step = (st.len > 1)
                             ? std::min(max_step, std::max(dp_px(12), (disp_w - dp_px(20)) / (static_cast<int>(st.len) - 1)))
                             : max_step;
    const int dot_r = (st.len > 9) ? std::max(3, dp_px(4)) : std::max(4, dp_px(5));
    const int start_x = cx - ((static_cast<int>(st.len) - 1) * dot_step) / 2;
    for (uint8_t i = 0; i < st.len; i++) {
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step + 1, disp_y + 1, dot_r, kChromeShadow);
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step, disp_y, dot_r, col_white);
    }
  }
  fast_hline(it, cx - disp_w / 2, disp_y + dp_px(15), disp_w, show_err ? col_red : chrome_card_brd_on());

  const int pad_top = disp_y + dp_px(24);
  const int pad_bot = ly.back_y - dp_px(24);
  const int avail_h = std::max(140, pad_bot - pad_top);
  const int row_gap = std::max(5, dp_px(8));
  const int key_h = std::clamp((avail_h - 3 * row_gap) / 4, 30, dp_px(48));
  const int row_step = key_h + row_gap;
  const int grid_h = 3 * row_step + key_h;
  const int row_y0 = pad_top + (avail_h - grid_h) / 2 + key_h / 2;
  const int bot_row_y = row_y0 + 3 * row_step;
  const int safe_grid_w = std::min(
      ly.max_content_w(300),
      std::min(ly.band_safe_span(row_y0, key_h, dp_px(12)),
               ly.band_safe_span(bot_row_y, key_h, dp_px(12))));
  const int col_gap = std::max(6, dp_px(10));
  const int key_w = std::clamp((safe_grid_w - 2 * col_gap) / 3, 54, dp_px(82));
  const int col_step = key_w + col_gap;

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

  const int cancel_y = ly.back_y;
  fast_print(it, cx + 1, cancel_y + 1, font_small, kChromeShadow, UiTextAlign::CENTER, "< Cancel");
  fast_print(it, cx, cancel_y, font_small, kChromeBackLabel, UiTextAlign::CENTER, "< Cancel");
  const int cancel_y0 = std::clamp(cancel_y - dp_px(22), 0, ly.h - 1);
  const int cancel_h = std::max(1, ly.h - cancel_y0);
  const int cancel_max_w = std::max(ly.min_hit_px(), ly.safe_span(cancel_y, 8));
  const int cancel_w = std::min(cancel_max_w, std::max(dp_px(200), ly.min_hit_px() * 2));
  const int cancel_pad = std::max(0, (ly.min_hit_px() - cancel_h + 1) / 2);
  frame_hits().add_rect(cx - cancel_w / 2, cancel_y0, cancel_w, cancel_h,
                        WidgetId::PIN_PAD_CANCEL, kHitTap | kHitFocusSlot, 0, cancel_pad);
}

}  // namespace ui


// ==============================================================================
// 1. Page Headers, Sub-Headers, Empty States & Time Readout (D-060 / D-070)
// ==============================================================================
void render_page_header(Rgb565Surface &it, int cx, const char *title,
                        UiFontView font) {
  if (title == nullptr || title[0] == '\0' || font == nullptr) return;
  const int y = layout().hdr_y;
  fast_print(it, cx + 1, y + 1, font, kChromeShadow, UiTextAlign::CENTER, title);
  fast_print(it, cx, y, font, UiColor::WHITE, UiTextAlign::CENTER, title);
}

void render_page_header(Rgb565Surface &it, int cx, const std::string &title,
                        UiFontView font) {
  if (title.empty() || font == nullptr) return;
  const auto ly = layout();
  const int hdr_w = std::max(80, std::min(ly.max_content_w(300), ly.safe_span(ly.hdr_y, dp_px(18))));
  const std::string fitted = fit_text_to_width_font(title, hdr_w, font);
  render_page_header(it, cx, fitted.c_str(), font);
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
  const auto ly = layout();
  const int max_w = std::min(ly.max_content_w(), std::max(80, ly.band_safe_span(cy, dp_px(48), dp_px(18))));
  std::string lines[2];
  const int n = ui::wrap_text_lines(std::string(text), max_w, font, lines, 2);
  if (n <= 1) {
    const std::string fit = fit_text_to_width_font(std::string(text), max_w, font);
    ui::shadow_print(it, cx, cy, font, text_c, UiTextAlign::CENTER, fit.c_str());
  } else {
    int tw = 0, x_off = 0, base = 0, th = 0;
    font->measure("Ag", &tw, &x_off, &base, &th);
    const int half_step = std::max(dp_px(13), (th + 6) / 2);
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
  shadow_print_impl(it, x, y, font, col, align, text, double_shadow);
}

int wrap_text_lines(const std::string &text, int max_w, UiFontView font,
                    std::string *out_lines, int max_lines) {
  if (text.empty() || font == nullptr || out_lines == nullptr || max_lines <= 0 || max_w <= 12) {
    return 0;
  }
  std::string clean;
  if (text.find('\n') == std::string::npos) {
    clean = sanitize_utf8_for_font(text, font);
  } else {
    clean.reserve(text.size());
    size_t seg = 0;
    while (seg <= text.size()) {
      const size_t nl = text.find('\n', seg);
      const size_t end = (nl == std::string::npos) ? text.size() : nl;
      if (end > seg) {
        clean.append(sanitize_utf8_for_font(text.substr(seg, end - seg), font));
      }
      if (nl == std::string::npos) break;
      clean.push_back('\n');
      seg = nl + 1;
    }
  }
  const size_t n = clean.size();
  if (clean.find('\n') == std::string::npos) {
    size_t s = 0;
    while (s < n && (clean[s] == ' ' || clean[s] == '\t' || clean[s] == '\r')) ++s;
    size_t e = n;
    while (e > s && (clean[e - 1] == ' ' || clean[e - 1] == '\t' || clean[e - 1] == '\r')) --e;
    if (s >= e) return 0;
    const std::string trimmed = (s == 0 && e == n) ? clean : clean.substr(s, e - s);
    int tw = 0, x_off = 0, base = 0, th = 0;
    font->measure(trimmed.c_str(), &tw, &x_off, &base, &th);
    if (tw <= max_w) {
      out_lines[0] = trimmed;
      return 1;
    }
    if (max_lines == 1) {
      out_lines[0] = fit_text_to_width_font(trimmed, max_w, font);
      return 1;
    }
  }

  int space_w = 0;
  {
    UiGlyphData sp{};
    if (font.find_glyph(32U, sp) && sp.advance > 0) {
      space_w = sp.advance;
    } else {
      int sp_w = 0, xo = 0, b = 0, h = 0;
      font->measure(" ", &sp_w, &xo, &b, &h);
      space_w = (sp_w > 0) ? sp_w : std::max(3, font.get_height() / 4);
    }
  }

  std::string word;
  std::string cand;
  word.reserve(48U);
  cand.reserve(96U);

  int count = 0;
  size_t pos = 0;
  while (pos < n && count < max_lines) {
    while (pos < n && (clean[pos] == ' ' || clean[pos] == '\t' || clean[pos] == '\r')) ++pos;
    if (pos >= n) break;
    if (clean[pos] == '\n') {
      ++pos;
      continue;
    }
    std::string line;
    int cur_w = 0;
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
      word.assign(clean, ws_end, word_end - ws_end);
      int ww = 0, wxo = 0, wb = 0, wh = 0;
      font->measure(word.c_str(), &ww, &wxo, &wb, &wh);
      if (line.empty()) {
        if (ww <= max_w) {
          line = word;
          cur_w = ww;
          pos = word_end;
        } else {
          line = fit_text_to_width_font(word, max_w, font);
          pos = word_end;
          break;
        }
      } else if (cur_w + space_w + ww <= max_w - space_w) {
        line.push_back(' ');
        line.append(word);
        cur_w += space_w + ww;
        pos = word_end;
      } else {
        cand.assign(line);
        cand.push_back(' ');
        cand.append(word);
        int cw = 0, cxo = 0, cb = 0, ch = 0;
        font->measure(cand.c_str(), &cw, &cxo, &cb, &ch);
        if (cw <= max_w) {
          line.swap(cand);
          cur_w = cw;
          pos = word_end;
        } else {
          break;
        }
      }
    }
    if (count + 1 == max_lines && pos < n) {
      size_t rem = pos;
      while (rem < n && (clean[rem] == ' ' || clean[rem] == '\n' || clean[rem] == '\r')) ++rem;
      if (rem < n) {
        line = fit_text_to_width_font(line + "...", max_w, font);
      }
    }
    out_lines[count++] = std::move(line);
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

  const int text_x = x0 + dp_px(16);
  const int max_text_w = std::max(dp_px(40), split_w - dp_px(24));
  const char *safe_pri = (primary_txt != nullptr) ? primary_txt : "";
  const char *safe_sec = (secondary_txt != nullptr) ? secondary_txt : "";
  if (safe_sec[0] != '\0') {
    const int dy_pri = std::min(h / 4, dp_px(9));
    const int dy_sec = std::min(h / 4 + dp_px(2), dp_px(13));
    const std::string pri_clamped = fit_text_to_width_font(safe_pri, max_text_w, font_pri);
    const std::string sec_clamped = fit_text_to_width_font(safe_sec, max_text_w, font_sec);
    fast_print(it, text_x + 1, cy - dy_pri + 1, font_pri, kChromeShadow, UiTextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x, cy - dy_pri, font_pri, pri_col, UiTextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x + 1, cy + dy_sec + 1, font_sec, kChromeShadow, UiTextAlign::CENTER_LEFT, sec_clamped.c_str());
    fast_print(it, text_x, cy + dy_sec, font_sec, sec_col, UiTextAlign::CENTER_LEFT, sec_clamped.c_str());
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
    const int btn_r = std::max(12, std::min(h / 2 - 6, dp_px(17)));
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
