#include "esphome/core/defines.h"
#include "ui_pages.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

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

// Home activity card transitions (D-028 / D-039): row carousel slide + 210 ms disappearance fade-out
// (kMotionMs, ease-out cubic; 0 = settled).
struct HomeActivityAnimState {
  int top = -1;
  int bottom = -1;
  int from_top = -1;
  int from_bottom = -1;
  uint32_t anim_start_ms = 0;
  bool was_visible = false;
  uint32_t fade_out_start_ms = 0;
  bool fade_out_two = false;
  bool fade_out_has_drop = false;
  HomeActivityItem fade_out_top_item{};
  HomeActivityItem fade_out_bot_item{};
  int fade_out_card_h = 0;
};

HomeActivityAnimState &home_act_state() {
  static HomeActivityAnimState st;
  return st;
}

HomeActivityItem s_rendered_act_items[kMaxHomeActivity]{};
int s_rendered_act_count = 0;
int s_act_band_y0 = 0;
int s_act_band_y1 = 0;
bool s_act_deck_rotates = false;

void store_rendered_activity_snapshot(const HomeActivityItem *items, int count) {
  const int n = std::clamp(count, 0, kMaxHomeActivity);
  for (int i = 0; i < n; ++i) {
    s_rendered_act_items[i] = items[i];
  }
  s_rendered_act_count = n;
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
  const Color fg_btn = (alpha >= 0.99f) ? btn_bg          : lerp_color(card_bg, theme_secondary, alpha);
  (void) fg_btn;
  const Color fg_btn_fill = (alpha >= 0.99f) ? btn_bg : lerp_color(card_bg, btn_bg, alpha);

  const int cx_ref     = SCREEN_CX;
  const int list_w     = layout().list_card_w;
  const int main_w     = layout().home_activity_w();
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
    } else if (cur_act.kind == 6) {
      fill_rect_row_major(it, gx - 7, gy - 6, 4, 5, fg_w);
      fill_rect_row_major(it, gx + 2, gy + 2, 5, 4, fg_w);
      fast_line(it, gx - 5, gy - 2, gx + 2, gy + 4, fg_w);
      fast_line(it, gx - 4, gy - 2, gx + 3, gy + 4, fg_w);
      fast_line(it, gx + 2, gy - 5, gx + 5, gy - 5, fg_sec);
      fast_line(it, gx + 5, gy - 5, gx + 5, gy - 2, fg_sec);
      fast_line(it, gx + 4, gy - 8, gx + 8, gy - 8, fg_sec);
      fast_line(it, gx + 8, gy - 8, gx + 8, gy - 4, fg_sec);
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
    } else if (cur_act.kind == 6) {
      fill_rect_row_major(it, gx - 9, gy - 9, 5, 6, fg_w);
      fill_rect_row_major(it, gx + 3, gy + 3, 6, 5, fg_w);
      for (int w = -1; w <= 1; w++) {
        fast_line(it, gx - 6 + w, gy - 4, gx + 4 + w, gy + 5, fg_w);
      }
      fast_line(it, gx + 2, gy - 6, gx + 6, gy - 6, fg_sec);
      fast_line(it, gx + 6, gy - 6, gx + 6, gy - 2, fg_sec);
      fast_line(it, gx + 4, gy - 10, gx + 10, gy - 10, fg_sec);
      fast_line(it, gx + 10, gy - 10, gx + 10, gy - 4, fg_sec);
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

  if (alpha >= 0.99f) {
    render_glass_circle(it, act_bx, gy, btn_r, fg_btn_fill, fg_sec);
  } else if (alpha > 0.01f && btn_r > 1) {
    const int d = 2 * btn_r + 1;
    const GlassBackdrop btn_bd = sample_glass_backdrop(act_bx - btn_r, gy - btn_r, d, d);
    Color btn_eff;
    int raw_at = 0, raw_ab = 0, raw_rt = 0, raw_rb = 0;
    resolve_glass_fill(btn_bd, fg_btn_fill, fg_sec, btn_eff, raw_at, raw_ab, raw_rt, raw_rb,
                       -1, GlassVariant::Clear);
    const int at = static_cast<int>(std::lround(static_cast<float>(raw_at) * alpha));
    const int ab = static_cast<int>(std::lround(static_cast<float>(raw_ab) * alpha));
    const int rt_a = static_cast<int>(std::lround(static_cast<float>(raw_rt) * alpha));
    const int rb_a = static_cast<int>(std::lround(static_cast<float>(raw_rb) * alpha));
    if (rt_a > 0 || rb_a > 0) {
      fill_round_rect_glass(it, act_bx - btn_r, gy - btn_r, d, d,
                            static_cast<float>(btn_r) + 0.5f, kChromeGlassSheen, rt_a, rb_a, 1);
    }
    if ((at > 0 || ab > 0) && d > 2) {
      fill_round_rect_glass(it, act_bx - btn_r + 1, gy - btn_r + 1, d - 2, d - 2,
                            std::max(0.5f, static_cast<float>(btn_r) - 0.5f), btn_eff, at, ab);
    }
  }

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
  } else if (cur_act.kind == 6) {
    const int sq = sym_s - 1;
    fill_rect_row_major(it, act_bx - sq, gy - sq, sq * 2 + 1, sq * 2 + 1, fg_w);
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
  const int card_w     = is_list_row ? layout().list_card_w : layout().home_activity_w();
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
inline void draw_cached_dock_mask(DisplayT &it, int bx, int by, int dock_r,
                                  esphome::Color c, esphome::Color sh_c, PredFn &&pred) {
  static uint64_t rows[64] = {};
  static int cached_r = -1;
  static int s_xmin = 0;
  static int s_ymin = 0;
  static int s_w = 0;
  static int s_h = 0;

  if (cached_r != dock_r) {
    const float dk = static_cast<float>(std::max(1, dock_r)) / 27.0f;
    const float inv_dk = 27.0f / static_cast<float>(std::max(1, dock_r));
    s_xmin = std::max(-30, static_cast<int>(std::floor(static_cast<float>(XMin) * dk)));
    const int sx_max = std::min(30, static_cast<int>(std::ceil(static_cast<float>(XMax) * dk)));
    s_ymin = std::max(-30, static_cast<int>(std::floor(static_cast<float>(YMin) * dk)));
    const int sy_max = std::min(30, static_cast<int>(std::ceil(static_cast<float>(YMax) * dk)));
    s_w = std::clamp(sx_max - s_xmin + 1, 1, 62);
    s_h = std::clamp(sy_max - s_ymin + 1, 1, 62);
    for (int ry = 0; ry < 64; ++ry) rows[ry] = 0ULL;
    for (int ry = 0; ry <= s_h; ++ry) {
      uint64_t m = 0ULL;
      const int src_y = static_cast<int>(std::lround(static_cast<float>(s_ymin + ry) * inv_dk));
      for (int rx = 0; rx <= s_w; ++rx) {
        const int src_x = static_cast<int>(std::lround(static_cast<float>(s_xmin + rx) * inv_dk));
        if (pred(src_x, src_y)) m |= (uint64_t{1} << rx);
      }
      rows[ry] = m;
    }
    cached_r = dock_r;
  }

  const uint64_t col_mask = (uint64_t{1} << s_w) - 1U;
  for (int ry = 0; ry < s_h; ry++) {
    uint64_t m = rows[ry] & col_mask;
    const uint64_t next_row = rows[ry + 1];
    const int py = by + s_ymin + ry;
    while (m != 0) {
      const int rx = std::countr_zero(m);
      m &= (m - 1U);
      const int px = bx + s_xmin + rx;
      if (((next_row >> (rx + 1)) & 1ULL) == 0ULL) {
        fast_pixel(it, px + 1, py + 1, sh_c);
      }
      fast_pixel(it, px, py, c);
    }
  }
}

enum class AreaGlyphKind : uint8_t {
  MAP_PIN = 0,
  SOFA,
  CULINARY,
  DINING,
  BED,
  NURSERY,
  SHOWER,
  DESK,
  TELEVISION,
  GAMEPAD,
  DUMBBELL,
  WASHER,
  GARAGE,
  TOOLS,
  WARDROBE,
  DOOR,
  STAIRS,
  ROOF,
  POOL,
  BALCONY,
  GARDEN,
  SPEAKER,
};

bool sv_contains_ci(std::string_view hay, std::string_view needle) {
  if (needle.empty() || hay.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(hay[i + j])));
      const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
      if (a != b) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

AreaGlyphKind classify_area_glyph(std::string_view icon_str, std::string_view area_name_fallback) {
  std::string_view slug = icon_str;
  if (slug.size() > 4 && (slug.substr(0, 4) == "mdi:" || slug.substr(0, 4) == "MDI:")) {
    slug = slug.substr(4);
  }
  if (!slug.empty()) {
    if (sv_contains_ci(slug, "sofa") || sv_contains_ci(slug, "couch") || sv_contains_ci(slug, "armchair")) {
      return AreaGlyphKind::SOFA;
    }
    if (sv_contains_ci(slug, "countertop") || sv_contains_ci(slug, "silverware") ||
        sv_contains_ci(slug, "stove") || sv_contains_ci(slug, "fridge") || sv_contains_ci(slug, "pot-steam")) {
      return AreaGlyphKind::CULINARY;
    }
    if (sv_contains_ci(slug, "table-chair") || sv_contains_ci(slug, "table-furniture")) {
      return AreaGlyphKind::DINING;
    }
    if (sv_contains_ci(slug, "baby") || sv_contains_ci(slug, "teddy-bear") || sv_contains_ci(slug, "cradle")) {
      return AreaGlyphKind::NURSERY;
    }
    if (sv_contains_ci(slug, "bed") || sv_contains_ci(slug, "bunk")) {
      return AreaGlyphKind::BED;
    }
    if (sv_contains_ci(slug, "shower") || sv_contains_ci(slug, "bathtub") ||
        sv_contains_ci(slug, "toilet") || sv_contains_ci(slug, "hand-wash")) {
      return AreaGlyphKind::SHOWER;
    }
    if (sv_contains_ci(slug, "desk") || sv_contains_ci(slug, "bookshelf") ||
        sv_contains_ci(slug, "briefcase") || sv_contains_ci(slug, "laptop")) {
      return AreaGlyphKind::DESK;
    }
    if (sv_contains_ci(slug, "television") || sv_contains_ci(slug, "movie") ||
        sv_contains_ci(slug, "theater") || sv_contains_ci(slug, "projector")) {
      return AreaGlyphKind::TELEVISION;
    }
    if (sv_contains_ci(slug, "gamepad") || sv_contains_ci(slug, "controller")) {
      return AreaGlyphKind::GAMEPAD;
    }
    if (sv_contains_ci(slug, "dumbbell") || sv_contains_ci(slug, "weight")) {
      return AreaGlyphKind::DUMBBELL;
    }
    if (sv_contains_ci(slug, "washing-machine") || sv_contains_ci(slug, "tumble-dryer") ||
        sv_contains_ci(slug, "iron")) {
      return AreaGlyphKind::WASHER;
    }
    if (sv_contains_ci(slug, "garage") || sv_contains_ci(slug, "car")) {
      return AreaGlyphKind::GARAGE;
    }
    if (sv_contains_ci(slug, "tools") || sv_contains_ci(slug, "hammer") ||
        sv_contains_ci(slug, "wrench") || sv_contains_ci(slug, "saw")) {
      return AreaGlyphKind::TOOLS;
    }
    if (sv_contains_ci(slug, "wardrobe") || sv_contains_ci(slug, "hanger")) {
      return AreaGlyphKind::WARDROBE;
    }
    if (sv_contains_ci(slug, "door") || sv_contains_ci(slug, "coat-rack")) {
      return AreaGlyphKind::DOOR;
    }
    if (sv_contains_ci(slug, "stairs") || sv_contains_ci(slug, "escalator")) {
      return AreaGlyphKind::STAIRS;
    }
    if (sv_contains_ci(slug, "home-roof") || sv_contains_ci(slug, "home-floor") ||
        sv_contains_ci(slug, "ladder")) {
      return AreaGlyphKind::ROOF;
    }
    if (sv_contains_ci(slug, "pool") || sv_contains_ci(slug, "waves") || sv_contains_ci(slug, "hot-tub")) {
      return AreaGlyphKind::POOL;
    }
    if (sv_contains_ci(slug, "balcony") || sv_contains_ci(slug, "deck") ||
        sv_contains_ci(slug, "grill") || sv_contains_ci(slug, "umbrella-beach") ||
        sv_contains_ci(slug, "sun-wireless")) {
      return AreaGlyphKind::BALCONY;
    }
    if (sv_contains_ci(slug, "tree") || sv_contains_ci(slug, "flower") ||
        sv_contains_ci(slug, "sprout") || sv_contains_ci(slug, "grass")) {
      return AreaGlyphKind::GARDEN;
    }
    if (sv_contains_ci(slug, "speaker") || sv_contains_ci(slug, "music") ||
        sv_contains_ci(slug, "cast-audio")) {
      return AreaGlyphKind::SPEAKER;
    }
  }

  if (!area_name_fallback.empty()) {
    if (sv_contains_ci(area_name_fallback, "living") || sv_contains_ci(area_name_fallback, "lounge") ||
        sv_contains_ci(area_name_fallback, "family") || sv_contains_ci(area_name_fallback, "den")) {
      return AreaGlyphKind::SOFA;
    }
    if (sv_contains_ci(area_name_fallback, "kitchen") || sv_contains_ci(area_name_fallback, "pantry")) {
      return AreaGlyphKind::CULINARY;
    }
    if (sv_contains_ci(area_name_fallback, "dining") || sv_contains_ci(area_name_fallback, "breakfast")) {
      return AreaGlyphKind::DINING;
    }
    if (sv_contains_ci(area_name_fallback, "nursery") || sv_contains_ci(area_name_fallback, "baby") ||
        sv_contains_ci(area_name_fallback, "kids")) {
      return AreaGlyphKind::NURSERY;
    }
    if (sv_contains_ci(area_name_fallback, "bed") || sv_contains_ci(area_name_fallback, "primary") ||
        sv_contains_ci(area_name_fallback, "guest") || sv_contains_ci(area_name_fallback, "suite")) {
      return AreaGlyphKind::BED;
    }
    if (sv_contains_ci(area_name_fallback, "bath") || sv_contains_ci(area_name_fallback, "shower") ||
        sv_contains_ci(area_name_fallback, "powder")) {
      return AreaGlyphKind::SHOWER;
    }
    if (sv_contains_ci(area_name_fallback, "office") || sv_contains_ci(area_name_fallback, "study") ||
        sv_contains_ci(area_name_fallback, "library")) {
      return AreaGlyphKind::DESK;
    }
    if (sv_contains_ci(area_name_fallback, "media") || sv_contains_ci(area_name_fallback, "theater") ||
        sv_contains_ci(area_name_fallback, "cinema")) {
      return AreaGlyphKind::TELEVISION;
    }
    if (sv_contains_ci(area_name_fallback, "game") || sv_contains_ci(area_name_fallback, "arcade")) {
      return AreaGlyphKind::GAMEPAD;
    }
    if (sv_contains_ci(area_name_fallback, "gym") || sv_contains_ci(area_name_fallback, "fitness")) {
      return AreaGlyphKind::DUMBBELL;
    }
    if (sv_contains_ci(area_name_fallback, "laundry") || sv_contains_ci(area_name_fallback, "utility")) {
      return AreaGlyphKind::WASHER;
    }
    if (sv_contains_ci(area_name_fallback, "garage") || sv_contains_ci(area_name_fallback, "carport")) {
      return AreaGlyphKind::GARAGE;
    }
    if (sv_contains_ci(area_name_fallback, "workshop") || sv_contains_ci(area_name_fallback, "shed")) {
      return AreaGlyphKind::TOOLS;
    }
    if (sv_contains_ci(area_name_fallback, "closet") || sv_contains_ci(area_name_fallback, "wardrobe")) {
      return AreaGlyphKind::WARDROBE;
    }
    if (sv_contains_ci(area_name_fallback, "entry") || sv_contains_ci(area_name_fallback, "foyer") ||
        sv_contains_ci(area_name_fallback, "hall") || sv_contains_ci(area_name_fallback, "porch")) {
      return AreaGlyphKind::DOOR;
    }
    if (sv_contains_ci(area_name_fallback, "stair") || sv_contains_ci(area_name_fallback, "landing")) {
      return AreaGlyphKind::STAIRS;
    }
    if (sv_contains_ci(area_name_fallback, "attic") || sv_contains_ci(area_name_fallback, "loft") ||
        sv_contains_ci(area_name_fallback, "basement")) {
      return AreaGlyphKind::ROOF;
    }
    if (sv_contains_ci(area_name_fallback, "pool") || sv_contains_ci(area_name_fallback, "spa")) {
      return AreaGlyphKind::POOL;
    }
    if (sv_contains_ci(area_name_fallback, "balcony") || sv_contains_ci(area_name_fallback, "patio") ||
        sv_contains_ci(area_name_fallback, "deck") || sv_contains_ci(area_name_fallback, "terrace")) {
      return AreaGlyphKind::BALCONY;
    }
    if (sv_contains_ci(area_name_fallback, "garden") || sv_contains_ci(area_name_fallback, "yard") ||
        sv_contains_ci(area_name_fallback, "courtyard")) {
      return AreaGlyphKind::GARDEN;
    }
    if (sv_contains_ci(area_name_fallback, "studio") || sv_contains_ci(area_name_fallback, "music")) {
      return AreaGlyphKind::SPEAKER;
    }
  }
  return AreaGlyphKind::MAP_PIN;
}

void draw_area_glyph_pass(esphome::display::Display &it, int cx, int cy, int r,
                          AreaGlyphKind kind, esphome::Color c) {
  const float k = static_cast<float>(std::max(5, r)) / 9.0f;
  auto sc = [k](int v) -> int { return static_cast<int>(std::lround(static_cast<float>(v) * k)); };
  auto scr = [k](int v) -> int { return std::max(1, static_cast<int>(std::lround(static_cast<float>(v) * k))); };

  switch (kind) {
    case AreaGlyphKind::SOFA:
      fill_rect_row_major(it, cx - sc(6), cy - sc(4), scr(13), scr(5), c);
      fill_rect_row_major(it, cx - sc(8), cy, scr(17), scr(5), c);
      fast_filled_circle(it, cx - sc(7), cy, scr(2), c);
      fast_filled_circle(it, cx + sc(7), cy, scr(2), c);
      fast_vline(it, cx - sc(6), cy + sc(5), scr(2), c);
      fast_vline(it, cx + sc(6), cy + sc(5), scr(2), c);
      break;
    case AreaGlyphKind::CULINARY:
      fast_vline(it, cx - sc(4), cy - sc(7), scr(5), c);
      fast_vline(it, cx - sc(2), cy - sc(7), scr(5), c);
      fast_hline(it, cx - sc(5), cy - sc(2), scr(5), c);
      fill_rect_row_major(it, cx - sc(3), cy - sc(2), scr(2), scr(9), c);
      fill_rect_row_major(it, cx + sc(3), cy - sc(7), scr(3), scr(7), c);
      fill_rect_row_major(it, cx + sc(4), cy, scr(2), scr(7), c);
      break;
    case AreaGlyphKind::DINING:
      fill_rect_row_major(it, cx - sc(7), cy - sc(2), scr(15), scr(2), c);
      fast_vline(it, cx, cy, scr(7), c);
      fast_vline(it, cx - sc(7), cy - sc(5), scr(11), c);
      fast_hline(it, cx - sc(7), cy + sc(2), scr(4), c);
      fast_vline(it, cx + sc(7), cy - sc(5), scr(11), c);
      fast_hline(it, cx + sc(4), cy + sc(2), scr(4), c);
      break;
    case AreaGlyphKind::BED:
      fill_rect_row_major(it, cx - sc(8), cy - sc(5), scr(2), scr(11), c);
      fill_rect_row_major(it, cx - sc(8), cy + sc(1), scr(17), scr(3), c);
      fill_rect_row_major(it, cx + sc(7), cy + sc(1), scr(2), scr(5), c);
      fill_rect_row_major(it, cx - sc(5), cy - sc(2), scr(4), scr(3), c);
      fill_rect_row_major(it, cx, cy - sc(3), scr(8), scr(4), c);
      break;
    case AreaGlyphKind::NURSERY:
      fast_filled_circle(it, cx - sc(5), cy - sc(5), scr(2), c);
      fast_filled_circle(it, cx + sc(5), cy - sc(5), scr(2), c);
      fast_circle(it, cx, cy + sc(1), scr(6), c);
      fast_filled_circle(it, cx - sc(2), cy, scr(1), c);
      fast_filled_circle(it, cx + sc(2), cy, scr(1), c);
      break;
    case AreaGlyphKind::SHOWER:
      fast_vline(it, cx - sc(6), cy - sc(7), scr(14), c);
      fast_hline(it, cx - sc(6), cy - sc(7), scr(9), c);
      fast_filled_triangle(it, cx + sc(2), cy - sc(7), cx - sc(1), cy - sc(3), cx + sc(5), cy - sc(3), c);
      fast_vline(it, cx, cy - sc(1), scr(3), c);
      fast_vline(it, cx + sc(3), cy, scr(3), c);
      fast_vline(it, cx + sc(1), cy + sc(4), scr(3), c);
      fast_vline(it, cx + sc(4), cy + sc(4), scr(3), c);
      break;
    case AreaGlyphKind::DESK:
      fast_rect_outline(it, cx - sc(5), cy - sc(7), scr(11), scr(7), c);
      fast_vline(it, cx, cy, scr(2), c);
      fill_rect_row_major(it, cx - sc(8), cy + sc(2), scr(17), scr(2), c);
      fast_vline(it, cx - sc(6), cy + sc(4), scr(4), c);
      fast_vline(it, cx + sc(6), cy + sc(4), scr(4), c);
      break;
    case AreaGlyphKind::TELEVISION:
      fast_rect_outline(it, cx - sc(8), cy - sc(6), scr(17), scr(10), c);
      fast_line(it, cx - sc(2), cy + sc(4), cx - sc(5), cy + sc(7), c);
      fast_line(it, cx + sc(2), cy + sc(4), cx + sc(5), cy + sc(7), c);
      fast_hline(it, cx - sc(5), cy + sc(7), scr(11), c);
      break;
    case AreaGlyphKind::GAMEPAD:
      fill_rect_row_major(it, cx - sc(6), cy - sc(3), scr(13), scr(6), c);
      fast_filled_circle(it, cx - sc(6), cy + sc(1), scr(3), c);
      fast_filled_circle(it, cx + sc(6), cy + sc(1), scr(3), c);
      break;
    case AreaGlyphKind::DUMBBELL:
      fill_rect_row_major(it, cx - sc(8), cy - sc(4), scr(2), scr(9), c);
      fill_rect_row_major(it, cx - sc(5), cy - sc(3), scr(2), scr(7), c);
      fill_rect_row_major(it, cx - sc(3), cy - sc(1), scr(7), scr(2), c);
      fill_rect_row_major(it, cx + sc(4), cy - sc(3), scr(2), scr(7), c);
      fill_rect_row_major(it, cx + sc(7), cy - sc(4), scr(2), scr(9), c);
      break;
    case AreaGlyphKind::WASHER:
      fast_rect_outline(it, cx - sc(6), cy - sc(7), scr(13), scr(15), c);
      fast_hline(it, cx - sc(5), cy - sc(4), scr(11), c);
      fast_circle(it, cx, cy + sc(2), scr(3), c);
      break;
    case AreaGlyphKind::GARAGE:
      fast_line(it, cx - sc(8), cy - sc(2), cx, cy - sc(7), c);
      fast_line(it, cx, cy - sc(7), cx + sc(8), cy - sc(2), c);
      fast_vline(it, cx - sc(6), cy - sc(2), scr(9), c);
      fast_vline(it, cx + sc(6), cy - sc(2), scr(9), c);
      fast_hline(it, cx - sc(4), cy + sc(1), scr(9), c);
      fast_hline(it, cx - sc(4), cy + sc(4), scr(9), c);
      fast_hline(it, cx - sc(4), cy + sc(7), scr(9), c);
      break;
    case AreaGlyphKind::TOOLS:
      fast_line(it, cx - sc(6), cy + sc(6), cx + sc(4), cy - sc(4), c);
      fast_line(it, cx - sc(5), cy + sc(6), cx + sc(5), cy - sc(4), c);
      fill_rect_row_major(it, cx + sc(1), cy - sc(7), scr(6), scr(3), c);
      break;
    case AreaGlyphKind::WARDROBE:
      fast_circle(it, cx, cy - sc(5), scr(2), c);
      fast_line(it, cx, cy - sc(3), cx - sc(8), cy + sc(4), c);
      fast_line(it, cx, cy - sc(3), cx + sc(8), cy + sc(4), c);
      fast_hline(it, cx - sc(8), cy + sc(4), scr(17), c);
      break;
    case AreaGlyphKind::DOOR:
      fast_rect_outline(it, cx - sc(5), cy - sc(7), scr(11), scr(15), c);
      fast_pixel(it, cx + sc(2), cy, c);
      fast_pixel(it, cx + sc(2), cy + 1, c);
      break;
    case AreaGlyphKind::STAIRS:
      fast_hline(it, cx - sc(7), cy + sc(6), scr(5), c);
      fast_vline(it, cx - sc(3), cy + sc(2), scr(5), c);
      fast_hline(it, cx - sc(3), cy + sc(2), scr(5), c);
      fast_vline(it, cx + sc(1), cy - sc(2), scr(5), c);
      fast_hline(it, cx + sc(1), cy - sc(2), scr(5), c);
      fast_vline(it, cx + sc(5), cy - sc(6), scr(5), c);
      fast_hline(it, cx + sc(5), cy - sc(6), scr(3), c);
      break;
    case AreaGlyphKind::ROOF:
      fast_line(it, cx - sc(8), cy + sc(1), cx, cy - sc(6), c);
      fast_line(it, cx, cy - sc(6), cx + sc(8), cy + sc(1), c);
      fast_hline(it, cx - sc(6), cy + sc(2), scr(13), c);
      fast_rect_outline(it, cx - sc(2), cy - sc(2), scr(5), scr(4), c);
      break;
    case AreaGlyphKind::POOL:
      fast_vline(it, cx + sc(4), cy - sc(7), scr(8), c);
      fast_vline(it, cx + sc(7), cy - sc(7), scr(8), c);
      fast_hline(it, cx + sc(4), cy - sc(4), scr(4), c);
      for (int row = 0; row < 2; ++row) {
        const int wy = cy + sc(2 + row * 4);
        fast_line(it, cx - sc(8), wy, cx - sc(4), wy - sc(2), c);
        fast_line(it, cx - sc(4), wy - sc(2), cx, wy, c);
        fast_line(it, cx, wy, cx + sc(4), wy - sc(2), c);
        fast_line(it, cx + sc(4), wy - sc(2), cx + sc(8), wy, c);
      }
      break;
    case AreaGlyphKind::BALCONY:
      fast_filled_circle(it, cx + sc(4), cy - sc(5), scr(2), c);
      fast_hline(it, cx - sc(8), cy, scr(17), c);
      fast_hline(it, cx - sc(8), cy + sc(6), scr(17), c);
      for (int b = -6; b <= 6; b += 4) {
        fast_vline(it, cx + sc(b), cy, scr(7), c);
      }
      break;
    case AreaGlyphKind::GARDEN:
      fast_filled_circle(it, cx, cy - sc(4), scr(4), c);
      fast_filled_circle(it, cx - sc(3), cy - sc(1), scr(3), c);
      fast_filled_circle(it, cx + sc(3), cy - sc(1), scr(3), c);
      fill_rect_row_major(it, cx - sc(1), cy + sc(2), scr(2), scr(6), c);
      break;
    case AreaGlyphKind::SPEAKER:
      fast_rect_outline(it, cx - sc(5), cy - sc(7), scr(11), scr(15), c);
      fast_pixel(it, cx, cy - sc(4), c);
      fast_circle(it, cx, cy + sc(2), scr(3), c);
      break;
    case AreaGlyphKind::MAP_PIN:
    default:
      fast_circle(it, cx, cy - sc(2), scr(5), c);
      fast_circle(it, cx, cy - sc(2), std::max(1, scr(5) - 1), c);
      fast_line(it, cx - sc(4), cy + sc(1), cx, cy + sc(7), c);
      fast_line(it, cx + sc(4), cy + sc(1), cx, cy + sc(7), c);
      fast_filled_circle(it, cx, cy - sc(2), scr(1), c);
      break;
  }
}

}  // namespace

