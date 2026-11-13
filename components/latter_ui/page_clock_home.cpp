#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace latter_ui {

namespace {

void render_battery_indicator(
    esphome::display::Display &it, int cx, int by, float batt, bool is_charging,
    esphome::display::BaseFont *font_batt, const esphome::Color &theme_primary,
    const esphome::Color &col_gold, const esphome::Color &col_green) {
  if (std::isnan(batt) || font_batt == nullptr) return;
  char batt_str[16];
  std::snprintf(batt_str, sizeof(batt_str), "%.0f%%", batt);
  const bool compact = (SCREEN_W < 380);
  const int text_x = cx + (compact ? 6 : 8);
  fast_print(it, text_x, by, font_batt, theme_primary, esphome::display::TextAlign::CENTER, batt_str);
  if (!compact) {
    fast_print(it, text_x + 1, by, font_batt, theme_primary, esphome::display::TextAlign::CENTER, batt_str);
  }
  const int bx = text_x - (compact ? (batt >= 100.0f ? 24 : 20) : (batt >= 100.0f ? 36 : 28));
  if (is_charging) {
    if (compact) {
      fast_filled_triangle(it, bx + 2, by - 6, bx - 2, by, bx + 1, by, col_gold);
      fast_filled_triangle(it, bx - 1, by, bx + 2, by, bx - 2, by + 6, col_gold);
    } else {
      fast_filled_triangle(it, bx + 3, by - 9, bx - 3, by, bx + 1, by, col_gold);
      fast_filled_triangle(it, bx - 1, by, bx + 2, by, bx - 2, by + 9, col_gold);
    }
  } else if (compact) {
    fill_rect_row_major(it, bx - 1, by - 6, 3, 1, col_green);
    fast_rect_outline(it, bx - 4, by - 5, 8, 11, col_green);
    const int fill_h = static_cast<int>(std::max(1.0f, std::min(7.0f, (batt / 100.0f) * 7.0f)));
    fill_rect_row_major(it, bx - 2, (by + 4) - fill_h, 4, fill_h, col_green);
  } else {
    fill_rect_row_major(it, bx - 2, by - 9, 4, 2, col_green);
    fast_rect_outline(it, bx - 5, by - 7, 10, 16, col_green);
    const int fill_h = static_cast<int>(std::max(1.0f, std::min(12.0f, (batt / 100.0f) * 12.0f)));
    fill_rect_row_major(it, bx - 3, (by + 7) - fill_h, 6, fill_h, col_green);
  }
}

// Home activity card transitions (D-028): what the card showed last, what it showed before that, and
// when the slide between them started (kMotionMs, ease-out cubic; 0 = settled).
struct HomeActivityCardState {
  int top = -1;
  int bottom = -1;
  int from_top = -1;
  int from_bottom = -1;
  uint32_t anim_start_ms = 0;
};

HomeActivityCardState &home_act_state() {
  static HomeActivityCardState st;
  return st;
}

// Computes an adaptive, slightly lighter 3D card palette that complements whichever
// weather sky background is currently active on the Home screen.
void resolve_home_card_palette(
    bool show_weather_bg, bool is_night, const std::string &cond,
    esphome::Color &out_card_bg, esphome::Color &out_card_brd, esphome::Color &out_btn_bg) {
  using esphome::Color;
  if (!show_weather_bg) {
    out_card_bg  = kChromeGlassTint;
    out_card_brd = kChromeCardBrdOn;
    out_btn_bg   = Color(32, 39, 52);
    return;
  }
  const bool is_fog = (cond.find("fog") != std::string::npos ||
                       cond.find("mist") != std::string::npos ||
                       cond.find("haze") != std::string::npos);
  if (is_night) {
    if (is_fog) {
      out_card_bg  = Color(28, 36, 48);
      out_card_brd = Color(58, 70, 88);
      out_btn_bg   = Color(38, 48, 62);
    } else {
      out_card_bg  = kChromeGlassTint;
      out_card_brd = kChromeCardBrdOn;
      out_btn_bg   = Color(32, 39, 52);
    }
    return;
  }

  const bool is_partly = (cond.find("partly") != std::string::npos ||
                          cond.find("mostlysunny") != std::string::npos ||
                          cond.find("partlysunny") != std::string::npos);
  const bool is_storm_or_rain = (cond.find("rain") != std::string::npos ||
                                 cond.find("pouring") != std::string::npos ||
                                 cond.find("drizzle") != std::string::npos ||
                                 cond.find("lightning") != std::string::npos ||
                                 cond.find("thunder") != std::string::npos);
  if (is_fog) {
    out_card_bg  = Color(50, 68, 88);
    out_card_brd = Color(84, 106, 132);
    out_btn_bg   = Color(62, 82, 104);
  } else if (is_storm_or_rain) {
    out_card_bg  = Color(34, 50, 74);
    out_card_brd = Color(64, 86, 116);
    out_btn_bg   = Color(44, 62, 90);
  } else if (cond.find("cloud") != std::string::npos && !is_partly) {
    out_card_bg  = Color(38, 58, 84);
    out_card_brd = Color(70, 96, 128);
    out_btn_bg   = Color(48, 72, 100);
  } else {
    out_card_bg  = Color(44, 76, 114);
    out_card_brd = Color(78, 116, 160);
    out_btn_bg   = Color(56, 92, 134);
  }
}

void draw_home_activity_row_contents(
    esphome::display::Display &it, int row_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, bool is_list_row, esphome::display::BaseFont *font_small,
    esphome::Color card_bg, esphome::Color btn_bg,
    esphome::Color theme_secondary, esphome::Color col_white,
    float alpha = 1.0f) {
  using esphome::Color;
  using esphome::display::TextAlign;

  const Color fg_w   = (alpha >= 0.99f) ? col_white       : lerp_color(card_bg, col_white, alpha);
  const Color fg_sec = (alpha >= 0.99f) ? theme_secondary : lerp_color(card_bg, theme_secondary, alpha);
  const Color fg_btn = (alpha >= 0.99f) ? btn_bg          : lerp_color(card_bg, btn_bg, alpha);

  const int cx_ref     = SCREEN_CX;
  const int list_w     = layout().list_card_w;
  const int main_w     = (SCREEN_W > 520) ? scale_x(380) : std::min(380, SCREEN_W - 32);
  const int card_w     = is_list_row ? list_w : main_w;
  const int card_left  = cx_ref - (card_w / 2);
  const int card_right = cx_ref + (card_w / 2);
  const int gx         = is_list_row ? (card_left + scale_x(18)) : (card_left + scale_x(28));
  const int txt_x      = is_list_row ? (card_left + scale_x(33)) : (card_left + scale_x(48));
  const int div1_x     = is_list_row ? (card_right - scale_x(44)) : (has_dropdown_ext ? (card_right - scale_x(108)) : (card_right - scale_x(56)));
  const int act_bx     = is_list_row ? (card_right - scale_x(22)) : (has_dropdown_ext ? (card_right - scale_x(80))  : (card_right - scale_x(28)));
  const int max_txt    = std::max(64, div1_x - txt_x - scale_x(8));
  const int btn_r      = is_list_row ? scale_s(13) : ((SCREEN_W < 380) ? 16 : scale_s(19));

  const int gy = row_y;
  if (is_list_row) {
    if (cur_act.kind == 1) {
      fill_rect_row_major(it, gx - 6, gy - 9, 13, 2, fg_w);
      fill_rect_row_major(it, gx - 6, gy + 8, 13, 2, fg_w);
      fast_line(it, gx - 5, gy - 7, gx - 1, gy, fg_w);
      fast_line(it, gx + 5, gy - 7, gx + 1, gy, fg_w);
      fast_line(it, gx - 1, gy, gx - 5, gy + 7, fg_w);
      fast_line(it, gx + 1, gy, gx + 5, gy + 7, fg_w);
      fast_filled_triangle(it, gx - 3, gy - 4, gx + 3, gy - 4, gx, gy - 1, fg_w);
      fast_filled_triangle(it, gx - 4, gy + 7, gx + 4, gy + 7, gx, gy + 2, fg_w);
    } else if (cur_act.kind == 2) {
      fill_rect_row_major(it, gx - 2, gy - 10, 5, 2, fg_w);
      fast_line(it, gx + 5, gy - 6, gx + 7, gy - 8, fg_w);
      fast_circle(it, gx, gy + 1, 8, fg_w);
      fast_circle(it, gx, gy + 1, 7, fg_sec);
      fast_line(it, gx, gy + 1, gx + 4, gy - 3, fg_w);
      fast_pixel(it, gx, gy + 1, fg_w);
    } else if (cur_act.kind == 3) {
      fast_filled_circle(it, gx - 4, gy + 5, 3, fg_w);
      fast_filled_circle(it, gx + 4, gy + 3, 3, fg_w);
      fill_rect_row_major(it, gx - 2, gy - 6, 2, 11, fg_w);
      fill_rect_row_major(it, gx + 5, gy - 8, 2, 11, fg_w);
      fast_line(it, gx - 2, gy - 6, gx + 6, gy - 8, fg_w);
      fast_line(it, gx - 2, gy - 5, gx + 6, gy - 7, fg_w);
    } else if (cur_act.kind == 5) {
      fast_rect_outline(it, gx - 7, gy - 6, 15, 14, fg_w);
      fill_rect_row_major(it, gx - 7, gy - 6, 15, 4, fg_w);
      fill_rect_row_major(it, gx - 4, gy - 8, 2, 3, fg_w);
      fill_rect_row_major(it, gx + 3, gy - 8, 2, 3, fg_w);
      fill_rect_row_major(it, gx - 4, gy + 1, 2, 2, fg_sec);
      fill_rect_row_major(it, gx,     gy + 1, 2, 2, fg_w);
      fill_rect_row_major(it, gx + 4, gy + 1, 2, 2, fg_w);
    } else {
      fast_filled_circle(it, gx - 6, gy - 6, 2, fg_w);
      fast_filled_circle(it, gx + 6, gy - 6, 2, fg_w);
      fast_circle(it, gx, gy + 1, 8, fg_w);
      fast_line(it, gx - 5, gy + 8, gx - 7, gy + 10, fg_w);
      fast_line(it, gx + 5, gy + 8, gx + 7, gy + 10, fg_w);
      fast_line(it, gx, gy + 1, gx,     gy - 4, fg_w);
      fast_line(it, gx, gy + 1, gx + 4, gy + 3, fg_w);
    }
  } else {
    if (cur_act.kind == 1) {
      fill_rect_row_major(it, gx - 9, gy - 12, 19, 2, fg_w);
      fill_rect_row_major(it, gx - 9, gy + 11, 19, 2, fg_w);
      fast_line(it, gx - 7, gy - 10, gx - 1, gy, fg_w);
      fast_line(it, gx - 6, gy - 10, gx,     gy, fg_w);
      fast_line(it, gx + 7, gy - 10, gx + 1, gy, fg_w);
      fast_line(it, gx + 6, gy - 10, gx,     gy, fg_w);
      fast_line(it, gx - 1, gy, gx - 7, gy + 10, fg_w);
      fast_line(it, gx,     gy, gx - 6, gy + 10, fg_w);
      fast_line(it, gx + 1, gy, gx + 7, gy + 10, fg_w);
      fast_line(it, gx,     gy, gx + 6, gy + 10, fg_w);
      fast_filled_triangle(it, gx - 4, gy - 6, gx + 4, gy - 6, gx, gy - 1, fg_w);
      fast_filled_triangle(it, gx - 5, gy + 9, gx + 5, gy + 9, gx, gy + 3, fg_w);
    } else if (cur_act.kind == 2) {
      fill_rect_row_major(it, gx - 3, gy - 13, 7, 2, fg_w);
      fill_rect_row_major(it, gx - 1, gy - 11, 3, 2, fg_w);
      fast_line(it, gx + 7, gy - 7, gx + 10, gy - 10, fg_w);
      fast_line(it, gx + 8, gy - 6, gx + 11, gy - 9,  fg_w);
      fast_circle(it, gx, gy + 2, 10, fg_w);
      fast_circle(it, gx, gy + 2, 9,  fg_w);
      fast_line(it, gx,     gy + 2, gx + 5, gy - 3, fg_w);
      fast_line(it, gx + 1, gy + 2, gx + 5, gy - 2, fg_w);
      fast_filled_circle(it, gx, gy + 2, 2, fg_w);
    } else if (cur_act.kind == 3) {
      fast_filled_circle(it, gx - 6, gy + 6, 4, fg_w);
      fast_filled_circle(it, gx + 5, gy + 4, 4, fg_w);
      fill_rect_row_major(it, gx - 3, gy - 9,  2, 15, fg_w);
      fill_rect_row_major(it, gx + 8, gy - 11, 2, 15, fg_w);
      fast_line(it, gx - 3, gy - 9, gx + 9, gy - 11, fg_w);
      fast_line(it, gx - 3, gy - 8, gx + 9, gy - 10, fg_w);
      fast_line(it, gx - 3, gy - 7, gx + 9, gy - 9,  fg_w);
    } else if (cur_act.kind == 5) {
      fast_rect_outline(it, gx - 10, gy - 9, 21, 20, fg_w);
      fast_rect_outline(it, gx - 9,  gy - 8, 19, 18, fg_w);
      fill_rect_row_major(it, gx - 9, gy - 8, 19, 5, fg_w);
      fill_rect_row_major(it, gx - 6, gy - 12, 3, 4, fg_w);
      fill_rect_row_major(it, gx + 4, gy - 12, 3, 4, fg_w);
      for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
          fill_rect_row_major(it, gx - 6 + c * 5, gy - 1 + r * 5, 3, 3, (r == 0 && c == 1) ? fg_sec : fg_w);
        }
      }
    } else {
      fast_filled_circle(it, gx - 8, gy - 7, 3, fg_w);
      fast_filled_circle(it, gx + 8, gy - 7, 3, fg_w);
      fast_circle(it, gx, gy + 2, 10, fg_w);
      fast_circle(it, gx, gy + 2, 9,  fg_w);
      fast_line(it, gx - 6, gy + 10, gx - 9, gy + 13, fg_w);
      fast_line(it, gx - 5, gy + 10, gx - 8, gy + 13, fg_w);
      fast_line(it, gx + 6, gy + 10, gx + 9, gy + 13, fg_w);
      fast_line(it, gx + 5, gy + 10, gx + 8, gy + 13, fg_w);
      fast_line(it, gx, gy + 2, gx,     gy - 4, fg_w);
      fast_line(it, gx, gy + 2, gx + 4, gy + 4, fg_w);
      fast_filled_circle(it, gx, gy + 2, 2, fg_w);
    }
  }

  struct FitActMemo {
    std::string raw{};
    std::string fitted{};
    int max_w{-1};
    const void *font{nullptr};
  };
  static FitActMemo fit_memos[4];
  static uint8_t fit_memo_rr = 0;
  const std::string *fit_ptr = nullptr;
  for (auto &m : fit_memos) {
    if (m.max_w == max_txt && m.font == font_small && m.raw == cur_act.label) {
      fit_ptr = &m.fitted;
      break;
    }
  }
  if (fit_ptr == nullptr) {
    auto &slot = fit_memos[fit_memo_rr & 3U];
    fit_memo_rr++;
    slot.raw = cur_act.label;
    slot.max_w = max_txt;
    slot.font = font_small;
    slot.fitted = fit_text_to_width_font(cur_act.label, max_txt, font_small);
    fit_ptr = &slot.fitted;
  }
  fast_print(it, txt_x, gy, font_small, fg_w, TextAlign::CENTER_LEFT, fit_ptr->c_str());

  render_glass_circle(it, act_bx, gy, btn_r, fg_btn, fg_sec);

  const int sym_s = is_list_row ? 5 : 6;
  if (cur_act.kind == 5) {
    for (int w = -1; w <= 0; w++) {
      fast_line(it, act_bx - 3 + w, gy - sym_s, act_bx + 3 + w, gy, fg_w);
      fast_line(it, act_bx + 3 + w, gy, act_bx - 3 + w, gy + sym_s, fg_w);
    }
  } else if (cur_act.kind == 4) {
    for (int w = -1; w <= 0; w++) {
      fast_line(it, act_bx - sym_s + w, gy - sym_s, act_bx + sym_s + w, gy + sym_s, fg_w);
      fast_line(it, act_bx - sym_s + w, gy + sym_s, act_bx + sym_s + w, gy - sym_s, fg_w);
    }
  } else if (cur_act.is_active) {
    fill_rect_row_major(it, act_bx - 5, gy - sym_s, 3, sym_s * 2 + 1, fg_w);
    fill_rect_row_major(it, act_bx + 3, gy - sym_s, 3, sym_s * 2 + 1, fg_w);
  } else {
    fast_filled_triangle(it, act_bx - 4, gy - sym_s, act_bx - 4, gy + sym_s, act_bx + sym_s + 1, gy, fg_w);
  }
}

