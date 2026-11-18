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
                                  GlassVariant variant) {
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
  const GlassBackdrop bd = sample_glass_backdrop(x0, y0, w, h);
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
                               GlassVariant variant, esphome::Color ref_text) {
  if (w <= 4 || h <= 4) return bg_c;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;

  GlassVariant eff_var = variant;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);
  const GlassBackdrop bd = sample_glass_backdrop(x0, y0, w, h);
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
                                   esphome::Color ref_text) {
  if (r <= 1) return bg_c;
  GlassVariant eff_var = variant;
  esphome::Color norm_tint, rim_col;
  normalize_glass_chrome(bg_c, border_c, eff_var, norm_tint, rim_col);

  const int d = 2 * r + 1;
  const GlassBackdrop bd = sample_glass_backdrop(cx_btn - r, cy_btn - r, d, d);
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
                               esphome::Color col_inactive) {
  (void)col_inactive;
  if (!show_up && !show_down) return;
  int mask_x = 0, chev_x = 0, hit_x0 = 0, hit_w = 0, min_hit_h = 0;
  compute_vertical_page_pips_x(SCREEN_W, LATTER_ROUND_DISPLAY != 0, LATTER_PANEL_PPI,
                               mask_x, chev_x, hit_x0, hit_w, min_hit_h);
  const int up_y = scale_y(162);
  const int dn_y = scale_y(278);
  if (!backdrop_active() && mask_x < SCREEN_W) {
    fill_rect_row_major(it, mask_x, up_y - scale_y(22), SCREEN_W - mask_x, (dn_y - up_y) + scale_y(44), col_bg);
  }
  if (show_up) {
    render_scroll_chevron(it, chev_x, up_y, true, col_active, kChromeShadow, backdrop_active());
    const int up_y0 = std::min(scale_y(70), std::max(0, SCREEN_CY - min_hit_h));
    const int up_h = std::max(min_hit_h, SCREEN_CY - up_y0);
    frame_hits().add_rect(hit_x0, up_y0, hit_w, std::min(SCREEN_H - up_y0, up_h),
                          WidgetId::CHEVRON_UP, kHitTap);
  }
  if (show_down) {
    render_scroll_chevron(it, chev_x, dn_y, false, col_active, kChromeShadow, backdrop_active());
    const int dn_h = std::max(min_hit_h, (SCREEN_H - scale_y(70)) - SCREEN_CY);
    frame_hits().add_rect(hit_x0, SCREEN_CY, hit_w, std::min(SCREEN_H - SCREEN_CY, dn_h),
                          WidgetId::CHEVRON_DOWN, kHitTap);
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
  if (title == nullptr || title[0] == '\0' || font == nullptr) return;
  const int y = layout().hdr_y;
  if (shadow || backdrop_active()) {
    fast_print(it, cx + 1, y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, title);
  }
  fast_print(it, cx, y, font, color, esphome::display::TextAlign::CENTER, title);
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
  render_modern_card(it, cx, cy, 296, 54, 14, kChromeBarTrack, kChromeCardBorder, text, text_c, font);
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
  if (action == "unlock" || action == "disarm" || action == "unlock_door" || action == "open_cover") {
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
// 3. Canonical Solid + Negative-Space Custom Glyph Library (D-008 / D-051)
//    Row-major sub-pixel AA CSG rasterizer with zero per-pixel sqrtf/sinf/cosf,
//    true transparent negative-space cutouts, fast_hline interior span batching,
//    and single-pass (+1, +1) kChromeShadow drop shadows.
// ==============================================================================
namespace {

inline float sd_circle_px(float x, float y, float cx0, float cy0, float rad) noexcept {
  if (rad <= 0.05f) return 4.0f;
  const float dx = x - cx0;
  const float dy = y - cy0;
  const float d2 = dx * dx + dy * dy;
  const float ro = rad + 1.25f;
  if (d2 >= ro * ro) return 2.0f;
  if (rad > 1.25f) {
    const float ri = rad - 1.25f;
    if (d2 <= ri * ri) return -2.0f;
    return (d2 - rad * rad) * (0.5f / rad);
  }
  return (d2 - rad * rad) / std::max(0.70f, 1.35f * rad);
}

inline float sd_ring_px(float x, float y, float cx0, float cy0,
                        float r_mid, float half_thick) noexcept {
  const float ht = std::max(0.45f, half_thick);
  const float r_out = r_mid + ht;
  const float r_in = std::max(0.0f, r_mid - ht);
  const float d_out = sd_circle_px(x, y, cx0, cy0, r_out);
  if (r_in <= 0.1f) return d_out;
  return std::max(d_out, -sd_circle_px(x, y, cx0, cy0, r_in));
}

inline float sd_rbox_px(float x, float y, float cx0, float cy0,
                        float hx, float hy, float cr) noexcept {
  if (hx <= 0.05f || hy <= 0.05f) return 4.0f;
  const float r = std::clamp(cr, 0.0f, std::min(hx, hy));
  const float dx = std::fabs(x - cx0) - (hx - r);
  const float dy = std::fabs(y - cy0) - (hy - r);
  if (dx <= 0.0f || dy <= 0.0f) {
    return std::max(dx, dy) - r;
  }
  if (r <= 0.15f) return std::max(dx, dy);
  const float d2 = dx * dx + dy * dy;
  const float ro = r + 1.25f;
  if (d2 >= ro * ro) return 2.0f;
  if (r > 1.25f) {
    const float ri = r - 1.25f;
    if (d2 <= ri * ri) return -2.0f;
    return (d2 - r * r) * (0.5f / r);
  }
  return (d2 - r * r) / std::max(0.70f, 1.35f * r);
}

struct SdfCapsule {
  float ax{0.0f}, ay{0.0f}, vx{0.0f}, vy{0.0f};
  float inv_len2{0.0f}, rad{1.0f}, ro2{4.0f}, ri2{-1.0f}, inv2r{0.5f};

  static SdfCapsule make(float x0, float y0, float x1, float y1, float r_px) noexcept {
    SdfCapsule c{};
    c.ax = x0;
    c.ay = y0;
    c.vx = x1 - x0;
    c.vy = y1 - y0;
    const float l2 = c.vx * c.vx + c.vy * c.vy;
    c.inv_len2 = (l2 > 1e-4f) ? (1.0f / l2) : 0.0f;
    c.rad = std::max(0.40f, r_px);
    const float ro = c.rad + 1.25f;
    c.ro2 = ro * ro;
    if (c.rad > 1.25f) {
      const float ri = c.rad - 1.25f;
      c.ri2 = ri * ri;
      c.inv2r = 0.5f / c.rad;
    } else {
      c.ri2 = -1.0f;
      c.inv2r = 1.0f / std::max(0.70f, 1.35f * c.rad);
    }
    return c;
  }

  inline float eval(float x, float y) const noexcept {
    const float px = x - ax;
    const float py = y - ay;
    const float t = (inv_len2 > 0.0f)
        ? std::clamp((px * vx + py * vy) * inv_len2, 0.0f, 1.0f)
        : 0.0f;
    const float qx = px - t * vx;
    const float qy = py - t * vy;
    const float d2 = qx * qx + qy * qy;
    if (d2 >= ro2) return 2.0f;
    if (d2 <= ri2) return -2.0f;
    return (d2 - rad * rad) * inv2r;
  }
};

struct SdfSymTri {
  float cx0{0.0f}, y_top{0.0f}, y_bot{1.0f}, nx{1.0f}, ny{0.0f};

  static SdfSymTri make(float cx, float y_apex, float y_base, float hw_base) noexcept {
    SdfSymTri t{};
    t.cx0 = cx;
    t.y_top = y_apex;
    t.y_bot = y_base;
    const float h = std::max(0.5f, y_base - y_apex);
    const float hw = std::max(0.5f, hw_base);
    const float inv_len = 1.0f / std::sqrt(h * h + hw * hw);
    t.nx = h * inv_len;
    t.ny = -hw * inv_len;
    return t;
  }

  inline float eval(float x, float y) const noexcept {
    const float ax = std::fabs(x - cx0);
    const float d_side = ax * nx + (y - y_top) * ny;
    return std::max({d_side, y - y_bot, y_top - y});
  }
};

template <typename Fn>
void rasterize_sdf_icon(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow, Fn &&sdf_px) {
  if (r <= 1) return;
  const int bound_r = std::clamp(r + 2, 3, 44);
  const int dx_min = std::max(-bound_r, -cx);
  const int dx_max = std::min(bound_r + (shadow ? 1 : 0), SCREEN_W - 1 - cx);
  if (dx_min > dx_max) return;

  const int dy_min = -bound_r;
  const int dy_max = bound_r + (shadow ? 1 : 0);
  const int y_start = std::max(cy + dy_min, clip_y0());
  const int y_end = std::min(cy + dy_max, clip_y1());
  if (y_start > y_end) return;

  const int span_w = dx_max - dx_min + 1;
  uint8_t prev_cov[96]{};
  uint8_t cur_cov[96]{};

  auto fill_cov_row = [&](int dy, uint8_t *dst) {
    if (dy > bound_r) {
      std::memset(dst, 0, static_cast<size_t>(span_w));
      return;
    }
    const float fy = static_cast<float>(dy);
    for (int i = 0; i < span_w; ++i) {
      const int dx = dx_min + i;
      if (dx > bound_r) {
        dst[i] = 0U;
      } else {
        const float d = sdf_px(static_cast<float>(dx), fy);
        dst[i] = static_cast<uint8_t>(aa_coverage_8(0.5f - d));
      }
    }
  };

  if (shadow && y_start > (cy + dy_min)) {
    fill_cov_row((y_start - 1) - cy, prev_cov);
  }

  for (int py = y_start; py <= y_end; ++py) {
    fill_cov_row(py - cy, cur_cov);
    int i = 0;
    while (i < span_w) {
      const uint8_t fg = cur_cov[i];
      if (fg == 255U) {
        int run_end = i + 1;
        while (run_end < span_w && cur_cov[run_end] == 255U) {
          ++run_end;
        }
        fast_hline(it, cx + dx_min + i, py, run_end - i, col);
        i = run_end;
        continue;
      }
      const int px = cx + dx_min + i;
      if (shadow && i > 0) {
        const int sh = prev_cov[i - 1];
        if (sh > 0) {
          const int sa = (sh * 184) >> 8;
          if (sa > 0) fast_pixel_blend(it, px, py, kChromeShadow, sa);
        }
      }
      if (fg > 0U) {
        fast_pixel_blend(it, px, py, col, static_cast<int>(fg));
      }
      ++i;
    }
    if (shadow) {
      std::memcpy(prev_cov, cur_cov, static_cast<size_t>(span_w));
    }
  }
}

}  // namespace

void draw_icon_power(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, bool shadow) {
  const int eff_r = std::max(6, (r * 62) / 100);
  const float rf = static_cast<float>(eff_r);
  const float ring_cy = 0.10f * rf;
  const float ring_r = 0.76f * rf;
  const float ht = std::max(1.05f, 0.15f * rf);
  // Gap at top (±31 deg around 12 o'clock) with rounded arc end-caps + top stem
  static constexpr float kSin31 = 0.515038f;
  static constexpr float kCos31 = 0.857167f;
  const float cap_x = ring_r * kSin31;
  const float cap_y = ring_cy - ring_r * kCos31;
  const SdfCapsule stem = SdfCapsule::make(0.0f, -0.86f * rf, 0.0f, 0.02f * rf, ht);

  rasterize_sdf_icon(it, cx, cy, eff_r, col, shadow, [&](float x, float y) -> float {
    float d_arc = 4.0f;
    const float ry = y - ring_cy;
    const float ax = std::fabs(x);
    if (!(ry < 0.0f && ax < (-ry * (kSin31 / kCos31)))) {
      d_arc = sd_ring_px(x, y, 0.0f, ring_cy, ring_r, ht);
    }
    d_arc = std::min(d_arc, sd_circle_px(ax, y, cap_x, cap_y, ht));
    return std::min(d_arc, stem.eval(x, y));
  });
}

void draw_icon_clock_solid(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float dial_r = 0.94f * rf;
  const float hw_hour = std::max(0.76f, 0.070f * rf);
  const float hw_min = std::max(0.68f, 0.056f * rf);
  const SdfCapsule h_hand = SdfCapsule::make(0.0f, 0.0f, -0.37f * rf, -0.15f * rf, hw_hour);
  const SdfCapsule m_hand = SdfCapsule::make(0.0f, 0.0f, 0.39f * rf, -0.45f * rf, hw_min);
  const float tick_card_w = std::max(0.76f, 0.068f * rf);
  const SdfCapsule t_vert = SdfCapsule::make(0.0f, 0.60f * rf, 0.0f, 0.77f * rf, tick_card_w);
  const SdfCapsule t_horiz = SdfCapsule::make(0.60f * rf, 0.0f, 0.77f * rf, 0.0f, tick_card_w);
  const float dot_r = std::max(0.84f, 0.066f * rf);
  const float dot_s30 = 0.350000f * rf;   // 0.70 * sin(30 deg)
  const float dot_c30 = 0.606218f * rf;   // 0.70 * cos(30 deg)
  const float arbor_r = std::max(1.22f, 0.132f * rf);
  const float pin_r = std::max(0.75f, 0.056f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_disc = sd_circle_px(x, y, 0.0f, 0.0f, dial_r);
    if (d_disc >= 1.5f) return d_disc;
    float d_cut = std::min({h_hand.eval(x, y), m_hand.eval(x, y),
                            sd_circle_px(x, y, 0.0f, 0.0f, arbor_r)});
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    if (r >= 10) {
      d_cut = std::min({d_cut, t_vert.eval(ax, ay), t_horiz.eval(ax, ay)});
    }
    if (r >= 12) {
      d_cut = std::min({d_cut,
                        sd_circle_px(ax, ay, dot_s30, dot_c30, dot_r),
                        sd_circle_px(ax, ay, dot_c30, dot_s30, dot_r)});
    }
    float res = std::max(d_disc, -d_cut);
    if (r >= 13) {
      res = std::min(res, sd_circle_px(x, y, 0.0f, 0.0f, pin_r));
    }
    return res;
  });
}

void draw_icon_hourglass(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float cap_hx = 0.72f * rf;
  const float cap_hy = std::max(1.0f, 0.095f * rf);
  const float cap_y = 0.86f * rf;
  const float bulb_span = 0.80f * rf;
  const float wall = std::max(1.65f, 0.175f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ay = std::fabs(y);
    const float ax = std::fabs(x);
    const float d_caps = std::min(
        sd_rbox_px(x, y, 0.0f, -cap_y, cap_hx, cap_hy, cap_hy),
        sd_rbox_px(x, y, 0.0f, cap_y, cap_hx, cap_hy, cap_hy));

    float d_bulb = 4.0f;
    if (ay <= bulb_span + 1.5f) {
      const float u = std::clamp(ay / bulb_span, 0.0f, 1.0f);
      const float s = u * u * (3.0f - 2.0f * u);
      const float hw_out = (0.17f + 0.49f * s) * rf;
      const float inv_n = 1.0f / (1.0f + 1.36f * u * (1.0f - u));
      d_bulb = std::max((ax - hw_out) * inv_n, ay - bulb_span);
    }
    const float d_solid = std::min(d_caps, d_bulb);
    if (d_solid >= 1.5f) return d_solid;

    // Smooth upper glass cavity cutout + subtle upper-lower sand air pocket
    float d_cut = 4.0f;
    const float y_top_in = -(bulb_span - std::max(1.2f, 0.08f * rf));
    const float y_neck_in = -0.10f * rf;
    if (y >= y_top_in - 1.5f && y <= y_neck_in + 1.5f) {
      const float u = std::clamp(-y / bulb_span, 0.0f, 1.0f);
      const float s = u * u * (3.0f - 2.0f * u);
      const float hw_in = std::max(0.0f, (0.17f + 0.49f * s) * rf - wall);
      const float inv_n = 1.0f / (1.0f + 1.36f * u * (1.0f - u));
      d_cut = std::max({(ax - hw_in) * inv_n, y_top_in - y, y - y_neck_in});
    }
    if (r >= 11) {
      // Subtle negative-space air crescent above the settled sand mound in lower bulb
      const float y_lo0 = 0.15f * rf;
      const float y_lo1 = 0.36f * rf;
      if (y >= y_lo0 - 1.5f && y <= y_lo1 + 1.5f) {
        const float u = std::clamp(y / bulb_span, 0.0f, 1.0f);
        const float s = u * u * (3.0f - 2.0f * u);
        const float hw_in = std::max(0.0f, (0.17f + 0.49f * s) * rf - wall);
        const float sand_arch = y_lo1 - 0.08f * rf * std::max(0.0f, 1.0f - (ax / std::max(1.0f, hw_in)));
        d_cut = std::min(d_cut, std::max({ax - hw_in, y_lo0 - y, y - sand_arch}));
      }
    }
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_alarm_clock(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float dial_cy = 0.10f * rf;
  const float dial_r = 0.74f * rf;
  const float moat_r = dial_r + std::max(1.35f, 0.13f * rf);
  const float bell_x = 0.56f * rf;
  const float bell_y = -0.54f * rf;
  const float bell_r = 0.34f * rf;
  const float foot_w = std::max(1.05f, 0.11f * rf);
  const SdfCapsule foot_l = SdfCapsule::make(-0.40f * rf, 0.66f * rf, -0.58f * rf, 0.90f * rf, foot_w);
  const SdfCapsule foot_r = SdfCapsule::make(0.40f * rf, 0.66f * rf, 0.58f * rf, 0.90f * rf, foot_w);
  const float hand_w = std::max(1.0f, 0.10f * rf);
  const SdfCapsule h_hand = SdfCapsule::make(0.0f, dial_cy, -0.28f * rf, dial_cy - 0.20f * rf, hand_w * 1.05f);
  const SdfCapsule m_hand = SdfCapsule::make(0.0f, dial_cy, 0.36f * rf, dial_cy - 0.26f * rf, hand_w);
  const float arbor_r = std::max(1.15f, 0.12f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ax = std::fabs(x);
    const float d_dial = sd_circle_px(x, y, 0.0f, dial_cy, dial_r);
    const float d_bells = std::max(
        sd_circle_px(ax, y, bell_x, bell_y, bell_r),
        -sd_circle_px(x, y, 0.0f, dial_cy, moat_r));
    const float d_feet = std::min(foot_l.eval(x, y), foot_r.eval(x, y));
    const float d_solid = std::min({d_dial, d_bells, d_feet});
    if (d_solid >= 1.5f) return d_solid;
    const float d_cut = std::min({h_hand.eval(x, y), m_hand.eval(x, y),
                                  sd_circle_px(x, y, 0.0f, dial_cy, arbor_r)});
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_stopwatch(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float dial_cy = 0.14f * rf;
  const float dial_r = 0.76f * rf;
  const float stem_w = std::max(1.05f, 0.11f * rf);
  const SdfCapsule crown_cap = SdfCapsule::make(-0.24f * rf, -0.86f * rf, 0.24f * rf, -0.86f * rf,
                                                std::max(1.0f, 0.095f * rf));
  const SdfCapsule crown_stem = SdfCapsule::make(0.0f, -0.86f * rf, 0.0f, -0.58f * rf, stem_w);
  const SdfCapsule pusher = SdfCapsule::make(0.50f * rf, -0.36f * rf, 0.72f * rf, -0.58f * rf, stem_w);
  const float hand_w = std::max(1.0f, 0.10f * rf);
  const SdfCapsule sweep = SdfCapsule::make(0.0f, dial_cy, 0.36f * rf, dial_cy - 0.36f * rf, hand_w);
  const float arbor_r = std::max(1.15f, 0.125f * rf);
  const float tick_r = std::max(0.80f, 0.07f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_dial = sd_circle_px(x, y, 0.0f, dial_cy, dial_r);
    const float d_solid = std::min({d_dial, crown_cap.eval(x, y),
                                    crown_stem.eval(x, y), pusher.eval(x, y)});
    if (d_solid >= 1.5f) return d_solid;
    float d_cut = std::min(sweep.eval(x, y), sd_circle_px(x, y, 0.0f, dial_cy, arbor_r));
    if (r >= 11) {
      d_cut = std::min({d_cut,
                        sd_circle_px(x, y, 0.0f, dial_cy - 0.55f * rf, tick_r),
                        sd_circle_px(x, y, -0.55f * rf, dial_cy, tick_r),
                        sd_circle_px(x, y, 0.0f, dial_cy + 0.55f * rf, tick_r)});
    }
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_shield_home(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float sh_hw = 0.84f * rf;
  const float y_top = -0.94f * rf;
  const float y_sh = -0.74f * rf;
  const float y_mid = -0.14f * rf;
  const float y_tip = 0.96f * rf;
  const SdfSymTri roof = SdfSymTri::make(0.0f, -0.50f * rf, -0.06f * rf, 0.48f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ax = std::fabs(x);
    float hw = sh_hw;
    float inv_n = 1.0f;
    if (y > y_mid) {
      const float u = std::clamp((y - y_mid) / (y_tip - y_mid), 0.0f, 1.0f);
      hw = sh_hw * (1.0f - 0.20f * u - 0.80f * u * u);
      inv_n = 1.0f / (1.0f + 0.36f * u * u);
    }
    const float y_crest = y_top + (y_sh - y_top) * std::min(1.0f, ax / sh_hw);
    const float d_shield = std::max({(ax - hw) * inv_n, (y_crest - y) * 0.97f, y - y_tip});
    if (d_shield >= 1.5f) return d_shield;

    // Centered negative-space house cutout (pitched roof + walls, with subtle solid doorway arch)
    const float d_walls = sd_rbox_px(x, y, 0.0f, 0.15f * rf, 0.33f * rf, 0.23f * rf, 0.05f * rf);
    float d_house = std::min(roof.eval(x, y), d_walls);
    if (r >= 12) {
      const float d_door = sd_rbox_px(x, y, 0.0f, 0.28f * rf, 0.105f * rf, 0.14f * rf, 0.09f * rf);
      d_house = std::max(d_house, -d_door);
    }
    return std::max(d_shield, -d_house);
  });
}

void draw_icon_camera(esphome::display::Display &it, int cx, int cy, int r,
                      esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float lens_cy = 0.14f * rf;
  const float moat_r = 0.42f * rf;
  const float pupil_r = 0.21f * rf;
  const float flash_r = std::max(0.90f, 0.085f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_body = sd_rbox_px(x, y, 0.0f, 0.14f * rf, 0.92f * rf, 0.64f * rf, 0.20f * rf);
    const float d_hump = sd_rbox_px(x, y, 0.0f, -0.52f * rf, 0.40f * rf, 0.24f * rf, 0.12f * rf);
    const float d_solid = std::min(d_body, d_hump);
    if (d_solid >= 1.5f) return d_solid;
    const float d_lens_moat = std::max(
        sd_circle_px(x, y, 0.0f, lens_cy, moat_r),
        -sd_circle_px(x, y, 0.0f, lens_cy, pupil_r));
    float d_cut = d_lens_moat;
    if (r >= 11) {
      d_cut = std::min(d_cut, sd_circle_px(x, y, 0.62f * rf, -0.24f * rf, flash_r));
    }
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_speaker(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool waves, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float shift_x = waves ? 0.0f : (0.24f * rf);
  const float arc_cx = -0.06f * rf + shift_x;
  const float r1 = 0.46f * rf;
  const float r2 = 0.80f * rf;
  const float w_ht = std::max(0.95f, 0.095f * rf);
  static constexpr float kSin40 = 0.642788f;
  static constexpr float kCos40 = 0.766044f;

  // Flared cone normal from (-0.48, 0.30) to (-0.04, 0.74): dx=0.44, dy=0.44 (45 deg)
  static constexpr float kInvSqrt2 = 0.707107f;

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ay = std::fabs(y);
    const float lx = x - shift_x;
    const float d_box = sd_rbox_px(lx, y, -0.68f * rf, 0.0f, 0.20f * rf, 0.32f * rf, 0.08f * rf);
    const float d_cone_slope = ((ay - 0.30f * rf) - (lx + 0.50f * rf)) * kInvSqrt2;
    const float d_cone = std::max({d_cone_slope, -0.52f * rf - lx, lx - (-0.04f * rf), ay - 0.76f * rf});
    float d = std::min(d_box, d_cone);

    if (waves) {
      const float wx = x - arc_cx;
      auto eval_wave = [&](float rw) -> float {
        float dw = 4.0f;
        if (wx > 0.0f && ay <= wx * (kSin40 / kCos40)) {
          dw = sd_ring_px(x, y, arc_cx, 0.0f, rw, w_ht);
        }
        return std::min(dw, sd_circle_px(x, ay, arc_cx + rw * kCos40, rw * kSin40, w_ht));
      };
      d = std::min({d, eval_wave(r1), eval_wave(r2)});
    }
    return d;
  });
}

void draw_icon_thermometer(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float tx = (r >= 10) ? (-0.10f * rf) : 0.0f;
  const float stem_w = std::max(1.8f, 0.25f * rf);
  const float bulb_cy = 0.48f * rf;
  const float bulb_r = 0.46f * rf;
  const SdfCapsule stem = SdfCapsule::make(tx, -0.72f * rf, tx, bulb_cy, stem_w);
  const float tick_h = std::max(0.85f, 0.075f * rf);
  const SdfCapsule tk0 = SdfCapsule::make(tx + stem_w + 0.14f * rf, -0.68f * rf,
                                          tx + stem_w + 0.40f * rf, -0.68f * rf, tick_h);
  const SdfCapsule tk1 = SdfCapsule::make(tx + stem_w + 0.14f * rf, -0.38f * rf,
                                          tx + stem_w + 0.32f * rf, -0.38f * rf, tick_h);
  const SdfCapsule tk2 = SdfCapsule::make(tx + stem_w + 0.14f * rf, -0.08f * rf,
                                          tx + stem_w + 0.40f * rf, -0.08f * rf, tick_h);
  const SdfCapsule slit = SdfCapsule::make(tx, -0.68f * rf, tx, -0.10f * rf,
                                           std::max(0.80f, 0.085f * rf));
  const float inner_r = std::max(1.1f, 0.18f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    float d_solid = std::min(stem.eval(x, y), sd_circle_px(x, y, tx, bulb_cy, bulb_r));
    if (r >= 10) {
      d_solid = std::min({d_solid, tk0.eval(x, y), tk1.eval(x, y), tk2.eval(x, y)});
    }
    if (d_solid >= 1.5f) return d_solid;
    const float d_cut = std::min(slit.eval(x, y), sd_circle_px(x, y, tx, bulb_cy, inner_r));
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_lock(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool is_locked, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float body_cy = 0.40f * rf;
  const float body_hx = 0.72f * rf;
  const float body_hy = 0.50f * rf;
  const float sh_cx = is_locked ? 0.0f : (0.28f * rf);
  const float sh_cy = is_locked ? (-0.18f * rf) : (-0.30f * rf);
  const float sh_r = 0.36f * rf;
  const float sh_ht = std::max(1.15f, 0.125f * rf);
  const float l_leg_end = is_locked ? 0.02f * rf : -0.18f * rf;
  const SdfCapsule leg_l = SdfCapsule::make(sh_cx - sh_r, sh_cy, sh_cx - sh_r, l_leg_end, sh_ht);
  const SdfCapsule leg_r = SdfCapsule::make(sh_cx + sh_r, sh_cy, sh_cx + sh_r, 0.04f * rf, sh_ht);
  const SdfCapsule kh_slot = SdfCapsule::make(0.0f, 0.34f * rf, 0.0f, 0.62f * rf,
                                              std::max(0.90f, 0.09f * rf));
  const float kh_r = std::max(1.25f, 0.155f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_body = sd_rbox_px(x, y, 0.0f, body_cy, body_hx, body_hy, 0.20f * rf);
    float d_arch = 4.0f;
    if (y <= sh_cy + 0.5f) {
      d_arch = sd_ring_px(x, y, sh_cx, sh_cy, sh_r, sh_ht);
    }
    const float d_solid = std::min({d_body, d_arch, leg_l.eval(x, y), leg_r.eval(x, y)});
    if (d_solid >= 1.5f) return d_solid;
    const float d_cut = std::min(sd_circle_px(x, y, 0.0f, 0.32f * rf, kh_r), kh_slot.eval(x, y));
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_garage_door(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool is_open, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const SdfSymTri roof = SdfSymTri::make(0.0f, -0.92f * rf, -0.26f * rf, 0.96f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_walls = sd_rbox_px(x, y, 0.0f, 0.30f * rf, 0.84f * rf, 0.58f * rf, 0.08f * rf);
    const float d_facade = std::min(roof.eval(x, y), d_walls);
    if (d_facade >= 1.5f) return d_facade;

    const float d_bay = sd_rbox_px(x, y, 0.0f, 0.38f * rf, 0.58f * rf, 0.52f * rf, 0.04f * rf);
    float d_slats = 4.0f;
    const float slat_hx = 0.48f * rf;
    const float slat_hy = std::max(0.85f, 0.085f * rf);
    if (is_open) {
      d_slats = sd_rbox_px(x, y, 0.0f, 0.02f * rf, slat_hx, slat_hy * 1.15f, 0.5f);
    } else {
      d_slats = std::min({
          sd_rbox_px(x, y, 0.0f, 0.06f * rf, slat_hx, slat_hy, 0.5f),
          sd_rbox_px(x, y, 0.0f, 0.36f * rf, slat_hx, slat_hy, 0.5f),
          sd_rbox_px(x, y, 0.0f, 0.66f * rf, slat_hx, slat_hy, 0.5f)});
    }
    const float d_cut = std::max(d_bay, -d_slats);
    return std::max(d_facade, -d_cut);
  });
}

void draw_icon_robot_vacuum(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float body_r = 0.94f * rf;
  const float bump_r = 0.72f * rf;
  const float bump_ht = std::max(0.85f, 0.075f * rf);
  const float turret_moat = 0.36f * rf;
  const float turret_cap = 0.18f * rf;
  const SdfCapsule dock_slit = SdfCapsule::make(-0.24f * rf, 0.66f * rf, 0.24f * rf, 0.66f * rf,
                                                std::max(0.75f, 0.065f * rf));

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_body = sd_circle_px(x, y, 0.0f, 0.0f, body_r);
    if (d_body >= 1.5f) return d_body;

    float d_bumper = 4.0f;
    if (y < -0.18f * rf && std::fabs(x) < 0.66f * rf) {
      d_bumper = sd_ring_px(x, y, 0.0f, 0.0f, bump_r, bump_ht);
    }
    const float d_lidar = std::max(
        sd_circle_px(x, y, 0.0f, 0.06f * rf, turret_moat),
        -sd_circle_px(x, y, 0.0f, 0.06f * rf, turret_cap));
    float d_cut = std::min(d_bumper, d_lidar);
    if (r >= 12) {
      d_cut = std::min(d_cut, dock_slit.eval(x, y));
    }
    return std::max(d_body, -d_cut);
  });
}

void draw_icon_eighth_notes(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float head_r = std::max(2.2f, 0.29f * rf);
  const float lx = -0.44f * rf;
  const float ly = 0.56f * rf;
  const float rx = 0.38f * rf;
  const float ry = 0.38f * rf;
  const float stem_w = std::max(1.0f, 0.105f * rf);
  const float sx_l = lx + head_r - stem_w * 0.6f;
  const float sy_l = -0.64f * rf;
  const float sx_r = rx + head_r - stem_w * 0.6f;
  const float sy_r = -0.84f * rf;
  const SdfCapsule stem_l = SdfCapsule::make(sx_l, sy_l, sx_l, ly, stem_w);
  const SdfCapsule stem_r = SdfCapsule::make(sx_r, sy_r, sx_r, ry, stem_w);
  const SdfCapsule beam_hi = SdfCapsule::make(sx_l, sy_l, sx_r, sy_r, std::max(1.2f, 0.135f * rf));

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    return std::min({sd_circle_px(x, y, lx, ly, head_r),
                     sd_circle_px(x, y, rx, ry, head_r),
                     stem_l.eval(x, y), stem_r.eval(x, y),
                     beam_hi.eval(x, y)});
  });
}

void draw_icon_calendar(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float tab_w = std::max(1.0f, 0.11f * rf);
  const SdfCapsule ring_l = SdfCapsule::make(-0.44f * rf, -0.92f * rf, -0.44f * rf, -0.58f * rf, tab_w);
  const SdfCapsule ring_r = SdfCapsule::make(0.44f * rf, -0.92f * rf, 0.44f * rf, -0.58f * rf, tab_w);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_page = sd_rbox_px(x, y, 0.0f, 0.12f * rf, 0.86f * rf, 0.78f * rf, 0.18f * rf);
    const float d_solid = std::min({d_page, ring_l.eval(x, y), ring_r.eval(x, y)});
    if (d_solid >= 1.5f) return d_solid;

    const float seam_h = std::max(0.80f, 0.075f * rf);
    float d_cut = sd_rbox_px(x, y, 0.0f, -0.26f * rf, 0.90f * rf, seam_h, 0.0f);
    const float pip_s = std::max(0.95f, 0.115f * rf);
    for (int rr = 0; rr < 2; ++rr) {
      const float py = (0.08f + static_cast<float>(rr) * 0.40f) * rf;
      for (int cc = -1; cc <= 1; ++cc) {
        const float px = static_cast<float>(cc) * 0.42f * rf;
        d_cut = std::min(d_cut, sd_rbox_px(x, y, px, py, pip_s, pip_s, 0.35f * pip_s));
      }
    }
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_cal_agenda(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float bar_hy = std::max(1.05f, 0.135f * rf);
  const float dot_r = std::max(1.15f, 0.145f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    float d = 4.0f;
    for (int row = -1; row <= 1; ++row) {
      const float ry = static_cast<float>(row) * 0.54f * rf;
      const float bar_hx = (row == 1) ? (0.44f * rf) : (0.56f * rf);
      const float bar_cx = (row == 1) ? (0.10f * rf) : (0.22f * rf);
      d = std::min({d,
                    sd_circle_px(x, y, -0.68f * rf, ry, dot_r),
                    sd_rbox_px(x, y, bar_cx, ry, bar_hx, bar_hy, bar_hy)});
    }
    return d;
  });
}

void draw_icon_cal_day(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float tab_w = std::max(1.0f, 0.11f * rf);
  const SdfCapsule ring_l = SdfCapsule::make(-0.44f * rf, -0.92f * rf, -0.44f * rf, -0.58f * rf, tab_w);
  const SdfCapsule ring_r = SdfCapsule::make(0.44f * rf, -0.92f * rf, 0.44f * rf, -0.58f * rf, tab_w);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_page = sd_rbox_px(x, y, 0.0f, 0.12f * rf, 0.86f * rf, 0.78f * rf, 0.18f * rf);
    const float d_solid = std::min({d_page, ring_l.eval(x, y), ring_r.eval(x, y)});
    if (d_solid >= 1.5f) return d_solid;

    const float seam_h = std::max(0.80f, 0.075f * rf);
    const float d_seam = sd_rbox_px(x, y, 0.0f, -0.26f * rf, 0.90f * rf, seam_h, 0.0f);
    const float d_focus = sd_rbox_px(x, y, 0.0f, 0.14f * rf, 0.50f * rf, 0.14f * rf, 0.08f * rf);
    const float d_sub = sd_rbox_px(x, y, -0.12f * rf, 0.48f * rf, 0.38f * rf, 0.10f * rf, 0.06f * rf);
    return std::max(d_solid, -std::min({d_seam, d_focus, d_sub}));
  });
}

void draw_icon_cal_week(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float tab_w = std::max(1.0f, 0.11f * rf);
  const SdfCapsule ring_l = SdfCapsule::make(-0.44f * rf, -0.92f * rf, -0.44f * rf, -0.58f * rf, tab_w);
  const SdfCapsule ring_r = SdfCapsule::make(0.44f * rf, -0.92f * rf, 0.44f * rf, -0.58f * rf, tab_w);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_page = sd_rbox_px(x, y, 0.0f, 0.12f * rf, 0.86f * rf, 0.78f * rf, 0.18f * rf);
    const float d_solid = std::min({d_page, ring_l.eval(x, y), ring_r.eval(x, y)});
    if (d_solid >= 1.5f) return d_solid;

    const float seam_h = std::max(0.80f, 0.075f * rf);
    float d_cut = sd_rbox_px(x, y, 0.0f, -0.26f * rf, 0.90f * rf, seam_h, 0.0f);
    const float col_hx = std::max(0.90f, 0.12f * rf);
    for (int cc = -1; cc <= 1; ++cc) {
      const float px = static_cast<float>(cc) * 0.42f * rf;
      d_cut = std::min(d_cut, sd_rbox_px(x, y, px, 0.28f * rf, col_hx, 0.36f * rf, 0.06f * rf));
    }
    return std::max(d_solid, -d_cut);
  });
}

void draw_icon_mic(esphome::display::Display &it, int cx, int cy, int r,
                   esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float stroke = std::max(0.95f, 0.105f * rf);
  const SdfCapsule stem = SdfCapsule::make(0.0f, 0.44f * rf, 0.0f, 0.82f * rf, stroke);
  const SdfCapsule base = SdfCapsule::make(-0.34f * rf, 0.82f * rf, 0.34f * rf, 0.82f * rf, stroke);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_cap = sd_rbox_px(x, y, 0.0f, -0.22f * rf, 0.30f * rf, 0.56f * rf, 0.30f * rf);
    float d_arc = 4.0f;
    if (y >= -0.18f * rf) {
      d_arc = sd_ring_px(x, y, 0.0f, -0.12f * rf, 0.56f * rf, stroke);
    }
    return std::min({d_cap, d_arc, stem.eval(x, y), base.eval(x, y)});
  });
}