// ==============================================================================
// Shared Home / Activity Deck State & Helpers
// ==============================================================================

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

void dismiss_wake_greeting() {
  wake_greeting_latched() = false;
  runtime_state().nav.wake_greeting_pending = false;
}

bool is_wake_greeting_visible(uint32_t now_ms, bool enabled) {
  if (!enabled || !wake_greeting_latched()) return false;
  if (now_ms < wake_greeting_start_ms()) return false;
  return (now_ms - wake_greeting_start_ms()) < kWakeGreetingTotalMs;
}

bool is_wake_greeting_fading(uint32_t now_ms, bool enabled) {
  if (!enabled || !wake_greeting_latched()) return false;
  if (now_ms < wake_greeting_start_ms()) return false;
  const uint32_t el = now_ms - wake_greeting_start_ms();
  return (el >= (kWakeGreetingHoldMs - 60U) && el <= (kWakeGreetingTotalMs + 60U));
}

bool is_doorbell_or_intercom_active() {
  const auto &ac = area_ctx_state();
  const auto &rs = runtime_state();
  const bool db_live = !ac.doorbell_talk_state.empty() &&
                       ac.doorbell_talk_state != "idle" &&
                       ac.doorbell_talk_state != "off" &&
                       ac.doorbell_talk_state != "none" &&
                       ac.doorbell_talk_state != "unavailable" &&
                       ac.doorbell_talk_state != "unknown";
  return db_live || rs.voice.intercom_turn || cam_call_active() || cam_listen_active();
}

