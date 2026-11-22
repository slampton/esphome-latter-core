#include "tools_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace latter_ui {

namespace {

struct CalculatorState {
  double prev_val = 0.0;
  int op = 0;  // 0 = none, 1 = +, 2 = -, 3 = *, 4 = /
  char entry[20] = "0";
  char history[48] = "";
  bool new_entry = true;
  bool error = false;
};

CalculatorState &calc_state() {
  static CalculatorState st;
  return st;
}

void format_calc_num_buf(double v, char *buf, size_t buf_len) {
  if (std::isnan(v) || std::isinf(v) || std::fabs(v) > 999999999999.0) {
    std::snprintf(buf, buf_len, "Error");
    calc_state().error = true;
    return;
  }
  if (std::fabs(v) < 1e-9) v = 0.0;
  if (std::fabs(v - std::round(v)) < 1e-7 && std::fabs(v) < 1e12) {
    std::snprintf(buf, buf_len, "%lld", static_cast<long long>(std::round(v)));
    return;
  }
  std::snprintf(buf, buf_len, "%.8g", v);
}

}  // namespace

void render_calculator_subpage(Rgb565Surface &it, const UiFrameContext &ctx) {
  const auto &st = calc_state();
  const bool compact_w = (SCREEN_W < 380);
  const int hist_right_x  = std::min(SCREEN_W - 18, ctx.cx + scale_x(103));
  const int entry_right_x = std::min(SCREEN_W - 18, ctx.cx + scale_x(129));
  const int div_half_w    = std::min(SCREEN_W / 2 - 14, scale_x(151));

  if (st.history[0] != '\0') {
    if (backdrop_active()) {
      fast_print(it, hist_right_x + 1, scale_y(42) + 1, ctx.font_small, kChromeShadow,
                 TextAlign::CENTER_RIGHT, st.history);
    }
    fast_print(it, hist_right_x, scale_y(42), ctx.font_small, ctx.col_slate,
               TextAlign::CENTER_RIGHT, st.history);
  }
  if (st.entry[0] != '\0') {
    const UiFontView entry_font = (std::strlen(st.entry) > 10) ? ctx.font_btn_bold : ctx.font_date;
    const UiColor entry_col = st.error ? ctx.col_red : ctx.col_white;
    if (backdrop_active()) {
      fast_print(it, entry_right_x + 1, scale_y(74) + 1, entry_font, kChromeShadow,
                 TextAlign::CENTER_RIGHT, st.entry);
    }
    fast_print(it, entry_right_x, scale_y(74), entry_font, entry_col,
               TextAlign::CENTER_RIGHT, st.entry);
  }
  fast_line(it, ctx.cx - div_half_w, scale_y(98), ctx.cx + div_half_w, scale_y(98),
            kChromeCardBorder);

  static constexpr int kBaseRowsY[5] = {131, 187, 243, 299, 355};
  static constexpr const char *kKeyLabels[5][4] = {
      {"C", "±", "%", "÷"},
      {"7", "8", "9", "×"},
      {"4", "5", "6", "-"},
      {"1", "2", "3", "+"},
      {"0", ".", "DEL", "="},
  };

  const int raw_key_w = compact_w ? 62 : 68;
  const int eff_key_w = (SCREEN_W > 520) ? scale_x(raw_key_w) : raw_key_w;
  const int eff_key_h = (SCREEN_W > 520) ? scale_y(42) : 42;

  for (int r = 0; r < 5; r++) {
    const int ky_mid = scale_y(kBaseRowsY[r]);
    for (int c = 0; c < 4; c++) {
      const int kx_mid = layout().calc_col_x(c);

      UiColor bg_c     = kChromeGlassTint;
      UiColor border_c = kChromeCardBorder;
      const UiColor text_c = ctx.col_white;

      if (c == 3) {
        const bool op_selected = st.new_entry &&
                                 ((r == 0 && st.op == 4) || (r == 1 && st.op == 3) ||
                                  (r == 2 && st.op == 2) || (r == 3 && st.op == 1));
        if (op_selected) {
          bg_c     = ctx.col_orange;
          border_c = ctx.col_orange;
        } else {
          border_c = (r == 4) ? ctx.theme_primary : scale_color(ctx.col_orange, 0.72f);
        }
      } else if (r == 0 || (r == 4 && c == 2)) {
        border_c = kChromeCardBrdOn;
      }

      const bool is_minus_key = (r == 2 && c == 3);
      const bool is_del_key   = (r == 4 && c == 2);
      const uint16_t key_idx = static_cast<uint16_t>(r * 4 + c);
      ui::keypad_button(it, kx_mid, ky_mid, eff_key_w, eff_key_h,
                        is_minus_key ? nullptr : kKeyLabels[r][c],
                        bg_c, border_c, text_c,
                        is_del_key ? ctx.font_small : ctx.font_btn_bold,
                        WidgetId::NONE);
      if (is_minus_key) {
        ui::draw_icon_minus(it, kx_mid, ky_mid, std::max(7, scale_s(9)), text_c, true);
      }

      const int min_px = layout().min_hit_px();
      const int calc_pad = std::max(
          0, (std::max(min_px - (eff_key_w + 4), min_px - (eff_key_h + 6)) + 1) / 2);
      frame_hits().add_centered_rect(kx_mid, ky_mid, eff_key_w + 4, eff_key_h + 6,
                                     WidgetId::CALC_KEY_BASE + key_idx,
                                     kHitTap | kHitFocusSlot,
                                     static_cast<int16_t>(key_idx), calc_pad);
    }
  }

  render_back_button(it, ctx.cx, ctx.font_small, layout().back_y, "< Back");
}

