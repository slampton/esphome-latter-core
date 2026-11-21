#if __has_include("ui_widgets.h")
#include "ui_widgets.h"
#else
#include "../../include/ui_widgets.h"
#endif

#if __has_include("hal_link_report.h")
#include "hal_link_report.h"
#else
#include "../../include/hal_link_report.h"
#endif

#ifdef USE_API
#if __has_include("esphome/components/api/api_server.h")
#include "esphome/components/api/api_server.h"
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace latter_ui {

// ==============================================================================
// 1. Shared Frosted-Glass Chrome, Headers, Mode Bars & Navigation Primitives
// ==============================================================================
void render_pill_button(esphome::display::Display &it, int x1, int x2, int y_mid, int r,
                        esphome::Color border_c, esphome::Color bg_c,
                        const char *label, esphome::Color text_c,
                        esphome::display::BaseFont *font,
                        GlassVariant variant) {
  const int px0 = x1 - r;
  const int py0 = y_mid - r;
  const int pw = (x2 - x1) + 2 * r + 1;
  const int ph = 2 * r + 1;
  if (pw <= 4 || ph <= 4) return;
  GlassVariant eff_var = variant;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark) ? sample_glass_backdrop(px0, py0, pw, ph) : GlassBackdrop{};
  esphome::Color eff_tint;
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
    fast_print(it, btn_cx + 1, y_mid + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, label);
    fast_print(it, btn_cx, y_mid, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
}

esphome::Color render_modern_card(esphome::display::Display &it, int cx_card, int cy_card,
                                  int w, int h, int r,
                                  esphome::Color bg_c, esphome::Color border_c,
                                  const char *label, esphome::Color text_c,
                                  esphome::display::BaseFont *font,
                                  GlassVariant variant,
                                  bool sample_surface) {
  if (SCREEN_W > 520) {
    if (w <= 340) w = scale_x(w);
    if (h <= 96)  h = scale_y(h);
    if (r <= 24)  r = scale_s(r);
  } else if (std::abs(cx_card - SCREEN_CX) <= 4) {
    if (SCREEN_W < 380 && h <= 48 && w >= 240 && w <= 330) {
      w = SCREEN_W - 88;
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
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark)
          ? sample_glass_backdrop(x0, y0, w, h)
          : (sample_surface ? sample_glass_mean(x0, y0, w, h) : GlassBackdrop{});
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), rim_col, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, std::max(0.5f, static_cast<float>(r) - 1.0f),
                        eff_tint, a_top, a_bot);
  const esphome::Color surf_bg = bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    fast_print(it, cx_card + 1, cy_card + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, label);
    fast_print(it, cx_card, cy_card, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
  return surf_bg;
}

esphome::Color render_card_box(esphome::display::Display &it, int x0, int y0, int w, int h,
                               esphome::Color bg_c, esphome::Color border_c, int r,
                               GlassVariant variant, esphome::Color ref_text,
                               bool sample_surface) {
  if (w <= 4 || h <= 4) return bg_c;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;

  GlassVariant eff_var = variant;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark)
          ? sample_glass_backdrop(x0, y0, w, h)
          : (sample_surface ? sample_glass_mean(x0, y0, w, h) : GlassBackdrop{});
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, norm_tint, ref_text, eff_tint, a_top, a_bot, rim_top, rim_bot, -1, eff_var);

  fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), rim_col, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, std::max(0.5f, static_cast<float>(r) - 1.0f),
                        eff_tint, a_top, a_bot);
  return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
}

esphome::Color render_glass_circle(esphome::display::Display &it, int cx_btn, int cy_btn, int r,
                                   esphome::Color bg_c, esphome::Color border_c,
                                   int inner_hole_r, GlassVariant variant,
                                   esphome::Color ref_text,
                                   bool sample_surface) {
  if (r <= 1) return bg_c;
  GlassVariant eff_var = variant;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);

  const int d = 2 * r + 1;
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark)
          ? sample_glass_backdrop(cx_btn - r, cy_btn - r, d, d)
          : (sample_surface ? sample_glass_mean(cx_btn - r, cy_btn - r, d, d) : GlassBackdrop{});
  esphome::Color eff_tint;
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

