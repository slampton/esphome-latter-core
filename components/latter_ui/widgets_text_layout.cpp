#include "ui_widgets.h"

#include <algorithm>
#include <string>

namespace latter_ui {

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