void reset_calculator_state() {
  auto &st = calc_state();
  st.prev_val = 0.0;
  st.op = 0;
  std::snprintf(st.entry, sizeof(st.entry), "0");
  st.history[0] = '\0';
  st.new_entry = true;
  st.error = false;
}

void handle_calculator_keypad_tap(int tx, int ty) {
  static constexpr int kBaseRowsY[5] = {131, 187, 243, 299, 355};
  const int raw_key_w = (SCREEN_W < 380) ? 62 : 68;
  const int eff_key_w = (SCREEN_W > 520) ? scale_x(raw_key_w) : raw_key_w;
  const int eff_key_h = (SCREEN_W > 520) ? scale_y(42) : 42;
  const int key_w_tol = (eff_key_w + 4) / 2;
  const int key_h_tol = (eff_key_h + 6) / 2;

  int r = -1;
  int c = -1;
  for (int i = 0; i < 5; i++) {
    if (std::abs(ty - scale_y(kBaseRowsY[i])) <= key_h_tol) {
      r = i;
      break;
    }
  }
  for (int j = 0; j < 4; j++) {
    if (std::abs(tx - layout().calc_col_x(j)) <= key_w_tol) {
      c = j;
      break;
    }
  }
  if (r < 0 || c < 0) return;

  auto &st = calc_state();
  if (st.error && !(r == 0 && c == 0)) {
    reset_calculator_state();
  }

  auto compute_op = [](double a, double b, int op_code) -> double {
    if (op_code == 1) return a + b;
    if (op_code == 2) return a - b;
    if (op_code == 3) return a * b;
    if (op_code == 4) return (std::fabs(b) > 1e-12) ? (a / b) : NAN;
    return b;
  };
  auto op_to_str = [](int op_code) -> const char * {
    if (op_code == 1) return "+";
    if (op_code == 2) return "-";
    if (op_code == 3) return "×";
    if (op_code == 4) return "÷";
    return "";
  };

  if (r == 0 && c == 0) {
    reset_calculator_state();
    return;
  }
  if (r == 0 && c == 1) {
    if (std::strcmp(st.entry, "0") != 0 && !st.error) {
      const size_t len = std::strlen(st.entry);
      if (st.entry[0] == '-') {
        std::memmove(st.entry, st.entry + 1, len);
      } else if (len + 1 < sizeof(st.entry)) {
        std::memmove(st.entry + 1, st.entry, len + 1);
        st.entry[0] = '-';
      }
    }
    return;
  }
  if (r == 0 && c == 2) {
    const double cur = std::strtod(st.entry, nullptr);
    if (st.op == 1 || st.op == 2) {
      format_calc_num_buf(st.prev_val * (cur / 100.0), st.entry, sizeof(st.entry));
      st.new_entry = false;
    } else {
      format_calc_num_buf(cur / 100.0, st.entry, sizeof(st.entry));
      st.new_entry = true;
    }
    return;
  }
  if (r == 4 && c == 2) {
    if (!st.new_entry && !st.error) {
      const size_t len = std::strlen(st.entry);
      if (len <= 1 || (len == 2 && st.entry[0] == '-')) {
        std::snprintf(st.entry, sizeof(st.entry), "0");
      } else {
        st.entry[len - 1] = '\0';
      }
    }
    return;
  }
  if (c == 3) {
    const int new_op = (r == 0) ? 4 : (r == 1) ? 3 : (r == 2) ? 2 : (r == 3) ? 1 : 5;
    const double cur = (st.entry[0] != '\0') ? std::strtod(st.entry, nullptr) : st.prev_val;

    if (new_op == 5) {
      if (st.op != 0) {
        char prev_b[20];
        format_calc_num_buf(st.prev_val, prev_b, sizeof(prev_b));
        std::snprintf(st.history, sizeof(st.history), "%.18s %.3s %.20s =", prev_b, op_to_str(st.op), st.entry);
        const double res = compute_op(st.prev_val, cur, st.op);
        format_calc_num_buf(res, st.entry, sizeof(st.entry));
        st.op = 0;
        st.new_entry = true;
      }
    } else {
      if (st.op != 0 && !st.new_entry && st.entry[0] != '\0') {
        const double res = compute_op(st.prev_val, cur, st.op);
        if (std::isnan(res) || std::isinf(res)) {
          format_calc_num_buf(res, st.entry, sizeof(st.entry));
          st.op = 0;
          st.new_entry = true;
          return;
        }
        st.prev_val = res;
      } else {
        st.prev_val = cur;
      }
      st.op = new_op;
      st.new_entry = true;
      char prev_b[20];
      format_calc_num_buf(st.prev_val, prev_b, sizeof(prev_b));
      std::snprintf(st.history, sizeof(st.history), "%s %s", prev_b, op_to_str(st.op));
      std::snprintf(st.entry, sizeof(st.entry), "%s", prev_b);
    }
    return;
  }
  if (r == 4 && c == 1) {
    if (st.new_entry || st.entry[0] == '\0') {
      std::snprintf(st.entry, sizeof(st.entry), "0.");
      st.new_entry = false;
    } else {
      const size_t len = std::strlen(st.entry);
      if (std::strchr(st.entry, '.') == nullptr && len < 11) {
        st.entry[len] = '.';
        st.entry[len + 1] = '\0';
      }
    }
    return;
  }

  int digit = 0;
  if (r == 1) digit = 7 + c;
  else if (r == 2) digit = 4 + c;
  else if (r == 3) digit = 1 + c;
  else if (r == 4 && c == 0) digit = 0;

  if (st.new_entry || std::strcmp(st.entry, "0") == 0) {
    std::snprintf(st.entry, sizeof(st.entry), "%d", digit);
    st.new_entry = false;
  } else {
    const size_t len = std::strlen(st.entry);
    if (len < 10) {
      st.entry[len] = static_cast<char>('0' + digit);
      st.entry[len + 1] = '\0';
    }
  }
}

}  // namespace latter_ui