void render_home_activity_card_row(
    esphome::display::Display &it, int cx, int pill_y, const HomeActivityItem &cur_act,
    bool has_dropdown_ext, esphome::display::BaseFont *font_small,
    esphome::Color theme_secondary, esphome::Color col_white,
    bool is_list_row,
    esphome::Color card_bg,
    esphome::Color card_brd,
    esphome::Color btn_bg) {
  const int card_w     = is_list_row ? layout().list_card_w : ((SCREEN_W > 520) ? scale_x(380) : std::min(380, SCREEN_W - 32));
  const int card_h     = is_list_row ?  44 : 68;
  const int card_r     = is_list_row ?  12 : 16;
  const int card_right = cx + (card_w / 2);
  const int div1_x     = is_list_row ? (card_right - scale_x(44)) : (has_dropdown_ext ? (card_right - scale_x(108)) : (card_right - scale_x(56)));
  const int div_h      = is_list_row ?  scale_y(14) : scale_y(23);
  const int div2_x     = card_right - scale_x(52);
  const int drop_bx    = card_right - scale_x(26);

  const esphome::Color surf_bg = render_modern_card(it, cx, pill_y, card_w, card_h, card_r, card_bg, card_brd,
                                                    nullptr, col_white, font_small);

  draw_home_activity_row_contents(
      it, pill_y, cur_act, has_dropdown_ext, is_list_row, font_small,
      surf_bg, btn_bg, theme_secondary, col_white, 1.0f);

  fast_line(it, div1_x, pill_y - div_h, div1_x, pill_y + div_h, card_brd);

  if (has_dropdown_ext) {
    fast_line(it, div2_x, pill_y - div_h, div2_x, pill_y + div_h, card_brd);
    for (int w = -1; w <= 1; w++) {
      fast_line(it, drop_bx - 8, pill_y - 4 + w, drop_bx, pill_y + 4 + w, col_white);
      fast_line(it, drop_bx + 8, pill_y - 4 + w, drop_bx, pill_y + 4 + w, col_white);
    }
  }
}