void draw_icon_lightbulb(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float dome_cy = -0.24f * rf;
  const float dome_r = 0.64f * rf;
  const float seam_h = std::max(0.80f, 0.07f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_dome = sd_circle_px(x, y, 0.0f, dome_cy, dome_r);
    const float d_neck = sd_rbox_px(x, y, 0.0f, 0.26f * rf, 0.38f * rf, 0.24f * rf, 0.08f * rf);
    const float d_base = sd_rbox_px(x, y, 0.0f, 0.72f * rf, 0.30f * rf, 0.16f * rf, 0.08f * rf);
    const float d_solid = std::min({d_dome, d_neck, d_base});
    if (d_solid >= 1.5f) return d_solid;
    const float d_seam = sd_rbox_px(x, y, 0.0f, 0.50f * rf, 0.50f * rf, seam_h, 0.0f);
    return std::max(d_solid, -d_seam);
  });
}

void draw_icon_ceiling_fan(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float hub_r = 0.24f * rf;
  const float pin_r = std::max(0.75f, 0.075f * rf);
  const float moat_r = 0.34f * rf;

  auto sd_blade = [&](float u, float v) -> float {
    // Swept symmetric paddle blade extending along +v from moat_r to 0.96*rf
    const float tip_cy = 0.72f * rf;
    const float tip_rx = 0.23f * rf;
    const float tip_ry = 0.24f * rf;
    const float dx_e = (u - 0.08f * rf) / tip_rx;
    const float dy_e = (v - tip_cy) / tip_ry;
    const float d_tip = (dx_e * dx_e + dy_e * dy_e - 1.0f) * (0.5f * std::min(tip_rx, tip_ry));
    float d_arm = 4.0f;
    if (v >= 0.28f * rf && v <= tip_cy + 1.0f) {
      const float t = std::clamp((v - 0.30f * rf) / (tip_cy - 0.30f * rf), 0.0f, 1.0f);
      const float u_lo = -0.11f * rf;
      const float u_hi = (0.11f + 0.18f * t) * rf;
      d_arm = std::max({u_lo - u, u - u_hi, 0.30f * rf - v, v - tip_cy});
    }
    return std::min(d_tip, d_arm);
  };

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_hub = std::max(sd_circle_px(x, y, 0.0f, 0.0f, hub_r),
                                 -sd_circle_px(x, y, 0.0f, 0.0f, pin_r));
    const float d_blades = std::max(
        std::min({sd_blade(x, -y), sd_blade(y, x), sd_blade(-x, y), sd_blade(-y, -x)}),
        -sd_circle_px(x, y, 0.0f, 0.0f, moat_r));
    return std::min(d_hub, d_blades);
  });
}