bool is_home_activity_carousel_animating(uint32_t now_ms) {
  const uint32_t t0 = home_act_state().anim_start_ms;
  return t0 != 0U && (now_ms < t0 || (now_ms - t0) < kMotionMs);
}

bool is_home_activity_fading_out(uint32_t now_ms) {
  const uint32_t t0 = home_act_state().fade_out_start_ms;
  return t0 != 0U && (now_ms < t0 || (now_ms - t0) < kMotionMs);
}

bool has_pending_home_activity_fade_settle(uint32_t now_ms) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0) return false;
  const uint32_t t0 = home_act_state().fade_out_start_ms;
  if (t0 == 0U || now_ms < t0) return false;
  const uint32_t el = now_ms - t0;
  return el >= kMotionMs && el < (kMotionMs + 250U);
}

bool home_activity_slide_active(uint32_t now_ms) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0) return false;
  return (home_activity_card_shown() && is_home_activity_carousel_animating(now_ms)) ||
         is_home_activity_fading_out(now_ms);
}

uint32_t home_activity_next_rotate_due_ms(uint32_t now_ms) {
  const auto &rs = runtime_state();
  if (rs.nav.gui_page != PageId::HOME || rs.nav.home_subpage != 0 ||
      !home_activity_card_shown() || !s_act_deck_rotates) {
    return 0U;
  }
  const uint32_t rem = kHomeActivityRotateMs - (now_ms % kHomeActivityRotateMs);
  return std::max<uint32_t>(1U, rem);
}