const char *resolve_wake_greeting(int hour, int minute, int day_of_year,
                                  const std::string &cond, float temp_f) {
  const int pick = (day_of_year + hour * 3 + (minute / 10)) % 3;

  if (!cond.empty()) {
    if (cond.find("lightning") != std::string::npos || cond.find("thunder") != std::string::npos) {
      return "Stay dry out there";
    }
    if (cond.find("rain") != std::string::npos || cond.find("pouring") != std::string::npos ||
        cond.find("drizzle") != std::string::npos) {
      return (pick == 0) ? "Rainy out there today" : "Don't forget an umbrella";
    }
    if (cond.find("snow") != std::string::npos || cond.find("sleet") != std::string::npos) {
      return "Bundle up out there";
    }
    if (cond.find("wind") != std::string::npos || cond.find("breez") != std::string::npos) {
      return (pick == 0) ? "Breezy out there today" : "Hold onto your hat today";
    }
    if ((cond == "sunny" || cond == "clear-day" || cond == "clearday") &&
        hour >= 9 && hour < 17 && !std::isnan(temp_f) && temp_f >= 64.0f && temp_f <= 85.0f && pick == 0) {
      return "It's a beautiful day";
    }
  }

  if (hour >= 5 && hour < 12) {
    if (pick == 1) return "Rise and shine";
    if (pick == 2) return "Have a great day";
    return "Good morning";
  }
  if (hour >= 12 && hour < 17) {
    if (pick == 1) return "Hope your day is going well";
    return "Good afternoon";
  }
  if (hour >= 17 && hour < 22) {
    if (pick == 1) return "Time to unwind";
    return "Good evening";
  }
  if (pick == 1) return "Up late tonight?";
  if (pick == 2) return "Rest easy tonight";
  return "Sleep well";
}

template <int XMin, int XMax, int YMin, int YMax, typename DisplayT, typename PredFn>
inline void draw_cached_dock_mask(DisplayT &it, int bx, int by,
                                  esphome::Color c, esphome::Color sh_c, PredFn &&pred) {
  static constexpr int kW = XMax - XMin + 1;
  static constexpr int kH = YMax - YMin + 1;
  static_assert(kW + 1 <= 64, "Dock glyph bitmask width must fit in uint64_t");
  static uint64_t rows[kH + 1] = {};
  static bool ready = false;
  if (!ready) {
    for (int ry = 0; ry <= kH; ry++) {
      uint64_t m = 0;
      for (int rx = 0; rx <= kW; rx++) {
        if (pred(XMin + rx, YMin + ry)) m |= (uint64_t{1} << rx);
      }
      rows[ry] = m;
    }
    ready = true;
  }
  const uint64_t col_mask = (uint64_t{1} << kW) - 1U;
  for (int ry = 0; ry < kH; ry++) {
    uint64_t m = rows[ry] & col_mask;
    const uint64_t next_row = rows[ry + 1];
    const int py = by + YMin + ry;
    while (m != 0) {
      const int rx = std::countr_zero(m);
      m &= (m - 1U);
      const int px = bx + XMin + rx;
      if (((next_row >> (rx + 1)) & 1ULL) == 0ULL) {
        fast_pixel(it, px + 1, py + 1, sh_c);
      }
      fast_pixel(it, px, py, c);
    }
  }
}

}  // namespace

// ==============================================================================
// Shared Home / Activity Deck State & Helpers
// ==============================================================================

bool &music_queue_cleared() {
  static bool cleared = false;
  return cleared;
}

int &home_activity_overlay_page() {
  static int pg = 0;
  return pg;
}

uint32_t &wake_greeting_start_ms() {
  static uint32_t ts = 0;
  return ts;
}

bool &wake_greeting_latched() {
  static bool latched = false;
  return latched;
}

bool is_wake_greeting_fading(uint32_t now_ms, bool enabled) {
  if (!enabled || !wake_greeting_latched()) return false;
  const uint32_t el = now_ms - wake_greeting_start_ms();
  return (el >= 7900U && el <= 10700U);
}

bool is_home_activity_carousel_animating(uint32_t now_ms) {
  const uint32_t t0 = home_act_state().anim_start_ms;
  return t0 != 0 && (now_ms - t0) < kMotionMs;
}

int collect_home_activities(
    uint32_t now_ms, bool is_24h_format,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    HomeActivityItem out_items[8],
    int cur_hour, int cur_min,
    const std::string &calendar_agenda_raw) {
  int count = 0;

  // 1. Active or Paused Countdown Timers (locks top slot ONLY if running with <= 60 seconds left)
  auto *t_slots = timer_slots();
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < 3 && count < 8; i++) {
      const bool match = (pass == 0) ? t_slots[i].running : (!t_slots[i].running && t_slots[i].paused);
      if (match) {
        const auto &s = t_slots[i];
        const std::string t_name = s.name.empty() ? ("Timer " + std::to_string(i + 1)) : s.name;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s • %02d:%02d",
                      t_name.c_str(), s.remaining_sec / 60, s.remaining_sec % 60);
        const bool is_urgent = s.running && (s.remaining_sec <= 60);
        out_items[count++] = {1, i, s.running, is_urgent, true, std::string(buf)};
      }
    }
  }

  // 2. Active or Paused Stopwatch (locks top slot whenever actively running)
  const uint32_t sw_ms = stopwatch_running ? (now_ms - stopwatch_start_time) : stopwatch_elapsed_ms;
  if ((stopwatch_running || sw_ms > 0) && count < 8) {
    const int mins = static_cast<int>((sw_ms / 60000U) % 60U);
    const int secs = static_cast<int>((sw_ms / 1000U) % 60U);
    const int tenths = static_cast<int>((sw_ms / 100U) % 10U);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Stopwatch • %02d:%02d.%d", mins, secs, tenths);
    out_items[count++] = {2, 0, stopwatch_running, stopwatch_running, true, std::string(buf)};
  }

  // 3. Active or Paused Music Queue
  const bool has_valid_track = !music_title.empty();
  if (music_playing) {
    music_queue_cleared() = false;
  }
  if ((music_playing || (has_valid_track && !music_queue_cleared())) && count < 8) {
    std::string m_txt = music_title;
    if (!has_valid_track) {
      m_txt = "Now Playing";
    } else if (!music_artist.empty()) {
      m_txt = music_artist + " • " + m_txt;
    }
    out_items[count++] = {3, 0, music_playing, false, true, m_txt};
  }

  // 4. Armed or Snoozed Alarms
  if (alarm_snooze_until > 0 && count < 8) {
    out_items[count++] = {4, 0, true, false, true, "Alarm • Snoozed"};
  } else {
    const int a_hrs[3] = {alarm1_h, alarm2_h, alarm3_h};
    const int a_mns[3] = {alarm1_m, alarm2_m, alarm3_m};
    const bool a_ens[3] = {alarm1_en, alarm2_en, alarm3_en};
    const int now_total_m = (cur_hour >= 0 && cur_min >= 0) ? (cur_hour * 60 + cur_min) : -1;
    for (int i = 0; i < 3 && count < 8; i++) {
      if (!a_ens[i]) continue;
      char t_buf[16], ap_buf[8] = "";
      format_time_hhmm(a_hrs[i], a_mns[i], is_24h_format, t_buf, sizeof(t_buf), ap_buf);
      char buf[48];
      if (!is_24h_format && ap_buf[0] != '\0') {
        std::snprintf(buf, sizeof(buf), "Alarm • %s %s", t_buf, ap_buf);
      } else {
        std::snprintf(buf, sizeof(buf), "Alarm • %s", t_buf);
      }
      bool within_4h = false;
      if (now_total_m >= 0) {
        const int a_total_m = a_hrs[i] * 60 + a_mns[i];
        const int diff_m = (a_total_m - now_total_m + 1440) % 1440;
        if (diff_m > 0 && diff_m <= 240) {
          within_4h = true;
        }
      }
      out_items[count++] = {4, i + 1, false, false, within_4h, std::string(buf)};
    }
  }

  // 5. Upcoming Calendar Event
  CalendarRecord first_rec[1];
  if (count < 8 && parse_calendar_records(calendar_agenda_raw, first_rec, 1) == 1) {
    const bool is_today = first_rec[0].time.rfind("TODAY", 0) == 0;
    out_items[count++] = {5, 0, false, false, is_today,
                          std::string(first_rec[0].time) + " • " + std::string(first_rec[0].title)};
  }

  // Device's Latter Activity Card setting (D-029)
  const uint8_t mask = activity_source_mask();
  int kept = 0;
  for (int i = 0; i < count; i++) {
    if (mask & (1U << out_items[i].kind)) {
      if (kept != i) out_items[kept] = out_items[i];
      kept++;
    }
  }
  return kept;
}

HomeActivityView resolve_home_activity_view(const HomeActivityItem items[8], int count,
                                            uint32_t now_ms, bool two_line) {
  HomeActivityView v;
  if (count <= 0) return v;
  int pin = -1;
  for (int i = 0; i < count && pin < 0; i++) {
    if (items[i].kind == 2 && items[i].urgent_pin) pin = i;
  }
  for (int i = 0; i < count && pin < 0; i++) {
    if (items[i].kind == 1 && items[i].urgent_pin) pin = i;
  }
  int deck[8];
  int n = 0;
  for (int i = 0; i < count; i++) {
    if (items[i].in_deck && i != pin) deck[n++] = i;
  }
  const uint32_t slot = now_ms / kHomeActivityRotateMs;
  if (pin >= 0) {
    v.top = pin;
    v.pinned = true;
    if (two_line && n > 0) v.bottom = deck[slot % static_cast<uint32_t>(n)];
    return v;
  }
  if (n == 0) {
    v.top = 0;
    return v;
  }
  const int lines = two_line ? 2 : 1;
  if (n <= lines) {
    v.top = deck[0];
    if (two_line && n > 1) v.bottom = deck[1];
    return v;
  }
  v.top = deck[slot % static_cast<uint32_t>(n)];
  if (two_line) v.bottom = deck[(slot + 1U) % static_cast<uint32_t>(n)];
  return v;
}