void draw_icon_curtains(esphome::display::Display &it, int cx, int cy, int r,
                        esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float rod_hy = std::max(0.95f, 0.095f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ax = std::fabs(x);
    const float d_rod = sd_rbox_px(x, y, 0.0f, -0.82f * rf, 0.94f * rf, rod_hy, rod_hy);
    // Twin sculpted curtain panels sweeping outward toward tie-back waist at y = +0.20*rf
    float d_panel = 4.0f;
    if (y >= -0.68f * rf - 1.5f && y <= 0.88f * rf + 1.5f) {
      const float u = std::clamp((y - (-0.68f * rf)) / (1.56f * rf), 0.0f, 1.0f);
      const float inner_x = (0.08f + 0.36f * std::min(1.0f, u * 1.45f)) * rf;
      const float d_drape = std::max({ax - 0.84f * rf, inner_x - ax,
                                      -0.68f * rf - y, y - 0.88f * rf});
      const float seam_h = std::max(0.75f, 0.06f * rf);
      const float d_tie = (r >= 11)
          ? sd_rbox_px(ax, y, 0.64f * rf, 0.24f * rf, 0.26f * rf, seam_h, 0.0f)
          : 4.0f;
      d_panel = std::max(d_drape, -d_tie);
    }
    return std::min(d_rod, d_panel);
  });
}

