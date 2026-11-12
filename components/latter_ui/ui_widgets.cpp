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
                        esphome::display::BaseFont *font) {
  const int px0 = x1 - r;
  const int py0 = y_mid - r;
  const int pw = (x2 - x1) + 2 * r + 1;
  const int ph = 2 * r + 1;
  const GlassBackdrop bd = sample_glass_backdrop(px0, py0, pw, ph);
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, bg_c, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot);
  const bool neutral_brd = (std::max({border_c.r, border_c.g, border_c.b}) <= 96);
  if (neutral_brd) {
    fill_round_rect_glass(it, px0, py0, pw, ph, static_cast<float>(r) + 0.5f,
                          kChromeGlassSheen, rim_top, rim_bot, 1);
    if (r > 1) {
      fill_round_rect_glass(it, px0 + 1, py0 + 1, pw - 2, ph - 2, static_cast<float>(r) - 0.5f,
                            eff_tint, a_top, a_bot);
    }
  } else {
    stroke_round_rect_aa(it, px0, py0, pw, ph, static_cast<float>(r) + 0.5f, border_c, border_c, 2);
    if (r > 2) {
      fill_round_rect_glass(it, px0 + 2, py0 + 2, pw - 4, ph - 4, static_cast<float>(r) - 1.5f,
                            eff_tint, a_top, a_bot);
    }
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
                                  esphome::display::BaseFont *font) {
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
  if (r > h / 2) r = h / 2;
  if (r < 2) r = 2;
  const int x0 = cx_card - w / 2;
  const int y0 = cy_card - h / 2;

  const bool is_lit_glass = (static_cast<int>(bg_c.r) + static_cast<int>(bg_c.g) + static_cast<int>(bg_c.b)) > 12;
  const esphome::Color brd_top = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.r) + 28)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.g) + 32)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.b) + 38)))
      : border_c;
  const esphome::Color brd_btm = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>((static_cast<int>(border_c.r) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.g) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.b) * 180) >> 8))
      : border_c;

  esphome::Color surf_bg = bg_c;
  if (is_lit_glass) {
    const GlassBackdrop bd = sample_glass_backdrop(x0, y0, w, h);
    const esphome::Color ref_fg = (label != nullptr && label[0] != '\0') ? text_c : kChromeGlassSheen;
    esphome::Color eff_tint;
    int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
    resolve_glass_fill(bd, bg_c, ref_fg, eff_tint, a_top, a_bot, rim_top, rim_bot);
    const bool neutral_brd = (std::max({border_c.r, border_c.g, border_c.b}) <= 96);
    if (neutral_brd) {
      fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), kChromeGlassSheen, rim_top, rim_bot, 1);
    } else {
      stroke_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), brd_top, brd_btm, 1);
    }
    fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, eff_tint, a_top, a_bot);
    surf_bg = bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
  } else {
    stroke_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), brd_top, brd_btm, 1);
    fill_round_rect_aa(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, bg_c, bg_c);
  }

  if (label != nullptr && label[0] != '\0' && font != nullptr) {
    fast_print(it, cx_card + 1, cy_card + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, label);
    fast_print(it, cx_card, cy_card, font, text_c, esphome::display::TextAlign::CENTER, label);
  }
  return surf_bg;
}

esphome::Color render_card_box(esphome::display::Display &it, int x0, int y0, int w, int h,
                               esphome::Color bg_c, esphome::Color border_c, int r) {
  if (w <= 4 || h <= 4) return bg_c;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 2) r = 2;

  const bool is_lit_glass = (static_cast<int>(bg_c.r) + static_cast<int>(bg_c.g) + static_cast<int>(bg_c.b)) > 12;
  const esphome::Color brd_top = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.r) + 28)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.g) + 32)),
                       static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.b) + 38)))
      : border_c;
  const esphome::Color brd_btm = is_lit_glass
      ? esphome::Color(static_cast<uint8_t>((static_cast<int>(border_c.r) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.g) * 180) >> 8),
                       static_cast<uint8_t>((static_cast<int>(border_c.b) * 180) >> 8))
      : border_c;

  if (is_lit_glass) {
    const GlassBackdrop bd = sample_glass_backdrop(x0, y0, w, h);
    esphome::Color eff_tint;
    int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
    resolve_glass_fill(bd, bg_c, kChromeGlassSheen, eff_tint, a_top, a_bot, rim_top, rim_bot);
    const bool neutral_brd = (std::max({border_c.r, border_c.g, border_c.b}) <= 96);
    if (neutral_brd) {
      fill_round_rect_glass(it, x0, y0, w, h, static_cast<float>(r), kChromeGlassSheen, rim_top, rim_bot, 1);
    } else {
      stroke_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), brd_top, brd_btm, 1);
    }
    fill_round_rect_glass(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, eff_tint, a_top, a_bot);
    return bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
  }

  stroke_round_rect_aa(it, x0, y0, w, h, static_cast<float>(r), brd_top, brd_btm, 1);
  fill_round_rect_aa(it, x0 + 1, y0 + 1, w - 2, h - 2, static_cast<float>(r) - 1.0f, bg_c, bg_c);
  return bg_c;
}