int get_activity_overlay_card_y(int vis_count, int row_idx) {
  const int stride = layout().slot5_stride;
  const int center_y = layout().slot5_cy;
  const int total_span = (vis_count - 1) * stride;
  const int start_y = center_y - (total_span / 2);
  return start_y + row_idx * stride;
}

// ==============================================================================
// Page 0: Clock Page Engine (Digital & Analog Watch Faces & Battery Gauge)
// ==============================================================================

void render_clock_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  frame_base(it, ctx.col_bg);

  const auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const bool is_24h_format = prefs.is_24h_format;
  const float batt = rs.hw_batt_level;
  const bool is_charging = rs.hw_batt_charging;
  const std::string &clock_face_opt = ctx.clock_face_style_opt;
  esphome::display::BaseFont *font_batt =
      (SCREEN_W < 380 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;

  const bool is_minimal_digital = (clock_face_opt == "Minimal Digital");
  const bool is_ring_digital    = (clock_face_opt == "Modern Digital (Bold)") ||
                                  (clock_face_opt.empty() && prefs.clock_face_digital);

  if (is_minimal_digital) {
#if LATTER_ROUND_DISPLAY
    fast_circle(it, cx, cy, 230, ctx.col_dark_gray);
    render_battery_indicator(it, cx, 348, batt, is_charging, font_batt,
                             ctx.theme_primary, ctx.col_gold, ctx.col_green);
#else
    fast_rect_outline(it, 6, 6, SCREEN_W - 12, SCREEN_H - 12, ctx.col_dark_gray);
#if LATTER_HAS_BATTERY
    render_battery_indicator(it, layout().battery_cx, layout().battery_y, batt, is_charging, font_batt,
                             ctx.theme_primary, ctx.col_gold, ctx.col_green);
#endif
#endif
    frame_hits().add_centered_rect(cx, cy - scale_y(8), scale_x(260), scale_y(96),
                                   WidgetId::HOME_TIME_READOUT, kHitTap);
    if (!ctx.now.is_valid()) {
      fast_print(it, cx, cy - scale_y(8), ctx.font_time, ctx.col_slate,
                 esphome::display::TextAlign::CENTER, kUnknownGlyph);
      return;
    }
    char date_buf[32];
    esphome::ESPTime now_copy = ctx.now;
    now_copy.strftime(date_buf, sizeof(date_buf), "%a, %b %d");
    fast_print(it, cx, cy - scale_y(82), ctx.font_date, ctx.theme_primary,
               esphome::display::TextAlign::CENTER, date_buf);

    char sq_dig[16], sq_ap[8] = "";
    format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, sq_dig, sizeof(sq_dig), sq_ap);
    fast_print(it, cx, cy - scale_y(8), ctx.font_time, ctx.col_white,
               esphome::display::TextAlign::CENTER, sq_dig);

    char sq_sec[32];
    if (is_24h_format) {
      std::snprintf(sq_sec, sizeof(sq_sec), "SEC  •  %02ds", ctx.now.second);
    } else {
      std::snprintf(sq_sec, sizeof(sq_sec), "%s  •  %02ds", sq_ap, ctx.now.second);
    }
    fast_print(it, cx, cy + scale_y(56), ctx.font_small, ctx.theme_secondary,
               esphome::display::TextAlign::CENTER, sq_sec);
    return;
  }

  // FACE 1 (MODERN ROMAN ANALOG) & FACE 2 (MODERN DIGITAL WITH CHAPTER RING)
  const float dial_scale = static_cast<float>(SCREEN_MIN_DIM) / 466.0f;
  const int r_outer = static_cast<int>(230.0f * dial_scale);
  const int r_mid   = static_cast<int>(228.0f * dial_scale);
  const int r_inner = static_cast<int>(222.0f * dial_scale);
  const int r_t_out = static_cast<int>(220.0f * dial_scale);
  const int r_t_in5 = static_cast<int>(212.0f * dial_scale);
  const int r_t_in1 = static_cast<int>(216.0f * dial_scale);

  fast_circle(it, cx, cy, r_outer, ctx.col_dark_gray);
  fast_circle(it, cx, cy, r_mid, ctx.col_slate);
  fast_circle(it, cx, cy, r_inner, ctx.col_dark_gray);

  for (int i = 0; i < 60; i++) {
    if (!is_ring_digital && i == 30) continue;
    const float s = DIAL_SIN[i];
    const float c = DIAL_COS[i];
    const int r_in = (i % 5 == 0) ? r_t_in5 : r_t_in1;
    const esphome::Color &tick_col = (i % 5 == 0) ? ctx.col_slate : ctx.col_gray;
    fast_line(it, cx + static_cast<int>(r_in * s), cy - static_cast<int>(r_in * c),
              cx + static_cast<int>(r_t_out * s), cy - static_cast<int>(r_t_out * c), tick_col);
  }

#if LATTER_HAS_BATTERY
  const int dial_batt_y = cy + static_cast<int>(115.0f * dial_scale);
  render_battery_indicator(it, cx, dial_batt_y, batt, is_charging, font_batt,
                           ctx.theme_primary, ctx.col_gold, ctx.col_green);
#endif

  frame_hits().add_centered_rect(cx, cy, scale_x(260), scale_y(160),
                                 WidgetId::HOME_TIME_READOUT, kHitTap);

  if (!ctx.now.is_valid()) {
    if (is_ring_digital) {
      fast_print(it, cx, cy - static_cast<int>(28.0f * dial_scale), ctx.font_time, ctx.col_slate,
                 esphome::display::TextAlign::CENTER, kUnknownGlyph);
    } else {
      fast_print(it, cx, cy + static_cast<int>(72.0f * dial_scale), ctx.font_date, ctx.col_slate,
                 esphome::display::TextAlign::CENTER, kUnknownGlyph);
      fast_filled_circle(it, cx, cy, 7, ctx.col_white);
      fast_filled_circle(it, cx, cy, 4, ctx.col_red);
      fast_filled_circle(it, cx, cy, 2, ctx.col_dark_gray);
    }
    return;
  }

  const int hour = ctx.now.hour;
  const int min = ctx.now.minute;
  const int sec = ctx.now.second;
  const int sec_idx = ((sec % 60) + 60) % 60;

  char date_buf[32];
  esphome::ESPTime now_copy = ctx.now;
  now_copy.strftime(date_buf, sizeof(date_buf), "%a, %b %d");

  if (is_ring_digital) {
    const float s_sec = DIAL_SIN[sec_idx];
    const float c_sec = DIAL_COS[sec_idx];
    const float perp_x = -c_sec;
    const float perp_y = -s_sec;
    const float r_s_in = 205.0f * dial_scale;
    const float r_s_out = 220.0f * dial_scale;
    for (int w = -1; w <= 1; w++) {
      const int x1 = cx + static_cast<int>(r_s_in * s_sec + w * perp_x);
      const int y1 = cy - static_cast<int>(r_s_in * c_sec - w * perp_y);
      const int x2 = cx + static_cast<int>(r_s_out * s_sec + w * perp_x);
      const int y2 = cy - static_cast<int>(r_s_out * c_sec - w * perp_y);
      fast_line(it, x1, y1, x2, y2, ctx.theme_primary);
    }

    fast_print(it, cx, cy - static_cast<int>(103.0f * dial_scale), ctx.font_date, ctx.theme_primary,
               esphome::display::TextAlign::CENTER, date_buf);

    char dig_buf[16];
    char ap_str[8] = "";
    format_time_hhmm(hour, min, is_24h_format, dig_buf, sizeof(dig_buf), ap_str);
    fast_print(it, cx, cy - static_cast<int>(28.0f * dial_scale), ctx.font_time, ctx.col_white,
               esphome::display::TextAlign::CENTER, dig_buf);

    char sec_buf[32];
    if (is_24h_format) {
      std::snprintf(sec_buf, sizeof(sec_buf), "SEC  •  %02ds", sec);
    } else {
      std::snprintf(sec_buf, sizeof(sec_buf), "%s  •  %02ds", ap_str, sec);
    }
    fast_print(it, cx, cy + static_cast<int>(29.0f * dial_scale), ctx.font_small, ctx.theme_secondary,
               esphome::display::TextAlign::CENTER, sec_buf);
  } else {
    fast_print(it, cx, cy - static_cast<int>(129.0f * dial_scale), ctx.font_date, ctx.theme_primary,
               esphome::display::TextAlign::CENTER, date_buf);
    char time_buf[32];
    if (is_24h_format) {
      std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour, min);
    } else {
      int h12 = hour % 12;
      if (h12 == 0) h12 = 12;
      std::snprintf(time_buf, sizeof(time_buf), "%d:%02d %s", h12, min, hour >= 12 ? "pm" : "am");
    }
    fast_print(it, cx, cy + static_cast<int>(72.0f * dial_scale), ctx.font_date, ctx.col_slate,
               esphome::display::TextAlign::CENTER, time_buf);

    const float h_angle = ((hour % 12) + min / 60.0f + sec / 3600.0f) * (2.0f * PI_F / 12.0f);
    const float m_angle = (min + sec / 60.0f) * (2.0f * PI_F / 60.0f);

    {
      float sin_h = 0.0f, cos_h = 1.0f;
      fast_sincos(h_angle, sin_h, cos_h);
      const float h_len = 88.0f * dial_scale;
      fast_filled_triangle(it, cx - static_cast<int>(7.0f * cos_h), cy - static_cast<int>(7.0f * sin_h),
                           cx + static_cast<int>(7.0f * cos_h), cy + static_cast<int>(7.0f * sin_h),
                           cx + static_cast<int>(h_len * sin_h), cy - static_cast<int>(h_len * cos_h),
                           ctx.col_white);
    }

    {
      float sin_m = 0.0f, cos_m = 1.0f;
      fast_sincos(m_angle, sin_m, cos_m);
      const float m_len = 148.0f * dial_scale;
      fast_filled_triangle(it, cx - static_cast<int>(4.0f * cos_m), cy - static_cast<int>(4.0f * sin_m),
                           cx + static_cast<int>(4.0f * cos_m), cy + static_cast<int>(4.0f * sin_m),
                           cx + static_cast<int>(m_len * sin_m), cy - static_cast<int>(m_len * cos_m),
                           ctx.col_slate);
    }

    {
      const float sin_s = DIAL_SIN[sec_idx];
      const float cos_s = DIAL_COS[sec_idx];
      const int tip_x = cx + static_cast<int>(168.0f * dial_scale * sin_s);
      const int tip_y = cy - static_cast<int>(168.0f * dial_scale * cos_s);
      const int tail_x = cx - static_cast<int>(30.0f * dial_scale * sin_s);
      const int tail_y = cy + static_cast<int>(30.0f * dial_scale * cos_s);
      fast_line(it, tail_x, tail_y, tip_x, tip_y, ctx.col_red);
      fast_filled_circle(it, tail_x, tail_y, 4, ctx.col_red);
    }

    fast_filled_circle(it, cx, cy, 7, ctx.col_white);
    fast_filled_circle(it, cx, cy, 4, ctx.col_red);
    fast_filled_circle(it, cx, cy, 2, ctx.col_dark_gray);
  }
}