void draw_icon_blinds_slats(esphome::display::Display &it, int cx, int cy, int r,
                            esphome::Color col, bool is_open, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float cord_x = 0.68f * rf;
  const SdfCapsule cord = SdfCapsule::make(cord_x, -0.72f * rf, cord_x, 0.56f * rf,
                                           std::max(0.75f, 0.065f * rf));

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    float d = sd_rbox_px(x, y, 0.0f, -0.76f * rf, 0.88f * rf,
                         std::max(1.1f, 0.12f * rf), 0.08f * rf);
    const int n_slats = is_open ? 2 : 4;
    const float slat_hy = std::max(0.90f, 0.085f * rf);
    for (int s = 0; s < n_slats; ++s) {
      const float sy = (-0.36f + static_cast<float>(s) * 0.36f) * rf;
      d = std::min(d, sd_rbox_px(x, y, -0.08f * rf, sy, 0.66f * rf, slat_hy, slat_hy));
    }
    d = std::min({d, cord.eval(x, y), sd_circle_px(x, y, cord_x, 0.64f * rf, std::max(1.2f, 0.13f * rf))});
    return d;
  });
}

void draw_icon_switch_pill(esphome::display::Display &it, int cx, int cy, int r,
                           esphome::Color col, bool is_on, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float pill_hx = 0.92f * rf;
  const float pill_hy = 0.54f * rf;
  const float thumb_cx = (is_on ? 0.38f : -0.38f) * rf;
  const float thumb_r = 0.34f * rf;

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_pill = sd_rbox_px(x, y, 0.0f, 0.0f, pill_hx, pill_hy, pill_hy);
    if (d_pill >= 1.5f) return d_pill;
    return std::max(d_pill, -sd_circle_px(x, y, thumb_cx, 0.0f, thumb_r));
  });
}

