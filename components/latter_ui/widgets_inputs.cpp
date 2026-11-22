#include "ui_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {
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
}  // namespace latter_ui