// ==============================================================================
// Page 1: Interactive Home Page Engine (Weather Sky + Activity Deck + Dock)
// ==============================================================================

void render_home_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  using esphome::Color;
  using esphome::display::TextAlign;

  auto &rs = runtime_state();
  const auto &prefs = persisted_prefs();
  const auto &ac = area_ctx_state();
  const auto &ws = weather_state();
  const auto &ms = media_state();
  const auto &cs = calendar_state();

  const int cx = ctx.cx;
  const int cy = ctx.cy;
  const uint32_t now_ms = ctx.now_ms;
  const int home_subpage = rs.nav.home_subpage;
  const bool is_24h_format = prefs.is_24h_format;
  const bool show_weather_bg = ctx.home_weather_bg;
  const bool show_wake_greeting = ctx.home_wake_greeting;
  const bool show_sun = ctx.home_sun_arc;
  const bool show_moon = ctx.home_moon_phase;

  const bool is_rem_media = !ms.is_local;
  const bool music_playing =
      is_music_playing_effective(is_rem_media, ctx.local_media_playing, ms.state, now_ms);
  const std::string &music_title =
      (is_rem_media || ctx.track_title.empty()) ? ms.title : ctx.track_title;
  const std::string &music_artist =
      (is_rem_media || ctx.track_artist.empty()) ? ms.artist : ctx.track_artist;

  const bool has_custom_wallpaper = (!ac.wallpaper_url.empty() &&
                                     ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                                     ctx.home_wallpaper_art.get_width() > 0);

  if (home_subpage == 1) {
    render_page_wallpaper_base(it, ctx.home_wallpaper_art, has_custom_wallpaper, cx, ctx.col_bg);
    render_page_header(it, cx, "HOME SETTINGS", ctx.font_small, ctx.theme_primary);

    static const char *const kLabels[5] = {"Music", "Area", "Remote", "Games", "Tools"};
    char buf[48];
    for (int i = 0; i < 5; i++) {
      const bool shown = dock_app_listed(kHomeSettingsDockTokens[i]);
      std::snprintf(buf, sizeof(buf), "%s Icon: %s", kLabels[i], shown ? "Shown" : "Hidden");
      const int y_mid = layout().slot5_y(i);
      const Color bg_c  = shown ? kChromeGlassTint : kChromeCardBgOff;
      const Color brd_c = shown ? kChromeCardBrdOn : kChromeCardBrdOff;
      const Color txt_c = shown ? ctx.col_white    : ctx.col_slate;
      render_modern_card(it, cx, y_mid, layout().list_card_w, 44, 12,
                         bg_c, brd_c, buf, txt_c, ctx.font_small);
      frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                     WidgetId::LIST_SLOT_BASE + i, kHitTap | kHitFocusSlot,
                                     static_cast<int16_t>(i));
    }

    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  HomeActivityItem act_items[8];
  const int act_count = collect_home_activities(
      now_ms, is_24h_format,
      rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
      music_playing, music_title, music_artist,
      rs.alarms.snooze_until,
      prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
      prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
      prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
      act_items,
      ctx.now.is_valid() ? ctx.now.hour : -1,
      ctx.now.is_valid() ? ctx.now.minute : -1,
      cs.summary);

  std::string cond = ws.condition;
  bool is_night = (ws.sun_state == "below_horizon" || cond == "clear-night" ||
                   cond == "clearnight" || (cond.find("night") != std::string::npos));
  apply_weather_sim_override(ctx.weather_sim_opt, cond, is_night);

  const std::string cond_title = format_weather_title(cond);
  const bool sim_override_on = (!ctx.weather_sim_opt.empty() && ctx.weather_sim_opt != "Live (Home Assistant)");
  const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
  const bool weather_ready = ((!std::isnan(ws.temperature) && sun_ready) || sim_override_on) &&
                             (cond_title != "Unavailable");
  const bool eff_weather_bg = show_weather_bg && weather_ready;

  const bool is_dim_backdrop = (home_subpage == 2);
  const bool use_custom_wallpaper = has_custom_wallpaper && !show_weather_bg;
  const int cel_x = layout().celestial_x;
  const int cel_y = layout().celestial_y;
  const bool show_celestial = is_night ? show_moon : show_sun;

  if (use_custom_wallpaper) {
    render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false,
                            is_dim_backdrop ? 2 : 0);
  } else if (eff_weather_bg) {
    backdrop_active() = true;
    if (is_dim_backdrop) {
      frame_base(it, ctx.col_bg);
      if (!is_night) {
#if LATTER_ROUND_DISPLAY
        fast_filled_circle(it, cx, cy, 228, Color(7, 13, 22));
#else
        fill_rect_row_major(it, 0, 0, SCREEN_W, SCREEN_H, Color(7, 13, 22));
#endif
      }
      if (show_celestial) {
        fast_filled_circle(it, cel_x, cel_y, LATTER_ROUND_DISPLAY ? 23 : scale_min(27),
                           is_night ? Color(22, 26, 34) : Color(42, 36, 12));
      }
    } else {
      double moon_cycle = 0.5;
      if (is_night) {
        compute_synodic_moon_phase(ctx.now.is_valid(),
                                   ctx.now.is_valid() ? static_cast<int64_t>(ctx.now.timestamp) : 0LL,
                                   ws.moon_phase, moon_cycle, nullptr, nullptr);
      }
      render_weather_motion_wallpaper(it, cx, cy, now_ms, cond, is_night,
                                      moon_cycle, cel_x, cel_y, true,
                                      rs.hw_accel_x, rs.hw_accel_y, show_celestial);
    }
  }
  frame_base(it, ctx.col_bg);

  // SUBPAGE 2: Frosted Glass Auto-Centered Live Activity Overlay
  if (home_subpage == 2) {
    char hdr_buf[36] = "ACTIVE ITEMS";
    if (ctx.now.is_valid()) {
      char t_sub[16];
      format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, t_sub, sizeof(t_sub));
      std::snprintf(hdr_buf, sizeof(hdr_buf), "ACTIVE  •  %s", t_sub);
    }
    render_page_header(it, cx, hdr_buf, ctx.font_small, ctx.theme_primary, true);

    Color ov_bg = kChromeGlassTint;
    Color ov_brd(56, 68, 86);
    Color ov_btn(34, 42, 56);
    if (use_custom_wallpaper) {
      ov_bg  = sample_image_ambient_tint(ctx.home_wallpaper_art, Color(20, 25, 34));
      ov_btn = sample_image_ambient_tint(ctx.home_wallpaper_art, Color(32, 40, 54));
    } else if (eff_weather_bg) {
      resolve_home_card_palette(eff_weather_bg, is_night, cond, ov_bg, ov_brd, ov_btn);
    }

    if (act_count <= 0) {
      render_empty_state(it, cx, cy, "No active activities", ctx.font_small, ctx.col_slate);
    } else {
      const int total_pages = (act_count > 5) ? 2 : 1;
      if (home_activity_overlay_page() >= total_pages) home_activity_overlay_page() = 0;
      if (home_activity_overlay_page() < 0) home_activity_overlay_page() = 0;

      const int start_i = home_activity_overlay_page() * 5;
      const int vis_cnt = std::min(5, act_count - start_i);

      for (int row = 0; row < vis_cnt; row++) {
        const int y_mid = get_activity_overlay_card_y(vis_cnt, row);
        render_home_activity_card_row(
            it, cx, y_mid, act_items[start_i + row], false, ctx.font_small,
            ctx.theme_secondary, ctx.col_white, true, ov_bg, ov_brd, ov_btn);
        frame_hits().add_centered_rect(cx, y_mid, layout().list_card_w, layout().slot5_h,
                                       WidgetId::LIST_SLOT_BASE + row, kHitTap | kHitFocusSlot,
                                       static_cast<int16_t>(start_i + row));
      }

      if (total_pages > 1) {
        const int chev_x = LATTER_ROUND_DISPLAY ? scale_min(415) : (SCREEN_W - scale_x(36));
        if (home_activity_overlay_page() > 0) {
          render_scroll_chevron(it, chev_x, scale_y(162), true, ctx.theme_secondary, ctx.col_black, true);
          frame_hits().add_circle(chev_x, scale_y(162), scale_s(28), WidgetId::HOME_ACTIVITY_PREV, kHitTap);
        }
        if (home_activity_overlay_page() + 1 < total_pages) {
          render_scroll_chevron(it, chev_x, scale_y(278), false, ctx.theme_secondary, ctx.col_black, true);
          frame_hits().add_circle(chev_x, scale_y(278), scale_s(28), WidgetId::HOME_ACTIVITY_NEXT, kHitTap);
        }
      }
    }

    render_back_button(it, cx, ctx.font_small, layout().back_y, "< Back");
    return;
  }

  if (eff_weather_bg && show_celestial) {
    frame_hits().add_circle(cel_x, cel_y, LATTER_ROUND_DISPLAY ? scale_min(30) : scale_min(34),
                            WidgetId::HOME_QUICK_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(10));
  }

  const Color fg_home_white = ctx.col_white;
  const Color fg_home_cond  = (!eff_weather_bg || is_night) ? ctx.theme_secondary : Color(255, 225, 110);

  const int z_date_y  = layout().home_date_y;
  const int z_clock_y = layout().home_clock_y;
  const int z_weath_y = layout().home_weather_y;

  ProfLap home_lap;
  // ZONE 1: Centered Date Header
  if (ctx.now.is_valid()) {
    char date_bar[32];
    esphome::ESPTime now_copy = ctx.now;
    now_copy.strftime(date_bar, sizeof(date_bar), "%a, %b %d");
    fast_print(it, cx + 1, z_date_y + 1, ctx.font_date, ctx.col_black, TextAlign::CENTER, date_bar);
    fast_print(it, cx, z_date_y, ctx.font_date, fg_home_white, TextAlign::CENTER, date_bar);
  }

  // ZONE 2: Hero Digital Clock (Tap -> Clock)
  char time_str[16];
  std::snprintf(time_str, sizeof(time_str), "%s", kUnknownGlyph);
  if (ctx.now.is_valid()) {
    format_time_hhmm(ctx.now.hour, ctx.now.minute, is_24h_format, time_str, sizeof(time_str));
  }
  fast_print(it, cx + 1, z_clock_y + 1, ctx.font_time, ctx.col_black, TextAlign::CENTER, time_str);
  fast_print(it, cx, z_clock_y, ctx.font_time, fg_home_white, TextAlign::CENTER, time_str);
  frame_hits().add_centered_rect(cx, z_clock_y, scale_x(260), scale_y(72),
                                 WidgetId::HOME_TIME_READOUT, kHitTap | kHitFocusSlot);

  // ZONE 3: Stepped-Up Weather Summary
  if (weather_ready) {
    char w_line[64];
    if (!std::isnan(ws.temperature)) {
      std::snprintf(w_line, sizeof(w_line), "%.0f° • %s", ws.temperature, cond_title.c_str());
    } else {
      std::snprintf(w_line, sizeof(w_line), "%s", cond_title.c_str());
    }
    const int max_w_span = std::min(scale_x(340), safe_span(z_weath_y));
    const std::string fit_w = fit_text_to_width_font(std::string(w_line), max_w_span, ctx.font_date);
    fast_print(it, cx + 1, z_weath_y + 1, ctx.font_date, ctx.col_black, TextAlign::CENTER, fit_w.c_str());
    fast_print(it, cx, z_weath_y, ctx.font_date, fg_home_cond, TextAlign::CENTER, fit_w.c_str());
    frame_hits().add_centered_rect(cx, z_weath_y, std::min(scale_x(300), max_w_span), scale_y(36),
                                   WidgetId::HOME_WEATHER_HERO, kHitTap | kHitFocusSlot);
  }

  // Transient Wake Greeting Latch (8.0s Full Brightness + 2.5s Smooth Fade-Out = 10.5s Total)
  if (!wake_greeting_latched() && ctx.now.is_valid() && now_ms < 25000U) {
    wake_greeting_start_ms() = now_ms;
    wake_greeting_latched() = true;
  }
  const uint32_t greet_el = (wake_greeting_latched() && now_ms >= wake_greeting_start_ms())
                                ? (now_ms - wake_greeting_start_ms())
                                : 999999U;
  const bool greeting_active = show_wake_greeting && wake_greeting_latched() &&
                               ctx.now.is_valid() && (greet_el < 10500U);

  // ZONE 4: Home activity card (D-028)
  if (ctx.font_date != nullptr) {
    static bool weather_h_measured = false;
    if (!weather_h_measured) {
      int m_w = 0, m_xo = 0, m_bl = 0, m_h = 0;
      ctx.font_date->measure("Ag", &m_w, &m_xo, &m_bl, &m_h);
      if (m_h > 0) home_weather_text_h() = m_h;
      weather_h_measured = true;
    }
  }
  const int act_card_cy = layout().home_activity_cy(home_weather_text_h());
  home_activity_card_shown() = (home_subpage == 0 && !greeting_active && act_count > 0);
  if (home_activity_card_shown()) {
    const auto &ly = layout();
    const bool two = ly.home_activity_two_line();
    const int card_h = ly.home_activity_h();
    const int card_w = ly.home_activity_w();
    const int pitch = ly.home_activity_pitch();
    const int row_top = two ? (act_card_cy - pitch / 2) : act_card_cy;
    const int row_bot = act_card_cy + pitch / 2;
    const bool has_drop = ly.home_activity_has_drop(act_count);
    const HomeActivityView view = resolve_home_activity_view(act_items, act_count, now_ms, two);
    rs.nav.home_activity_idx = view.top;

    auto &st = home_act_state();
    if (view.top != st.top || view.bottom != st.bottom) {
      st.from_top = st.top;
      st.from_bottom = st.bottom;
      st.top = view.top;
      st.bottom = view.bottom;
      st.anim_start_ms = (st.from_top >= 0) ? ((now_ms == 0) ? 1U : now_ms) : 0U;
    }
    float e = 1.0f;
    if (st.anim_start_ms != 0) {
      const uint32_t el = now_ms - st.anim_start_ms;
      if (el < kMotionMs) {
        e = ease_out_cubic(static_cast<float>(el) / static_cast<float>(kMotionMs));
      } else {
        st.anim_start_ms = 0;
      }
    }
    const bool animating = (e < 1.0f);

    Color tint, card_brd, btn_bg;
    resolve_home_card_palette(eff_weather_bg, is_night, cond, tint, card_brd, btn_bg);
    const int card_r = std::min(card_h / 2, (SCREEN_W > 520) ? scale_s(16) : 16);
    const int card_x0 = cx - card_w / 2;
    const int card_y0 = act_card_cy - card_h / 2;
    const GlassBackdrop backdrop = sample_glass_backdrop(card_x0, card_y0, card_w, card_h);
    static int glass_alpha_state = 0;
    const int glass_a = backdrop.valid
        ? glass_alpha_smooth(glass_alpha_state, glass_alpha_for_text(tint, backdrop.bright, ctx.col_white))
        : 256;
    render_glass_card(it, card_x0, card_y0, card_w, card_h, card_r, tint, glass_a, backdrop);
    Color eff_card_tint;
    int card_a_top = 0, card_a_bot = 0, card_rim_top = 0, card_rim_bot = 0;
    resolve_glass_fill(backdrop, tint, ctx.col_white, eff_card_tint,
                       card_a_top, card_a_bot, card_rim_top, card_rim_bot, glass_a);
    const Color card_bg = backdrop.valid
        ? mix_color_q8(backdrop.mean, eff_card_tint, (card_a_top + card_a_bot) / 2)
        : eff_card_tint;

    const int rise = scale_y(16);
    auto draw_row = [&](int item, int y, float alpha) {
      if (item < 0 || item >= act_count || alpha <= 0.05f) return;
      draw_home_activity_row_contents(it, y, act_items[item], has_drop, false, ctx.font_small,
                                      card_bg, btn_bg, ctx.theme_secondary, ctx.col_white,
                                      std::min(1.0f, alpha));
    };
    auto change_in_place = [&](int from, int to, int y) {
      if (!animating || from == to) {
        draw_row(to, y, 1.0f);
        return;
      }
      draw_row(from, y - static_cast<int>(static_cast<float>(rise) * e), 1.0f - e);
      draw_row(to, y + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e);
    };
    if (!two) {
      change_in_place(st.from_top, view.top, act_card_cy);
    } else if (view.bottom < 0) {
      if (animating && st.from_bottom < 0) change_in_place(st.from_top, view.top, act_card_cy);
      else draw_row(view.top, act_card_cy, 1.0f);
    } else if (animating && st.from_bottom >= 0 && !view.pinned && view.top == st.from_bottom) {
      draw_row(st.from_top, row_top - static_cast<int>(static_cast<float>(rise) * e), 1.0f - e);
      draw_row(view.top, row_bot - static_cast<int>(static_cast<float>(pitch) * e), 1.0f);
      draw_row(view.bottom, row_bot + static_cast<int>(static_cast<float>(rise) * (1.0f - e)), e);
    } else if (animating && st.from_bottom >= 0) {
      change_in_place(st.from_top, view.top, row_top);
      change_in_place(st.from_bottom, view.bottom, row_bot);
    } else {
      draw_row(view.top, row_top, 1.0f);
      draw_row(view.bottom, row_bot, 1.0f);
    }

    const int card_right = cx + card_w / 2;
    const int div1_x = has_drop ? (card_right - scale_x(108)) : (card_right - scale_x(56));
    const int div_h = scale_y(two ? 17 : 23);
    if (two && view.bottom >= 0) {
      fast_line(it, div1_x, row_top - div_h, div1_x, row_top + div_h, card_brd);
      fast_line(it, div1_x, row_bot - div_h, div1_x, row_bot + div_h, card_brd);
    } else {
      fast_line(it, div1_x, act_card_cy - div_h, div1_x, act_card_cy + div_h, card_brd);
    }
    if (has_drop) {
      const int div2_x = card_right - scale_x(52);
      const int col_h = card_h / 2 - scale_y(10);
      fast_line(it, div2_x, act_card_cy - col_h, div2_x, act_card_cy + col_h, card_brd);
      const int d_bx = card_right - scale_x(26);
      for (int w = -1; w <= 1; w++) {
        fast_line(it, d_bx - 8, act_card_cy - 4 + w, d_bx, act_card_cy + 4 + w, ctx.col_white);
        fast_line(it, d_bx + 8, act_card_cy - 4 + w, d_bx, act_card_cy + 4 + w, ctx.col_white);
      }
    }

    frame_hits().add_centered_rect(cx, act_card_cy, card_w, card_h,
                                   WidgetId::HOME_ACTIVITY_CARD, kHitTap | kHitFocusSlot,
                                   static_cast<int16_t>(view.top >= 0 ? view.top : 0));
  }

  if (greeting_active) {
    const char *raw_greet = resolve_wake_greeting(
        ctx.now.hour, ctx.now.minute, ctx.now.day_of_year, cond, ws.temperature);
    const std::string fit_greet = fit_text_to_width_font(
        std::string(raw_greet), std::min(scale_x(340), safe_span(act_card_cy)), ctx.font_date);
    const int greet_y = act_card_cy - 2;

    float alpha = 1.0f;
    if (greet_el > 8000U) {
      float u = 1.0f - static_cast<float>(greet_el - 8000U) * (1.0f / 2500.0f);
      if (u < 0.0f) u = 0.0f;
      alpha = u * u * (3.0f - 2.0f * u);
    }

    Color bg_target(0, 0, 0);
    if (eff_weather_bg && !is_night) {
      bg_target = Color(28, 54, 84);
      if (cond.find("fog") != std::string::npos || cond.find("mist") != std::string::npos) {
        bg_target = Color(32, 46, 60);
      } else if (cond.find("cloud") != std::string::npos && cond.find("partly") == std::string::npos) {
        bg_target = Color(22, 38, 56);
      } else if (cond.find("rain") != std::string::npos || cond.find("thunder") != std::string::npos ||
                 cond.find("lightning") != std::string::npos) {
        bg_target = Color(18, 30, 46);
      }
    }

    const Color fg_c = lerp_color(bg_target, ctx.col_white, alpha);
    if (alpha > 0.35f) {
      const Color sh_c = lerp_color(bg_target, ctx.col_black, alpha);
      fast_print(it, cx + 1, greet_y + 1, ctx.font_date, sh_c, TextAlign::CENTER, fit_greet.c_str());
    }
    fast_print(it, cx, greet_y, ctx.font_date, fg_c, TextAlign::CENTER, fit_greet.c_str());
  }

  home_lap.mark(ProfSect::HOME_TEXT);

  // ZONE 5: Fluid 4-Icon Sliding App Dock
  int dock_apps[kMaxDockApps] = {0};
  const int dock_count = collect_home_dock_apps(dock_apps);
  if (dock_count > 0) {
    const int total_dock_pages = (dock_count + 3) / 4;
    if (home_dock_page() < 0 || home_dock_page() >= total_dock_pages) {
      home_dock_page() = 0;
    }
    int win_xs[4] = {0, 0, 0, 0};
    bool has_left_arr = false, has_right_arr = false;
    get_home_dock_window_xs(dock_count, home_dock_page(), cx, win_xs, has_left_arr, has_right_arr);
    const int start_idx = get_home_dock_start_index(dock_count, home_dock_page());
    const int vis_count = std::min(4, dock_count - start_idx);
    const int dock_y = layout().dock_y;
    const int dock_r = layout().dock_r;

    const int dock_pad = std::max(0, layout().dock_hit_r() - dock_r);
    for (int v = 0; v < vis_count; v++) {
      frame_hits().add_circle(win_xs[v], dock_y, dock_r,
                              WidgetId::DOCK_SLOT_BASE + v,
                              kHitTap | kHitFocusSlot | kHitFocusCircle,
                              static_cast<int16_t>(dock_apps[start_idx + v]),
                              dock_pad);
    }

    auto draw_dock_glyph = [&](int app_id, int bx, int by, Color c, bool is_shadow, Color cut_c) {
      if (app_id == 6) {
        fast_filled_triangle(it, bx + 11, by - 15, bx + 6, by - 7, bx + 16, by - 7, c);
        fill_rect_row_major(it, bx + 8, by - 14, 7, 3, c);
        fast_vline(it, bx + 11, by - 7, 19, c);
        fast_vline(it, bx + 12, by - 7, 19, c);
        fast_hline(it, bx + 8, by + 12, 8, c);
        fill_rect_row_major(it, bx - 12, by - 4, 15, 8, c);
        fast_filled_circle(it, bx - 11, by - 2, 2, c);
        fast_filled_circle(it, bx + 1,  by - 2, 2, c);
        fast_filled_circle(it, bx - 14, by + 3, 3, c);
        fast_filled_circle(it, bx + 4,  by + 3, 3, c);
        fill_rect_row_major(it, bx - 15, by + 3, 21, 7, c);
        fill_rect_row_major(it, bx - 14, by + 10, 3, 3, c);
        fill_rect_row_major(it, bx + 2,  by + 10, 3, 3, c);
        if (!is_shadow) {
          fast_vline(it, bx - 5, by - 4, 8, cut_c);
          fast_hline(it, bx - 12, by + 4, 15, cut_c);
        }
      } else if (app_id == 2) {
        fast_filled_circle(it, bx - 9, by + 9, 5, c);
        fast_filled_circle(it, bx + 8, by + 6, 5, c);
        fill_rect_row_major(it, bx - 5, by - 12, 3, 21, c);
        fill_rect_row_major(it, bx + 11, by - 15, 3, 21, c);
        fast_line(it, bx - 5, by - 12, bx + 13, by - 15, c);
        fast_line(it, bx - 5, by - 11, bx + 13, by - 14, c);
        fast_line(it, bx - 5, by - 10, bx + 13, by - 13, c);
        fast_line(it, bx - 5, by - 9,  bx + 13, by - 12, c);
      } else if (app_id == 7) {
        if (!is_shadow) {
          draw_cached_dock_mask<-9, 9, -16, 16>(it, bx, by, c, ctx.col_black, [](int dx, int dy) -> bool {
            const int ax = std::abs(dx);
            const int ay = std::abs(dy);
            if (ax > 9 || ay > 16) return false;
            if (ax > 6 && ay > 13 && ((ax - 6) * (ax - 6) + (ay - 13) * (ay - 13) > 10)) {
              return false;
            }
            if (dy >= -13 && dy <= -12 && ax >= 4 && ax <= 6) {
              return false;
            }
            const int dpy = dy + 4;
            const int r2 = dx * dx + dpy * dpy;
            if (r2 >= 6 && r2 <= 34) {
              if (r2 >= 16 && (std::abs(dx - dpy) <= 1 || std::abs(dx + dpy) <= 1)) {
                return true;
              }
              return false;
            }
            if (((dy >= 4 && dy <= 5) || (dy >= 8 && dy <= 9) || (dy >= 12 && dy <= 13)) &&
                (ax <= 1 || (ax >= 4 && ax <= 6))) {
              return false;
            }
            return true;
          });
        }
      } else if (app_id == 8) {
        if (!is_shadow) {
          draw_cached_dock_mask<-12, 12, -12, 12>(it, bx, by, c, ctx.col_black, [](int dx, int dy) -> bool {
            const int ax = std::abs(dx);
            const int ay = std::abs(dy);
            if (ax < 2 || ax > 11 || ay < 2 || ay > 11) return false;
            const int lx = ax - 2;
            const int ly = ay - 2;
            if ((lx == 0 || lx == 9) && (ly == 0 || ly == 9)) return false;
            if (dx > 0 && dy > 0 && lx >= 3 && lx <= 6 && ly >= 3 && ly <= 6) {
              return false;
            }
            return true;
          });
        }
      } else if (app_id == 3) {
        if (!is_shadow) {
          draw_cached_dock_mask<-19, 19, -12, 12>(it, bx, by, c, ctx.col_black, [](int dx, int dy) -> bool {
            static constexpr int8_t GP_OUT_MAX[25] = {
               9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18, 17, 17, 16, 15, 13
            };
            static constexpr int8_t GP_OUT_MIN[25] = {
               0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  6,  8,  9,  9, 10, 10, 11, 11, 12, 12
            };
            if (dy < -12 || dy > 12) return false;
            const int row = dy + 12;
            const int ax = std::abs(dx);
            if (ax < GP_OUT_MIN[row] || ax > GP_OUT_MAX[row]) return false;

            const int dpx = dx + 7;
            const int dpy = dy + 4;
            if ((std::abs(dpx) <= 1 && std::abs(dpy) <= 5) ||
                (std::abs(dpy) <= 1 && std::abs(dpx) <= 5)) {
              return false;
            }

            const int abx = dx - 7;
            const int aby = dy + 4;
            auto is_dot = [](int x, int y, int cx_d, int cy_d) -> bool {
              const int rx = x - cx_d, ry = y - cy_d;
              return (rx * rx + ry * ry) <= 2;
            };
            if (is_dot(abx, aby, 0, -4) || is_dot(abx, aby, 0, 4) ||
                is_dot(abx, aby, -4, 0) || is_dot(abx, aby, 4, 0)) {
              return false;
            }
            return true;
          });
        }
      } else if (app_id == 4) {
        if (!is_shadow) {
          draw_cached_dock_mask<-16, 16, -16, 16>(it, bx, by, c, ctx.col_black, [](int dx, int dy) -> bool {
            const int r2 = dx * dx + dy * dy;
            if (r2 <= 24 || r2 > 232) return false;
            if (r2 <= 128) return true;
            const float fx = static_cast<float>(dx);
            const float fy = static_cast<float>(dy);
            static constexpr float INV_SQRT2 = 0.70710678f;
            const float p_v  = std::fabs(fx),                    a_v  = std::fabs(fy);
            const float p_h  = std::fabs(fy),                    a_h  = std::fabs(fx);
            const float p_d1 = std::fabs((fx - fy) * INV_SQRT2), a_d1 = std::fabs((fx + fy) * INV_SQRT2);
            const float p_d2 = std::fabs((fx + fy) * INV_SQRT2), a_d2 = std::fabs((fx - fy) * INV_SQRT2);
            auto in_tooth = [](float perp, float along) -> bool {
              if (along > 15.0f) return false;
              const float max_w = 2.65f - (along - 11.0f) * 0.20f;
              return (perp <= max_w);
            };
            return in_tooth(p_v, a_v) || in_tooth(p_h, a_h) ||
                   in_tooth(p_d1, a_d1) || in_tooth(p_d2, a_d2);
          });
        }
      } else if (app_id == 12) {
        fill_rect_row_major(it, bx - 13, by - 7, 26, 17, c);
        fill_rect_row_major(it, bx - 5, by - 11, 10, 4, c);
        if (!is_shadow) {
          fast_filled_circle(it, bx, by + 1, 6, cut_c);
          fast_circle(it, bx, by + 1, 4, c);
          fast_filled_circle(it, bx, by + 1, 2, c);
        }
      } else if (app_id == 9) {
        fill_rect_row_major(it, bx + 7, by - 14, 4, 8, c);
        fast_filled_triangle(it, bx, by - 16, bx - 16, by - 2, bx + 16, by - 2, c);
        fill_rect_row_major(it, bx - 16, by - 2, 33, 2, c);
        fill_rect_row_major(it, bx - 12, by, 25, 13, c);
        if (!is_shadow) {
          fast_hline(it, bx - 12, by, 25, cut_c);
          fill_rect_row_major(it, bx - 8, by + 3, 6, 10, cut_c);
          fast_pixel(it, bx - 4, by + 8, c);
          fill_rect_row_major(it, bx + 2, by + 3, 7, 6, cut_c);
          fast_vline(it, bx + 5, by + 3, 6, c);
          fast_hline(it, bx + 2, by + 5, 7, c);
        }
      } else if (app_id == 5) {
        // Sun behind foreground cloud
        fast_filled_circle(it, bx + 6, by - 6, 5, c);
        fast_vline(it, bx + 6, by - 14, 2, c);
        fast_hline(it, bx + 13, by - 6, 2, c);
        fast_line(it, bx + 11, by - 11, bx + 12, by - 12, c);
        fast_line(it, bx + 1, by - 11, bx, by - 12, c);
        if (!is_shadow) {
          fast_filled_circle(it, bx - 2, by + 1, 9, cut_c);
          fast_filled_circle(it, bx + 6, by + 4, 8, cut_c);
          fill_rect_row_major(it, bx - 11, by + 2, 19, 9, cut_c);
        }
        fast_filled_circle(it, bx - 9, by + 5, 5, c);
        fast_filled_circle(it, bx - 2, by + 1, 7, c);
        fast_filled_circle(it, bx + 6, by + 4, 6, c);
        fill_rect_row_major(it, bx - 9, by + 4, 16, 7, c);
      }
    };

    const float anim_start = home_dock_anim().step_window(start_idx, now_ms);
    const int stride       = get_dock_slot_stride(win_xs, vis_count);
    const int left_cx      = win_xs[0];
    const int right_cx     = win_xs[vis_count - 1];
    const int clip_pad     = stride / 2 + dock_r;

    Color dock_tint = kChromeGlassTint;
    if (eff_weather_bg) {
      Color dock_card_bg, dock_brd, dock_btn_bg;
      resolve_home_card_palette(eff_weather_bg, is_night, cond, dock_card_bg, dock_brd, dock_btn_bg);
      dock_tint = mix_color_q8(dock_btn_bg, kChromeHomePill, is_night ? 18 : 24);
    } else if (use_custom_wallpaper) {
      dock_tint = sample_image_ambient_tint(ctx.home_wallpaper_art, kChromeGlassTint);
    }

    for (int g_idx = 0; g_idx < dock_count; g_idx++) {
      const float rel_slot = static_cast<float>(g_idx) - anim_start;
      if (rel_slot < -0.85f || rel_slot > static_cast<float>(vis_count - 1) + 0.85f) continue;
      const int bx = left_cx + static_cast<int>(std::roundf(rel_slot * static_cast<float>(stride)));
      if (bx < left_cx - clip_pad || bx > right_cx + clip_pad) continue;

      float alpha = 1.0f;
      if (rel_slot < 0.0f) {
        alpha = std::max(0.15f, 1.0f + rel_slot * 1.15f);
      } else if (rel_slot > static_cast<float>(vis_count - 1)) {
        alpha = std::max(0.15f, 1.0f - (rel_slot - static_cast<float>(vis_count - 1)) * 1.15f);
      }

      const Color slot_fg_c(static_cast<uint8_t>(ctx.col_white.r * alpha),
                            static_cast<uint8_t>(ctx.col_white.g * alpha),
                            static_cast<uint8_t>(ctx.col_white.b * alpha));
      const int app_id = dock_apps[g_idx];
      const int d = 2 * dock_r + 1;
      const GlassBackdrop bd = sample_glass_backdrop(bx - dock_r, dock_y - dock_r, d, d);
      Color eff_tint;
      int raw_a_top = 0, raw_a_bot = 0, raw_rim_top = 0, raw_rim_bot = 0;
      resolve_glass_fill(bd, dock_tint, kChromeGlassSheen, eff_tint,
                         raw_a_top, raw_a_bot, raw_rim_top, raw_rim_bot);
      const int a_top     = static_cast<int>(std::roundf(static_cast<float>(raw_a_top) * alpha));
      const int a_bot     = static_cast<int>(std::roundf(static_cast<float>(raw_a_bot) * alpha));
      const int sheen_top = static_cast<int>(std::roundf(static_cast<float>(raw_rim_top) * alpha));
      const int sheen_bot = static_cast<int>(std::roundf(static_cast<float>(raw_rim_bot) * alpha));
      fill_round_rect_glass(it, bx - dock_r, dock_y - dock_r, d, d, static_cast<float>(dock_r) + 0.5f,
                            kChromeGlassSheen, sheen_top, sheen_bot, 1);
      fill_round_rect_glass(it, bx - dock_r + 1, dock_y - dock_r + 1, d - 2, d - 2,
                            static_cast<float>(dock_r) - 0.5f, eff_tint, a_top, a_bot);
      const Color slot_cut_c = bd.valid ? mix_color_q8(bd.mean, eff_tint, (a_top + a_bot) / 2) : eff_tint;
      if (alpha > 0.55f) {
        draw_dock_glyph(app_id, bx + 1, dock_y + 1, ctx.col_black, true, ctx.col_black);
      }
      draw_dock_glyph(app_id, bx, dock_y, slot_fg_c, false, slot_cut_c);
    }

    const int pips_y = layout().dock_pips_y;
    render_viewport_scrollbar(it, cx, pips_y, dock_count, anim_start,
                              ctx.theme_primary, false);
  }