esphome::Color render_glass_circle(esphome::display::Display &it, int cx_btn, int cy_btn, int r,
                                   esphome::Color bg_c, esphome::Color border_c) {
  if (r <= 1) return bg_c;
  const esphome::Color brd_top(
      static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.r) + 34)),
      static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.g) + 38)),
      static_cast<uint8_t>(std::min(255, static_cast<int>(border_c.b) + 44)));
  const esphome::Color brd_btm(
      static_cast<uint8_t>((static_cast<int>(border_c.r) * 165) >> 8),
      static_cast<uint8_t>((static_cast<int>(border_c.g) * 165) >> 8),
      static_cast<uint8_t>((static_cast<int>(border_c.b) * 165) >> 8));

  const int d = 2 * r + 1;
  const GlassBackdrop bd = sample_glass_backdrop(cx_btn - r, cy_btn - r, d, d);
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, bg_c, kChromeGlassSheen, eff_tint, a_top, a_bot, rim_top, rim_bot);
  const bool neutral_brd = (std::max({border_c.r, border_c.g, border_c.b}) <= 96);
  if (neutral_brd) {
    fill_round_rect_glass(it, cx_btn - r, cy_btn - r, d, d, static_cast<float>(r) + 0.5f,
                          kChromeGlassSheen, rim_top, rim_bot, 1);
  } else {
    stroke_round_rect_aa(it, cx_btn - r, cy_btn - r, d, d, static_cast<float>(r) + 0.5f, brd_top, brd_btm, 1);
  }
  fill_round_rect_glass(it, cx_btn - r + 1, cy_btn - r + 1, d - 2, d - 2, static_cast<float>(r) - 0.5f,
                        eff_tint, a_top, a_bot);
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
  const int cr_in  = std::max(4, cr_out - thick);
  const int x_lc   = pad + cr_out;
  const int x_rc   = SCREEN_W - 1 - pad - cr_out;
  const int y_tc   = pad + cr_out;
  const int y_bc   = SCREEN_H - 1 - pad - cr_out;

  for (int y = pad; y <= SCREEN_H - 1 - pad; y++) {
    if (y >= y_tc && y <= y_bc) {
      fast_hline(it, pad, y, thick, ring_c);
      fast_hline(it, SCREEN_W - pad - thick, y, thick, ring_c);
    } else {
      const int dy = (y < y_tc) ? (y_tc - y) : (y - y_bc);
      if (dy > cr_out) continue;
      const int dx_out = chord_half_span_int(cr_out, dy, true);
      if (dy >= cr_in) {
        const int x0 = x_lc - dx_out;
        const int x1 = x_rc + dx_out;
        if (x1 >= x0) {
          fast_hline(it, x0, y, x1 - x0 + 1, ring_c);
        }
      } else {
        const int dx_in = chord_half_span_int(cr_in, dy, false);
        const int seg = std::max(thick, dx_out - dx_in + 1);
        fast_hline(it, x_lc - dx_out, y, seg, ring_c);
        fast_hline(it, x_rc + dx_out - seg + 1, y, seg, ring_c);
      }
    }
  }
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
  frame_hits().add_centered_rect(cx, y_pos, scale_x(180), scale_y(36), WidgetId::HOME_BAR, kHitTap);
}