void draw_icon_sparkle(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float ray_w = std::max(0.85f, 0.08f * rf);
  const SdfCapsule d1 = SdfCapsule::make(-0.52f * rf, -0.52f * rf, 0.52f * rf, 0.52f * rf, ray_w);
  const SdfCapsule d2 = SdfCapsule::make(-0.52f * rf, 0.52f * rf, 0.52f * rf, -0.52f * rf, ray_w);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    // Sculpted 4-point concave diamond star + 4 subtle diagonal rays
    const float u_x = std::clamp(ax / (0.94f * rf), 0.0f, 1.0f);
    const float u_y = std::clamp(ay / (0.94f * rf), 0.0f, 1.0f);
    const float w_h = 0.24f * rf * (1.0f - u_x) * (1.0f - u_x) + std::max(0.75f, 0.06f * rf) * (1.0f - u_x);
    const float w_v = 0.24f * rf * (1.0f - u_y) * (1.0f - u_y) + std::max(0.75f, 0.06f * rf) * (1.0f - u_y);
    const float star_h = std::max(ay - w_h, ax - 0.94f * rf);
    const float star_v = std::max(ax - w_v, ay - 0.94f * rf);
    return std::min({star_h, star_v, d1.eval(x, y), d2.eval(x, y),
                     sd_circle_px(x, y, 0.0f, 0.0f, 0.24f * rf)});
  });
}