void render_scroll_chevron(esphome::display::Display &it, int x, int y, bool is_up,
                           esphome::Color c, esphome::Color shadow_c, bool shadow) {
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

void render_bezel_alert_border(esphome::display::Display &it, int cx, int cy, esphome::Color ring_c) {
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

void render_home_indicator_bar(esphome::display::Display &it, int cx, int y_pos,
                               esphome::Color bar_col) {
  const int half_w = scale_x(54);
  const int bar_h  = (SCREEN_H > 520) ? 7 : 5;
  const int cap_r  = bar_h / 2;
  const float cap = 0.5f * static_cast<float>(bar_h);
  fill_round_rect_aa(it, cx - half_w, y_pos - cap_r + 1, 2 * half_w + 1, bar_h, cap, kChromeShadow);
  fill_round_rect_aa(it, cx - half_w, y_pos - cap_r, 2 * half_w + 1, bar_h, cap, bar_col);
}

void render_viewport_scrollbar(esphome::display::Display &it, int cx, int y_pos, int total_items,
                               float cur_start_idx, esphome::Color col_thumb, bool is_dim) {
  if (total_items <= 4) return;
  const int track_w = (SCREEN_W < 380) ? 84 : ((SCREEN_W > 520) ? 168 : 112);
  const int track_h = (SCREEN_W > 520) ? 6 : 4;
  const int r_cap   = track_h / 2;
  const int track_x0 = cx - (track_w / 2);
  const int track_y0 = y_pos - r_cap;

  const float cap = static_cast<float>(r_cap);
  esphome::Color eff_tint;
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

  const esphome::Color thumb_tint =
      mix_color_q8(kChromeGlassSheen, col_thumb, is_semantic_glass_color(col_thumb) ? 96 : 48);
  fill_round_rect_glass(it, thumb_x0, track_y0, thumb_w, track_h, cap,
                        thumb_tint, is_dim ? 136 : 216, is_dim ? 120 : 196);
}

void render_dock_page_pips(esphome::display::Display &it, int cx, int y_pos, int total_pages,
                           int active_page, esphome::Color col_active, esphome::Color col_inactive) {
  if (total_pages <= 1) return;
  const int spacing = scale_x(14);
  const int dot_r   = (SCREEN_W > 520) ? 3 : 2;
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

void render_vertical_page_pips(esphome::display::Display &it, bool show_up, bool show_down,
                               esphome::Color col_bg, esphome::Color col_active,
                               esphome::Color col_inactive, int bottom_limit_y) {
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

void render_back_button(esphome::display::Display &it, int cx, esphome::display::BaseFont *font,
                        int y_pos, const char *label, esphome::Color text_col) {
  const int back_lbl_y = LATTER_ROUND_DISPLAY ? y_pos : ((y_pos >= 400) ? kBackLabelY : scale_y(y_pos));
  if (label == nullptr) return;
  if (std::strcmp(label, "Home") == 0) {
    render_home_indicator_bar(it, cx, kHomeBarY);
    return;
  }
  fast_print(it, cx + 1, back_lbl_y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, label);
  fast_print(it, cx, back_lbl_y, font, text_col, esphome::display::TextAlign::CENTER, label);
#if LATTER_ROUND_DISPLAY
  frame_hits().add_rect(116, 404, 235, 62, WidgetId::BACK_BUTTON, kHitTap);
#else
  const int bh = scale_y(64);
  const int bw = scale_x(120);
  frame_hits().add_rect(cx - bw, SCREEN_H - bh, 2 * bw + 1, bh, WidgetId::BACK_BUTTON, kHitTap);
#endif
}

void render_page_header(esphome::display::Display &it, int cx, const char *title,
                        esphome::display::BaseFont *font, esphome::Color color, bool shadow) {
  (void)color;
  (void)shadow;
  if (title == nullptr || title[0] == '\0' || font == nullptr) return;
  const int y = layout().hdr_y;
  fast_print(it, cx + 1, y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, title);
  fast_print(it, cx, y, font, esphome::Color::WHITE, esphome::display::TextAlign::CENTER, title);
}

void render_page_header(esphome::display::Display &it, int cx, const std::string &title,
                        esphome::display::BaseFont *font, esphome::Color color, bool shadow) {
  if (title.empty() || font == nullptr) return;
  const std::string fitted = fit_text_to_width_font(title, scale_x(280), font);
  render_page_header(it, cx, fitted.c_str(), font, color, shadow);
}

void render_sub_header(esphome::display::Display &it, int cx, const char *text,
                       esphome::display::BaseFont *font, esphome::Color color) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  if (backdrop_active()) {
    fast_print(it, cx + 1, layout().sub_hdr_y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, text);
  }
  fast_print(it, cx, layout().sub_hdr_y, font, color, esphome::display::TextAlign::CENTER, text);
}

void render_sub_header(esphome::display::Display &it, int cx, const std::string &text,
                       esphome::display::BaseFont *font, esphome::Color color) {
  render_sub_header(it, cx, text.c_str(), font, color);
}

void render_empty_state(esphome::display::Display &it, int cx, int cy, const char *text,
                        esphome::display::BaseFont *font, esphome::Color text_c) {
  // D-043 / D-048 / D-070: Zero frosted glass behind passive empty-state text.
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  const int max_w = std::min(kMaxContentColW, std::max(80, layout().band_safe_span(cy, scale_y(48), 18)));
  std::string lines[2];
  const int n = ui::wrap_text_lines(std::string(text), max_w, font, lines, 2);
  if (n <= 1) {
    const std::string fit = fit_text_to_width_font(std::string(text), max_w, font);
    ui::shadow_print(it, cx, cy, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
  } else {
    int tw = 0, x_off = 0, base = 0, th = 0;
    font->measure("Ag", &tw, &x_off, &base, &th);
    const int half_step = std::max(scale_y(13), (th + 6) / 2);
    ui::shadow_print(it, cx, cy - half_step, font, text_c, esphome::display::TextAlign::CENTER, lines[0].c_str());
    ui::shadow_print(it, cx, cy + half_step, font, text_c, esphome::display::TextAlign::CENTER, lines[1].c_str());
  }
}

void render_time_readout(esphome::display::Display &it, int cx, const char *text,
                         esphome::display::BaseFont *font_time, esphome::Color color, int y_pos) {
  if (text == nullptr || font_time == nullptr) return;
  if (backdrop_active()) {
    fast_print(it, cx + 1, y_pos + 1, font_time, kChromeShadow, esphome::display::TextAlign::CENTER, text);
  }
  fast_print(it, cx, y_pos, font_time, color, esphome::display::TextAlign::CENTER, text);
}

bool offline_pill_due(uint32_t now_ms) {
  auto &ls = link_state();
#ifdef USE_API
  if (esphome::api::global_api_server != nullptr &&
      esphome::api::global_api_server->is_connected_with_state_subscription()) {
    if (!ls.ha_connected) {
      ls.ha_connected = true;
      ls.last_ha_connect_ms = now_ms;
    }
    return false;
  }
#endif
  if (link_report().ha_clients > 0) return false;
  return !ls.ha_connected && (now_ms - ls.last_ha_disconnect_ms) >= 8000U;
}

void render_offline_pill(esphome::display::Display &it, int cx, uint32_t now_ms,
                         esphome::display::BaseFont *font, esphome::Color text_c) {
  if (font == nullptr || !offline_pill_due(now_ms)) return;
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
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(GlassBackdrop{}, kChromeGlassTint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot,
                     -1, GlassVariant::Clear);
  fill_round_rect_glass(it, x0 - 1, sy0 - 1, pill_w + 2, sh + 2, rad + 1.0f, kChromeGlassSheen, rim_top, rim_bot, 1);
  fill_round_rect_glass(it, x0, sy0, pill_w, sh, rad, eff_tint, a_top, a_bot);
  const int text_top = y0 + pad_y - (base - glyph_h);
  fast_print(it, cx + 1, text_top + 1, font, kChromeShadow, esphome::display::TextAlign::TOP_CENTER, kLabel);
  fast_print(it, cx, text_top, font, text_c, esphome::display::TextAlign::TOP_CENTER, kLabel);
}

void render_in_app_mode_bar_track(esphome::display::Display &it, int cx, const int *win_xs,
                                  int vis_count, int active_vis_idx, bool has_left_arrow,
                                  bool has_right_arrow, int total_count, int active_global_idx,
                                  esphome::Color theme_primary, esphome::Color col_white,
                                  int explicit_win_start) {
  if (vis_count <= 0 || win_xs == nullptr) return;

  const uint32_t now_ms = hal_millis_now();
  const int win_start_idx = (explicit_win_start >= 0)
                                ? explicit_win_start
                                : ((active_vis_idx >= 0) ? (active_global_idx - active_vis_idx) : 0);
  const float anim_start  = mode_bar_anim().step_window(win_start_idx, now_ms);
  const float anim_active = mode_bar_anim().step_active(active_global_idx, now_ms);

  const int by = layout().app_dock_y;
  const int dr = layout().dock_r;
  const int r_track = dr + scale_s(3);
  const int left_cx  = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride   = get_dock_slot_stride(win_xs, vis_count);

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
      const esphome::Color slot_rim = mix_color_q8(kChromeGlassSheen, theme_primary, kGlassSemanticRimMaxQ8);
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

void render_progress_arc_r224(esphome::display::Display &it, int cx, int cy, float prog,
                              esphome::Color col) {
#if !LATTER_ROUND_DISPLAY
  if (prog <= 0.001f) return;
  if (prog > 1.0f) prog = 1.0f;
  (void)cx;
  (void)cy;
  const int bar_x0 = 36;
  const int bar_w  = SCREEN_W - 72;
  const int fill_w = static_cast<int>(prog * static_cast<float>(bar_w));
  fill_rect_row_major(it, bar_x0, 6, bar_w, 4, kChromeTrack);
  if (fill_w > 0) {
    fill_rect_row_major(it, bar_x0, 6, fill_w, 4, col);
  }
  return;
#else
  if (prog <= 0.003f) return;
  if (prog > 1.0f) prog = 1.0f;

  static constexpr int R_BORDER = 226;
  static constexpr int R_IN     = 227;
  static constexpr int R_OUT    = 232;
  static constexpr int R_MID    = 229;
  static constexpr int R_BORDER_SQ = R_BORDER * R_BORDER;
  static constexpr int R_IN_SQ     = R_IN * R_IN;

  const float theta = prog * (2.0f * PI_F);
  float sin_t = 0.0f, cos_t = 1.0f;
  fast_sincos(theta, sin_t, cos_t);
  const float ux = sin_t;
  const float uy = -cos_t;
  const bool past_half = (prog >= 0.5f);

  const esphome::Color col_hi(
      static_cast<uint8_t>(col.r + ((255 - col.r) >> 2)),
      static_cast<uint8_t>(col.g + ((255 - col.g) >> 2)),
      static_cast<uint8_t>(col.b + ((255 - col.b) >> 2)));

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
    const int dx_out = static_cast<int>(CIRCLE_HW[y]);
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
    const int dx_in  = cur_dx_in;
    const int dx_brd = cur_dx_brd;

    if (past_half) {
      const int span = dx_out - dx_in + 1;
      if (dx_brd > 0 && dx_in > dx_brd) {
        fast_hline(it, cx + dx_brd, y, dx_in - dx_brd, kChromeShadow);
      }
      fast_hline(it, cx + dx_in, y, span, col);
      if (span >= 3) {
        fast_pixel(it, cx + dx_in + (span >> 1), y, col_hi);
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
        fast_hline(it, cx - dx_out, y, span, col);
        if (span >= 3) {
          fast_pixel(it, cx - dx_out + (span >> 1), y, col_hi);
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
      const esphome::Color c = hsv_to_rgb(static_cast<float>(i * 5) / 360.0f, 1.0f, 1.0f);
      lut[i] = {c.r, c.g, c.b};
    }
    ready = true;
  }
  return lut;
}

bool is_sensitive_perimeter_action_impl(const std::string &domain_or_entity,
                                        const std::string &action, int level) {
  if (action == "unlock" || action == "disarm" || action == "open_cover") {
    return true;
  }
  if (domain_or_entity == "lock" && action == "open") {
    return true;
  }
  if (domain_or_entity == "home_hub.garage" && action == "turn_on") {
    return true;
  }
  if (domain_or_entity.rfind("cover.", 0) == 0 &&
      (domain_or_entity.find("garage") != std::string::npos ||
       domain_or_entity.find("gate") != std::string::npos) &&
      (action == "turn_on" || action == "open" || action == "toggle" ||
       (action == "set_level" && level > 0))) {
    return true;
  }
  return false;
}

}  // namespace

// ==============================================================================
// 2. Semantic Chrome Surface Tokens (aligned 1:1 with ui_types.h kChrome* tokens)
// ==============================================================================
esphome::Color chrome_card_bg_on() { return kChromeCardBgOn; }
esphome::Color chrome_card_bg_off() { return kChromeCardBgOff; }
esphome::Color chrome_card_brd_on() { return kChromeCardBrdOn; }
esphome::Color chrome_card_brd_off() { return kChromeCardBrdOff; }
esphome::Color chrome_danger_bg() { return kChromeDangerBg; }
esphome::Color chrome_modal_bg() { return kChromeModalBg; }
esphome::Color chrome_key_bg() { return kChromeKeyBg; }
esphome::Color chrome_key_brd() { return kChromeKeyBrd; }
esphome::Color chrome_wheel_inner_ring() { return kChromeWheelInnerRing; }
esphome::Color chrome_wheel_outer_ring() { return kChromeWheelOuterRing; }

// ==============================================================================
// 3. Universal Material Design Icon Dispatch (D-051 / D-071)
//    All 43 ui::draw_icon_* helpers and ui::draw_route_icon() delegate 1:1 to
//    the baked 7,447-icon @mdi/js v7.4.47 vector catalog (render_mdi_icon),
//    matching Latter Studio ROUTE_PREVIEW_ICONS identically with zero internal
//    scale_s() double-scaling and dynamic active-Area resolution on APP_AREA.
// ==============================================================================
namespace {

inline void draw_mdi_r(esphome::display::Display &it, const char *slug,
                       int cx, int cy, int r, esphome::Color col,
                       bool shadow, const char *fallback = "mdi:apps") {
  if (r <= 1) return;
  const int mdi_px = std::max(12, 2 * r);
  render_mdi_icon(it, slug, cx, cy, mdi_px, col, shadow, fallback);
}

}  // namespace

void draw_icon_power(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:power", cx, cy, r, col, shadow, "mdi:power");
}

void draw_icon_clock_solid(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:clock-outline", cx, cy, r, col, shadow, "mdi:clock-outline");
}

void draw_icon_hourglass(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:timer-sand", cx, cy, r, col, shadow, "mdi:clock-outline");
}

void draw_icon_alarm_clock(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:alarm", cx, cy, r, col, shadow, "mdi:clock-outline");
}

void draw_icon_stopwatch(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:timer-outline", cx, cy, r, col, shadow, "mdi:clock-outline");
}

void draw_icon_shield_home(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:shield-home", cx, cy, r, col, shadow, "mdi:home");
}

void draw_icon_camera(esphome::display::Display &it, int cx, int cy, int r,
                      esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:video", cx, cy, r, col, shadow, "mdi:cctv");
}

void draw_icon_speaker(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool waves, bool shadow, bool muted) {
  const char *slug = muted ? "mdi:volume-off" : (waves ? "mdi:volume-high" : "mdi:volume-medium");
  draw_mdi_r(it, slug, cx, cy, r, col, shadow, "mdi:volume-high");
}

void draw_icon_thermometer(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:thermometer", cx, cy, r, col, shadow, "mdi:thermostat");
}

void draw_icon_lock(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool is_locked, bool shadow) {
  draw_mdi_r(it, is_locked ? "mdi:lock" : "mdi:lock-open-variant", cx, cy, r, col, shadow, "mdi:lock");
}

void draw_icon_garage_door(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool is_open, bool shadow) {
  draw_mdi_r(it, is_open ? "mdi:garage-open" : "mdi:garage", cx, cy, r, col, shadow, "mdi:garage");
}

void draw_icon_robot_vacuum(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:robot-vacuum", cx, cy, r, col, shadow, "mdi:home");
}

void draw_icon_eighth_notes(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:music", cx, cy, r, col, shadow, "mdi:music");
}

void draw_icon_calendar(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar", cx, cy, r, col, shadow, "mdi:calendar");
}

void draw_icon_cal_agenda(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-text", cx, cy, r, col, shadow, "mdi:calendar");
}

void draw_icon_cal_day(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-today", cx, cy, r, col, shadow, "mdi:calendar");
}

void draw_icon_cal_week(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-week", cx, cy, r, col, shadow, "mdi:calendar");
}

void draw_icon_mic(esphome::display::Display &it, int cx, int cy, int r,
                   esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:microphone", cx, cy, r, col, shadow, "mdi:microphone");
}

void draw_icon_lightbulb(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:lightbulb", cx, cy, r, col, shadow, "mdi:lightbulb");
}

void draw_icon_ceiling_fan(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:fan", cx, cy, r, col, shadow, "mdi:fan");
}

void draw_icon_curtains(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:curtains", cx, cy, r, col, shadow, "mdi:blinds-horizontal");
}

void draw_icon_blinds_slats(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool is_open, bool shadow) {
  draw_mdi_r(it, is_open ? "mdi:blinds-horizontal" : "mdi:blinds-horizontal-closed",
             cx, cy, r, col, shadow, "mdi:blinds-horizontal");
}

void draw_icon_switch_pill(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool is_on, bool shadow) {
  draw_mdi_r(it, is_on ? "mdi:toggle-switch" : "mdi:toggle-switch-off-outline",
             cx, cy, r, col, shadow, "mdi:toggle-switch");
}

void draw_icon_sparkle(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:shimmer", cx, cy, r, col, shadow, "mdi:shimmer");
}

void draw_icon_bento_grid(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:view-grid", cx, cy, r, col, shadow, "mdi:apps");
}

void draw_icon_droplet(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:water", cx, cy, r, col, shadow, "mdi:water");
}

void draw_icon_intercom_handset(esphome::display::Display &it, int cx, int cy, int r,
                                esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:bullhorn", cx, cy, r, col, shadow, "mdi:bullhorn");
}

void draw_icon_bell(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:bell-ring", cx, cy, r, col, shadow, "mdi:bell-ring");
}

void draw_icon_thumbtack(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:pin", cx, cy, r, col, shadow, "mdi:pin");
}

void draw_icon_play_pause(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:play-pause", cx, cy, r, col, shadow, "mdi:play-pause");
}

void draw_icon_hamburger(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:menu", cx, cy, r, col, shadow, "mdi:menu");
}

void draw_icon_grid_2x2(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:view-grid", cx, cy, r, col, shadow, "mdi:view-grid");
}

void draw_icon_carousel(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:view-carousel", cx, cy, r, col, shadow, "mdi:view-carousel");
}

void draw_icon_gear(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:cog-outline", cx, cy, r, col, shadow, "mdi:cog");
}

void draw_icon_chevron(esphome::display::Display &it, int cx, int cy, int r,
                       bool is_right, esphome::Color col, bool shadow) {
  draw_mdi_r(it, is_right ? "mdi:chevron-right" : "mdi:chevron-left",
             cx, cy, r, col, shadow, "mdi:chevron-right");
}

void draw_icon_plus(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:plus", cx, cy, r, col, shadow, "mdi:plus");
}

void draw_icon_minus(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:minus", cx, cy, r, col, shadow, "mdi:minus");
}

void draw_icon_home(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:home", cx, cy, r, col, shadow, "mdi:home");
}

void draw_icon_forecast_chart(esphome::display::Display &it, int cx, int cy, int r,
                              esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:calendar-week", cx, cy, r, col, shadow, "mdi:calendar-week");
}

void draw_icon_sun(esphome::display::Display &it, int cx, int cy, int r,
                   esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:white-balance-sunny", cx, cy, r, col, shadow, "mdi:white-balance-sunny");
}

void draw_icon_crescent_moon(esphome::display::Display &it, int cx, int cy, int r,
                             esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:moon-waning-crescent", cx, cy, r, col, shadow, "mdi:moon-waning-crescent");
}

void draw_icon_wind(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow) {
  draw_mdi_r(it, "mdi:weather-windy", cx, cy, r, col, shadow, "mdi:weather-windy");
}

void draw_icon_arrow_v(esphome::display::Display &it, int cx, int cy, int r,
                       bool is_up, esphome::Color col, bool shadow) {
  draw_mdi_r(it, is_up ? "mdi:arrow-up" : "mdi:arrow-down",
             cx, cy, r, col, shadow, "mdi:arrow-up");
}

void draw_route_icon(esphome::display::Display &it, RouteId route, int cx, int cy, int r,
                     esphome::Color col, esphome::Color cut_c, bool shadow) {
  (void)cut_c;
  const int mdi_px = std::max(16, 2 * r);
  switch (route) {
    case RouteId::APP_PINNED:
      render_mdi_icon(it, "mdi:pin", cx, cy, mdi_px, col, shadow, "mdi:pin");
      return;
    case RouteId::APP_MUSIC:
      render_mdi_icon(it, "mdi:music", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_LIBRARY:
      render_mdi_icon(it, "mdi:music-box-outline", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_SPEAKERS:
      render_mdi_icon(it, "mdi:speaker-multiple", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_FAVORITES:
      render_mdi_icon(it, "mdi:heart", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_PLAYLISTS:
      render_mdi_icon(it, "mdi:playlist-music", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_QUEUE:
      render_mdi_icon(it, "mdi:playlist-play", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_ALBUM_TRACKS:
      render_mdi_icon(it, "mdi:album", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_LYRICS:
      render_mdi_icon(it, "mdi:script-text", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::MUSIC_INFO:
      render_mdi_icon(it, "mdi:information-outline", cx, cy, mdi_px, col, shadow, "mdi:music");
      return;
    case RouteId::APP_HOME_HUB:
      render_mdi_icon(it, "mdi:home", cx, cy, mdi_px, col, shadow, "mdi:home");
      return;
    case RouteId::HOME_HUB_ALARM:
      render_mdi_icon(it, "mdi:shield-home", cx, cy, mdi_px, col, shadow, "mdi:home");
      return;
    case RouteId::APP_CAMERAS:
      render_mdi_icon(it, "mdi:cctv", cx, cy, mdi_px, col, shadow, "mdi:video");
      return;
    case RouteId::CAMERAS_GRID:
      render_mdi_icon(it, "mdi:view-grid", cx, cy, mdi_px, col, shadow, "mdi:cctv");
      return;
    case RouteId::CAMERAS_SINGLE:
      render_mdi_icon(it, "mdi:video", cx, cy, mdi_px, col, shadow, "mdi:cctv");
      return;
    case RouteId::CAMERAS_CAROUSEL:
      render_mdi_icon(it, "mdi:view-carousel", cx, cy, mdi_px, col, shadow, "mdi:cctv");
      return;
    case RouteId::CAMERAS_SETTINGS:
      render_mdi_icon(it, "mdi:cog-outline", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::HOME_HUB_INTERCOM:
      render_mdi_icon(it, "mdi:bullhorn", cx, cy, mdi_px, col, shadow, "mdi:home");
      return;
    case RouteId::HOME_HUB_THERMOSTAT:
      render_mdi_icon(it, "mdi:thermostat", cx, cy, mdi_px, col, shadow, "mdi:thermometer");
      return;
    case RouteId::AREA_CLIMATE:
      render_mdi_icon(it, "mdi:thermometer", cx, cy, mdi_px, col, shadow, "mdi:thermostat");
      return;
    case RouteId::HOME_HUB_ENTRY:
      render_mdi_icon(it, "mdi:lock", cx, cy, mdi_px, col, shadow, "mdi:home");
      return;
    case RouteId::HOME_HUB_VACUUM:
      render_mdi_icon(it, "mdi:robot-vacuum", cx, cy, mdi_px, col, shadow, "mdi:home");
      return;
    case RouteId::APP_AREA:
      // Dynamic active Area icon from Home Assistant (D-071); falls back to mdi:sofa.
      render_mdi_icon(it, resolve_area_dock_icon(area_ctx_state()), cx, cy, mdi_px, col, shadow, "mdi:sofa");
      return;
    case RouteId::AREA_PICKER:
      render_mdi_icon(it, "mdi:floor-plan", cx, cy, mdi_px, col, shadow, "mdi:map-marker-outline");
      return;
    case RouteId::AREA_SCENE:
      render_mdi_icon(it, "mdi:shimmer", cx, cy, mdi_px, col, shadow, "mdi:sofa");
      return;
    case RouteId::AREA_LIGHT:
      render_mdi_icon(it, "mdi:lightbulb", cx, cy, mdi_px, col, shadow, "mdi:sofa");
      return;
    case RouteId::AREA_COVER:
      render_mdi_icon(it, "mdi:blinds-horizontal", cx, cy, mdi_px, col, shadow, "mdi:sofa");
      return;
    case RouteId::AREA_FAN:
      render_mdi_icon(it, "mdi:fan", cx, cy, mdi_px, col, shadow, "mdi:sofa");
      return;
    case RouteId::AREA_SWITCH:
      render_mdi_icon(it, "mdi:toggle-switch", cx, cy, mdi_px, col, shadow, "mdi:sofa");
      return;
    case RouteId::AREA_MEDIA:
      render_mdi_icon(it, "mdi:television-speaker", cx, cy, mdi_px, col, shadow, "mdi:speaker");
      return;
    case RouteId::AREA_ALL:
      render_mdi_icon(it, "mdi:view-grid", cx, cy, mdi_px, col, shadow, "mdi:apps");
      return;
    case RouteId::APP_REMOTE:
      render_mdi_icon(it, "mdi:remote-tv", cx, cy, mdi_px, col, shadow, "mdi:remote");
      return;
    case RouteId::REMOTE_DEVICES:
      render_mdi_icon(it, "mdi:remote", cx, cy, mdi_px, col, shadow, "mdi:remote-tv");
      return;
    case RouteId::APP_WEATHER:
    case RouteId::WEATHER_CURRENT:
      render_mdi_icon(it, "mdi:weather-partly-cloudy", cx, cy, mdi_px, col, shadow, "mdi:cloud");
      return;
    case RouteId::WEATHER_FORECAST:
      render_mdi_icon(it, "mdi:calendar-week", cx, cy, mdi_px, col, shadow, "mdi:cloud");
      return;
    case RouteId::WEATHER_SUN:
      render_mdi_icon(it, "mdi:white-balance-sunny", cx, cy, mdi_px, col, shadow, "mdi:weather-sunny");
      return;
    case RouteId::WEATHER_MOON:
      render_mdi_icon(it, "mdi:moon-waning-crescent", cx, cy, mdi_px, col, shadow, "mdi:weather-night");
      return;
    case RouteId::APP_TIME:
    case RouteId::APP_CLOCK:
    case RouteId::TIME_CLOCK:
      render_mdi_icon(it, "mdi:clock-outline", cx, cy, mdi_px, col, shadow, "mdi:clock-outline");
      return;
    case RouteId::TIME_TIMERS:
      render_mdi_icon(it, "mdi:timer-sand", cx, cy, mdi_px, col, shadow, "mdi:clock-outline");
      return;
    case RouteId::TIME_ALARMS:
      render_mdi_icon(it, "mdi:alarm", cx, cy, mdi_px, col, shadow, "mdi:clock-outline");
      return;
    case RouteId::TIME_STOPWATCH:
    case RouteId::TOOLS_STOPWATCH:
      render_mdi_icon(it, "mdi:timer-outline", cx, cy, mdi_px, col, shadow, "mdi:clock-outline");
      return;
    case RouteId::APP_TOOLS:
      render_mdi_icon(it, "mdi:view-grid-outline", cx, cy, mdi_px, col, shadow, "mdi:apps");
      return;
    case RouteId::TOOLS_LEVEL:
      render_mdi_icon(it, "mdi:spirit-level", cx, cy, mdi_px, col, shadow, "mdi:compass-outline");
      return;
    case RouteId::TOOLS_FLASHLIGHT:
      render_mdi_icon(it, "mdi:flashlight", cx, cy, mdi_px, col, shadow, "mdi:lightbulb");
      return;
    case RouteId::TOOLS_CALCULATOR:
      render_mdi_icon(it, "mdi:calculator-variant", cx, cy, mdi_px, col, shadow, "mdi:calculator");
      return;
    case RouteId::APP_CALENDAR:
      render_mdi_icon(it, "mdi:calendar", cx, cy, mdi_px, col, shadow, "mdi:calendar");
      return;
    case RouteId::CALENDAR_OVERVIEW:
      render_mdi_icon(it, "mdi:calendar-text", cx, cy, mdi_px, col, shadow, "mdi:calendar");
      return;
    case RouteId::CALENDAR_DAY:
      render_mdi_icon(it, "mdi:calendar-today", cx, cy, mdi_px, col, shadow, "mdi:calendar");
      return;
    case RouteId::CALENDAR_WEEK:
      render_mdi_icon(it, "mdi:calendar-week", cx, cy, mdi_px, col, shadow, "mdi:calendar");
      return;
    case RouteId::CALENDAR_MONTH:
      render_mdi_icon(it, "mdi:calendar-month", cx, cy, mdi_px, col, shadow, "mdi:calendar");
      return;
    case RouteId::TOOLS_DICE:
      render_mdi_icon(it, "mdi:dice-5", cx, cy, mdi_px, col, shadow, "mdi:dice-multiple");
      return;
    case RouteId::TOOLS_COIN_FLIP:
      render_mdi_icon(it, "mdi:alpha-c-circle", cx, cy, mdi_px, col, shadow, "mdi:circle-double");
      return;
    case RouteId::APP_GAMES:
      render_mdi_icon(it, "mdi:controller", cx, cy, mdi_px, col, shadow, "mdi:gamepad-variant");
      return;
    case RouteId::GAMES_MARBLES:
      render_mdi_icon(it, "mdi:billiards", cx, cy, mdi_px, col, shadow, "mdi:circle-multiple");
      return;
    case RouteId::GAMES_ARCHERY:
      render_mdi_icon(it, "mdi:bullseye-arrow", cx, cy, mdi_px, col, shadow, "mdi:target");
      return;
    case RouteId::GAMES_BRICKS:
      render_mdi_icon(it, "mdi:wall", cx, cy, mdi_px, col, shadow, "mdi:view-grid");
      return;
    case RouteId::GAMES_CHOMP:
      render_mdi_icon(it, "mdi:pac-man", cx, cy, mdi_px, col, shadow, "mdi:ghost");
      return;
    case RouteId::GAMES_2048:
      render_mdi_icon(it, "mdi:numeric-2-box-multiple", cx, cy, mdi_px, col, shadow, "mdi:grid");
      return;
    case RouteId::GAMES_MEMORY:
      render_mdi_icon(it, "mdi:cards", cx, cy, mdi_px, col, shadow, "mdi:apps");
      return;
    case RouteId::SETTINGS_DISPLAY:
      render_mdi_icon(it, "mdi:brightness-6", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_SLEEP:
      render_mdi_icon(it, "mdi:power-sleep", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_SOUND:
      render_mdi_icon(it, "mdi:volume-high", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_VOICE:
      render_mdi_icon(it, "mdi:microphone", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_MUSIC:
      render_mdi_icon(it, "mdi:music-box-outline", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_HOME:
      render_mdi_icon(it, "mdi:home-outline", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_ACTIONS:
      render_mdi_icon(it, "mdi:gesture-tap-button", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_UPDATE:
      render_mdi_icon(it, "mdi:cellphone-arrow-down", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_INFO:
      render_mdi_icon(it, "mdi:information-outline", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
    case RouteId::SETTINGS_ROOT:
    case RouteId::NONE:
    default:
      render_mdi_icon(it, "mdi:cog", cx, cy, mdi_px, col, shadow, "mdi:cog");
      return;
  }
}

// ==============================================================================
// 4. Interactive Hit-Registered Widgets, Functional Components & Modals (D-070)
// ==============================================================================
void toggle_switch(esphome::display::Display &it, int cx_sw, int cy_sw, bool is_on,
                   esphome::Color active_col) {
  const int sw_w = scale_x(40);
  const int sw_h = scale_y(22);
  const int x0 = cx_sw - sw_w / 2;
  const int y0 = cy_sw - sw_h / 2;
  const float rad = 0.5f * static_cast<float>(sw_h);

  GlassVariant eff_var = GlassVariant::Clear;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(is_on ? active_col : kChromeGlassTint,
                         is_on ? active_col : kChromeTrackBorder,
                         eff_var, norm_tint, rim_col);
  const GlassBackdrop bd =
      (eff_var == GlassVariant::ReadableDark) ? sample_glass_backdrop(x0, y0, sw_w, sw_h) : GlassBackdrop{};
  esphome::Color eff_tint;
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

void shadow_print(esphome::display::Display &it, int x, int y,
                  esphome::display::BaseFont *font, esphome::Color col,
                  esphome::display::TextAlign align, const char *text,
                  bool double_shadow) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  if (double_shadow) {
    fast_print(it, x + 2, y + 2, font, kChromeShadow, align, text);
  }
  fast_print(it, x + 1, y + 1, font, kChromeShadow, align, text);
  fast_print(it, x, y, font, col, align, text);
}

int wrap_text_lines(const std::string &text, int max_w, esphome::display::BaseFont *font,
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

void mode_bar(esphome::display::Display &it, int cx,
              int total_count, int active_global_idx, int win_start,
              esphome::Color theme_primary, esphome::Color col_white,
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
    const esphome::Color raw_c = is_act ? theme_primary : col_white;
    float alpha = 1.0f;
    if (rel_slot < 0.0f) {
      alpha = std::max(0.25f, 1.0f + rel_slot * 1.4f);
    } else if (rel_slot > static_cast<float>(vis_count - 1)) {
      alpha = std::max(0.25f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.4f);
    }
    const int alpha_q8 = std::clamp(static_cast<int>(std::lround(alpha * 256.0f)), 0, 256);
    const esphome::Color fg_c = mix_color_q8(kChromeBarTrack, raw_c, alpha_q8);
    draw_icon_fn(it, g_idx, bx, by, ir, fg_c, (alpha > 0.65f), user_ctx);
  }
}

void header_pager_bar(esphome::display::Display &it, int cx, int cy,
                      const std::string &title, esphome::display::BaseFont *font,
                      esphome::Color col_white, bool show_arrows,
                      uint16_t prev_wid, uint16_t next_wid,
                      int16_t prev_param, int16_t next_param) {
  if (font == nullptr) return;
  const auto &ly = layout();
  if (!show_arrows) {
    const int max_w = std::min(kMaxContentColW, std::max(80, ly.safe_span(cy, 16)));
    const std::string fit = fit_text_to_width_font(title, max_w, font);
    shadow_print(it, cx, cy, font, col_white, esphome::display::TextAlign::CENTER, fit.c_str());
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

  // D-048 / D-070: Title text sits bare with 1 px kChromeShadow — ZERO glass behind passive text.
  const std::string fit = fit_text_to_width_font(title, title_max_w, font);
  shadow_print(it, cx, cy, font, col_white, esphome::display::TextAlign::CENTER, fit.c_str());
}

void action_pill(esphome::display::Display &it, int x0, int w, int y_mid, int h,
                 esphome::Color border_c, esphome::Color bg_c,
                 const char *label, esphome::Color text_c, esphome::display::BaseFont *font,
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
    shadow_print(it, start_x + icon_px + icon_gap, y_mid, font, text_c,
                 esphome::display::TextAlign::CENTER_LEFT, fit.c_str());
  } else if (has_mdi) {
    const int icon_px = std::clamp(eff_h - scale_y(12), 14, scale_s(22));
    render_mdi_icon(it, leading_mdi, cx, y_mid, icon_px, text_c, true);
  } else if (has_lbl) {
    const int max_txt_w = std::max(24, eff_w - scale_x(14));
    const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
    shadow_print(it, cx, y_mid, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
  }

  if (widget_id != WidgetId::NONE) {
    const int min_h = (hit_h_min >= 0) ? hit_h_min : layout().min_hit_px();
    const int hit_h = std::max(eff_h, min_h);
    const int hit_w = std::max(eff_w, std::min(min_h, eff_w + scale_x(12)));
    frame_hits().add_centered_rect(cx, y_mid, hit_w, hit_h, widget_id, flags, param);
  }
}

void action_pill_centered(esphome::display::Display &it, int cx, int w, int y_mid, int h,
                          esphome::Color border_c, esphome::Color bg_c,
                          const char *label, esphome::Color text_c,
                          esphome::display::BaseFont *font,
                          uint16_t widget_id, uint8_t flags, int16_t param,
                          int hit_h_min, const char *leading_mdi) {
  action_pill(it, cx - w / 2, w, y_mid, h, border_c, bg_c, label, text_c, font,
              widget_id, flags, param, hit_h_min, leading_mdi);
}

void action_pill_row(esphome::display::Display &it, int cy, int h,
                     int max_row_w, int max_btn_w, int gap,
                     const ActionPillSpec *items, int count,
                     esphome::display::BaseFont *font, int inset) {
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

void segmented_toggle(esphome::display::Display &it, int cx, int cy, int total_w, int h,
                      const char *left_label, const char *right_label, bool right_active,
                      esphome::display::BaseFont *font,
                      esphome::Color theme_primary, esphome::Color col_white,
                      esphome::Color col_slate,
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

void setting_cycle_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                       const char *label, bool enabled,
                       esphome::display::BaseFont *font,
                       esphome::Color col_white, esphome::Color col_slate,
                       esphome::Color chevron_col,
                       uint16_t widget_id, uint8_t flags, int16_t param) {
  const int eff_w = std::max(80, w);
  const int eff_h = std::max(28, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const esphome::Color bg_c = enabled ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color brd_c = enabled ? chrome_card_brd_on() : chrome_card_brd_off();
  const esphome::Color txt_c = enabled ? col_white : col_slate;

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, txt_c);

  const int chv_inset = scale_x(16);
  const int max_txt_w = std::max(40, eff_w - (enabled ? (2 * chv_inset + scale_x(24)) : scale_x(20)));
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
    shadow_print(it, cx, cy, font, txt_c, esphome::display::TextAlign::CENTER, fit.c_str());
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

void slider_card_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                     const char *label, int pct, int min_pct, int max_pct, bool enabled,
                     esphome::Color fill_c, esphome::Color brd_c,
                     esphome::display::BaseFont *font, esphome::Color text_c,
                     uint16_t widget_id, int16_t param) {
  const int eff_w = std::max(80, w);
  const int eff_h = std::max(28, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const int card_r = kRadiusCardPx();
  const esphome::Color bg_c = enabled ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color eff_brd = enabled ? brd_c : chrome_card_brd_off();

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
    shadow_print(it, cx, cy, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
  }
}

void list_row(esphome::display::Display &it, int cx, int cy, int w, int h,
              const char *label, const char *sub_label,
              esphome::Color bg_c, esphome::Color brd_c,
              esphome::Color text_c, esphome::Color sub_c,
              esphome::display::BaseFont *font,
              uint16_t widget_id, uint8_t flags, int16_t param,
              const char *leading_mdi, const char *right_badge,
              esphome::Color right_badge_c) {
  const int eff_w = std::max(80, w);
  const int eff_h = std::max(28, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;

  // D-048: Non-interactive rows (widget_id == NONE) render bare shadowed text with zero glass.
  if (widget_id == WidgetId::NONE) {
    if (label != nullptr && label[0] != '\0' && font != nullptr) {
      const std::string fit = fit_text_to_width_font(std::string(label), std::max(40, eff_w - scale_x(16)), font);
      shadow_print(it, cx, cy, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
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
    shadow_print(it, x0 + eff_w - pad_r, cy, font, right_badge_c,
                 esphome::display::TextAlign::CENTER_RIGHT, right_badge);
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
      shadow_print(it, x0 + pad_l, cy - half_step, font, text_c,
                   esphome::display::TextAlign::CENTER_LEFT, fit1.c_str());
      shadow_print(it, x0 + pad_l, cy + half_step, font, sub_c,
                   esphome::display::TextAlign::CENTER_LEFT, fit2.c_str());
    } else if (has_mdi || has_badge) {
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      shadow_print(it, x0 + pad_l, cy, font, text_c,
                   esphome::display::TextAlign::CENTER_LEFT, fit.c_str());
    } else {
      const std::string fit = fit_text_to_width_font(std::string(label), max_txt_w, font);
      shadow_print(it, cx, cy, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
    }
  }

  frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, flags, param);
}

void labeled_action_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                        const char *title, const char *status,
                        esphome::Color title_c, esphome::Color status_c,
                        esphome::display::BaseFont *title_font,
                        esphome::display::BaseFont *status_font,
                        const char *cta_label, int cta_w, int cta_h,
                        esphome::Color cta_border_c, esphome::Color cta_bg_c,
                        esphome::Color cta_text_c,
                        esphome::display::BaseFont *cta_font,
                        uint16_t cta_wid, uint8_t cta_flags,
                        int16_t cta_param, int cta_hit_h_min,
                        TileDrawIconFn draw_icon_fn, void *icon_ctx,
                        esphome::Color icon_c) {
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
                   esphome::display::TextAlign::CENTER_LEFT, fit_title.c_str());
      shadow_print(it, txt_x0, cy + std::max(9, eff_h / 5), status_font, status_c,
                   esphome::display::TextAlign::CENTER_LEFT, fit_status.c_str());
    } else {
      shadow_print(it, txt_x0, cy, title_font, title_c,
                   esphome::display::TextAlign::CENTER_LEFT, fit_title.c_str());
    }
  }

  action_pill(it, cta_x0, eff_cta_w, cy, eff_cta_h, cta_border_c, cta_bg_c,
              cta_label, cta_text_c, cta_font, cta_wid, cta_flags, cta_param, cta_hit_h_min);
}

void grid_tile(esphome::display::Display &it, int cx, int cy, int w, int h,
               const char *label, bool active, bool disabled,
               esphome::Color accent_c, esphome::Color text_c,
               esphome::display::BaseFont *font,
               TileDrawIconFn draw_icon_fn, void *user_ctx,
               uint16_t widget_id, int16_t param) {
  const int eff_w = std::max(48, w);
  const int eff_h = std::max(48, h);
  const int x0 = cx - eff_w / 2;
  const int y0 = cy - eff_h / 2;
  const esphome::Color bg_c = active ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color brd_c = active ? accent_c : (disabled ? chrome_card_brd_off() : chrome_card_brd_on());

  render_card_box(it, x0, y0, eff_w, eff_h, bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, text_c);

  const int icon_r = std::min(card_icon_r(eff_h), std::max(10, eff_h / 4));
  const int icon_cy = cy - eff_h / 6;
  if (draw_icon_fn != nullptr) {
    draw_icon_fn(it, cx, icon_cy, icon_r, disabled ? text_c : (active ? accent_c : text_c), true, user_ctx);
  }
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    const int lbl_y = cy + (eff_h * 27) / 100;
    const std::string fit = fit_text_to_width_font(std::string(label), std::max(32, eff_w - scale_x(12)), font);
    shadow_print(it, cx, lbl_y, font, text_c, esphome::display::TextAlign::CENTER, fit.c_str());
  }
  if (widget_id != WidgetId::NONE && !disabled) {
    frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

void icon_circle_button(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color bg_c, esphome::Color brd_c,
                        esphome::Color icon_c,
                        TileDrawIconFn draw_icon_fn, void *user_ctx,
                        uint16_t widget_id, uint8_t flags, int16_t param, int hit_pad) {
  const int eff_r = std::max(10, r);
  render_glass_circle(it, cx, cy, eff_r, bg_c, brd_c);
  if (draw_icon_fn != nullptr) {
    draw_icon_fn(it, cx, cy, dock_icon_r(eff_r), icon_c, true, user_ctx);
  }
  if (widget_id != WidgetId::NONE) {
    const int min_r = (layout().min_hit_px() + 1) / 2;
    const int pad = (hit_pad >= 0) ? hit_pad : std::max(scale_s(6), min_r - eff_r);
    frame_hits().add_circle(cx, cy, eff_r, widget_id, flags, param, pad);
  }
}

void keypad_button(esphome::display::Display &it, int cx, int cy, int w, int h,
                   const char *label, esphome::Color bg_c, esphome::Color brd_c,
                   esphome::Color text_c, esphome::display::BaseFont *font,
                   uint16_t widget_id, int16_t param) {
  const int eff_w = std::max(24, w);
  const int eff_h = std::max(24, h);
  render_card_box(it, cx - eff_w / 2, cy - eff_h / 2, eff_w, eff_h,
                  bg_c, brd_c, kRadiusKeyPx(), GlassVariant::Clear, text_c);
  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    shadow_print(it, cx, cy, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
  if (widget_id != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx, cy, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

// ==============================================================================
// 5. Legacy Toggle Row, Steppers, Color Wheel & Modals
// ==============================================================================
void toggle_card_row(esphome::display::Display &it, int cx, int y_mid, int w, int h,
                     const char *label, bool is_on, esphome::display::BaseFont *font,
                     esphome::Color active_col, esphome::Color text_on, esphome::Color text_off,
                     uint16_t widget_id, int16_t param) {
  // Explicit screen-space bounds (D-070): zero hidden auto-scaling, unified kRadiusCardPx.
  const int eff_w = std::min(SCREEN_W - 16, std::max(80, w));
  const int eff_h = std::max(28, h);
  const esphome::Color bg_c = is_on ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color brd_c = is_on ? active_col : chrome_card_brd_off();
  const esphome::Color fg = is_on ? text_on : text_off;
  render_card_box(it, cx - eff_w / 2, y_mid - eff_h / 2, eff_w, eff_h,
                  bg_c, brd_c, kRadiusCardPx(), GlassVariant::Clear, fg);

  const int text_x = cx - eff_w / 2 + scale_x(14);
  const int sw_cx = cx + eff_w / 2 - scale_x(28);
  const int max_lbl_px = std::max(60, (sw_cx - scale_x(26)) - text_x);
  const std::string fit_lbl = fit_text_to_width_font(
      label != nullptr ? std::string(label) : std::string(), max_lbl_px, font);
  shadow_print(it, text_x, y_mid, font, fg, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());
  toggle_switch(it, sw_cx, y_mid, is_on, active_col);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx, y_mid, eff_w, eff_h, widget_id, kHitTap | kHitFocusSlot, param);
  }
}

void pill_button(esphome::display::Display &it, int x1, int x2, int y_mid, int r,
                 esphome::Color border_c, esphome::Color bg_c,
                 const char *label, esphome::Color text_c, esphome::display::BaseFont *font,
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

void pill_button_box(esphome::display::Display &it, int x0, int w, int y_mid, int r,
                     esphome::Color border_c, esphome::Color bg_c,
                     const char *label, esphome::Color text_c, esphome::display::BaseFont *font,
                     uint16_t widget_id, uint8_t flags, int16_t param, GlassVariant variant) {
  const int safe_w = std::max(2, w);
  const int eff_r = std::min(std::max(1, r), safe_w / 2);
  const int x1 = x0 + eff_r;
  const int x2 = std::max(x1, x0 + safe_w - 1 - eff_r);
  pill_button(it, x1, x2, y_mid, eff_r, border_c, bg_c, label, text_c, font,
              widget_id, flags, param, variant);
}

void stepper_row(esphome::display::Display &it, int cx, int y_top, int h,
                 const char *const labels[4], esphome::display::BaseFont *font,
                 esphome::Color col_text, uint16_t base_widget_id) {
  const int btn_w = (SCREEN_W < 380) ? 50 : scale_x(60);
  const int step = (SCREEN_W < 380) ? 66 : scale_x(80);
  const int eff_h = std::max(28, h);
  const int y_mid = y_top + eff_h / 2;
  const int rad = kRadiusKeyPx();
  const esphome::Color bg_c = chrome_card_bg_on();
  const esphome::Color brd_c = chrome_card_brd_on();
  for (int i = 0; i < 4; i++) {
    const int bx = cx + static_cast<int>((static_cast<float>(i) - 1.5f) * static_cast<float>(step));
    render_card_box(it, bx - btn_w / 2, y_top, btn_w, eff_h, bg_c, brd_c, rad,
                    GlassVariant::Clear, col_text);
    if (labels[i] != nullptr && labels[i][0] != '\0' && font != nullptr) {
      shadow_print(it, bx, y_mid, font, col_text, esphome::display::TextAlign::CENTER, labels[i]);
    }
    if (base_widget_id != WidgetId::NONE) {
      frame_hits().add_centered_rect(
          bx, y_mid, btn_w, eff_h,
          static_cast<uint16_t>(base_widget_id + i),
          kHitTap | kHitFocusSlot, static_cast<int16_t>(i));
    }
  }
}

esphome::Color hsv_wheel_sector_color(int sector_idx_72, float sat) {
  int idx = sector_idx_72 % 72;
  if (idx < 0) idx += 72;
  const HsvUnitRgb u = hsv_unit_lut_72()[idx];
  const float s = std::max(0.0f, std::min(1.0f, sat));
  const float inv = 1.0f - s;
  return esphome::Color(
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.r) * s),
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.g) * s),
      static_cast<uint8_t>(255.0f * inv + static_cast<float>(u.b) * s));
}

bool hsv_wheel_pick(int tx, int ty, int cx, int cy, int outer_r_466, int inner_r_466,
                    float &out_hue_deg, float &out_sat) {
  const float ws = (SCREEN_MIN_DIM > 520) ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f)
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

void hsv_color_wheel(esphome::display::Display &it, int cx, int cy, int outer_r_466, int inner_r_466,
                     float cur_hue_deg, float cur_sat,
                     esphome::Color col_bg, esphome::Color col_white, uint16_t widget_id) {
  const float ws = (SCREEN_MIN_DIM > 520) ? (static_cast<float>(SCREEN_MIN_DIM) / 466.0f)
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
      const esphome::Color seg_c = hsv_wheel_sector_color(a_idx, sat_b);
      const int x0 = cx + static_cast<int>(static_cast<float>(r0) * c_v);
      const int y0 = cy + static_cast<int>(static_cast<float>(r0) * s_v);
      const int x1 = cx + static_cast<int>(static_cast<float>(r1) * c_v);
      const int y1 = cy + static_cast<int>(static_cast<float>(r1) * s_v);
      fast_line(it, x0, y0, x1, y1, seg_c);
      fast_line(it, x0 + 1, y0, x1 + 1, y1, seg_c);
      fast_line(it, x0, y0 + 1, x1, y1 + 1, seg_c);
    }
  }

  const esphome::Color cur_rgb = hsv_to_rgb(cur_hue_deg / 360.0f, cur_sat, 1.0f);
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

void confirm_modal(esphome::display::Display &it, int cx, const char *title,
                   const char *line1, const char *line2,
                   const char *cancel_lbl, const char *confirm_lbl,
                   esphome::Color confirm_border, esphome::Color confirm_bg,
                   esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_body,
                   esphome::Color theme_primary, esphome::Color col_white, esphome::Color col_slate,
                   uint16_t cancel_wid, uint16_t confirm_wid) {
  (void)theme_primary;
  render_page_header(it, cx, title, font_hdr, col_white, true);
  if (line1 != nullptr && line1[0] != '\0') {
    const int w1 = std::min(kMaxContentColW, layout().safe_span(scale_y(132), 18));
    const std::string f1 = fit_text_to_width_font(std::string(line1), w1, font_body);
    shadow_print(it, cx, scale_y(132), font_body, col_white, esphome::display::TextAlign::CENTER, f1.c_str());
  }
  if (line2 != nullptr && line2[0] != '\0') {
    const int w2 = std::min(kMaxContentColW, layout().safe_span(scale_y(168), 18));
    const std::string f2 = fit_text_to_width_font(std::string(line2), w2, font_body);
    shadow_print(it, cx, scale_y(168), font_body, col_slate, esphome::display::TextAlign::CENTER, f2.c_str());
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

void game_over_sheet(esphome::display::Display &it, int cx, int cy,
                     const char *title, esphome::Color title_col,
                     const char *score_line, const char *best_line, bool is_new_best,
                     const char *cta_lbl,
                     esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_body,
                     esphome::Color theme_primary, esphome::Color col_white,
                     esphome::Color col_gold, esphome::Color col_slate, uint16_t cta_wid) {
  // D-048 / D-070: Zero passive glass box behind text / zero glass-on-glass.
  const int scrim_w = std::min(kMaxContentColW, std::min(SCREEN_W - 24, scale_x(312)));
  const int scrim_h = scale_y(206);
  fill_round_rect_glass(it, cx - scrim_w / 2, cy - scale_y(102), scrim_w, scrim_h,
                        static_cast<float>(kRadiusCardPx()), kChromeShadow, 176, 176);
  shadow_print(it, cx, cy - scale_y(60), font_hdr, title_col, esphome::display::TextAlign::CENTER, title, true);
  if (score_line != nullptr && score_line[0] != '\0') {
    shadow_print(it, cx, cy - scale_y(18), font_body, col_white, esphome::display::TextAlign::CENTER, score_line);
  }
  if (best_line != nullptr && best_line[0] != '\0') {
    shadow_print(it, cx, cy + scale_y(14), font_body, is_new_best ? col_gold : col_slate,
                 esphome::display::TextAlign::CENTER, best_line);
  }
  const int pill_h = std::max(40, scale_y(44));
  const int pill_w = std::min(scrim_w - scale_x(36), scale_x(204));
  action_pill_centered(it, cx, pill_w, cy + scale_y(62), pill_h,
                       theme_primary, chrome_card_bg_on(), cta_lbl, col_white, font_body,
                       cta_wid, kHitTap | kHitFocusSlot, 0);
}

PinPadModalState &pin_pad_state() {
  return g_pin_pad;
}

void pin_pad_scrub() {
  volatile char *p = g_pin_pad.digits;
  for (size_t i = 0; i < sizeof(g_pin_pad.digits); i++) {
    p[i] = '\0';
  }
  g_pin_pad.len = 0;
  g_pin_pad.open = false;
  g_pin_pad.target = PinPadTargetKind::NONE;
  g_pin_pad.error_msg[0] = '\0';
}

void pin_pad_open(PinPadTargetKind kind, const char *prompt_label) {
  pin_pad_scrub();
  g_pin_pad.open = true;
  g_pin_pad.target = kind;
  if (prompt_label != nullptr) {
    std::strncpy(g_pin_pad.prompt, prompt_label, sizeof(g_pin_pad.prompt) - 1);
    g_pin_pad.prompt[sizeof(g_pin_pad.prompt) - 1] = '\0';
  } else {
    g_pin_pad.prompt[0] = '\0';
  }
}

bool pin_pad_press_digit(uint8_t digit) {
  if (digit > 9 || g_pin_pad.len >= 8) return false;
  g_pin_pad.digits[g_pin_pad.len++] = static_cast<char>('0' + digit);
  g_pin_pad.digits[g_pin_pad.len] = '\0';
  g_pin_pad.error_msg[0] = '\0';
  return true;
}

void pin_pad_backspace() {
  if (g_pin_pad.len > 0) {
    g_pin_pad.digits[--g_pin_pad.len] = '\0';
  }
  g_pin_pad.error_msg[0] = '\0';
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

void pin_pad_modal(esphome::display::Display &it, int cx, int cy, uint32_t now_ms,
                   esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_key,
                   esphome::display::BaseFont *font_small,
                   esphome::Color theme_primary, esphome::Color col_white,
                   esphome::Color col_slate, esphome::Color col_red) {
  (void)cy;
  (void)font_hdr;
  auto &st = pin_pad_state();
  const char *hdr = (st.prompt[0] != '\0') ? st.prompt : "ENTER SECURITY PIN";
  render_page_header(it, cx, hdr, font_small, col_white, true);

  const int disp_y = scale_y(84);
  const int disp_w = std::min(kMaxContentColW, std::min(SCREEN_W - 44, scale_x(236)));
  const bool show_err = (st.error_msg[0] != '\0' && (now_ms - st.error_ms) < 2200U);
  // D-048 / D-070: Zero frosted glass behind read-only PIN entry display; subtle bottom rule only.
  if (show_err) {
    shadow_print(it, cx, disp_y, font_small, col_white, esphome::display::TextAlign::CENTER, st.error_msg);
  } else if (st.len == 0) {
    shadow_print(it, cx, disp_y, font_small, col_slate, esphome::display::TextAlign::CENTER, "4-8 Digits");
  } else {
    const int dot_step = scale_x(18);
    const int start_x = cx - ((static_cast<int>(st.len) - 1) * dot_step) / 2;
    for (uint8_t i = 0; i < st.len; i++) {
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step + 1, disp_y + 1, scale_s(5), kChromeShadow);
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step, disp_y, scale_s(5), col_white);
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
    const esphome::Color brd = is_ok ? (ok_ready ? theme_primary : chrome_key_brd()) : chrome_key_brd();
    const esphome::Color bg  = is_ok && ok_ready ? chrome_card_bg_on() : chrome_key_bg();
    const esphome::Color fg  = (is_ok && !ok_ready) ? col_slate : col_white;
    keypad_button(it, kx, ky, key_w, key_h, kLabels[idx], bg, brd, fg,
                  (is_ok || is_del) ? font_small : font_key, kWids[idx], static_cast<int16_t>(idx));
  }

  render_back_button(it, cx, font_small, layout().back_y, "< Cancel");
  frame_hits().add_centered_rect(cx, layout().back_y, scale_x(150), scale_y(36),
                                 WidgetId::PIN_PAD_CANCEL, kHitTap, 0);
}

// ==============================================================================
// 4. Shared Sub-Pixel SDF Horology Primitives & Split Action Card Row
// ==============================================================================
void draw_aa_line(esphome::display::Display &it, float x0, float y0, float x1, float y1,
                  float width, esphome::Color col, int alpha_max) {
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

void draw_aa_disc(esphome::display::Display &it, float cx, float cy, float r,
                  esphome::Color col, int alpha_max) {
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

void draw_aa_ring(esphome::display::Display &it, float cx, float cy, float r,
                  float thick, esphome::Color col, int alpha_max) {
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

void draw_aa_tapered_hand(esphome::display::Display &it, float cx, float cy, float angle_rad,
                          float tail_len, float tip_len, float root_w, float tip_w,
                          esphome::Color body_col, esphome::Color lume_col,
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

void render_analog_chapter_ring(esphome::display::Display &it, int cx, int cy, int r_dial,
                                esphome::Color cardinal_col, esphome::Color major_col,
                                esphome::Color minor_col, bool double_twelve) {
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

void render_center_arbor_aa(esphome::display::Display &it, int cx, int cy,
                            esphome::Color outer_col, esphome::Color cap_col,
                            esphome::Color jewel_col) {
  const int r_outer = std::max(6, scale_min(8));
  const int r_cap = std::max(4, scale_min(5));
  const int r_jewel = std::max(2, scale_min(2));
  fast_filled_circle_aa(it, cx + 1, cy + 1, r_outer + 1, kChromeShadow);
  fast_filled_circle_aa(it, cx, cy, r_outer, outer_col);
  fast_filled_circle_aa(it, cx, cy, r_cap, cap_col);
  fast_filled_circle_aa(it, cx, cy, r_jewel, jewel_col);
}

void split_action_card_row(esphome::display::Display &it, int cx, int cy, int w, int h,
                           const char *primary_txt, esphome::display::BaseFont *font_pri,
                           esphome::Color pri_col,
                           const char *secondary_txt, esphome::display::BaseFont *font_sec,
                           esphome::Color sec_col,
                           bool card_active, esphome::Color accent_col,
                           bool action_is_toggle, bool action_on,
                           uint16_t left_wid, uint16_t right_wid, int16_t param) {
  using esphome::display::TextAlign;
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
  if (secondary_txt != nullptr && secondary_txt[0] != '\0') {
    const std::string pri_clamped = fit_text_to_width_font_base(primary_txt, max_text_w, font_pri);
    const std::string sec_clamped = fit_text_to_width_font_base(secondary_txt, max_text_w, font_sec);
    fast_print(it, text_x + 1, cy - scale_y(9) + 1, font_pri, kChromeShadow, TextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x, cy - scale_y(9), font_pri, pri_col, TextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x + 1, cy + scale_y(13) + 1, font_sec, kChromeShadow, TextAlign::CENTER_LEFT, sec_clamped.c_str());
    fast_print(it, text_x, cy + scale_y(13), font_sec, sec_col, TextAlign::CENTER_LEFT, sec_clamped.c_str());
  } else {
    const std::string pri_clamped = fit_text_to_width_font_base(
        (primary_txt != nullptr) ? primary_txt : "", max_text_w, font_pri);
    fast_print(it, text_x + 1, cy + 1, font_pri, kChromeShadow, TextAlign::CENTER_LEFT, pri_clamped.c_str());
    fast_print(it, text_x, cy, font_pri, pri_col, TextAlign::CENTER_LEFT, pri_clamped.c_str());
  }

  const int right_w = w - split_w;
  const int right_cx = div_x + right_w / 2;
  if (action_is_toggle) {
    toggle_switch(it, right_cx, cy, action_on, accent_col);
  } else {
    const int btn_r = std::max(12, std::min(h / 2 - 6, scale_s(17)));
    fast_filled_circle_aa(it, right_cx, cy, btn_r,
                          action_on ? accent_col : kChromeTrack);
    fast_circle_aa(it, right_cx, cy, static_cast<float>(btn_r),
                   action_on ? accent_col : kChromeTrackBorder, 1.5f);
    const int ic_px = std::max(14, (btn_r * 13) / 10);
    render_mdi_icon(it, action_on ? "mdi:pause" : "mdi:play", right_cx, cy, ic_px,
                    action_on ? kChromePauseGlyph : kChromePlayGlyph, false);
  }

  frame_hits().add_rect(x0, y0, split_w, h,
                        left_wid, kHitTap | kHitFocusSlot, param);
  frame_hits().add_rect(div_x, y0, right_w, h,
                        right_wid, kHitTap | kHitFocusSlot, param);
}

}  // namespace ui
}  // namespace latter_ui