bool home_activity_band(int &out_y0, int &out_y1) {
  if ((!home_activity_card_shown() && home_act_state().fade_out_start_ms == 0U) ||
      s_act_band_y1 <= s_act_band_y0) {
    return false;
  }
  out_y0 = s_act_band_y0;
  out_y1 = s_act_band_y1;
  return true;
}

const HomeActivityItem *home_activity_rendered_items(int &out_count) {
  out_count = s_rendered_act_count;
  return s_rendered_act_items;
}

void render_area_archetype_icon(esphome::display::Display &it, int cx, int cy, int r,
                                std::string_view icon_str, std::string_view area_name_fallback,
                                esphome::Color color) {
  const AreaGlyphKind kind = classify_area_glyph(icon_str, area_name_fallback);
  draw_area_glyph_pass(it, cx + 1, cy + 1, r, kind, kChromeShadow);
  draw_area_glyph_pass(it, cx, cy, r, kind, color);
}

int collect_home_activities(
    uint32_t now_ms, bool is_24h_format,
    bool stopwatch_running, uint32_t stopwatch_start_time, uint32_t stopwatch_elapsed_ms,
    bool music_playing, const std::string &music_title, const std::string &music_artist,
    uint32_t alarm_snooze_until,
    int alarm1_h, int alarm1_m, bool alarm1_en,
    int alarm2_h, int alarm2_m, bool alarm2_en,
    int alarm3_h, int alarm3_m, bool alarm3_en,
    HomeActivityItem out_items[kMaxHomeActivity],
    int cur_hour, int cur_min,
    const std::string &calendar_agenda_raw) {
  int count = 0;

  // 0. Active Live Intercom / Doorbell Call (highest urgent pin priority)
  if (is_doorbell_or_intercom_active() && count < kMaxHomeActivity) {
    const auto &ac = area_ctx_state();
    std::string lbl = "Intercom • Live";
    if (ac.doorbell_talk_state == "ringing") {
      lbl = "Doorbell • Ringing";
    } else if (cam_call_active() || cam_listen_active() ||
               (!ac.doorbell_talk_state.empty() && ac.doorbell_talk_state != "idle" &&
                ac.doorbell_talk_state != "off" && ac.doorbell_talk_state != "none" &&
                ac.doorbell_talk_state != "unavailable" && ac.doorbell_talk_state != "unknown")) {
      lbl = "Doorbell • Live";
    }
    out_items[count++] = {6, 0, true, true, true, lbl};
  }

  // 1. Active or Paused Countdown Timers (locks top slot ONLY if running with <= 60 seconds left)
  auto *t_slots = timer_slots();
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < 3 && count < kMaxHomeActivity; i++) {
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
  if ((stopwatch_running || sw_ms > 0) && count < kMaxHomeActivity) {
    const int mins = static_cast<int>((sw_ms / 60000U) % 60U);
    const int secs = static_cast<int>((sw_ms / 1000U) % 60U);
    const int tenths = static_cast<int>((sw_ms / 100U) % 10U);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Stopwatch • %02d:%02d.%d", mins, secs, tenths);
    out_items[count++] = {2, 0, stopwatch_running, stopwatch_running, true, std::string(buf)};
  }

  // 3. Active or Recently-Paused Music Queue (D-039: 5-min live-pause window)
  const auto &ms = media_state();
  const bool is_playing_eff = music_playing;
  if (is_playing_eff) {
    music_queue_cleared() = false;
  }
  const bool is_paused_st = is_music_paused_effective(is_playing_eff, ms.is_local, ms.state);
  const bool has_valid_track = !music_title.empty() || !ms.title.empty();
  const bool music_vis =
      (is_playing_eff || !music_queue_cleared()) &&
      audio_session().is_music_visible_on_home_activity(
          is_playing_eff, is_paused_st, has_valid_track, now_ms);
  if (music_vis && count < kMaxHomeActivity) {
    std::string m_txt = !music_title.empty() ? music_title : ms.title;
    if (m_txt.empty()) {
      m_txt = "Now Playing";
    } else if (!music_artist.empty()) {
      m_txt = music_artist + " • " + m_txt;
    }
    out_items[count++] = {3, 0, is_playing_eff, false, true, m_txt};
  }

  // 4. Armed or Snoozed Alarms
  if (alarm_snooze_until > 0 && count < kMaxHomeActivity) {
    out_items[count++] = {4, 0, true, false, true, "Alarm • Snoozed"};
  } else {
    const int a_hrs[3] = {alarm1_h, alarm2_h, alarm3_h};
    const int a_mns[3] = {alarm1_m, alarm2_m, alarm3_m};
    const bool a_ens[3] = {alarm1_en, alarm2_en, alarm3_en};
    const int now_total_m = (cur_hour >= 0 && cur_min >= 0) ? (cur_hour * 60 + cur_min) : -1;
    for (int i = 0; i < 3 && count < kMaxHomeActivity; i++) {
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

  // 5. Today's Upcoming Calendar Events (up to 3 TODAY items in rotating deck; first future item in overlay)
  CalendarRecord cal_recs[8];
  const size_t n_cal = parse_calendar_records(calendar_agenda_raw, cal_recs, 8);
  int added_today = 0;
  for (size_t c = 0; c < n_cal && added_today < 3 && count < kMaxHomeActivity; ++c) {
    if (cal_recs[c].time.rfind("TODAY", 0) == 0) {
      out_items[count++] = {
          5, static_cast<int>(c), false, false, true,
          std::string(cal_recs[c].time) + " • " + std::string(cal_recs[c].title)};
      ++added_today;
    }
  }
  if (added_today == 0 && n_cal > 0 && count < kMaxHomeActivity) {
    out_items[count++] = {
        5, 0, false, false, false,
        std::string(cal_recs[0].time) + " • " + std::string(cal_recs[0].title)};
  }

  // Device's Latter Activity Card setting (D-029); live Intercom/Doorbell (kind == 6) always surfaces.
  const uint8_t mask = activity_source_mask();
  int kept = 0;
  for (int i = 0; i < count; i++) {
    const bool keep = (out_items[i].kind == 6) || ((mask & (1U << out_items[i].kind)) != 0U);
    if (keep) {
      if (kept != i) out_items[kept] = out_items[i];
      kept++;
    }
  }
  return kept;
}

HomeActivityView resolve_home_activity_view(const HomeActivityItem items[], int count,
                                            uint32_t now_ms, bool two_line) {
  HomeActivityView v;
  if (count <= 0) return v;
  int pin = -1;
  for (int i = 0; i < count && pin < 0; i++) {
    if (items[i].kind == 6 && items[i].urgent_pin) pin = i;
  }
  for (int i = 0; i < count && pin < 0; i++) {
    if (items[i].kind == 2 && items[i].urgent_pin) pin = i;
  }
  for (int i = 0; i < count && pin < 0; i++) {
    if (items[i].kind == 1 && items[i].urgent_pin) pin = i;
  }
  int deck[kMaxHomeActivity];
  int n = 0;
  for (int i = 0; i < count && n < kMaxHomeActivity; i++) {
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
  const UiStrView clock_face_opt = ctx.clock_face_style_opt;
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
  const std::string music_title =
      (is_rem_media || ctx.track_title.empty()) ? ms.title : ctx.track_title.str();
  const std::string music_artist =
      (is_rem_media || ctx.track_artist.empty()) ? ms.artist : ctx.track_artist.str();

  const bool sim_override_on = (!ctx.weather_sim_opt.empty() && ctx.weather_sim_opt != "Live (Home Assistant)");
  const bool has_custom_wallpaper = (!ac.wallpaper_url.empty() &&
                                     ac.wallpaper_url == rs.music.loaded_wallpaper_url &&
                                     ctx.home_wallpaper_art.get_width() > 0);
  const BackdropMode bd_mode = resolve_backdrop_mode(
      PageId::HOME, home_subpage, show_weather_bg, sim_override_on, has_custom_wallpaper);

  if (home_subpage == 1) {
    s_act_deck_rotates = false;
    home_activity_card_shown() = false;
    home_act_state().was_visible = false;
    home_act_state().fade_out_start_ms = 0U;
    render_page_wallpaper_base(it, ctx.home_wallpaper_art,
                               bd_mode == BackdropMode::CustomWallpaper, cx, ctx.col_bg);
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

  struct HomeActivitiesMemo {
    HomeActivityItem items[kMaxHomeActivity]{};
    int count{-1};
    uint32_t sw_tick{0xFFFFFFFFU};
    int t_rem[3]{-1, -1, -1};
    uint8_t t_flags{0xFFU};
    std::string t_names[3]{};
    bool is_24h{false};
    bool music_on{false};
    bool music_cleared{false};
    bool music_vis{false};
    std::string m_title{};
    std::string m_artist{};
    uint32_t snooze{0xFFFFFFFFU};
    uint8_t a_h[3]{0, 0, 0};
    uint8_t a_m[3]{0, 0, 0};
    uint8_t a_en{0xFFU};
    int cur_h{-2};
    int cur_m{-2};
    uint8_t src_mask{0xFFU};
    std::string cal_raw{};
    bool intercom_on{false};
    std::string doorbell_st{};
  };
  static HomeActivitiesMemo act_memo;
  const auto *ts_chk = timer_slots();
  const uint32_t cur_sw_tick = rs.stopwatch.running
      ? ((now_ms - rs.stopwatch.start_time_ms) / 100U)
      : (rs.stopwatch.elapsed_ms / 100U);
  const uint8_t cur_t_flags =
      (ts_chk[0].running ? 1U : 0U) | (ts_chk[0].paused ? 2U : 0U) |
      (ts_chk[1].running ? 4U : 0U) | (ts_chk[1].paused ? 8U : 0U) |
      (ts_chk[2].running ? 16U : 0U) | (ts_chk[2].paused ? 32U : 0U) |
      (rs.stopwatch.running ? 64U : 0U);
  const uint8_t cur_a_en =
      (prefs.alarms[0].enabled ? 1U : 0U) |
      (prefs.alarms[1].enabled ? 2U : 0U) |
      (prefs.alarms[2].enabled ? 4U : 0U);
  const int cur_h = ctx.now.is_valid() ? ctx.now.hour : -1;
  const int cur_m = ctx.now.is_valid() ? ctx.now.minute : -1;
  const uint8_t cur_mask = activity_source_mask();
  const bool eff_cleared = music_playing ? false : music_queue_cleared();
  const bool eff_paused_memo =
      !eff_cleared && is_music_paused_effective(music_playing, ms.is_local, ms.state);
  const bool cur_music_vis =
      (music_playing || !eff_cleared) &&
      audio_session().is_music_visible_on_home_activity(
          music_playing, eff_paused_memo, !music_title.empty() || !ms.title.empty(), now_ms);
  const bool cur_intercom = is_doorbell_or_intercom_active();
  if (act_memo.count < 0 ||
      act_memo.sw_tick != cur_sw_tick ||
      act_memo.t_flags != cur_t_flags ||
      act_memo.t_rem[0] != ts_chk[0].remaining_sec ||
      act_memo.t_rem[1] != ts_chk[1].remaining_sec ||
      act_memo.t_rem[2] != ts_chk[2].remaining_sec ||
      act_memo.t_names[0] != ts_chk[0].name ||
      act_memo.t_names[1] != ts_chk[1].name ||
      act_memo.t_names[2] != ts_chk[2].name ||
      act_memo.is_24h != is_24h_format ||
      act_memo.music_on != music_playing ||
      act_memo.music_cleared != eff_cleared ||
      act_memo.music_vis != cur_music_vis ||
      act_memo.m_title != music_title ||
      act_memo.m_artist != music_artist ||
      act_memo.snooze != rs.alarms.snooze_until ||
      act_memo.a_en != cur_a_en ||
      act_memo.a_h[0] != prefs.alarms[0].hour || act_memo.a_m[0] != prefs.alarms[0].minute ||
      act_memo.a_h[1] != prefs.alarms[1].hour || act_memo.a_m[1] != prefs.alarms[1].minute ||
      act_memo.a_h[2] != prefs.alarms[2].hour || act_memo.a_m[2] != prefs.alarms[2].minute ||
      act_memo.cur_h != cur_h || act_memo.cur_m != cur_m ||
      act_memo.src_mask != cur_mask ||
      act_memo.cal_raw != cs.summary ||
      act_memo.intercom_on != cur_intercom ||
      act_memo.doorbell_st != ac.doorbell_talk_state) {
    act_memo.count = collect_home_activities(
        now_ms, is_24h_format,
        rs.stopwatch.running, rs.stopwatch.start_time_ms, rs.stopwatch.elapsed_ms,
        music_playing, music_title, music_artist,
        rs.alarms.snooze_until,
        prefs.alarms[0].hour, prefs.alarms[0].minute, prefs.alarms[0].enabled,
        prefs.alarms[1].hour, prefs.alarms[1].minute, prefs.alarms[1].enabled,
        prefs.alarms[2].hour, prefs.alarms[2].minute, prefs.alarms[2].enabled,
        act_memo.items, cur_h, cur_m, cs.summary);
    act_memo.sw_tick = cur_sw_tick;
    act_memo.t_flags = cur_t_flags;
    for (int ti = 0; ti < 3; ti++) {
      act_memo.t_rem[ti] = ts_chk[ti].remaining_sec;
      act_memo.t_names[ti] = ts_chk[ti].name;
      act_memo.a_h[ti] = prefs.alarms[ti].hour;
      act_memo.a_m[ti] = prefs.alarms[ti].minute;
    }
    act_memo.is_24h = is_24h_format;
    act_memo.music_on = music_playing;
    act_memo.music_cleared = music_queue_cleared();
    act_memo.music_vis = cur_music_vis;
    act_memo.m_title = music_title;
    act_memo.m_artist = music_artist;
    act_memo.snooze = rs.alarms.snooze_until;
    act_memo.a_en = cur_a_en;
    act_memo.cur_h = cur_h;
    act_memo.cur_m = cur_m;
    act_memo.src_mask = cur_mask;
    act_memo.cal_raw = cs.summary;
    act_memo.intercom_on = cur_intercom;
    act_memo.doorbell_st = ac.doorbell_talk_state;
  }
  const HomeActivityItem *act_items = act_memo.items;
  const int act_count = act_memo.count;
  store_rendered_activity_snapshot(act_items, act_count);

  std::string cond = ws.condition;
  bool is_night = (ws.sun_state == "below_horizon" || cond == "clear-night" ||
                   cond == "clearnight" || (cond.find("night") != std::string::npos));
  apply_weather_sim_override(ctx.weather_sim_opt, cond, is_night);

  static std::string s_memo_cond_in;
  static std::string s_memo_cond_title = "Unavailable";
  if (cond != s_memo_cond_in) {
    s_memo_cond_in = cond;
    s_memo_cond_title = format_weather_title(cond);
  }
  const std::string &cond_title = s_memo_cond_title;
  const bool sun_ready = (!ws.sun_state.empty() && ws.sun_state != "unknown" && ws.sun_state != "unavailable");
  const bool weather_ready = ((!std::isnan(ws.temperature) && sun_ready) || sim_override_on) &&
                             (cond_title != "Unavailable");
  const bool live_sky_drawn = (bd_mode == BackdropMode::LiveWeatherSky && weather_ready);

  const int cel_x = layout().celestial_x;
  const int cel_y = layout().celestial_y;
  const bool show_celestial = is_night ? show_moon : show_sun;

  if (home_subpage == 2) {
    s_act_deck_rotates = false;
    home_activity_card_shown() = false;
    home_act_state().was_visible = false;
    home_act_state().fade_out_start_ms = 0U;
    if (bd_mode == BackdropMode::CustomWallpaper) {
      render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false, 2);
    } else if ((show_weather_bg || sim_override_on) && weather_ready) {
      backdrop_active() = true;
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
      frame_base(it, ctx.col_bg);
    }
  } else {
    if (live_sky_drawn) {
      backdrop_active() = true;
      double moon_cycle = 0.5;
      if (is_night) {
        compute_synodic_moon_phase(ctx.now.is_valid(),
                                   ctx.now.is_valid() ? static_cast<int64_t>(ctx.now.timestamp) : 0LL,
                                   ws.moon_phase, moon_cycle, nullptr, nullptr);
      }
      render_weather_motion_wallpaper(it, cx, cy, now_ms, cond, is_night,
                                      moon_cycle, cel_x, cel_y, true,
                                      rs.hw_accel_x, rs.hw_accel_y, show_celestial);
    } else if (has_custom_wallpaper) {
      render_album_art_canvas(it, ctx.home_wallpaper_art, cx, false, false, true, false, 0);
    } else {
      frame_base(it, ctx.col_bg);
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

    const Color ov_bg = kChromeGlassTint;
    const Color ov_brd = kChromeCardBorder;
    const Color ov_btn = kChromeGlassTint;

    if (act_count <= 0) {
      render_empty_state(it, cx, cy, "No active activities", ctx.font_small, ctx.col_slate);
    } else {
      const int total_pages = std::max(1, (act_count + 4) / 5);
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

  if (live_sky_drawn && show_celestial) {
    frame_hits().add_circle(cel_x, cel_y, LATTER_ROUND_DISPLAY ? scale_min(30) : scale_min(34),
                            WidgetId::HOME_QUICK_BTN, kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(10));
  }

  const Color fg_home_white = ctx.col_white;
  const Color fg_home_cond  = (!live_sky_drawn || is_night) ? ctx.theme_secondary : Color(255, 225, 110);

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

  // Evaluate Activity View ahead of Wake Greeting so urgent pinned items immediately supersede greeting
  const auto &ly = layout();
  const bool two = ly.home_activity_two_line();
  const HomeActivityView view = resolve_home_activity_view(act_items, act_count, now_ms, two);

  // Transient Wake Greeting (2.8s Full Brightness + 1.2s True-Alpha Cubic Fade-Out = 4.0s Total)
  if (rs.nav.wake_greeting_pending) {
    rs.nav.wake_greeting_pending = false;
    if (show_wake_greeting && ctx.now.is_valid() && !view.pinned) {
      wake_greeting_latched() = true;
      wake_greeting_start_ms() = (now_ms == 0U) ? 1U : now_ms;
    }
  }
  if (wake_greeting_latched()) {
    const uint32_t el_chk = (now_ms >= wake_greeting_start_ms()) ? (now_ms - wake_greeting_start_ms()) : 0U;
    if (!show_wake_greeting || view.pinned || !ctx.now.is_valid() || el_chk >= kWakeGreetingTotalMs) {
      wake_greeting_latched() = false;
    }
  }
  const uint32_t greet_el = (wake_greeting_latched() && now_ms >= wake_greeting_start_ms())
                                ? (now_ms - wake_greeting_start_ms())
                                : 999999U;
  const bool greeting_active = wake_greeting_latched() && (greet_el < kWakeGreetingTotalMs);

  // ZONE 4: Home activity card (D-028 / D-038)
  if (ctx.font_date != nullptr) {
    static bool weather_h_measured = false;
    if (!weather_h_measured) {
      int m_w = 0, m_xo = 0, m_bl = 0, m_h = 0;
      ctx.font_date->measure("Ag", &m_w, &m_xo, &m_bl, &m_h);
      if (m_h > 0) home_weather_text_h() = m_h;
      weather_h_measured = true;
    }
  }
  const int act_card_cy = ly.home_activity_cy(home_weather_text_h());
  home_activity_card_shown() = (home_subpage == 0 && !greeting_active && act_count > 0);
  if (home_activity_card_shown()) {
    const int card_h = ly.home_activity_h();
    const int card_w = ly.home_activity_w();
    const int pitch = ly.home_activity_pitch();
    const int row_top = two ? (act_card_cy - pitch / 2) : act_card_cy;
    const int row_bot = act_card_cy + pitch / 2;
    const bool has_drop = ly.home_activity_has_drop(act_count);
    rs.nav.home_activity_idx = view.top;
    s_act_band_y0 = std::max(0, act_card_cy - card_h / 2 - 2);
    s_act_band_y1 = std::min(SCREEN_H - 1, act_card_cy + card_h / 2 + 2);

    int deck_cnt = 0;
    for (int di = 0; di < act_count; ++di) {
      if (act_items[di].in_deck && (!view.pinned || di != view.top)) ++deck_cnt;
    }
    s_act_deck_rotates = view.pinned ? (two && deck_cnt > 1) : (deck_cnt > (two ? 2 : 1));

    auto &st = home_act_state();
    st.fade_out_start_ms = 0U;
    st.was_visible = true;
    st.fade_out_two = two && (view.bottom >= 0);
    st.fade_out_has_drop = has_drop;
    st.fade_out_card_h = card_h;
    st.fade_out_top_item =
        (view.top >= 0 && view.top < act_count) ? act_items[view.top] : HomeActivityItem{};
    st.fade_out_bot_item =
        (st.fade_out_two && view.bottom >= 0 && view.bottom < act_count) ? act_items[view.bottom]
                                                                         : HomeActivityItem{};
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

    const Color tint = kChromeGlassTint;
    const Color card_brd = kChromeCardBrdOn;
    const Color btn_bg = kChromeGlassTint;
    const int card_r = std::min(card_h / 2, (SCREEN_W > 520) ? scale_s(16) : 16);
    const int card_x0 = cx - card_w / 2;
    const int card_y0 = act_card_cy - card_h / 2;
    if ((card_y0 + card_h) > clip_y0() && card_y0 <= clip_y1()) {
      const GlassBackdrop backdrop = sample_glass_backdrop(card_x0, card_y0, card_w, card_h);
      static int glass_alpha_state = 0;
      static uint32_t last_glass_alpha_ms = 0;
      const uint32_t alpha_dt_ms = (last_glass_alpha_ms == 0 || now_ms < last_glass_alpha_ms)
                                       ? 16U
                                       : (now_ms - last_glass_alpha_ms);
      last_glass_alpha_ms = now_ms;
      const int glass_a = backdrop.valid
          ? glass_alpha_smooth(glass_alpha_state,
                               glass_alpha_for_text(tint, backdrop.bright, ctx.col_white),
                               alpha_dt_ms)
          : 256;
      render_glass_card(it, card_x0, card_y0, card_w, card_h, card_r, tint, glass_a, backdrop,
                        GlassVariant::Clear, ctx.col_white);
      const Color card_bg = backdrop.valid ? mix_color_q8(backdrop.mean, tint, glass_a) : tint;

      const int prev_clip_y0 = clip_y0();
      const int prev_clip_y1 = clip_y1();
      const int inner_y0 = std::max(prev_clip_y0, card_y0 + 1);
      const int inner_y1 = std::min(prev_clip_y1, card_y0 + card_h - 2);
      if (inner_y0 <= inner_y1) {
        set_clip_y_band(inner_y0, inner_y1);
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
        set_clip_y_band(prev_clip_y0, prev_clip_y1);
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
    }

    register_home_activity_hit_regions(
        frame_hits(), cx, act_card_cy, card_w, card_h,
        two && (view.bottom >= 0), has_drop, view.top, view.bottom,
        scale_x(56), scale_x(52));
  } else {
    s_act_deck_rotates = false;
    auto &st = home_act_state();
    if (home_subpage != 0 || greeting_active) {
      st.was_visible = false;
      st.fade_out_start_ms = 0U;
      st.top = -1;
      st.bottom = -1;
      st.anim_start_ms = 0U;
    } else {
      if (st.was_visible) {
        st.was_visible = false;
        st.top = -1;
        st.bottom = -1;
        st.from_top = -1;
        st.from_bottom = -1;
        st.anim_start_ms = 0U;
        st.fade_out_start_ms = (now_ms == 0U) ? 1U : now_ms;
      }
      if (st.fade_out_start_ms != 0U) {
        const uint32_t el = (now_ms >= st.fade_out_start_ms) ? (now_ms - st.fade_out_start_ms) : 0U;
        if (el >= kMotionMs) {
          st.fade_out_start_ms = 0U;
        } else {
          const float e = ease_out_cubic(static_cast<float>(el) / static_cast<float>(kMotionMs));
          const float fade_alpha = std::clamp(1.0f - e, 0.0f, 1.0f);
          const int card_h = (st.fade_out_card_h > 0) ? st.fade_out_card_h : ly.home_activity_h();
          const int card_w = ly.home_activity_w();
          const int pitch = ly.home_activity_pitch();
          const int row_top = st.fade_out_two ? (act_card_cy - pitch / 2) : act_card_cy;
          const int row_bot = act_card_cy + pitch / 2;
          const int card_r = std::min(card_h / 2, (SCREEN_W > 520) ? scale_s(16) : 16);
          const int card_x0 = cx - card_w / 2;
          const int card_y0 = act_card_cy - card_h / 2;
          s_act_band_y0 = std::max(0, card_y0 - 2);
          s_act_band_y1 = std::min(SCREEN_H - 1, card_y0 + card_h + 2);

          const int prev_clip_y0 = clip_y0();
          const int prev_clip_y1 = clip_y1();
          const int sc_y0 = std::max(prev_clip_y0, card_y0);
          const int sc_y1 = std::min(prev_clip_y1, card_y0 + card_h - 1);
          if (fade_alpha > 0.01f && sc_y0 <= sc_y1) {
            set_clip_y_band(sc_y0, sc_y1);
            const Color tint = kChromeGlassTint;
            const GlassBackdrop backdrop = sample_glass_backdrop(card_x0, card_y0, card_w, card_h);
            Color eff_tint;
            int raw_a_top = 0, raw_a_bot = 0, raw_rim_top = 0, raw_rim_bot = 0;
            resolve_glass_fill(backdrop, tint, ctx.col_white, eff_tint,
                               raw_a_top, raw_a_bot, raw_rim_top, raw_rim_bot,
                               -1, GlassVariant::Clear);
            const int a_top = static_cast<int>(std::lround(static_cast<float>(raw_a_top) * fade_alpha));
            const int a_bot = static_cast<int>(std::lround(static_cast<float>(raw_a_bot) * fade_alpha));
            const int rim_top = static_cast<int>(std::lround(static_cast<float>(raw_rim_top) * fade_alpha));
            const int rim_bot = static_cast<int>(std::lround(static_cast<float>(raw_rim_bot) * fade_alpha));
            if (rim_top > 0 || rim_bot > 0) {
              fill_round_rect_glass(it, card_x0, card_y0, card_w, card_h, static_cast<float>(card_r),
                                    kChromeGlassSheen, rim_top, rim_bot, 1);
            }
            if ((a_top > 0 || a_bot > 0) && card_w > 2 && card_h > 2) {
              fill_round_rect_glass(it, card_x0 + 1, card_y0 + 1, card_w - 2, card_h - 2,
                                    std::max(0.5f, static_cast<float>(card_r) - 1.0f), eff_tint, a_top, a_bot);
            }
            const int mean_a = (a_top + a_bot) / 2;
            const Color base_mean = backdrop.valid ? backdrop.mean : ctx.col_bg;
            const Color card_bg = mix_color_q8(base_mean, eff_tint, mean_a);

            const int inner_y0 = std::max(sc_y0, card_y0 + 1);
            const int inner_y1 = std::min(sc_y1, card_y0 + card_h - 2);
            if (inner_y0 <= inner_y1) {
              set_clip_y_band(inner_y0, inner_y1);
              if (st.fade_out_two) {
                if (st.fade_out_top_item.kind != 0) {
                  draw_home_activity_row_contents(
                      it, row_top, st.fade_out_top_item, st.fade_out_has_drop, false, ctx.font_small,
                      card_bg, tint, ctx.theme_secondary, ctx.col_white, fade_alpha);
                }
                if (st.fade_out_bot_item.kind != 0) {
                  draw_home_activity_row_contents(
                      it, row_bot, st.fade_out_bot_item, st.fade_out_has_drop, false, ctx.font_small,
                      card_bg, tint, ctx.theme_secondary, ctx.col_white, fade_alpha);
                }
              } else if (st.fade_out_top_item.kind != 0) {
                draw_home_activity_row_contents(
                    it, act_card_cy, st.fade_out_top_item, st.fade_out_has_drop, false, ctx.font_small,
                    card_bg, tint, ctx.theme_secondary, ctx.col_white, fade_alpha);
              }

              const Color div_col = lerp_color(card_bg, kChromeCardBrdOn, fade_alpha);
              const int card_right = cx + card_w / 2;
              const int div1_x = st.fade_out_has_drop ? (card_right - scale_x(108)) : (card_right - scale_x(56));
              const int div_h = scale_y(st.fade_out_two ? 17 : 23);
              if (st.fade_out_two) {
                fast_line(it, div1_x, row_top - div_h, div1_x, row_top + div_h, div_col);
                fast_line(it, div1_x, row_bot - div_h, div1_x, row_bot + div_h, div_col);
              } else {
                fast_line(it, div1_x, act_card_cy - div_h, div1_x, act_card_cy + div_h, div_col);
              }
              if (st.fade_out_has_drop) {
                const int div2_x = card_right - scale_x(52);
                const int col_h = card_h / 2 - scale_y(10);
                fast_line(it, div2_x, act_card_cy - col_h, div2_x, act_card_cy + col_h, div_col);
                const Color chev_col = lerp_color(card_bg, ctx.col_white, fade_alpha);
                const int d_bx = card_right - scale_x(26);
                for (int w = -1; w <= 1; w++) {
                  fast_line(it, d_bx - 8, act_card_cy - 4 + w, d_bx, act_card_cy + 4 + w, chev_col);
                  fast_line(it, d_bx + 8, act_card_cy - 4 + w, d_bx, act_card_cy + 4 + w, chev_col);
                }
              }
            }
            set_clip_y_band(prev_clip_y0, prev_clip_y1);
          }
        }
      }
    }
  }

  if (greeting_active) {
    const char *raw_greet = resolve_wake_greeting(
        ctx.now.hour, ctx.now.minute, ctx.now.day_of_year, cond, ws.temperature);
    const std::string fit_greet = fit_text_to_width_font(
        std::string(raw_greet), std::min(scale_x(340), safe_span(act_card_cy)), ctx.font_date);
    const int greet_y = act_card_cy - 2;

    float alpha = 1.0f;
    if (greet_el > kWakeGreetingHoldMs) {
      const float u = std::clamp(
          1.0f - static_cast<float>(greet_el - kWakeGreetingHoldMs) / static_cast<float>(kWakeGreetingFadeMs),
          0.0f, 1.0f);
      alpha = u * u * (3.0f - 2.0f * u);
    }
    const int alpha_q8 = std::clamp(static_cast<int>(std::lround(alpha * 256.0f)), 0, 256);
    if (alpha_q8 > 0) {
      const int shadow_q8 = (alpha_q8 * 192) >> 8;
      if (shadow_q8 > 8) {
        fast_print_alpha(it, cx + 1, greet_y + 1, ctx.font_date, kChromeShadow,
                         TextAlign::CENTER, fit_greet.c_str(), shadow_q8);
      }
      fast_print_alpha(it, cx, greet_y, ctx.font_date, ctx.col_white,
                       TextAlign::CENTER, fit_greet.c_str(), alpha_q8);
    }
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
    const int dock_y = ly.dock_y;
    const int dock_r = ly.dock_r;

    const int dock_pad = std::max(0, ly.dock_hit_r() - dock_r);
    for (int v = 0; v < vis_count; v++) {
      frame_hits().add_circle(win_xs[v], dock_y, dock_r,
                              WidgetId::DOCK_SLOT_BASE + v,
                              kHitTap | kHitFocusSlot | kHitFocusCircle,
                              static_cast<int16_t>(dock_apps[start_idx + v]),
                              dock_pad);
    }

    const float dk = static_cast<float>(std::max(1, dock_r)) / 27.0f;
    auto sd = [dk](int v) -> int { return static_cast<int>(std::lround(static_cast<float>(v) * dk)); };
    auto sdr = [dk](int v) -> int { return std::max(1, static_cast<int>(std::lround(static_cast<float>(v) * dk))); };

    auto draw_dock_glyph = [&](int app_id, int bx, int by, Color c, bool is_shadow, Color cut_c) {
      if (app_id == 6) {
        fast_filled_triangle(it, bx + sd(11), by - sd(15), bx + sd(6), by - sd(7), bx + sd(16), by - sd(7), c);
        fill_rect_row_major(it, bx + sd(8), by - sd(14), sdr(7), sdr(3), c);
        fast_vline(it, bx + sd(11), by - sd(7), sdr(19), c);
        fast_vline(it, bx + sd(12), by - sd(7), sdr(19), c);
        fast_hline(it, bx + sd(8), by + sd(12), sdr(8), c);
        fill_rect_row_major(it, bx - sd(12), by - sd(4), sdr(15), sdr(8), c);
        fast_filled_circle(it, bx - sd(11), by - sd(2), sdr(2), c);
        fast_filled_circle(it, bx + sd(1),  by - sd(2), sdr(2), c);
        fast_filled_circle(it, bx - sd(14), by + sd(3), sdr(3), c);
        fast_filled_circle(it, bx + sd(4),  by + sd(3), sdr(3), c);
        fill_rect_row_major(it, bx - sd(15), by + sd(3), sdr(21), sdr(7), c);
        fill_rect_row_major(it, bx - sd(14), by + sd(10), sdr(3), sdr(3), c);
        fill_rect_row_major(it, bx + sd(2),  by + sd(10), sdr(3), sdr(3), c);
        if (!is_shadow) {
          fast_vline(it, bx - sd(5), by - sd(4), sdr(8), cut_c);
          fast_hline(it, bx - sd(12), by + sd(4), sdr(15), cut_c);
        }
      } else if (app_id == 2) {
        fast_filled_circle(it, bx - sd(9), by + sd(9), sdr(5), c);
        fast_filled_circle(it, bx + sd(8), by + sd(6), sdr(5), c);
        fill_rect_row_major(it, bx - sd(5), by - sd(12), sdr(3), sdr(21), c);
        fill_rect_row_major(it, bx + sd(11), by - sd(15), sdr(3), sdr(21), c);
        fast_line(it, bx - sd(5), by - sd(12), bx + sd(13), by - sd(15), c);
        fast_line(it, bx - sd(5), by - sd(11), bx + sd(13), by - sd(14), c);
        fast_line(it, bx - sd(5), by - sd(10), bx + sd(13), by - sd(13), c);
        fast_line(it, bx - sd(5), by - sd(9),  bx + sd(13), by - sd(12), c);
      } else if (app_id == 7) {
        if (!is_shadow) {
          draw_cached_dock_mask<-9, 9, -16, 16>(it, bx, by, dock_r, c, ctx.col_black, [](int dx, int dy) -> bool {
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
          draw_cached_dock_mask<-12, 12, -12, 12>(it, bx, by, dock_r, c, ctx.col_black, [](int dx, int dy) -> bool {
            const int ax = std::abs(dx);
            const int ay = std::abs(dy);
            if (ax < 2 || ax > 11 || ay < 2 || ay > 11) return false;
            const int lx = ax - 2;
            const int ly_c = ay - 2;
            if ((lx == 0 || lx == 9) && (ly_c == 0 || ly_c == 9)) return false;
            if (dx > 0 && dy > 0 && lx >= 3 && lx <= 6 && ly_c >= 3 && ly_c <= 6) {
              return false;
            }
            return true;
          });
        }
      } else if (app_id == 3) {
        if (!is_shadow) {
          draw_cached_dock_mask<-19, 19, -12, 12>(it, bx, by, dock_r, c, ctx.col_black, [](int dx, int dy) -> bool {
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
          draw_cached_dock_mask<-16, 16, -16, 16>(it, bx, by, dock_r, c, ctx.col_black, [](int dx, int dy) -> bool {
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
        fill_rect_row_major(it, bx - sd(13), by - sd(7), sdr(26), sdr(17), c);
        fill_rect_row_major(it, bx - sd(5), by - sd(11), sdr(10), sdr(4), c);
        if (!is_shadow) {
          fast_filled_circle(it, bx, by + sd(1), sdr(6), cut_c);
          fast_circle(it, bx, by + sd(1), sdr(4), c);
          fast_filled_circle(it, bx, by + sd(1), sdr(2), c);
        }
      } else if (app_id == 9) {
        fill_rect_row_major(it, bx + sd(7), by - sd(14), sdr(4), sdr(8), c);
        fast_filled_triangle(it, bx, by - sd(16), bx - sd(16), by - sd(2), bx + sd(16), by - sd(2), c);
        fill_rect_row_major(it, bx - sd(16), by - sd(2), sdr(33), sdr(2), c);
        fill_rect_row_major(it, bx - sd(12), by, sdr(25), sdr(13), c);
        if (!is_shadow) {
          fast_hline(it, bx - sd(12), by, sdr(25), cut_c);
          fill_rect_row_major(it, bx - sd(8), by + sd(3), sdr(6), sdr(10), cut_c);
          fast_pixel(it, bx - sd(4), by + sd(8), c);
          fill_rect_row_major(it, bx + sd(2), by + sd(3), sdr(7), sdr(6), cut_c);
          fast_vline(it, bx + sd(5), by + sd(3), sdr(6), c);
          fast_hline(it, bx + sd(2), by + sd(5), sdr(7), c);
        }
      } else if (app_id == 5) {
        fast_filled_circle(it, bx + sd(6), by - sd(6), sdr(5), c);
        fast_vline(it, bx + sd(6), by - sd(14), sdr(2), c);
        fast_hline(it, bx + sd(13), by - sd(6), sdr(2), c);
        fast_line(it, bx + sd(11), by - sd(11), bx + sd(12), by - sd(12), c);
        fast_line(it, bx + sd(1), by - sd(11), bx, by - sd(12), c);
        if (!is_shadow) {
          fast_filled_circle(it, bx - sd(2), by + sd(1), sdr(9), cut_c);
          fast_filled_circle(it, bx + sd(6), by + sd(4), sdr(8), cut_c);
          fill_rect_row_major(it, bx - sd(11), by + sd(2), sdr(19), sdr(9), cut_c);
        }
        fast_filled_circle(it, bx - sd(9), by + sd(5), sdr(5), c);
        fast_filled_circle(it, bx - sd(2), by + sd(1), sdr(7), c);
        fast_filled_circle(it, bx + sd(6), by + sd(4), sdr(6), c);
        fill_rect_row_major(it, bx - sd(9), by + sd(4), sdr(16), sdr(7), c);
      }
    };

    const float anim_start = home_dock_anim().step_window(start_idx, now_ms);
    const int stride       = get_dock_slot_stride(win_xs, vis_count);
    const int left_cx      = win_xs[0];
    const int right_cx     = win_xs[vis_count - 1];
    const int clip_pad     = stride / 2 + dock_r;

    const Color dock_tint = kChromeGlassTint;

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

    const int pips_y = ly.dock_pips_y;
    render_viewport_scrollbar(it, cx, pips_y, dock_count, anim_start,
                              ctx.theme_primary, false);
  }

  // ZONE 6: Battery Readout & Geometry-Adaptive Roaming Area Vector Glyph
  const bool show_roaming_area = is_roaming_area_indicator_active();
#if LATTER_ROUND_DISPLAY
  const int batt_cx = show_roaming_area ? (ly.battery_cx + scale_x(16)) : ly.battery_cx;
#if LATTER_HAS_BATTERY
  {
    esphome::display::BaseFont *font_batt =
        (SCREEN_W < 380 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
    render_battery_indicator(it, batt_cx, ly.battery_y,
                             rs.hw_batt_level, rs.hw_batt_charging, font_batt,
                             ctx.theme_secondary, ctx.col_gold, ctx.col_green);
  }
#endif
  if (show_roaming_area) {
    const int area_cx = batt_cx - scale_x(46);
    const int area_cy = ly.battery_y;
    render_area_archetype_icon(it, area_cx, area_cy, scale_s(9),
                               ac.area_icon, ac.area_name, ctx.theme_secondary);
    frame_hits().add_circle(area_cx, area_cy, scale_s(12),
                            WidgetId::HOME_AREA_ICON,
                            kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(10));
  }
#else
#if LATTER_HAS_BATTERY
  {
    esphome::display::BaseFont *font_batt =
        (SCREEN_W < 380 && ctx.font_micro != nullptr) ? ctx.font_micro : ctx.font_small;
    render_battery_indicator(it, ly.battery_cx, ly.battery_y,
                             rs.hw_batt_level, rs.hw_batt_charging, font_batt,
                             ctx.theme_secondary, ctx.col_gold, ctx.col_green);
  }
#endif
  if (show_roaming_area) {
    const int area_cx = ly.battery_cx;
    const int area_cy = ly.battery_y + scale_y(24);
    render_area_archetype_icon(it, area_cx, area_cy, scale_s(9),
                               ac.area_icon, ac.area_name, ctx.theme_secondary);
    frame_hits().add_circle(area_cx, area_cy, scale_s(12),
                            WidgetId::HOME_AREA_ICON,
                            kHitTap | kHitFocusSlot | kHitFocusCircle,
                            0, scale_s(10));
  }
#endif
  home_lap.mark(ProfSect::HOME_DOCK);
}

// ==============================================================================
// Top-Level Active Page Dispatcher
// ==============================================================================

void render_active_page(esphome::display::Display &it, const UiFrameContext &ctx) {
  const int pg = runtime_state().nav.gui_page;
  if (pg != PageId::HOME) {
    home_activity_card_shown() = false;
    home_act_state().was_visible = false;
    home_act_state().fade_out_start_ms = 0U;
  }
  switch (pg) {
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