#if LATTER_HAS_BATTERY
  esphome::display::BaseFont *font_batt =
      (SCREEN_W < 380 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
  render_battery_indicator(it, layout().battery_cx, layout().battery_y,
                           rs.hw_batt_level, rs.hw_batt_charging, font_batt,
                           ctx.theme_secondary, ctx.col_gold, ctx.col_green);
#endif
  home_lap.mark(ProfSect::HOME_DOCK);
}

// ==============================================================================
// Top-Level Active Page Dispatcher
// ==============================================================================

void render_active_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  switch (runtime_state().nav.gui_page) {
    case PageId::CLOCK:
      render_clock_page(it, ctx);
      break;
    case PageId::HOME:
      render_home_page(it, ctx);
      break;
    case PageId::MUSIC:
      render_music_page(it, ctx);
      break;
    case PageId::GAMES:
      render_games_page(it, ctx);
      break;
    case PageId::SYSTEM:
      render_system_page(it, ctx);
      break;
    case PageId::WEATHER:
      render_weather_page(it, ctx);
      break;
    case PageId::AREA:
      render_area_control_page(it, ctx);
      break;
    case PageId::REMOTE:
      render_tv_remote_page(it, ctx);
      break;
    case PageId::TOOLS:
      render_tools_page(it, ctx);
      break;
    case PageId::WHOLE_HOME:
      render_whole_home_page(it, ctx);
      break;
    default:
      render_home_page(it, ctx);
      break;
  }
}

}  // namespace latter_ui