void draw_icon_bento_grid(esphome::display::Display &it, int cx, int cy, int r,
                          esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float off = 0.44f * rf;
  const float th = 0.34f * rf;
  const float cr = 0.12f * rf;
  const float hole = std::max(1.0f, 0.14f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    float d = sd_rbox_px(ax, ay, off, off, th, th, cr);
    if (x > 0.0f && y > 0.0f && r >= 10) {
      d = std::max(d, -sd_rbox_px(x, y, off, off, hole, hole, 0.04f * rf));
    }
    return d;
  });
}

void draw_icon_droplet(esphome::display::Display &it, int cx, int cy, int r,
                       esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(5, r));
  const float bulb_cy = 0.24f * rf;
  const float bulb_r = 0.62f * rf;
  const SdfSymTri cone = SdfSymTri::make(0.0f, -0.92f * rf, bulb_cy + 0.06f * rf, bulb_r * 0.98f);
  const SdfCapsule glint = SdfCapsule::make(-0.26f * rf, 0.14f * rf, -0.14f * rf, 0.42f * rf,
                                            std::max(0.80f, 0.085f * rf));

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_solid = std::min(sd_circle_px(x, y, 0.0f, bulb_cy, bulb_r), cone.eval(x, y));
    if (d_solid >= 1.5f || r < 8) return d_solid;
    return std::max(d_solid, -glint.eval(x, y));
  });
}

void draw_icon_intercom_handset(esphome::display::Display &it, int cx, int cy, int r,
                                esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const SdfCapsule spine = SdfCapsule::make(-0.54f * rf, -0.34f * rf, 0.34f * rf, 0.54f * rf,
                                            std::max(1.25f, 0.17f * rf));
  const SdfCapsule ear = SdfCapsule::make(-0.66f * rf, -0.62f * rf, -0.36f * rf, -0.32f * rf,
                                          std::max(1.5f, 0.22f * rf));
  const SdfCapsule mic = SdfCapsule::make(0.32f * rf, 0.36f * rf, 0.62f * rf, 0.66f * rf,
                                          std::max(1.5f, 0.22f * rf));
  const float w_ht = std::max(0.85f, 0.085f * rf);
  const float w_cx = 0.04f * rf;
  const float w_cy = -0.04f * rf;

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    float d = std::min({spine.eval(x, y), ear.eval(x, y), mic.eval(x, y)});
    if (x > w_cx + 0.04f * rf && y < w_cy - 0.04f * rf) {
      d = std::min({d,
                    sd_ring_px(x, y, w_cx, w_cy, 0.44f * rf, w_ht),
                    sd_ring_px(x, y, w_cx, w_cy, 0.74f * rf, w_ht)});
    }
    return d;
  });
}

void draw_icon_bell(esphome::display::Display &it, int cx, int cy, int r,
                    esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const float dome_r = 0.56f * rf;
  const float dome_cy = -0.18f * rf;

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_knob = sd_circle_px(x, y, 0.0f, -0.82f * rf, 0.14f * rf);
    const float d_dome = sd_circle_px(x, y, 0.0f, dome_cy, dome_r);
    const float d_skirt = sd_rbox_px(x, y, 0.0f, 0.12f * rf, dome_r, 0.30f * rf, 0.06f * rf);
    const float d_brim = sd_rbox_px(x, y, 0.0f, 0.44f * rf, 0.78f * rf,
                                    std::max(1.0f, 0.11f * rf), 0.11f * rf);
    const float d_clap = std::max(
        sd_circle_px(x, y, 0.0f, 0.68f * rf, 0.20f * rf),
        -sd_rbox_px(x, y, 0.0f, 0.48f * rf, 0.50f * rf, std::max(1.0f, 0.11f * rf), 0.0f));
    return std::min({d_knob, d_dome, d_skirt, d_brim, d_clap});
  });
}