void render_viewport_scrollbar(esphome::display::Display &it, int cx, int y_pos, int total_items,
                               float cur_start_idx, esphome::Color col_thumb, bool is_dim) {
  if (total_items <= 4) return;
  const int track_w = (SCREEN_W < 380) ? 84 : ((SCREEN_W > 520) ? 168 : 112);
  const int track_h = (SCREEN_W > 520) ? 6 : 4;
  const int r_cap   = track_h / 2;
  const int track_x0 = cx - (track_w / 2);
  const int track_y0 = y_pos - r_cap;

  const esphome::Color col_track = is_dim ? kChromeTrackDim : kChromeTrack;
  const esphome::Color col_brd   = is_dim ? kChromeTrackBorderDim : kChromeTrackBorder;

  const float cap = static_cast<float>(r_cap);
  if (!is_dim) {
    fill_round_rect_aa(it, track_x0 + 1, track_y0 + 1, track_w, track_h, cap, kChromeShadow);
  }
  stroke_round_rect_aa(it, track_x0, track_y0, track_w, track_h, cap, col_brd, col_brd, 1);
  fill_round_rect_aa(it, track_x0 + 1, track_y0 + 1, track_w - 2, track_h - 2, cap - 1.0f, col_track);

  const int min_thumb_w = (SCREEN_W > 520) ? 28 : 20;
  const int thumb_w = std::max(min_thumb_w, std::min(track_w, (track_w * 4) / total_items));
  const float max_start = static_cast<float>(std::max(1, total_items - 4));
  const float clamped_start = std::max(0.0f, std::min(max_start, cur_start_idx));
  const float frac = clamped_start / max_start;
  const int thumb_x0 = track_x0 + static_cast<int>(std::round(frac * static_cast<float>(track_w - thumb_w)));

  fill_round_rect_aa(it, thumb_x0, track_y0, thumb_w, track_h, cap, col_thumb);
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
#if LATTER_ROUND_DISPLAY
  const int mask_x = 395;
  const int chev_x = 415;
#else
  const int mask_x = (SCREEN_W < 380) ? (SCREEN_W - 30) : ((SCREEN_W > 520) ? (SCREEN_W - 88) : (SCREEN_W - 42));
  const int chev_x = (SCREEN_W < 380) ? (SCREEN_W - 15) : ((SCREEN_W > 520) ? (SCREEN_W - 48) : (SCREEN_W - 21));
#endif
  const int up_y = scale_y(162);
  const int dn_y = scale_y(278);
  if (!backdrop_active()) {
    fill_rect_row_major(it, mask_x, up_y - scale_y(22), SCREEN_W - mask_x, (dn_y - up_y) + scale_y(44), col_bg);
  }
  if (show_up) {
    render_scroll_chevron(it, chev_x, up_y, true, col_active, kChromeShadow, backdrop_active());
    frame_hits().add_rect(mask_x - scale_x(12), scale_y(70), SCREEN_W - (mask_x - scale_x(12)),
                          SCREEN_CY - scale_y(70), WidgetId::CHEVRON_UP, kHitTap);
  }
  if (show_down) {
    render_scroll_chevron(it, chev_x, dn_y, false, col_active, kChromeShadow, backdrop_active());
    frame_hits().add_rect(mask_x - scale_x(12), SCREEN_CY, SCREEN_W - (mask_x - scale_x(12)),
                          (SCREEN_H - scale_y(70)) - SCREEN_CY, WidgetId::CHEVRON_DOWN, kHitTap);
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
                         esphome::display::BaseFont *font_time, esphome::Color color) {
  if (text == nullptr || font_time == nullptr) return;
  fast_print(it, cx, scale_y(142), font_time, color, esphome::display::TextAlign::CENTER, text);
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
  const GlassBackdrop bd = sample_glass_backdrop(x0 - 1, std::max(0, sy0 - 1), pill_w + 2, pill_h + 2);
  const int min_gold_a = bd.valid ? glass_alpha_for_text(kChromeGlassTint, bd.bright, text_c) : 184;
  esphome::Color eff_tint;
  int a_top = 0, a_bot = 0, rim_top = 0, rim_bot = 0;
  resolve_glass_fill(bd, kChromeGlassTint, text_c, eff_tint, a_top, a_bot, rim_top, rim_bot, min_gold_a);
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
  const esphome::Color track_bg = kChromeBarTrack;

  const int tx0 = left_cx - r_track;
  const int tw = (right_cx - left_cx) + 2 * r_track + 1;
  const int th = 2 * r_track + 1;
  const GlassBackdrop bd = sample_glass_backdrop(tx0, by - r_track, tw, th);
  const int track_alpha = bd.valid ? glass_alpha_for_text(kChromeGlassTint, bd.bright, kChromeGlassSheen) : 168;
  render_glass_card(it, tx0, by - r_track, tw, th, r_track, kChromeGlassTint, track_alpha);

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
      const esphome::Color ring_col(
          static_cast<uint8_t>(track_bg.r + (theme_primary.r - track_bg.r) * ring_alpha),
          static_cast<uint8_t>(track_bg.g + (theme_primary.g - track_bg.g) * ring_alpha),
          static_cast<uint8_t>(track_bg.b + (theme_primary.b - track_bg.b) * ring_alpha));
      stroke_round_rect_aa(it, abx - dr, by - dr, 2 * dr + 1, 2 * dr + 1,
                           static_cast<float>(dr) + 0.5f, ring_col, ring_col, 2);
      fill_round_rect_glass(it, abx - dr + 2, by - dr + 2, 2 * dr - 3, 2 * dr - 3,
                            static_cast<float>(dr) - 1.5f, theme_primary, 48, 28);
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
  if (domain_or_entity == "whole_home.garage" && action == "turn_on") {
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
// 2. Semantic Chrome Surface Tokens (zero dead chrome_remote_* shims)
// ==============================================================================
esphome::Color chrome_card_bg_on() { return esphome::Color(24, 30, 38); }
esphome::Color chrome_card_bg_off() { return esphome::Color(15, 18, 24); }
esphome::Color chrome_card_brd_on() { return esphome::Color(52, 62, 78); }
esphome::Color chrome_card_brd_off() { return esphome::Color(34, 40, 50); }
esphome::Color chrome_danger_bg() { return esphome::Color(34, 18, 22); }
esphome::Color chrome_modal_bg() { return esphome::Color(18, 22, 29); }
esphome::Color chrome_key_bg() { return esphome::Color(20, 25, 33); }
esphome::Color chrome_key_brd() { return esphome::Color(44, 54, 68); }
esphome::Color chrome_wheel_inner_ring() { return esphome::Color(70, 82, 96); }
esphome::Color chrome_wheel_outer_ring() { return esphome::Color(42, 50, 62); }

// ==============================================================================
// 3. Deduplicated Canonical Vector Glyph Library
// ==============================================================================
void draw_icon_power(esphome::display::Display &it, int cx, int cy, int r,
                     esphome::Color col, esphome::Color bg_cut) {
  const int ring_r = std::max(5, (r * 52) / 100);
  fast_circle_aa(it, cx, cy + 1, static_cast<float>(ring_r), col, 2.0f);
  fill_rect_row_major(it, cx - scale_s(4), cy - ring_r - 2, scale_s(8) + 1, scale_s(6), bg_cut);
  fill_rect_row_major(it, cx - 1, cy - ring_r - 2, 3, ring_r + 2, col);
}

void draw_icon_thermometer(esphome::display::Display &it, int cx, int cy,
                           esphome::Color col, esphome::Color bulb_inner) {
  const int stem_h = scale_s(14);
  const int bulb_r = scale_s(5);
  fill_round_rect_aa(it, cx - scale_s(2), cy - stem_h / 2 - 2, 2 * scale_s(2) + 1, stem_h,
                     static_cast<float>(scale_s(2)), col);
  fast_filled_circle_aa(it, cx, cy + stem_h / 2, bulb_r, col);
  if (bulb_r > 2) {
    fast_filled_circle_aa(it, cx, cy + stem_h / 2, bulb_r - 2, bulb_inner);
  }
}

void draw_icon_eighth_notes(esphome::display::Display &it, int cx, int cy, esphome::Color col) {
  const int dx = scale_s(6);
  const int top_y = cy - scale_s(9);
  const int bot_y = cy + scale_s(5);
  const int head_r = std::max(3, scale_s(4));
  fast_filled_circle_aa(it, cx - dx, bot_y + 1, head_r, col);
  fast_filled_circle_aa(it, cx + dx - 1, bot_y - 1, head_r, col);
  fill_rect_row_major(it, cx - dx + head_r - 1, top_y + 2, 2, (bot_y - top_y), col);
  fill_rect_row_major(it, cx + dx + head_r - 2, top_y, 2, (bot_y - top_y), col);
  fast_line(it, cx - dx + head_r - 1, top_y + 2, cx + dx + head_r - 1, top_y, col);
  fast_line(it, cx - dx + head_r - 1, top_y + 3, cx + dx + head_r - 1, top_y + 1, col);
}

void draw_icon_speaker(esphome::display::Display &it, int cx, int cy, esphome::Color col, bool waves) {
  const int bx = cx - scale_s(7);
  fill_rect_row_major(it, bx, cy - scale_s(4), scale_s(4), scale_s(8) + 1, col);
  fast_filled_triangle(it, bx + scale_s(3), cy - scale_s(4),
                       bx + scale_s(9), cy - scale_s(9),
                       bx + scale_s(9), cy + scale_s(9), col);
  fast_filled_triangle(it, bx + scale_s(3), cy - scale_s(4),
                       bx + scale_s(3), cy + scale_s(4),
                       bx + scale_s(9), cy + scale_s(9), col);
  if (waves) {
    const int wx = bx + scale_s(12);
    fast_line(it, wx, cy - scale_s(4), wx + scale_s(2), cy, col);
    fast_line(it, wx + scale_s(2), cy, wx, cy + scale_s(4), col);
    fast_line(it, wx + scale_s(4), cy - scale_s(7), wx + scale_s(7), cy, col);
    fast_line(it, wx + scale_s(7), cy, wx + scale_s(4), cy + scale_s(7), col);
  }
}

void draw_icon_lock(esphome::display::Display &it, int cx, int cy, esphome::Color col,
                    esphome::Color keyhole_col, bool is_locked) {
  const int bw = scale_s(16);
  const int bh = scale_s(12);
  const int by = cy - scale_s(1);
  const int sh_r = scale_s(5);
  const int sh_cx = is_locked ? cx : (cx + scale_s(4));
  fast_circle_aa(it, sh_cx, by - scale_s(2), static_cast<float>(sh_r), col, 2.0f);
  fill_round_rect_aa(it, cx - bw / 2, by, bw, bh, 3.0f, col);
  fast_filled_circle_aa(it, cx, by + bh / 2 - 1, std::max(1, scale_s(2)), keyhole_col);
}

void draw_icon_shield(esphome::display::Display &it, int cx, int cy, esphome::Color col) {
  const int hw = scale_s(9);
  const int top_y = cy - scale_s(10);
  const int mid_y = cy + scale_s(1);
  const int bot_y = cy + scale_s(10);
  fast_line(it, cx, top_y, cx - hw, top_y + scale_s(3), col);
  fast_line(it, cx, top_y, cx + hw, top_y + scale_s(3), col);
  fast_vline(it, cx - hw, top_y + scale_s(3), mid_y - (top_y + scale_s(3)), col);
  fast_vline(it, cx + hw, top_y + scale_s(3), mid_y - (top_y + scale_s(3)), col);
  fast_line(it, cx - hw, mid_y, cx, bot_y, col);
  fast_line(it, cx + hw, mid_y, cx, bot_y, col);
}

void draw_icon_hourglass(esphome::display::Display &it, int cx, int cy,
                         esphome::Color col, esphome::Color accent, int half_h) {
  const int hh = scale_s(half_h);
  const int hw = std::max(5, (hh * 82) / 100);
  fill_rect_row_major(it, cx - hw - 1, cy - hh, 2 * hw + 3, 2, col);
  fill_rect_row_major(it, cx - hw - 1, cy + hh - 1, 2 * hw + 3, 2, col);
  fast_line(it, cx - hw + 1, cy - hh + 2, cx - 1, cy, col);
  fast_line(it, cx - hw + 2, cy - hh + 2, cx, cy, col);
  fast_line(it, cx + hw - 1, cy - hh + 2, cx + 1, cy, col);
  fast_line(it, cx + hw - 2, cy - hh + 2, cx, cy, col);
  fast_line(it, cx - 1, cy, cx - hw + 1, cy + hh - 2, col);
  fast_line(it, cx, cy, cx - hw + 2, cy + hh - 2, col);
  fast_line(it, cx + 1, cy, cx + hw - 1, cy + hh - 2, col);
  fast_line(it, cx, cy, cx + hw - 2, cy + hh - 2, col);
  const int sw = std::max(3, hw / 2);
  fast_filled_triangle(it, cx - sw, cy - hh / 2, cx + sw, cy - hh / 2, cx, cy - 2, col);
  fast_line(it, cx, cy - 2, cx, cy + 3, accent);
  fast_filled_triangle(it, cx - sw - 1, cy + hh - 3, cx + sw + 1, cy + hh - 3, cx, cy + 3, col);
}

void draw_icon_alarm_clock(esphome::display::Display &it, int cx, int cy,
                           esphome::Color col, esphome::Color surf_bg, int radius) {
  const int r = scale_s(radius);
  const int bell_off = std::max(5, (r * 86) / 100);
  const int bell_r = std::max(3, r / 3);
  fast_filled_circle_aa(it, cx - bell_off, cy - bell_off + 1, bell_r, col);
  fast_filled_circle_aa(it, cx + bell_off, cy - bell_off + 1, bell_r, col);
  fast_filled_circle_aa(it, cx, cy + 2, r + 2, surf_bg);
  fast_circle_aa(it, cx, cy + 2, static_cast<float>(r), col, 2.0f);
  fast_line(it, cx - (r * 7) / 10, cy + r, cx - r, cy + r + scale_s(3), col);
  fast_line(it, cx + (r * 7) / 10, cy + r, cx + r, cy + r + scale_s(3), col);
  fast_line(it, cx, cy + 2, cx, cy + 2 - std::max(3, r / 2), col);
  fast_line(it, cx, cy + 2, cx + std::max(3, r / 2), cy + 4, col);
  fast_filled_circle_aa(it, cx, cy + 2, std::max(1, scale_s(2)), col);
}

void draw_icon_stopwatch(esphome::display::Display &it, int cx, int cy,
                         esphome::Color col, esphome::Color accent, int radius) {
  const int r = scale_s(radius);
  fill_rect_row_major(it, cx - scale_s(4), cy - r - scale_s(4), 2 * scale_s(4) + 1, 2, col);
  fill_rect_row_major(it, cx - 1, cy - r - scale_s(2), 3, scale_s(3), col);
  fast_line(it, cx + (r * 7) / 10, cy - (r * 7) / 10, cx + r, cy - r, col);
  fast_circle_aa(it, cx, cy + 2, static_cast<float>(r), col, 2.0f);
  fast_filled_circle_aa(it, cx, cy + 2, std::max(1, scale_s(2)), col);
  fast_line(it, cx, cy + 2, cx + std::max(3, r / 2), cy + 2 - std::max(3, r / 2), accent);
}

void draw_icon_calendar(esphome::display::Display &it, int cx, int cy,
                        esphome::Color col, esphome::Color accent, int half_w) {
  const int hw = scale_s(half_w);
  const int hh = scale_s(half_w);
  stroke_round_rect_aa(it, cx - hw, cy - hh + 2, 2 * hw + 1, 2 * hh - 1, 3.0f, col, col, 2);
  fill_rect_row_major(it, cx - hw + 2, cy - hh + 3, 2 * hw - 3, std::max(3, hh / 2), accent);
  fill_rect_row_major(it, cx - hw / 2 - 1, cy - hh - 1, 2, 4, col);
  fill_rect_row_major(it, cx + hw / 2, cy - hh - 1, 2, 4, col);
  const int dot_r = std::max(1, scale_s(1));
  for (int r = 0; r < 2; r++) {
    for (int c = -1; c <= 1; c++) {
      fast_filled_circle(it, cx + c * (hw / 2), cy + 2 + r * std::max(3, hh / 3), dot_r, col);
    }
  }
}

void draw_icon_camera(esphome::display::Display &it, int cx, int cy,
                      esphome::Color col, esphome::Color surf_bg, int half_w) {
  const int hw = scale_s(half_w);
  const int hh = std::max(5, (hw * 75) / 100);
  fill_round_rect_aa(it, cx - hw, cy - hh + 2, 2 * hw + 1, 2 * hh - 1, 4.0f, col);
  fill_round_rect_aa(it, cx - scale_s(5), cy - hh - 1, 2 * scale_s(5) + 1, 4, 2.0f, col);
  const int lr = std::max(3, hh / 2);
  fast_filled_circle_aa(it, cx, cy + 2, lr + 1, surf_bg);
  fast_circle_aa(it, cx, cy + 2, static_cast<float>(lr), col, 1.5f);
}

void draw_icon_lightbulb(esphome::display::Display &it, int cx, int cy,
                         esphome::Color col, bool rays) {
  const int r = scale_s(7);
  fast_filled_circle_aa(it, cx, cy - scale_s(3), r, col);
  fill_rect_row_major(it, cx - scale_s(4), cy + r - scale_s(5), 2 * scale_s(4) + 1, scale_s(5), col);
  fast_hline(it, cx - scale_s(3), cy + r + scale_s(2), 2 * scale_s(3) + 1, col);
  if (rays) {
    const int ro = r + scale_s(4);
    fast_line(it, cx, cy - scale_s(3) - ro, cx, cy - scale_s(3) - ro - scale_s(2), col);
    fast_line(it, cx - ro, cy - scale_s(3), cx - ro - scale_s(2), cy - scale_s(3), col);
    fast_line(it, cx + ro, cy - scale_s(3), cx + ro + scale_s(2), cy - scale_s(3), col);
  }
}

void draw_icon_fan(esphome::display::Display &it, int cx, int cy, esphome::Color col) {
  const int br = scale_s(4);
  const int off = scale_s(5);
  fast_filled_circle_aa(it, cx, cy - off, br, col);
  fast_filled_circle_aa(it, cx, cy + off, br, col);
  fast_filled_circle_aa(it, cx - off, cy, br, col);
  fast_filled_circle_aa(it, cx + off, cy, br, col);
  fast_filled_circle_aa(it, cx, cy, std::max(2, scale_s(2)), kChromeShadow);
}

void draw_icon_blinds(esphome::display::Display &it, int cx, int cy, esphome::Color col) {
  const int hw = scale_s(10);
  const int top_y = cy - scale_s(9);
  fill_rect_row_major(it, cx - hw, top_y, 2 * hw + 1, 3, col);
  for (int i = 1; i <= 3; i++) {
    fill_rect_row_major(it, cx - hw + 2, top_y + i * scale_s(5), 2 * hw - 3, 2, col);
  }
  fast_vline(it, cx + hw - 2, top_y + 2, scale_s(17), col);
}

void draw_icon_settings_gear(esphome::display::Display &it, int cx, int cy,
                             esphome::Color col, esphome::Color surf_bg) {
  const int ro = scale_s(9);
  const int tooth_r = scale_s(11);
  for (int i = 0; i < 8; i++) {
    const float rad = static_cast<float>(i) * (PI_F * 0.25f);
    float sv = 0.0f, cv = 1.0f;
    fast_sincos(rad, sv, cv);
    fast_filled_circle_aa(it, cx + static_cast<int>(std::round(cv * tooth_r)),
                          cy + static_cast<int>(std::round(sv * tooth_r)),
                          std::max(2, scale_s(2)), col);
  }
  fast_filled_circle_aa(it, cx, cy, ro, col);
  fast_filled_circle_aa(it, cx, cy, std::max(3, scale_s(4)), surf_bg);
}

void draw_icon_remote(esphome::display::Display &it, int cx, int cy,
                      esphome::Color col, esphome::Color surf_bg) {
  const int hw = scale_s(7);
  const int hh = scale_s(12);
  fill_round_rect_aa(it, cx - hw, cy - hh, 2 * hw + 1, 2 * hh + 1, 4.0f, col);
  fast_filled_circle_aa(it, cx, cy - scale_s(4), std::max(3, scale_s(4)), surf_bg);
  fast_filled_circle_aa(it, cx - scale_s(3), cy + scale_s(5), std::max(1, scale_s(1)), surf_bg);
  fast_filled_circle_aa(it, cx + scale_s(3), cy + scale_s(5), std::max(1, scale_s(1)), surf_bg);
  fast_filled_circle_aa(it, cx - scale_s(3), cy + scale_s(9), std::max(1, scale_s(1)), surf_bg);
  fast_filled_circle_aa(it, cx + scale_s(3), cy + scale_s(9), std::max(1, scale_s(1)), surf_bg);
}

void draw_icon_gamepad(esphome::display::Display &it, int cx, int cy,
                       esphome::Color col, esphome::Color surf_bg) {
  const int hw = scale_s(11);
  const int hh = scale_s(7);
  fill_round_rect_aa(it, cx - hw, cy - hh, 2 * hw + 1, 2 * hh + 1, static_cast<float>(hh), col);
  fill_rect_row_major(it, cx - scale_s(6), cy - scale_s(1), scale_s(5), 2, surf_bg);
  fill_rect_row_major(it, cx - scale_s(4), cy - scale_s(3), 2, scale_s(5), surf_bg);
  fast_filled_circle_aa(it, cx + scale_s(5), cy - scale_s(2), std::max(1, scale_s(1)), surf_bg);
  fast_filled_circle_aa(it, cx + scale_s(3), cy + scale_s(2), std::max(1, scale_s(1)), surf_bg);
}

void draw_icon_wrench(esphome::display::Display &it, int cx, int cy, esphome::Color col) {
  const int d = scale_s(8);
  fast_line(it, cx - d, cy + d, cx + d - 2, cy - d + 2, col);
  fast_line(it, cx - d + 1, cy + d, cx + d - 1, cy - d + 2, col);
  fast_line(it, cx - d, cy + d - 1, cx + d - 2, cy - d + 1, col);
  fast_circle_aa(it, cx + scale_s(5), cy - scale_s(5), static_cast<float>(scale_s(5)), col, 2.0f);
  fast_filled_circle_aa(it, cx - d + 1, cy + d - 1, std::max(2, scale_s(2)), col);
}

// ==============================================================================
// 4. Interactive Hit-Registered Widgets & Modals
// ==============================================================================
void toggle_switch(esphome::display::Display &it, int cx_sw, int cy_sw, bool is_on,
                   esphome::Color active_col) {
  const int sw_w = scale_x(40);
  const int sw_h = scale_y(22);
  const float rad = 0.5f * static_cast<float>(sw_h);
  const esphome::Color track_c = is_on ? active_col : kChromeTrack;
  const esphome::Color brd_c = is_on ? active_col : kChromeTrackBorder;
  stroke_round_rect_aa(it, cx_sw - sw_w / 2, cy_sw - sw_h / 2, sw_w, sw_h, rad, brd_c, brd_c, 1);
  fill_round_rect_aa(it, cx_sw - sw_w / 2 + 1, cy_sw - sw_h / 2 + 1, sw_w - 2, sw_h - 2, rad - 1.0f, track_c);
  const int thumb_r = std::max(5, sw_h / 2 - 3);
  const int thumb_cx = is_on ? (cx_sw + sw_w / 2 - thumb_r - 3) : (cx_sw - sw_w / 2 + thumb_r + 3);
  fast_filled_circle_aa(it, thumb_cx + 1, cy_sw + 1, thumb_r, kChromeShadow);
  fast_filled_circle_aa(it, thumb_cx, cy_sw, thumb_r, kChromeGlassSheen);
}

void toggle_card_row(esphome::display::Display &it, int cx, int y_mid, int w, int h,
                     const char *label, bool is_on, esphome::display::BaseFont *font,
                     esphome::Color active_col, esphome::Color text_on, esphome::Color text_off,
                     uint16_t widget_id, int16_t param) {
  const esphome::Color bg_c = is_on ? chrome_card_bg_on() : chrome_card_bg_off();
  const esphome::Color brd_c = is_on ? active_col : chrome_card_brd_off();
  render_modern_card(it, cx, y_mid, w, h, 10, bg_c, brd_c, "", text_on, font);
  int eff_w = w;
  if (SCREEN_W > 520 && eff_w <= 340) eff_w = scale_x(eff_w);
  else if (SCREEN_W < 380 && h <= 48 && eff_w >= 240 && eff_w <= 330) eff_w = SCREEN_W - 88;
  else if (eff_w > SCREEN_W - 20) eff_w = SCREEN_W - 20;

  const int text_x = cx - eff_w / 2 + scale_x(14);
  const int sw_cx = cx + eff_w / 2 - scale_x(28);
  const int max_lbl_px = std::max(60, (sw_cx - scale_x(26)) - text_x);
  const std::string fit_lbl = fit_text_to_width_font(
      label != nullptr ? std::string(label) : std::string(), max_lbl_px, font);
  const esphome::Color fg = is_on ? text_on : text_off;
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
                 uint16_t widget_id, uint8_t flags, int16_t param) {
  render_pill_button(it, x1, x2, y_mid, r, border_c, bg_c, label, text_c, font);
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
  const int btn_w = scale_x(62);
  const int step = scale_x(68);
  const int y_mid = y_top + h / 2;
  const esphome::Color bg_c = chrome_card_bg_on();
  const esphome::Color brd_c = chrome_card_brd_on();
  for (int i = 0; i < 4; i++) {
    const int bx = cx + static_cast<int>((static_cast<float>(i) - 1.5f) * static_cast<float>(step));
    render_modern_card(it, bx, y_mid, btn_w, h, 8, bg_c, brd_c, labels[i], col_text, font);
    if (base_widget_id != WidgetId::NONE) {
      frame_hits().add_centered_rect(
          bx, y_mid, btn_w, h,
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
                     chrome_card_bg_on(), chrome_card_brd_on(), cancel_lbl, col_white, font_body);
  render_modern_card(it, cx + gap_x, btn_y, btn_w, btn_h, 12,
                     confirm_bg, confirm_border, confirm_lbl, col_white, font_body);

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
                  chrome_modal_bg(), title_col, 14);
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
              cta_wid, kHitTap | kHitFocusSlot);
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
  if (policy != "block") return false;
  return is_sensitive_perimeter_action_impl(domain_or_entity, action, level);
}

bool requires_perimeter_pin_prompt(const std::string &policy, const std::string &domain_or_entity,
                                   const std::string &action, int level, bool alarm_has_code) {
  if (!is_sensitive_perimeter_action_impl(domain_or_entity, action, level)) return false;
  if (policy == "allow" || policy == "block") return false;
  if (policy == "require_pin") return true;
#if LATTER_IS_STATIONARY
  (void)alarm_has_code;
  return true;
#else
  return alarm_has_code;
#endif
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
                     chrome_modal_bg(), show_err ? col_red : chrome_card_brd_on(),
                     "", col_white, font_small);
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
                       kLabels[idx], fg, (is_ok || is_del) ? font_small : font_key);
    frame_hits().add_centered_rect(kx, ky, key_w, key_h, kWids[idx], kHitTap | kHitFocusSlot,
                                   static_cast<int16_t>(idx));
  }

  render_back_button(it, cx, font_small, layout().back_y, "< Cancel");
  frame_hits().add_centered_rect(cx, layout().back_y, scale_x(150), scale_y(36),
                                 WidgetId::PIN_PAD_CANCEL, kHitTap, 0);
}

}  // namespace ui
}  // namespace latter_ui