void draw_icon_thumbtack(esphome::display::Display &it, int cx, int cy, int r,
                         esphome::Color col, bool shadow) {
  const float rf = static_cast<float>(std::max(6, r));
  const SdfSymTri body_cone = SdfSymTri::make(0.0f, -1.20f * rf, 0.16f * rf, 0.80f * rf);
  const SdfCapsule pin_stem = SdfCapsule::make(0.0f, 0.22f * rf, 0.0f, 0.86f * rf,
                                               std::max(0.85f, 0.095f * rf));
  const float seam_hy = std::max(0.75f, 0.065f * rf);

  rasterize_sdf_icon(it, cx, cy, r, col, shadow, [&](float x, float y) -> float {
    const float d_cap = sd_rbox_px(x, y, 0.0f, -0.68f * rf, 0.52f * rf,
                                   std::max(1.05f, 0.13f * rf), 0.12f * rf);
    const float d_neck = sd_rbox_px(x, y, 0.0f, -0.34f * rf, 0.28f * rf, 0.22f * rf, 0.04f * rf);
    const float d_flare = std::max(body_cone.eval(x, y), -0.44f * rf - y);
    const float d_flange = sd_rbox_px(x, y, 0.0f, 0.14f * rf, 0.66f * rf,
                                      std::max(1.05f, 0.12f * rf), 0.12f * rf);
    const float d_head = std::max(
        std::min({d_cap, d_neck, d_flare, d_flange}),
        -sd_rbox_px(x, y, 0.0f, -0.50f * rf, 0.42f * rf, seam_hy, 0.0f));
    return std::min(d_head, pin_stem.eval(x, y));
  });
}

void draw_route_icon(esphome::display::Display &it, RouteId route, int cx, int cy, int r,
                     esphome::Color col, esphome::Color cut_c, bool shadow) {
  (void)cut_c;
  const int mdi_px = std::max(16, 2 * r);
  switch (route) {
    case RouteId::APP_PINNED:
      draw_icon_thumbtack(it, cx, cy, r, col, shadow);
      return;
    case RouteId::APP_MUSIC:
    case RouteId::MUSIC_LIBRARY:
      draw_icon_eighth_notes(it, cx, cy, r, col, shadow);
      return;
    case RouteId::MUSIC_SPEAKERS:
      draw_icon_speaker(it, cx, cy, r, col, true, shadow);
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
      draw_icon_shield_home(it, cx, cy, r, col, shadow);
      return;
    case RouteId::HOME_HUB_CAMERAS:
      draw_icon_camera(it, cx, cy, r, col, shadow);
      return;
    case RouteId::HOME_HUB_INTERCOM:
      draw_icon_intercom_handset(it, cx, cy, r, col, shadow);
      return;
    case RouteId::HOME_HUB_THERMOSTAT:
    case RouteId::AREA_CLIMATE:
      draw_icon_thermometer(it, cx, cy, r, col, shadow);
      return;
    case RouteId::HOME_HUB_ENTRY:
      draw_icon_lock(it, cx, cy, r, col, true, shadow);
      return;
    case RouteId::HOME_HUB_VACUUM:
      draw_icon_robot_vacuum(it, cx, cy, r, col, shadow);
      return;
    case RouteId::APP_AREA:
      render_mdi_icon(it, "mdi:sofa", cx, cy, mdi_px, col, shadow, "mdi:home-outline");
      return;
    case RouteId::AREA_PICKER:
      render_mdi_icon(it, "mdi:floor-plan", cx, cy, mdi_px, col, shadow, "mdi:map-marker-outline");
      return;
    case RouteId::AREA_SCENE:
      draw_icon_sparkle(it, cx, cy, r, col, shadow);
      return;
    case RouteId::AREA_LIGHT:
      draw_icon_lightbulb(it, cx, cy, r, col, shadow);
      return;
    case RouteId::AREA_COVER:
      draw_icon_blinds_slats(it, cx, cy, r, col, false, shadow);
      return;
    case RouteId::AREA_FAN:
      draw_icon_ceiling_fan(it, cx, cy, r, col, shadow);
      return;
    case RouteId::AREA_SWITCH:
      draw_icon_switch_pill(it, cx, cy, r, col, true, shadow);
      return;
    case RouteId::AREA_MEDIA:
      render_mdi_icon(it, "mdi:television-speaker", cx, cy, mdi_px, col, shadow, "mdi:speaker");
      return;
    case RouteId::AREA_ALL:
      draw_icon_bento_grid(it, cx, cy, r, col, shadow);
      return;
    case RouteId::APP_REMOTE:
    case RouteId::REMOTE_DEVICES:
      render_mdi_icon(it, "mdi:remote-tv", cx, cy, mdi_px, col, shadow, "mdi:remote");
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
      draw_icon_clock_solid(it, cx, cy, r, col, shadow);
      return;
    case RouteId::TIME_TIMERS:
      draw_icon_hourglass(it, cx, cy, r, col, shadow);
      return;
    case RouteId::TIME_ALARMS:
      draw_icon_alarm_clock(it, cx, cy, r, col, shadow);
      return;
    case RouteId::TIME_STOPWATCH:
    case RouteId::TOOLS_STOPWATCH:
      draw_icon_stopwatch(it, cx, cy, r, col, shadow);
      return;
    case RouteId::APP_TOOLS:
      draw_icon_bento_grid(it, cx, cy, r, col, shadow);
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
    case RouteId::CALENDAR_MONTH:
      draw_icon_calendar(it, cx, cy, r, col, shadow);
      return;
    case RouteId::CALENDAR_OVERVIEW:
      draw_icon_cal_agenda(it, cx, cy, r, col, shadow);
      return;
    case RouteId::CALENDAR_DAY:
      draw_icon_cal_day(it, cx, cy, r, col, shadow);
      return;
    case RouteId::CALENDAR_WEEK:
      draw_icon_cal_week(it, cx, cy, r, col, shadow);
      return;
    case RouteId::TOOLS_DICE:
      render_mdi_icon(it, "mdi:dice-5", cx, cy, mdi_px, col, shadow, "mdi:dice-multiple");
      return;
    case RouteId::TOOLS_COIN_FLIP:
      render_mdi_icon(it, "mdi:alpha-c-circle", cx, cy, mdi_px, col, shadow, "mdi:circle-double");
      return;
    case RouteId::APP_GAMES:
      render_mdi_icon(it, "mdi:gamepad-variant", cx, cy, mdi_px, col, shadow, "mdi:controller");
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
      render_mdi_icon(it, "mdi:cog", cx, cy, mdi_px, col, shadow, "mdi:apps");
      return;
  }
}

// ==============================================================================
// 4. Interactive Hit-Registered Widgets & Modals
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

void toggle_card_row(esphome::display::Display &it, int cx, int y_mid, int w, int h,
                     const char *label, bool is_on, esphome::display::BaseFont *font,
                     esphome::Color active_col, esphome::Color text_on, esphome::Color text_off,
                     uint16_t widget_id, int16_t param) {
  const esphome::Color bg_c = is_on ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color brd_c = is_on ? active_col : chrome_card_brd_off();
  const esphome::Color fg = is_on ? text_on : text_off;
  render_modern_card(it, cx, y_mid, w, h, 10, bg_c, brd_c, "", fg, font);
  int eff_w = w;
  if (SCREEN_W > 520 && eff_w <= 340) eff_w = scale_x(eff_w);
  else if (SCREEN_W < 380 && h <= 48 && eff_w >= 240 && eff_w <= 330) eff_w = SCREEN_W - 88;
  else if (eff_w > SCREEN_W - 20) eff_w = SCREEN_W - 20;

  const int text_x = cx - eff_w / 2 + scale_x(14);
  const int sw_cx = cx + eff_w / 2 - scale_x(28);
  const int max_lbl_px = std::max(60, (sw_cx - scale_x(26)) - text_x);
  const std::string fit_lbl = fit_text_to_width_font(
      label != nullptr ? std::string(label) : std::string(), max_lbl_px, font);
  fast_print(it, text_x + 1, y_mid + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());
  fast_print(it, text_x, y_mid, font, fg, esphome::display::TextAlign::CENTER_LEFT, fit_lbl.c_str());
  toggle_switch(it, sw_cx, y_mid, is_on, active_col);

  if (widget_id != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx, y_mid, eff_w, h, widget_id, kHitTap | kHitFocusSlot, param);
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

void stepper_row(esphome::display::Display &it, int cx, int y_top, int h,
                 const char *const labels[4], esphome::display::BaseFont *font,
                 esphome::Color col_text, uint16_t base_widget_id) {
  const int btn_w = (SCREEN_W < 380) ? 50 : scale_x(60);
  const int step = (SCREEN_W < 380) ? 66 : scale_x(80);
  const int eff_h = (SCREEN_H > 520 && h <= 64) ? scale_y(h) : h;
  const int y_mid = y_top + eff_h / 2;
  const int rad = std::max(4, scale_s(8));
  const esphome::Color bg_c = chrome_card_bg_on();
  const esphome::Color brd_c = chrome_card_brd_on();
  for (int i = 0; i < 4; i++) {
    const int bx = cx + static_cast<int>((static_cast<float>(i) - 1.5f) * static_cast<float>(step));
    render_card_box(it, bx - btn_w / 2, y_top, btn_w, eff_h, bg_c, brd_c, rad,
                    GlassVariant::Clear, col_text);
    if (labels[i] != nullptr && labels[i][0] != '\0' && font != nullptr) {
      fast_print(it, bx + 1, y_mid + 1, font, kChromeShadow,
                 esphome::display::TextAlign::CENTER, labels[i]);
      fast_print(it, bx, y_mid, font, col_text,
                 esphome::display::TextAlign::CENTER, labels[i]);
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
  render_page_header(it, cx, title, font_hdr, theme_primary);
  if (line1 != nullptr && line1[0] != '\0') {
    fast_print(it, cx, scale_y(132), font_body, col_white, esphome::display::TextAlign::CENTER, line1);
  }
  if (line2 != nullptr && line2[0] != '\0') {
    fast_print(it, cx, scale_y(168), font_body, col_slate, esphome::display::TextAlign::CENTER, line2);
  }
  const int btn_y = scale_y(242);
  const int btn_w = (SCREEN_W < 380) ? 116 : scale_x(126);
  const int btn_h = scale_y(54);
  const int gap_x = (SCREEN_W < 380) ? 64 : scale_x(72);

  render_modern_card(it, cx - gap_x, btn_y, btn_w, btn_h, 12,
                     chrome_card_bg_on(), chrome_card_brd_on(), cancel_lbl, col_white, font_body,
                     GlassVariant::Clear);
  render_modern_card(it, cx + gap_x, btn_y, btn_w, btn_h, 12,
                     confirm_bg, confirm_border, confirm_lbl, col_white, font_body,
                     GlassVariant::Clear);

  if (cancel_wid != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx - gap_x, btn_y, btn_w, btn_h, cancel_wid, kHitTap | kHitFocusSlot, 0);
  }
  if (confirm_wid != WidgetId::NONE) {
    frame_hits().add_centered_rect(cx + gap_x, btn_y, btn_w, btn_h, confirm_wid, kHitTap | kHitFocusSlot, 1);
  }
}

void game_over_sheet(esphome::display::Display &it, int cx, int cy,
                     const char *title, esphome::Color title_col,
                     const char *score_line, const char *best_line, bool is_new_best,
                     const char *cta_lbl,
                     esphome::display::BaseFont *font_hdr, esphome::display::BaseFont *font_body,
                     esphome::Color theme_primary, esphome::Color col_white,
                     esphome::Color col_gold, esphome::Color col_slate, uint16_t cta_wid) {
  const int box_w = std::min(SCREEN_W - 28, scale_x(296));
  const int box_h = scale_y(196);
  render_card_box(it, cx - box_w / 2, cy - scale_y(96), box_w, box_h,
                  kChromeGlassTint, title_col, 14, GlassVariant::Clear,
                  is_new_best ? col_gold : col_slate);
  fast_print(it, cx, cy - scale_y(60), font_hdr, title_col, esphome::display::TextAlign::CENTER, title);
  if (score_line != nullptr && score_line[0] != '\0') {
    fast_print(it, cx, cy - scale_y(20), font_body, col_white, esphome::display::TextAlign::CENTER, score_line);
  }
  if (best_line != nullptr && best_line[0] != '\0') {
    fast_print(it, cx, cy + scale_y(12), font_body, is_new_best ? col_gold : col_slate,
               esphome::display::TextAlign::CENTER, best_line);
  }
  const int pill_r = scale_s(18);
  const int pill_hw = std::min(box_w / 2 - pill_r - 12, scale_x(72));
  pill_button(it, cx - pill_hw, cx + pill_hw, cy + scale_y(58), pill_r,
              theme_primary, chrome_card_bg_on(), cta_lbl, col_white, font_body,
              cta_wid, kHitTap | kHitFocusSlot, 0, GlassVariant::Clear);
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
  render_page_header(it, cx, hdr, font_small, theme_primary);

  const int disp_y = scale_y(84);
  const int disp_w = std::min(SCREEN_W - 44, scale_x(236));
  const int disp_h = scale_y(36);
  const bool show_err = (st.error_msg[0] != '\0' && (now_ms - st.error_ms) < 2200U);
  render_modern_card(it, cx, disp_y, disp_w, disp_h, 10,
                     kChromeGlassTint, show_err ? col_red : chrome_card_brd_on(),
                     "", st.len == 0 && !show_err ? col_slate : col_white, font_small,
                     GlassVariant::Clear);
  if (show_err) {
    fast_print(it, cx, disp_y, font_small, col_white, esphome::display::TextAlign::CENTER, st.error_msg);
  } else if (st.len == 0) {
    fast_print(it, cx, disp_y, font_small, col_slate, esphome::display::TextAlign::CENTER, "4-8 Digits");
  } else {
    const int dot_step = scale_x(18);
    const int start_x = cx - ((static_cast<int>(st.len) - 1) * dot_step) / 2;
    for (uint8_t i = 0; i < st.len; i++) {
      fast_filled_circle_aa(it, start_x + static_cast<int>(i) * dot_step, disp_y, scale_s(5), col_white);
    }
  }

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
    render_modern_card(it, kx, ky, key_w, key_h, 10, bg, brd,
                       kLabels[idx], fg, (is_ok || is_del) ? font_small : font_key,
                       GlassVariant::Clear);
    frame_hits().add_centered_rect(kx, ky, key_w, key_h, kWids[idx], kHitTap | kHitFocusSlot,
                                   static_cast<int16_t>(idx));
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
  const int rad = std::min(h / 2, scale_s(14));
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
    if (action_on) {
      // Crisp pause bars
      const int bw = std::max(2, scale_s(3));
      const int bh = std::max(8, scale_s(11));
      const int gap = std::max(2, scale_s(3));
      fill_rect_row_major(it, right_cx - gap - bw + 1, cy - bh / 2, bw, bh, esphome::Color(12, 16, 22));
      fill_rect_row_major(it, right_cx + gap, cy - bh / 2, bw, bh, esphome::Color(12, 16, 22));
    } else {
      // Crisp play triangle
      const int th = std::max(5, scale_s(6));
      const int tw = std::max(7, scale_s(9));
      fast_filled_triangle(it,
                           right_cx - tw / 2 + 2, cy - th,
                           right_cx - tw / 2 + 2, cy + th,
                           right_cx + tw / 2 + 2, cy,
                           esphome::Color(245, 248, 252));
    }
  }

  frame_hits().add_rect(x0, y0, split_w, h,
                        left_wid, kHitTap | kHitFocusSlot, param);
  frame_hits().add_rect(div_x, y0, right_w, h,
                        right_wid, kHitTap | kHitFocusSlot, param);
}

}  // namespace ui
}  // namespace latter_ui
